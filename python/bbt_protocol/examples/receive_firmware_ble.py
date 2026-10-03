"""Receive a BBT transfer over BLE and save it to a file.

This example initiates the transfer with a PULL_REQ for base transfer id 0x02.
On the wire, the receiver sets the role bit (0x80) for START_ACK/NACK/COMPLETE
responses, while the sender transmits START/CHUNK/END using the base id. The
device then sends START, the receiver replies with START_ACK, and after END the
receiver sends either NACK or COMPLETE depending on whether all chunks were
received.
"""
from __future__ import annotations

import argparse
import asyncio
import logging
import struct
from pathlib import Path
from typing import Dict, Optional

from ..frame import (
    BBT_OPCODE_COMPLETE,
    BBT_OPCODE_PULL_REQ,
    BBT_OPCODE_START_ACK,
    BBT_OPCODE_NACK,
    bbt_crc16_ccitt,
    build_nack,
    transfer_id_with_receiver_role,
)
from ..protocol import BbtProtocol
from ..transports.ble_transport import BleTransport

TRANSFER_ID = 0x02
DEFAULT_OUTPUT = "test_receive.log"
DEFAULT_REQUESTED_MAX_CHUNK_SIZE = 240

HHPT_SERVICE_UUID = "0000FF00-0000-1000-8000-00805F9B34FB"
HHPT_BBT_RX_UUID = "0000FF07-0000-1000-8000-00805F9B34FB"  # write to device (RX)
HHPT_BBT_TX_UUID = "0000FF08-0000-1000-8000-00805F9B34FB"  # notify/indicate from device (TX)

DEFAULT_DEVICE_ADDRESS = "00:80:E1:27:A7:0E"


def build_pull_req(transfer_id: int, max_chunk_size: int) -> bytes:
    return bytes([BBT_OPCODE_PULL_REQ]) + struct.pack("<B H", transfer_id, max_chunk_size)


def build_start_ack(transfer_id: int, status: int, accepted_chunk_size: int) -> bytes:
    return bytes([BBT_OPCODE_START_ACK]) + struct.pack("<B B H", transfer_id, status, accepted_chunk_size)


def build_complete(transfer_id: int) -> bytes:
    return bytes([BBT_OPCODE_COMPLETE]) + struct.pack("<B", transfer_id)


class FileReceiver:
    def __init__(self, output_path: Path, transfer_id: int, logger: logging.Logger, transport: BleTransport):
        self.output_path = output_path
        self.transfer_id = transfer_id
        self.log = logger
        self.transport = transport
        self.expected_total_size: Optional[int] = None
        self.expected_total_chunks: Optional[int] = None
        self.chunk_size: Optional[int] = None
        self.chunks: Dict[int, bytes] = {}
        self.start_seen = False
        self.done_event = asyncio.Event()
        self.error_received = False
        self.error_code: Optional[int] = None
        self.abort_received = False
        import time
        self.last_rx_time = time.time()
        self.nack_retry_count = 0
        self.timeout_task: Optional[asyncio.Task] = None

    def on_start(self, meta: dict) -> None:
        if meta.get("transfer_id") != self.transfer_id:
            return

        import time
        self.start_seen = True
        self.expected_total_size = meta.get("total_size")
        self.expected_total_chunks = meta.get("total_chunks")
        self.chunk_size = meta.get("chunk_size")
        self.chunks.clear()
        self.done_event.clear()
        self.error_received = False
        self.error_code = None
        self.abort_received = False
        self.last_rx_time = time.time()
        self.nack_retry_count = 0
        if not self.timeout_task:
            self.timeout_task = asyncio.create_task(self._check_timeout_loop())
        self.log.info(
            "START received: transfer_id=0x%02X total_size=%d chunk_size=%d total_chunks=%d",
            self.transfer_id,
            self.expected_total_size,
            self.chunk_size,
            self.expected_total_chunks,
        )

        wire_id = transfer_id_with_receiver_role(self.transfer_id)
        self.log.info("Sending START_ACK: transfer_id=0x%02X accepted_chunk_size=%d", wire_id, self.chunk_size or 0)
        asyncio.create_task(
            self.transport.send(build_start_ack(wire_id, 0x00, self.chunk_size or 0))
        )
        self.log.info("START_ACK sent: transfer_id=0x%02X accepted_chunk_size=%d", wire_id, self.chunk_size or 0)

    def on_chunk(self, chunk: dict) -> None:
        if chunk.get("transfer_id") != self.transfer_id:
            return
        if not self.start_seen:
            self.log.warning("CHUNK received before START for transfer_id=0x%02X", self.transfer_id)
            return

        seq = chunk.get("seq")
        payload = chunk.get("payload", b"")
        payload_len = chunk.get("payload_len", 0)
        payload_crc = chunk.get("payload_crc", 0)

        if payload_len != len(payload):
            self.log.warning(
                "CHUNK length mismatch: seq=%d expected_len=%d actual_len=%d",
                seq,
                payload_len,
                len(payload),
            )
            return

        # Strictly validate chunk size against negotiated size parameters
        if self.expected_total_chunks is not None and self.chunk_size is not None and self.expected_total_size is not None:
            if seq == self.expected_total_chunks - 1:
                expected_last_len = self.expected_total_size - seq * self.chunk_size
                if len(payload) != expected_last_len:
                    self.log.error(
                        "CHUNK seq=%d payload length mismatch for last chunk: expected=%d got=%d",
                        seq,
                        expected_last_len,
                        len(payload),
                    )
                    return
            else:
                if len(payload) != self.chunk_size:
                    self.log.error(
                        "CHUNK seq=%d payload length mismatch: expected=%d got=%d",
                        seq,
                        self.chunk_size,
                        len(payload),
                    )
                    return

        calc_crc = bbt_crc16_ccitt(payload)
        if calc_crc != payload_crc:
            self.log.error(
                "CHUNK CRC mismatch: seq=%d expected=0x%04X got=0x%04X",
                seq,
                payload_crc,
                calc_crc,
            )
            return

        if seq in self.chunks:
            import time
            self.last_rx_time = time.time()
            self.nack_retry_count = 0
            return

        import time
        self.last_rx_time = time.time()
        self.nack_retry_count = 0

        self.chunks[seq] = payload
        if self.expected_total_chunks:
            self.log.info("CHUNK received: seq=%d (%d/%d)", seq, len(self.chunks), self.expected_total_chunks)
        else:
            self.log.info("CHUNK received: seq=%d len=%d", seq, len(payload))

    def on_end(self, transfer_id: int) -> None:
        if transfer_id != self.transfer_id:
            return
        if not self.start_seen:
            self.log.warning("END received without START for transfer_id=0x%02X", self.transfer_id)
            return

        import time
        self.last_rx_time = time.time()
        self.nack_retry_count = 0

        if self.expected_total_chunks is not None:
            missing = [seq for seq in range(self.expected_total_chunks) if seq not in self.chunks]
            if missing:
                self.log.error("Missing %d chunks, not writing file. First missing: %s", len(missing), missing[:10])
                self.send_nack()
                return

        data = b"".join(self.chunks[seq] for seq in sorted(self.chunks))
        if self.expected_total_size is not None and len(data) != self.expected_total_size:
            self.log.warning("Size mismatch: expected=%d actual=%d", self.expected_total_size, len(data))

        text_content = data.decode("utf-8", errors="ignore").replace("\x00", "")
        with open(self.output_path, "w", encoding="utf-8") as handle:
            handle.write(text_content)
        
        wire_id = transfer_id_with_receiver_role(self.transfer_id)
        asyncio.create_task(self.transport.send(build_complete(wire_id)))
        self.log.info("Wrote %d bytes to %s", len(data), self.output_path)
        self.log.info("COMPLETE sent: transfer_id=0x%02X", wire_id)
        if self.timeout_task:
            self.timeout_task.cancel()
        self.done_event.set()

    def on_abort(self, transfer_id: int) -> None:
        if transfer_id != self.transfer_id:
            return
        self.log.error("ABORT received for transfer_id=0x%02X", transfer_id)
        self.abort_received = True
        if self.timeout_task:
            self.timeout_task.cancel()
        self.done_event.set()

    def on_error(self, err_dict: dict) -> None:
        transfer_id = err_dict.get("transfer_id")
        if transfer_id != self.transfer_id:
            return
        error_code = err_dict.get("error", 0)
        self.log.error("ERROR received for transfer_id=0x%02X: code=%d", transfer_id, error_code)
        self.error_received = True
        self.error_code = error_code
        if self.timeout_task:
            self.timeout_task.cancel()
        self.done_event.set()

    def send_nack(self) -> None:
        if self.expected_total_chunks is not None:
            bitmap_len = (self.expected_total_chunks + 7) // 8
            bitmap = bytearray(bitmap_len)
            for seq in self.chunks:
                bitmap[seq // 8] |= 1 << (seq % 8)

            # Segment NACK bitmap to fit within BLE MTU constraints
            max_payload = self.transport.mtu - 3
            nack_header_len = 5
            payload_space = max_payload - nack_header_len
            if payload_space <= 0:
                payload_space = 20  # Fallback safety

            offset = 0
            total_bytes = len(bitmap)
            while offset < total_bytes:
                chunk_len = min(total_bytes - offset, payload_space)
                flags = 0x01 if (offset + chunk_len) < total_bytes else 0x00
                bitmap_segment = bitmap[offset:offset + chunk_len]

                wire_id = transfer_id_with_receiver_role(self.transfer_id)
                pkt = build_nack(wire_id, flags, bytes(bitmap_segment))
                asyncio.create_task(self.transport.send(pkt))
                self.log.info(
                    "NACK segment sent: transfer_id=0x%02X offset=%d/%d chunk_len=%d flags=0x%02X",
                    wire_id,
                    offset,
                    total_bytes,
                    chunk_len,
                    flags,
                )
                offset += chunk_len

    async def _check_timeout_loop(self) -> None:
        import time
        try:
            while not self.done_event.is_set():
                await asyncio.sleep(0.1)
                if self.done_event.is_set():
                    break
                
                # Check if we have timed out (1.0 second = 1000ms idle timeout)
                elapsed = time.time() - self.last_rx_time
                if elapsed >= 1.0:
                    self.nack_retry_count += 1
                    if self.nack_retry_count >= 5:
                        self.log.error("Max NACK retries (5) reached due to idle timeout. Timing out transfer.")
                        self.error_received = True
                        self.error_code = 0x0A  # BBT_PROTO_ERR_TIMEOUT
                        self.done_event.set()
                        break
                    
                    self.log.warning("Idle timeout: No packets received for %.1f seconds. Sending NACK (attempt %d/5)", elapsed, self.nack_retry_count)
                    self.send_nack()
                    self.last_rx_time = time.time()
        except asyncio.CancelledError:
            pass


async def main(
    address: Optional[str],
    output_path: Path,
    timeout: float,
    rx_uuid: str,
    tx_uuid: str,
    device_name: Optional[str],
    mtu: int,
    requested_max_chunk_size: int,
) -> int:
    logging.basicConfig(level=logging.INFO)
    logger = logging.getLogger("receive_ble")

    transport = BleTransport(
        address=address,
        rx_char_uuid=rx_uuid,
        tx_char_uuid=tx_uuid,
        mtu=mtu,
        device_name=device_name,
    )
    proto = BbtProtocol(transport, role="receiver")

    receiver = FileReceiver(output_path=output_path, transfer_id=TRANSFER_ID, logger=logger, transport=transport)
    proto.register_callbacks(
        on_start=receiver.on_start,
        on_chunk=receiver.on_chunk,
        on_end=receiver.on_end,
        on_abort=receiver.on_abort,
        on_error=receiver.on_error,
    )

    logger.info(
        "Connecting to BLE device %s (output=%s)",
        address or device_name or "<unknown>",
        output_path,
    )
    logger.info("Using RX UUID %s and TX UUID %s", rx_uuid, tx_uuid)
    logger.info("Requesting transfer id 0x%02X", transfer_id_with_receiver_role(TRANSFER_ID))

    await proto.start()

    await transport.send(build_pull_req(transfer_id_with_receiver_role(TRANSFER_ID), requested_max_chunk_size))
    logger.info(
        "PULL_REQ sent: transfer_id=0x%02X max_chunk_size=%d",
        transfer_id_with_receiver_role(TRANSFER_ID),
        requested_max_chunk_size,
    )

    try:
        await asyncio.wait_for(receiver.done_event.wait(), timeout=timeout)
        if receiver.abort_received:
            logger.error("Transfer aborted by remote peer")
            return 1
        if receiver.error_received:
            logger.error("Transfer failed due to remote error: code=%d", receiver.error_code or 0)
            return 1
        logger.info("Transfer complete")
        return 0
    except asyncio.TimeoutError:
        logger.error("Timed out waiting for transfer")
        return 1
    finally:
        if receiver.timeout_task:
            receiver.timeout_task.cancel()
        await proto.stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--address", default=DEFAULT_DEVICE_ADDRESS, help="BLE device address")
    parser.add_argument("--device-name", default=None, help="BLE device name if address is unknown")
    parser.add_argument("--rx-uuid", default=HHPT_BBT_RX_UUID, help="Characteristic UUID to write to")
    parser.add_argument("--tx-uuid", default=HHPT_BBT_TX_UUID, help="Notify/indicate characteristic UUID to subscribe to")
    parser.add_argument("--output", default=DEFAULT_OUTPUT, help="Output file path")
    parser.add_argument("--timeout", type=float, default=60.0, help="Timeout in seconds")
    parser.add_argument("--mtu", type=int, default=128, help="Write chunk size to use")
    parser.add_argument(
        "--max-chunk-size",
        type=int,
        default=DEFAULT_REQUESTED_MAX_CHUNK_SIZE,
        help="Requested maximum chunk size for the sender to use",
    )
    args = parser.parse_args()

    rc = asyncio.run(
        main(
            address=args.address or DEFAULT_DEVICE_ADDRESS,
            output_path=Path(args.output),
            timeout=args.timeout,
            rx_uuid=args.rx_uuid,
            tx_uuid=args.tx_uuid,
            device_name=args.device_name,
            mtu=args.mtu,
            requested_max_chunk_size=args.max_chunk_size,
        )
    )
    raise SystemExit(rc)
