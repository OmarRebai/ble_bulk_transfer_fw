"""Async transport base for BBT protocol tests.

Defines the abstract interface used by the protocol harness so transports
(USB/UART/BLE/Mock) expose a consistent async API: open/close/send and a
callback registration point for incoming raw bytes.
"""
from __future__ import annotations

import asyncio
from abc import ABC, abstractmethod
from typing import Callable, Optional

class AsyncTransport(ABC):
    """Abstract async transport interface used by BbtProtocol.

    Implementations must be awaitable and deliver raw bytes to the registered
    callback. The transport may impose an MTU (get_mtu()) and should handle
    connection/reconnect semantics internally.
    """

    def __init__(self, loop: Optional[asyncio.AbstractEventLoop] = None):
        self._loop = loop or asyncio.get_event_loop()
        self._callback: Optional[Callable[[bytes], None]] = None

    @abstractmethod
    async def open(self) -> None:
        """Open the transport and prepare for send/receive."""
        raise NotImplementedError

    @abstractmethod
    async def close(self) -> None:
        """Close the transport and release resources."""
        raise NotImplementedError

    @abstractmethod
    async def send(self, data: bytes) -> None:
        """Send raw bytes on the transport. Should respect MTU when appropriate."""
        raise NotImplementedError

    def register_callback(self, cb: Callable[[bytes], None]) -> None:
        """Register a callback to receive bytes as they arrive from the wire."""
        self._callback = cb

    def get_mtu(self) -> int:
        """Return the transport MTU (max payload per write). Default 20 bytes."""
        return 20
