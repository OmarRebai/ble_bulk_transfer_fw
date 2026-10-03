"""Receive a BBT transfer over UART and save it to a file.

This example listens for a single transfer (transfer_id=0x02) and writes the
reassembled payload to test_receive.log. It validates CHUNK CRCs and logs
missing chunks. Add NACK/START_ACK handling if your device expects it.
"""
from __future__ import annotations

import argparse
import asyncio
import logging
from pathlib import Path
from typing import Dict, Optional

from bbt_protocol.frame import bbt_crc16_ccitt
from bbt_protocol.protocol import BbtProtocol
from bbt_protocol.transports.uart_transport import UartTransport

TRANSFER_ID = 0x02


class FileReceiver:
    def __init__(self, output_path: Path, transfer_id: int, logger: logging.Logger):
        self.output_path = output_path
        self.transfer_id = transfer_id
        self.log = logger
        self.expected_total_size: Optional[int] = None
        self.expected_total_chunks: Optional[int] = None
        self.chunk_size: Optional[int] = None
        self.chunks: Dict[int, bytes] = {}
        self.start_seen = False
        self.done_event = asyncio.Event()

    def on_start(self, meta: dict) -> None:
        if meta.get("transfer_id") != self.transfer_id:
            return
        self.start_seen = True
        self.expected_total_size = meta.get("total_size")
        self.expected_total_chunks = meta.get("total_chunks")
        self.chunk_size = meta.get("chunk_size")
        self.chunks.clear()
        self.done_event.clear()
        self.log.info(
            "START received: transfer_id=0x%02X total_size=%d chunk_size=%d total_chunks=%d",
            self.transfer_id,
            self.expected_total_size,
            self.chunk_size,
            self.expected_total_chunks,
        )
        # TODO: send START_ACK if your receiver is required to acknowledge START.

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
            self.log.warning("CHUNK length mismatch: seq=%d expected_len=%d actual_len=%d", seq, payload_len, len(payload))
            return

        calc_crc = bbt_crc16_ccitt(payload)
        if calc_crc != payload_crc:
            self.log.error(
                "CHUNK CRC mismatch: seq=%d expected=0x%04X got=0x%04X",
                seq,
                payload_crc,
                calc_crc,
            )
            # TODO: send NACK for this sequence if your device expects it.
            return

        if seq in self.chunks:
            return
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

        if self.expected_total_chunks is not None:
            missing = [seq for seq in range(self.expected_total_chunks) if seq not in self.chunks]
            if missing:
                self.log.error("Missing %d chunks, not writing file. First missing: %s", len(missing), missing[:10])
                # TODO: send NACK bitmap to request retransmission.
                return

        data = b"".join(self.chunks[seq] for seq in sorted(self.chunks))
        if self.expected_total_size is not None and len(data) != self.expected_total_size:
            self.log.warning("Size mismatch: expected=%d actual=%d", self.expected_total_size, len(data))

        self.output_path.write_bytes(data)
        self.log.info("Wrote %d bytes to %s", len(data), self.output_path)
        self.done_event.set()


async def main(port: str, baudrate: int, output_path: Path, timeout: float) -> int:
    logging.basicConfig(level=logging.INFO)
    logger = logging.getLogger("receive_uart")

    transport = UartTransport(port=port, baudrate=baudrate)
    proto = BbtProtocol(transport, role="receiver")

    receiver = FileReceiver(output_path=output_path, transfer_id=TRANSFER_ID, logger=logger)
    proto.register_callbacks(on_start=receiver.on_start, on_chunk=receiver.on_chunk, on_end=receiver.on_end)

    logger.info("Listening on %s @ %d baud for transfer_id=0x%02X", port, baudrate, TRANSFER_ID)
    await proto.start()

    try:
        await asyncio.wait_for(receiver.done_event.wait(), timeout=timeout)
        logger.info("Transfer complete")
        return 0
    except asyncio.TimeoutError:
        logger.error("Timed out waiting for transfer")
        return 1
    finally:
        await proto.stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0", help="UART port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200, help="UART baudrate")
    parser.add_argument("--output", default="test_receive.log", help="Output file path")
    parser.add_argument("--timeout", type=float, default=60.0, help="Timeout in seconds")
    args = parser.parse_args()

    rc = asyncio.run(main(port=args.port, baudrate=args.baud, output_path=Path(args.output), timeout=args.timeout))
    raise SystemExit(rc)
