"""BLE transport adapter using bleak.

This adapter is intentionally lightweight: it opens a BleakClient, writes
packets to a characteristic and receives notifications/indications by
registering a callback. It expects the remote peer to send raw BBT frames on
the configured TX characteristic(s).
"""
from __future__ import annotations

import asyncio
from typing import Callable, Optional

try:
    from bleak import BleakClient
except Exception:  # pragma: no cover - optional dependency
    BleakClient = None  # type: ignore

from ..frame import BBT_OPCODE_CHUNK

class BleTransport:
    def __init__(self, address: str, rx_char_uuid: str, tx_char_uuid: str, mtu: int = 252,
                 device_name: Optional[str] = None, loop: Optional[asyncio.AbstractEventLoop] = None,
                 indicate_char_uuid: Optional[str] = None):
        self.address = address
        self.rx_char_uuid = rx_char_uuid
        self.tx_char_uuid = tx_char_uuid
        self.indicate_char_uuid = indicate_char_uuid or tx_char_uuid
        self.mtu = mtu
        self.device_name = device_name
        self._loop = loop or asyncio.get_event_loop()
        self._client = None
        self._callback: Optional[Callable[[bytes], None]] = None

    def register_callback(self, cb: Callable[[bytes], None]) -> None:
        self._callback = cb

    async def open(self) -> None:
        if BleakClient is None:
            raise RuntimeError("bleak is required for BLE transport (pip install bleak)")
        self._client = BleakClient(self.address)
        await self._client.connect()
        subscribed = set()
        for char_uuid in (self.tx_char_uuid, self.indicate_char_uuid):
            if char_uuid not in subscribed:
                await self._client.start_notify(char_uuid, self._on_notify)
                subscribed.add(char_uuid)

    async def close(self) -> None:
        if self._client is None:
            return
        stopped = set()
        for char_uuid in (self.tx_char_uuid, self.indicate_char_uuid):
            if char_uuid in stopped:
                continue
            try:
                await self._client.stop_notify(char_uuid)
            except Exception:
                pass
            stopped.add(char_uuid)
        await self._client.disconnect()
        self._client = None

    async def send(self, data: bytes) -> None:
        if self._client is None:
            raise RuntimeError("BLE client not connected")
        write_with_response = len(data) == 0 or data[0] != BBT_OPCODE_CHUNK
        await self._client.write_gatt_char(self.rx_char_uuid, data, response=write_with_response)

    def _on_notify(self, sender: int, data: bytearray) -> None:
        if self._callback:
            # deliver bytes to callback
            self._callback(bytes(data))
