"""High-level BBT protocol test harness with retransmit/NACK handling.

This module implements a transport-agnostic protocol layer that supports
sending a transfer (START -> CHUNK* -> END) and responding to NACK bitmaps
by retransmitting missing sequences. The implementation focuses on being
testable via MockTransport and on providing clear hooks for tests.
"""
from __future__ import annotations

import asyncio
import logging
import time
from math import ceil
from typing import Callable, Optional, List, Dict

from .frame import (
    BBT_OPCODE_NACK,
    BBT_OPCODE_START_ACK,
    BBT_OPCODE_ABORT,
    BBT_OPCODE_ERROR,
    BBT_OPCODE_COMPLETE,
    get_frame_length_from_buffer,
    parse_start,
    parse_chunk,
    parse_nack,
    parse_end,
    parse_start_ack,
    parse_abort,
    parse_error,
    parse_complete,
    build_start,
    build_chunk,
    build_end,
    build_complete,
    transfer_id_base,
    transfer_id_with_sender_role,
    transfer_id_with_receiver_role,
)

from .transports.base import AsyncTransport

Logger = logging.getLogger(__name__)
BBT_MIN_CHUNK_SIZE = 1
BBT_MAX_CHUNK_SIZE = 480

class BbtProtocol:
    """Protocol harness that can send files and react to NACK bitmaps.

    Usage:
        proto = BbtProtocol(transport, role="sender")
        await proto.start()
        ok = await proto.send_file(transfer_id, data, chunk_size)

    The implementation stores outgoing chunks so it can retransmit when a
    NACK bitmap arrives. It is intentionally simple and designed for tests.
    """

    def __init__(self, transport: AsyncTransport, role: str = "receiver", loop: Optional[asyncio.AbstractEventLoop] = None,
                 logger: Optional[logging.Logger] = None):
        self.transport = transport
        self.role = role
        self.loop = loop or asyncio.get_event_loop()
        self.log = logger or Logger
        self._buffer = bytearray()

        # callbacks
        self._on_start: Optional[Callable] = None
        self._on_chunk: Optional[Callable] = None
        self._on_end: Optional[Callable] = None
        self._on_nack: Optional[Callable] = None
        self._on_abort: Optional[Callable] = None
        self._on_error: Optional[Callable] = None

        # outgoing transfer state: transfer_id -> {chunks: {seq:bytes}, total_chunks: int, missing: List[int], last_nack: float}
        self._pending_transfers: Dict[int, Dict] = {}

    def register_callbacks(self, on_start: Optional[Callable] = None, on_chunk: Optional[Callable] = None,
                           on_end: Optional[Callable] = None, on_nack: Optional[Callable] = None,
                           on_abort: Optional[Callable] = None, on_error: Optional[Callable] = None) -> None:
        self._on_start = on_start
        self._on_chunk = on_chunk
        self._on_end = on_end
        self._on_nack = on_nack
        self._on_abort = on_abort
        self._on_error = on_error

    async def start(self) -> None:
        await self.transport.open()
        self.transport.register_callback(self._on_data)

    async def stop(self) -> None:
        await self.transport.close()

    async def send_start(self, transfer_id: int, total_size: int, chunk_size: int, total_chunks: int) -> None:
        wire_transfer_id = transfer_id_with_sender_role(transfer_id)
        pkt = build_start(wire_transfer_id, total_size, chunk_size, total_chunks)
        await self.transport.send(pkt)
        print(f"Sent START: transfer_id={wire_transfer_id} total_size={total_size} chunk_size={chunk_size} total_chunks={total_chunks}")

    async def send_chunk(self, transfer_id: int, seq: int, payload: bytes) -> None:
        wire_transfer_id = transfer_id_with_sender_role(transfer_id)
        pkt = build_chunk(wire_transfer_id, seq, payload)
        await self.transport.send(pkt)
        print(f"Sent CHUNK: transfer_id={wire_transfer_id} seq={seq} payload_len={len(payload)}")

    async def send_end(self, transfer_id: int) -> None:
        wire_transfer_id = transfer_id_with_sender_role(transfer_id)
        pkt = build_end(wire_transfer_id)
        await self.transport.send(pkt)

    async def send_complete(self, transfer_id: int) -> None:
        wire_transfer_id = transfer_id_with_receiver_role(transfer_id)
        pkt = build_complete(wire_transfer_id)
        await self.transport.send(pkt)

    async def send_file(self, transfer_id: int, data: bytes, chunk_size: int, timeout: float = 5.0) -> bool:
        """Send a file (data) as a sequence of chunks and handle NACK retransmits.

        Returns True if a COMPLETE frame is received before timeout,
        False otherwise.
        """
        total_chunks = ceil(len(data) / chunk_size)
        # prepare chunks
        chunks: Dict[int, bytes] = {}
        for seq in range(total_chunks):
            offs = seq * chunk_size
            chunks[seq] = data[offs:offs + chunk_size]

        # register pending transfer state and wait for the receiver to accept the transfer
        start_ack_event = asyncio.Event()
        self._pending_transfers[transfer_id] = {
            "chunks": chunks,
            "total_chunks": total_chunks,
            "missing": [],
            "last_nack": time.time(),
            "start_ack": start_ack_event,
            "start_status": None,
            "accepted_chunk_size": chunk_size,
            "nack_bitmap": bytearray(),
            "complete": asyncio.Event(),
            "completion_success": None,
        }

        await self.send_start(transfer_id, len(data), chunk_size, total_chunks)

        try:
            await asyncio.wait_for(start_ack_event.wait(), timeout=timeout)
        except asyncio.TimeoutError:
            self._pending_transfers.pop(transfer_id, None)
            return False

        pending = self._pending_transfers.get(transfer_id)
        if pending is None or pending["start_status"] != 0:
            self._pending_transfers.pop(transfer_id, None)
            return False

        accepted_chunk_size = pending["accepted_chunk_size"]
        if (BBT_MIN_CHUNK_SIZE <= accepted_chunk_size <= BBT_MAX_CHUNK_SIZE
                and accepted_chunk_size < chunk_size):
            total_chunks = ceil(len(data) / accepted_chunk_size)
            chunks = {}
            for seq in range(total_chunks):
                offs = seq * accepted_chunk_size
                chunks[seq] = data[offs:offs + accepted_chunk_size]
            pending["chunks"] = chunks
            pending["total_chunks"] = total_chunks

        # send all chunks after START_ACK is received
        for seq in range(total_chunks):
            await self.send_chunk(transfer_id, seq, pending["chunks"][seq])
            await asyncio.sleep(0.02)  # yield to allow NACKs to be processed

        await self.send_end(transfer_id)

        # wait for COMPLETE or error/abort signal
        try:
            await asyncio.wait_for(pending["complete"].wait(), timeout=timeout)
        except asyncio.TimeoutError:
            self._pending_transfers.pop(transfer_id, None)
            return False

        success = bool(pending.get("completion_success"))
        self._pending_transfers.pop(transfer_id, None)
        return success

    def _on_data(self, data: bytes) -> None:
        # Called by transport on incoming bytes
        self._buffer.extend(data)
        while True:
            needed = get_frame_length_from_buffer(self._buffer)
            if needed is None:
                break
            frame = bytes(self._buffer[:needed])
            del self._buffer[:needed]
            # handle frame
            try:
                self._handle_frame(frame)
            except Exception:
                self.log.exception("Error handling frame")

    def _handle_frame(self, frame: bytes) -> None:
        opcode = frame[0]
        if opcode == BBT_OPCODE_NACK:
            nack = parse_nack(frame)
            if nack:
                self._handle_nack(nack)
            return

        # handle other frames synchronously (callbacks may be coroutine)
        if opcode == 0x01:
            meta = parse_start(frame)
            if meta and self._on_start:
                meta["transfer_id"] = transfer_id_base(meta["transfer_id"])
                res = self._on_start(meta)
                if asyncio.iscoroutine(res):
                    asyncio.create_task(res)
            return
        if opcode == BBT_OPCODE_START_ACK:
            ack = parse_start_ack(frame)
            if ack is not None:
                transfer_id = transfer_id_base(ack.get("transfer_id"))
                pending = self._pending_transfers.get(transfer_id)
                if pending is not None:
                    pending["start_status"] = ack.get("status")
                    pending["accepted_chunk_size"] = ack.get("accepted_chunk_size", pending["accepted_chunk_size"])
                    start_ack_event = pending.get("start_ack")
                    if isinstance(start_ack_event, asyncio.Event):
                        start_ack_event.set()
            return
        if opcode == 0x02:
            chunk = parse_chunk(frame)
            if chunk and self._on_chunk:
                chunk["transfer_id"] = transfer_id_base(chunk["transfer_id"])
                res = self._on_chunk(chunk)
                if asyncio.iscoroutine(res):
                    asyncio.create_task(res)
            return
        if opcode == 0x03:
            if self._on_end:
                transfer_id = parse_end(frame)
                if transfer_id is None:
                    return
                res = self._on_end(transfer_id_base(transfer_id))
                if asyncio.iscoroutine(res):
                    asyncio.create_task(res)
            return
        if opcode == BBT_OPCODE_COMPLETE:
            transfer_id = parse_complete(frame)
            if transfer_id is None:
                return
            base_id = transfer_id_base(transfer_id)
            pending = self._pending_transfers.get(base_id)
            if pending is not None:
                pending["completion_success"] = True
                complete_event = pending.get("complete")
                if isinstance(complete_event, asyncio.Event):
                    complete_event.set()
            return
        if opcode == BBT_OPCODE_ABORT:
            transfer_id = parse_abort(frame)
            if transfer_id is not None and self._on_abort:
                base_id = transfer_id_base(transfer_id)
                pending = self._pending_transfers.get(base_id)
                if pending is not None:
                    pending["completion_success"] = False
                    complete_event = pending.get("complete")
                    if isinstance(complete_event, asyncio.Event):
                        complete_event.set()
                res = self._on_abort(base_id)
                if asyncio.iscoroutine(res):
                    asyncio.create_task(res)
            return
        if opcode == BBT_OPCODE_ERROR:
            err = parse_error(frame)
            if err and self._on_error:
                base_id = transfer_id_base(err.get("transfer_id"))
                pending = self._pending_transfers.get(base_id)
                if pending is not None:
                    pending["start_status"] = err.get("error")
                    start_ack_event = pending.get("start_ack")
                    if isinstance(start_ack_event, asyncio.Event):
                        start_ack_event.set()
                    pending["completion_success"] = False
                    complete_event = pending.get("complete")
                    if isinstance(complete_event, asyncio.Event):
                        complete_event.set()
                err["transfer_id"] = base_id
                res = self._on_error(err)
                if asyncio.iscoroutine(res):
                    asyncio.create_task(res)
            return

    def _handle_nack(self, nack: dict) -> None:
        """Process a parsed NACK (bitmap). Retransmit missing chunks found in
        the sender's pending state.
        """
        transfer_id = transfer_id_base(nack.get("transfer_id"))
        flags = nack.get("flags", 0)
        bitmap = nack.get("bitmap", b"")
        pending = self._pending_transfers.get(transfer_id)
        if not pending:
            return

        accumulated = pending.setdefault("nack_bitmap", bytearray())
        accumulated.extend(bitmap)

        if (flags & 0x01) != 0:
            return

        full_bitmap = bytes(accumulated)
        pending["nack_bitmap"] = bytearray()
        pending["last_nack"] = time.time()

        missing = self.missing_seqs_from_bitmap(full_bitmap, pending["total_chunks"])  # seqs that are missing (bit==0)
        pending["missing"] = missing

        if self._on_nack:
            try:
                res = self._on_nack({"transfer_id": transfer_id, "flags": flags, "bitmap": full_bitmap})
                if asyncio.iscoroutine(res):
                    asyncio.create_task(res)
            except Exception:
                self.log.exception("on_nack callback error")

        for seq in missing:
            chunk = pending["chunks"].get(seq)
            if chunk is None:
                continue
            asyncio.create_task(self.send_chunk(transfer_id, seq, chunk))

    # Utility to compute missing sequences from bitmap
    @staticmethod
    def missing_seqs_from_bitmap(bitmap: bytes, total_chunks: int) -> List[int]:
        missing: List[int] = []
        for seq in range(total_chunks):
            byte_index = seq // 8
            bit_index = seq % 8
            if byte_index >= len(bitmap):
                missing.append(seq)
            else:
                if (bitmap[byte_index] & (1 << bit_index)) == 0:
                    missing.append(seq)
        return missing
