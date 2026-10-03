import asyncio

from bbt_protocol.frame import build_chunk, build_start
from bbt_protocol.transports.ble_async import BleTransport as AsyncBleTransport
from bbt_protocol.transports.ble_transport import BleTransport


class FakeBleakClient:
    def __init__(self):
        self.writes = []
        self.started = []
        self.stopped = []
        self.connected = False
        self.disconnected = False

    async def connect(self):
        self.connected = True

    async def disconnect(self):
        self.disconnected = True

    async def start_notify(self, uuid, callback):
        self.started.append((uuid, callback))

    async def stop_notify(self, uuid):
        self.stopped.append(uuid)

    async def write_gatt_char(self, uuid, data, response=None):
        self.writes.append((uuid, bytes(data), response))


def test_ble_transport_writes_control_frames_with_response_and_chunks_without():
    async def _run():
        client = FakeBleakClient()
        transport = BleTransport("addr", "rx", "tx")
        transport._client = client

        start = build_start(1, total_size=10, chunk_size=5, total_chunks=2)
        chunk = build_chunk(1, seq=0, payload=b"hello")

        await transport.send(start)
        await transport.send(chunk)

        assert client.writes[0] == ("rx", start, True)
        assert client.writes[1] == ("rx", chunk, False)

    asyncio.run(_run())


def test_async_ble_transport_writes_control_frames_with_response_and_chunks_without():
    async def _run():
        client = FakeBleakClient()
        transport = AsyncBleTransport(rx_char_uuid="rx", tx_char_uuid="tx", mtu=64)
        transport._client = client
        transport._open = True

        start = build_start(1, total_size=10, chunk_size=5, total_chunks=2)
        chunk = build_chunk(1, seq=0, payload=b"hello")

        await transport.send(start)
        await transport.send(chunk)

        assert client.writes[0] == ("rx", start, True)
        assert client.writes[1] == ("rx", chunk, False)

    asyncio.run(_run())


def test_async_ble_transport_keeps_chunk_write_type_when_split_by_mtu():
    async def _run():
        client = FakeBleakClient()
        transport = AsyncBleTransport(rx_char_uuid="rx", tx_char_uuid="tx", mtu=5)
        transport._client = client
        transport._open = True

        chunk = build_chunk(1, seq=0, payload=b"hello world")

        await transport.send(chunk)

        assert len(client.writes) > 1
        assert all(write[2] is False for write in client.writes)
        assert b"".join(write[1] for write in client.writes) == chunk

    asyncio.run(_run())


def test_ble_transport_subscribes_to_notify_and_indicate_characteristics(monkeypatch):
    async def _run():
        client = FakeBleakClient()
        monkeypatch.setattr("bbt_protocol.transports.ble_transport.BleakClient", lambda address: client)

        transport = BleTransport("addr", "rx", "notify-tx", indicate_char_uuid="indicate-tx")

        await transport.open()
        await transport.close()

        assert [entry[0] for entry in client.started] == ["notify-tx", "indicate-tx"]
        assert client.stopped == ["notify-tx", "indicate-tx"]

    asyncio.run(_run())


def test_async_ble_transport_subscribes_once_when_notify_and_indicate_match(monkeypatch):
    async def _run():
        client = FakeBleakClient()
        monkeypatch.setattr("bbt_protocol.transports.ble_async.BleakClient", lambda address: client)

        transport = AsyncBleTransport(
            address="addr",
            rx_char_uuid="rx",
            tx_char_uuid="shared-tx",
            indicate_char_uuid="shared-tx",
        )

        await transport.open()
        await transport.close()

        assert [entry[0] for entry in client.started] == ["shared-tx"]
        assert client.stopped == ["shared-tx"]

    asyncio.run(_run())
