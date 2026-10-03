"""Async UART transport using pyserial/pyserial-asyncio.

This implementation follows the AsyncTransport interface and chunks writes by
MTU. It falls back to a blocking serial object executed in the event loop's
executor when serial_asyncio isn't available.
"""
from __future__ import annotations

import asyncio
from typing import Optional

from .base import AsyncTransport

class UartTransport(AsyncTransport):
    """Async UART transport adapter.

    Usage: provide port and baudrate. The transport exposes get_mtu() which
    defaults to a large value for UART (practically unlimited compared to BLE).
    """

    def __init__(self, port: str, baudrate: int = 115200, loop: Optional[asyncio.AbstractEventLoop] = None, mtu: int = 4096):
        super().__init__(loop=loop)
        self.port = port
        self.baudrate = baudrate
        self._reader_task: Optional[asyncio.Task] = None
        self._use_asyncio = False
        self._reader = None
        self._writer = None
        self._serial = None
        self._open = False
        self._mtu = mtu

    def get_mtu(self) -> int:
        return self._mtu

    async def open(self) -> None:
        try:
            import serial_asyncio
            self._use_asyncio = True
            self._reader, self._writer = await serial_asyncio.open_serial_connection(url=self.port, baudrate=self.baudrate)
            self._open = True
            self._reader_task = asyncio.create_task(self._read_asyncio())
        except Exception:
            import serial
            self._serial = serial.Serial(self.port, self.baudrate, timeout=0.1)
            self._open = True
            self._reader_task = asyncio.create_task(self._read_blocking())

    async def close(self) -> None:
        self._open = False
        if self._reader_task:
            self._reader_task.cancel()
            try:
                await self._reader_task
            except Exception:
                pass
        if self._writer is not None:
            try:
                self._writer.close()
            except Exception:
                pass
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:
                pass

    async def send(self, data: bytes) -> None:
        if not self._open:
            raise RuntimeError("UART transport not open")
        # chunk according to MTU
        for offset in range(0, len(data), self.get_mtu()):
            chunk = data[offset:offset + self.get_mtu()]
            if self._use_asyncio and self._writer is not None:
                self._writer.write(chunk)
                await self._writer.drain()
            else:
                await self._loop.run_in_executor(None, self._serial.write, chunk)
                await self._loop.run_in_executor(None, self._serial.flush)

    async def _read_asyncio(self) -> None:
        try:
            while self._open:
                data = await self._reader.read(4096)
                if data and self._callback:
                    self._callback(data)
        except asyncio.CancelledError:
            return

    async def _read_blocking(self) -> None:
        try:
            while self._open:
                data = await self._loop.run_in_executor(None, self._serial.read, 4096)
                if data and self._callback:
                    self._loop.call_soon_threadsafe(self._callback, data)
        except asyncio.CancelledError:
            return

