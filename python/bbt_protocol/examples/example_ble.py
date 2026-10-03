"""Example: using BbtProtocol over BLE transport.

Requires bleak and a target device exposing the BBT characteristic.
"""
import asyncio
from bbt_protocol.transports.ble_transport import BleTransport
from bbt_protocol.protocol import BbtProtocol

CHAR_UUID = "0000fff1-0000-1000-8000-00805f9b34fb"  # example, replace
DEVICE_ADDR = "12:34:56:78:9A:BC"

async def main():
    t = BleTransport(address=DEVICE_ADDR, char_uuid=CHAR_UUID)
    proto = BbtProtocol(t, role="sender")

    def on_nack(nack):
        print("Received NACK:", nack)

    proto.register_callbacks(on_nack=on_nack)

    await proto.start()
    await proto.send_start(transfer_id=1, total_size=1024, chunk_size=256, total_chunks=4)
    await proto.send_chunk(transfer_id=1, seq=0, payload=b"hello")
    await proto.send_end(transfer_id=1)

    await asyncio.sleep(2)
    await proto.stop()

if __name__ == "__main__":
    asyncio.run(main())