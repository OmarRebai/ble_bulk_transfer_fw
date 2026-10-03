"""In-memory transport for unit tests.

MockTransport can be paired with another MockTransport to simulate a bidirectional
link. It supports MTU, artificial delay, drop-rate and reordering; it's ideal
for exercising the protocol layer deterministically in unit tests.
"""
from __future__ import annotations

import asyncio
import random
from typing import Optional, Callable

from .base import AsyncTransport

class MockTransport(AsyncTransport):
    """Simple in-memory transport that delivers bytes to a peer's callback.

    Important test knobs:
    - mtu: maximum chunk size delivered per send
    - drop_rate: probability [0..1) that an outgoing chunk is dropped
    - delay_range: (min_s, max_s) artificial per-chunk delay
    - reorder: if True, chunks may be shuffled before delivery
    """

    def __init__(self, mtu: int = 128, loop: Optional[asyncio.AbstractEventLoop] = None, name: str = "mock"):
        super().__init__(loop=loop)
        self._mtu = mtu
        self._peer: Optional["MockTransport"] = None
        self.name = name
        self._open = False
        self.drop_rate = 0.0
        self.delay_range = (0.0, 0.0)
        self.reorder = False

    def set_peer(self, peer: "MockTransport") -> None:
        self._peer = peer

    async def open(self) -> None:
        self._open = True

    async def close(self) -> None:
        self._open = False

    def get_mtu(self) -> int:
        return self._mtu

    async def send(self, data: bytes) -> None:
        if not self._open:
            raise RuntimeError("MockTransport not open")
        if self._peer is None:
            raise RuntimeError("MockTransport has no peer")
        # split respecting MTU
        chunks = [data[i:i + self._mtu] for i in range(0, len(data), self._mtu)]
        if self.reorder and len(chunks) > 1:
            random.shuffle(chunks)
        for chunk in chunks:
            if random.random() < self.drop_rate:
                continue
            delay = random.uniform(self.delay_range[0], self.delay_range[1])
            if delay > 0:
                await asyncio.sleep(delay)
            # deliver on peer's loop
            loop = self._peer._loop
            loop.call_soon_threadsafe(self._peer._deliver, chunk)

    def _deliver(self, data: bytes) -> None:
        if self._callback:
            try:
                self._callback(data)
            except Exception:
                # swallow exceptions to avoid failing the transport
                pass
