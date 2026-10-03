"""BBT protocol testing package.

Provides a protocol layer and transport adapters (UART and BLE) to test the
embedded C BBT implementation from Python.
"""

from .protocol import BbtProtocol
from .frame import (
    BBT_OPCODE_START,
    BBT_OPCODE_CHUNK,
    BBT_OPCODE_END,
    BBT_OPCODE_NACK,
    bbt_crc16_ccitt,
    bbt_crc32_update,
    build_start,
    parse_start,
    build_chunk,
    parse_chunk,
    build_nack,
    parse_nack,
    get_frame_length_from_buffer,
)

from .transports.uart_transport import UartTransport
from .transports.ble_transport import BleTransport

__all__ = [
    "BbtProtocol",
    "UartTransport",
    "BleTransport",
]