"""UART transport adapter using pyserial (async-friendly).

This module provides a small async wrapper around pyserial. If
pyserial-asyncio is available it will be used; otherwise a thread/executor
fallback will be used to perform non-blocking reads.
"""
from __future__ import annotations

import asyncio
from typing import Callable, Optional

class UartTransport:
    """Async UART transport adapter.

    Usage:
        t = UartTransport(port="/dev/ttyUSB0", baudrate=115200)
        await t.open()
        t.register_callback(on_bytes)
        await t.send(b"hello")
    """

    def __init__(self, port: str, baudrate: int = 115200, loop: Optional[asyncio.AbstractEventLoop] = None):
        self.port = port
        self.baudrate = baudrate
        self._loop = loop or asyncio.get_event_loop()
        self._callback: Optional[Callable[[bytes], None]] = None
        self._running = False
        self._reader_task: Optional[asyncio.Task] = None
        self._use_asyncio = False
        self._reader = None
        self._writer = None
        self._serial = None

    def register_callback(self, cb: Callable[[bytes], None]) -> None:
        self._callback = cb

    async def open(self) -> None:
        try:
            import serial_asyncio
            self._use_asyncio = True
            self._reader, self._writer = await serial_asyncio.open_serial_connection(url=self.port, baudrate=self.baudrate)
            self._running = True
            self._reader_task = asyncio.create_task(self._read_asyncio())
        except Exception:
            # fallback to blocking serial in executor
            import serial
            self._serial = serial.Serial(self.port, self.baudrate, timeout=0.1)
            self._running = True
            self._reader_task = asyncio.create_task(self._read_blocking())

    async def close(self) -> None:
        self._running = False
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
        if self._use_asyncio and self._writer is not None:
            self._writer.write(data)
            await self._writer.drain()
            return
        # blocking fallback
        await self._loop.run_in_executor(None, self._serial.write, data)
        await self._loop.run_in_executor(None, self._serial.flush)

    async def _read_asyncio(self) -> None:
        try:
            while self._running:
                data = await self._reader.read(1024)
                if data and self._callback:
                    # deliver on loop thread
                    self._callback(data)
        except asyncio.CancelledError:
            return

    async def _read_blocking(self) -> None:
        try:
            while self._running:
                data = await self._loop.run_in_executor(None, self._serial.read, 1024)
                if data and self._callback:
                    # schedule callback on event loop
                    self._loop.call_soon_threadsafe(self._callback, data)
        except asyncio.CancelledError:
            return
