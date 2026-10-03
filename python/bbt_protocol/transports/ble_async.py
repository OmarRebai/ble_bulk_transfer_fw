"""Bleak-based BLE transport adapter (async).

Provides separate RX (write) and TX (notify/indicate) characteristic support
and optionally discovers devices by name. Writes are chunked according to the
configured MTU. Notifications and indications on TX characteristics are
delivered to the registered callback.
"""
from __future__ import annotations

import asyncio
from typing import Optional

try:
    from bleak import BleakClient, BleakScanner
except Exception:  # pragma: no cover - optional dependency
    BleakClient = None
    BleakScanner = None

from .base import AsyncTransport
from ..frame import BBT_OPCODE_CHUNK

class BleTransport(AsyncTransport):
    """Minimal Bleak-backed transport implementing AsyncTransport.

    Parameters
    - address: device address (MAC on Linux/Windows or UUID on macOS). If None
      and device_name is provided, the transport will attempt to discover the
      device by name before connecting.
    - rx_char_uuid: characteristic UUID used for writes (device RX)
    - tx_char_uuid: characteristic UUID used for chunk notifications (device TX)
    - indicate_char_uuid: optional characteristic UUID used for control
      indications. Defaults to tx_char_uuid.
    - mtu: maximum write chunk size to use
    - device_name: optional name to discover if address is not known
    """

    def __init__(self, address: Optional[str] = None, rx_char_uuid: Optional[str] = None,
                 tx_char_uuid: Optional[str] = None, loop: Optional[asyncio.AbstractEventLoop] = None,
                 mtu: int = 128, device_name: Optional[str] = None,
                 indicate_char_uuid: Optional[str] = None):
        super().__init__(loop=loop)
        self.address = address
        self.rx_char_uuid = rx_char_uuid
        self.tx_char_uuid = tx_char_uuid
        self.indicate_char_uuid = indicate_char_uuid
        self.device_name = device_name
        self._client = None
        self._mtu = mtu
        self._open = False

    def get_mtu(self) -> int:
        return self._mtu

    async def _discover_by_name(self, name: str) -> Optional[str]:
        if BleakScanner is None:
            return None
        devices = await BleakScanner.discover()
        for d in devices:
            if d.name and d.name == name:
                return d.address
        return None

    async def open(self) -> None:
        if BleakClient is None:
            raise RuntimeError("bleak is required for BLE transport (pip install bleak)")
        # discover if needed
        if self.address is None and self.device_name:
            addr = await self._discover_by_name(self.device_name)
            if addr:
                self.address = addr
        if self.address is None:
            raise RuntimeError("No device address available to connect")

        self._client = BleakClient(self.address)
        await self._client.connect()

        # default tx to rx if only one characteristic provided
        if self.tx_char_uuid is None:
            self.tx_char_uuid = self.rx_char_uuid
        if self.indicate_char_uuid is None:
            self.indicate_char_uuid = self.tx_char_uuid

        if self.tx_char_uuid is None and self.indicate_char_uuid is None:
            raise RuntimeError("No TX notify/indicate characteristic UUID configured")

        subscribed = set()
        for char_uuid in (self.tx_char_uuid, self.indicate_char_uuid):
            if char_uuid is not None and char_uuid not in subscribed:
                await self._client.start_notify(char_uuid, self._on_notify)
                subscribed.add(char_uuid)
        self._open = True

    async def close(self) -> None:
        if not self._open:
            return
        stopped = set()
        for char_uuid in (self.tx_char_uuid, self.indicate_char_uuid):
            if char_uuid is None or char_uuid in stopped:
                continue
            try:
                await self._client.stop_notify(char_uuid)
            except Exception:
                pass
            stopped.add(char_uuid)
        try:
            await self._client.disconnect()
        except Exception:
            pass
        self._client = None
        self._open = False

    async def send(self, data: bytes) -> None:
        if not self._open:
            raise RuntimeError("BLE client not connected")
        if self.rx_char_uuid is None:
            raise RuntimeError("No RX/write characteristic configured")
        write_with_response = len(data) == 0 or data[0] != BBT_OPCODE_CHUNK
        # chunk according to MTU
        for offset in range(0, len(data), self.get_mtu()):
            chunk = data[offset:offset + self.get_mtu()]
            await self._client.write_gatt_char(self.rx_char_uuid, chunk, response=write_with_response)

    def _on_notify(self, sender: int, data: bytearray) -> None:
        if self._callback:
            try:
                self._callback(bytes(data))
            except Exception:
                # swallow to avoid crashing notification thread
                pass
