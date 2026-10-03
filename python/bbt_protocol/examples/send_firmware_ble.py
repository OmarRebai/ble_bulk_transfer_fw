"""Send a firmware file to a device using BBT protocol over BLE.

Usage:
    python send_firmware_ble.py --file path/to/habePOS-Firmware-29_19_79.bin [--address 00:80:E1:27:A7:0E]

The script uses the supplied HHPT characteristic UUIDs and defaults and
sends the file with chunk_size=240. It prints progress and the final result.
"""
from __future__ import annotations

import asyncio
import logging
import os
import sys
import time
from typing import Optional

from ..protocol import BbtProtocol
from ..transports.ble_transport import BleTransport

HHPT_SERVICE_UUID = "0000FF00-0000-1000-8000-00805F9B34FB"
HHPT_BBT_RX_UUID = "0000FF07-0000-1000-8000-00805F9B34FB"  # write to device (RX)
HHPT_BBT_TX_UUID = "0000FF08-0000-1000-8000-00805F9B34FB"  # notify/indicate from device (TX)

DEFAULT_DEVICE_ADDRESS = "00:80:E1:27:A7:0E"
DEFAULT_DEVICE_NAME = "habePOS-A70E"
DEFAULT_FIRMWARE = "habePOS-Firmware-29_19_79.bin"
CHUNK_SIZE = 240


async def main(firmware_path: Optional[str] = None, address: Optional[str] = None) -> int:
    logging.basicConfig(level=logging.INFO)
    logger = logging.getLogger("send_firmware")

    if firmware_path is None:
        firmware_path = os.path.join(os.getcwd(), DEFAULT_FIRMWARE)

    if not os.path.exists(firmware_path):
        logger.error("Firmware file not found: %s", firmware_path)
        return 2

    with open(firmware_path, "rb") as fh:
        data = fh.read()

    transport = BleTransport(address=address or DEFAULT_DEVICE_ADDRESS,
                             rx_char_uuid=HHPT_BBT_RX_UUID,
                             tx_char_uuid=HHPT_BBT_TX_UUID,
                             mtu=252,
                             device_name=DEFAULT_DEVICE_NAME)

    proto = BbtProtocol(transport, role="sender")

    def on_nack(nack):
        logger.info("NACK received: transfer_id=%s flags=%s bitmap_len=%d",
                    nack.get("transfer_id"), nack.get("flags"), len(nack.get("bitmap", b"")))

    proto.register_callbacks(on_nack=on_nack)

    logger.info("Connecting to device %s (name=%s)", address or DEFAULT_DEVICE_ADDRESS, DEFAULT_DEVICE_NAME)
    await proto.start()

    transfer_id = 0x01
    logger.info("Starting transfer id=%d size=%d chunk_size=%d total_chunks=%d",
                transfer_id, len(data), CHUNK_SIZE, (len(data) + CHUNK_SIZE - 1) // CHUNK_SIZE)

    ok = await proto.send_file(transfer_id, data, CHUNK_SIZE, timeout=120.0)

    logger.info("Transfer result: %s", "OK" if ok else "FAILED")

    await proto.stop()
    return 0 if ok else 1


if __name__ == "__main__":
    import argparse

    p = argparse.ArgumentParser()
    p.add_argument("--file", help="firmware file to send", default=None)
    p.add_argument("--address", help="device address", default=None)
    args = p.parse_args()

    rc = asyncio.run(main(firmware_path=args.file, address=args.address))
    sys.exit(rc)
