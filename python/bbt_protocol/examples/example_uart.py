"""Example: using BbtProtocol over UART transport.

This demonstrates creating a protocol instance and sending a START + a chunk.
"""
import asyncio
from bbt_protocol.transports.uart_transport import UartTransport
from bbt_protocol.protocol import BbtProtocol

async def main():
    t = UartTransport(port="/dev/ttyUSB0", baudrate=115200)
    proto = BbtProtocol(t, role="sender")

    def on_start(meta):
        print("Received START:", meta)

    def on_chunk(chunk):
        print("Received CHUNK:", chunk)

    proto.register_callbacks(on_start=on_start, on_chunk=on_chunk)

    await proto.start()

    # send a start + one chunk
    await proto.send_start(transfer_id=1, total_size=1024, chunk_size=256, total_chunks=4)
    await proto.send_chunk(transfer_id=1, seq=0, payload=b"hello world")
    await proto.send_end(transfer_id=1)

    await asyncio.sleep(1)
    await proto.stop()

if __name__ == "__main__":
    asyncio.run(main())