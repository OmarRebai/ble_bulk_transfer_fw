"""BBT frame builders and parsers (Python port of bbt_packet helpers).

This module implements packet framing identical to the C implementation so
Python tests can generate and parse BBT protocol frames.
"""
from __future__ import annotations

import struct
from typing import Optional, Tuple

# Opcodes (from bbt_types.h)
BBT_OPCODE_START = 0x01
BBT_OPCODE_CHUNK = 0x02
BBT_OPCODE_END = 0x03
BBT_OPCODE_ABORT = 0x04
BBT_OPCODE_PULL_REQ = 0x05

BBT_OPCODE_START_ACK = 0x81
BBT_OPCODE_NACK = 0x82
BBT_OPCODE_COMPLETE = 0x83
BBT_OPCODE_ERROR = 0x84

# Packet size constants (wire sizes)
BBT_PACKET_START_FIXED_SIZE = 10  # opcode + start_meta (1 + 1 + 4 + 2 + 2)
BBT_PACKET_CHUNK_HEADER_FORMAT = "<BBHHH"  # opcode, transfer_id, seq, payload_len, payload_crc16
BBT_PACKET_CHUNK_HEADER_SIZE = struct.calcsize(BBT_PACKET_CHUNK_HEADER_FORMAT)
BBT_NACK_HEADER_SIZE = 1 + 4  # opcode(1) + nack_header(4)

# Transfer ID helpers (role bit)
BBT_TRANSFER_ID_ROLE_MASK = 0x80
BBT_TRANSFER_ID_ID_MASK = 0x7F


def transfer_id_with_sender_role(base_id: int) -> int:
    _validate_transfer_base_id(base_id)
    return base_id & BBT_TRANSFER_ID_ID_MASK


def transfer_id_with_receiver_role(base_id: int) -> int:
    _validate_transfer_base_id(base_id)
    return (base_id & BBT_TRANSFER_ID_ID_MASK) | BBT_TRANSFER_ID_ROLE_MASK


def transfer_id_base(wire_id: int) -> int:
    return wire_id & BBT_TRANSFER_ID_ID_MASK


def transfer_id_is_receiver_role(wire_id: int) -> bool:
    return (wire_id & BBT_TRANSFER_ID_ROLE_MASK) != 0


def _validate_transfer_base_id(base_id: int) -> None:
    if base_id < 0 or base_id > BBT_TRANSFER_ID_ID_MASK:
        raise ValueError(f"transfer_id out of range: {base_id}")

# Helpers: CRC16-CCITT-FALSE

def bbt_crc16_ccitt(data: bytes) -> int:
    """CRC16-CCITT-FALSE (poly 0x1021, init 0xFFFF)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

# CRC32 IEEE 802.3 (poly reflected 0xEDB88320, init 0xFFFFFFFF, xorout 0xFFFFFFFF)
# Mirrors the C API: bbt_crc32_init / bbt_crc32_update / bbt_crc32_final.
#
# WARNING: do NOT use binascii.crc32 here.  binascii.crc32(data, value) treats
# `value` as a raw internal state with no implicit init/xorout, so passing the
# C init value 0xFFFFFFFF produces a result that is XOR-inverted compared to what
# the C bbt_crc32_update() computes for the same input.  Use the explicit loop
# below to guarantee bit-for-bit compatibility with the C implementation.

def bbt_crc32_init() -> int:
    """Return the CRC32 initial state (mirrors bbt_crc32_init in C)."""
    return 0xFFFFFFFF

def bbt_crc32_update(crc: int, data: bytes) -> int:
    """Feed *data* into the running CRC32 state (mirrors bbt_crc32_update in C).

    *crc* must be the value returned by bbt_crc32_init() or a previous call to
    bbt_crc32_update().  Do NOT pass 0 as the initial value — that produces a
    result incompatible with the C implementation.
    """
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return crc & 0xFFFFFFFF

def bbt_crc32_final(crc: int) -> int:
    """Apply the final XOR and return the CRC32 value (mirrors bbt_crc32_final in C)."""
    return crc ^ 0xFFFFFFFF


# =========================
# Packet builders/parsers
# =========================

def build_start(transfer_id: int, total_size: int, chunk_size: int, total_chunks: int) -> bytes:
    """Build a START packet (opcode + bbt_start_meta_t)."""
    return bytes([BBT_OPCODE_START]) + struct.pack("<B I H H", transfer_id, total_size, chunk_size, total_chunks)


def parse_start(data: bytes) -> Optional[dict]:
    """Parse START packet. Return dict or None if invalid/too short."""
    if len(data) < BBT_PACKET_START_FIXED_SIZE:
        return None
    if data[0] != BBT_OPCODE_START:
        return None
    # header starts at offset 1
    transfer_id, total_size, chunk_size, total_chunks = struct.unpack_from("<B I H H", data, 1)
    return {
        "transfer_id": transfer_id,
        "total_size": total_size,
        "chunk_size": chunk_size,
        "total_chunks": total_chunks,
    }


def build_chunk(transfer_id: int, seq: int, payload: bytes) -> bytes:
    """Build a CHUNK packet (opcode + header + payload)."""
    payload_len = len(payload)
    payload_crc = bbt_crc16_ccitt(payload)
    header = struct.pack(
        BBT_PACKET_CHUNK_HEADER_FORMAT,
        BBT_OPCODE_CHUNK,
        transfer_id,
        seq,
        payload_len,
        payload_crc,
    )
    return header + payload



def parse_chunk(data: bytes) -> Optional[dict]:
    """Parse CHUNK packet. Returns dict with header and payload or None."""
    if len(data) < BBT_PACKET_CHUNK_HEADER_SIZE:
        return None
    if data[0] != BBT_OPCODE_CHUNK:
        return None
    opcode, transfer_id, seq, payload_len, payload_crc = struct.unpack_from(BBT_PACKET_CHUNK_HEADER_FORMAT, data, 0)
    total_needed = BBT_PACKET_CHUNK_HEADER_SIZE + payload_len
    if len(data) < total_needed:
        return None
    payload = data[BBT_PACKET_CHUNK_HEADER_SIZE:total_needed]
    return {
        "transfer_id": transfer_id,
        "seq": seq,
        "payload_len": payload_len,
        "payload_crc": payload_crc,
        "payload": payload,
    }


def build_end(transfer_id: int) -> bytes:
    return bytes([BBT_OPCODE_END]) + struct.pack("<B", transfer_id)


def parse_end(data: bytes) -> Optional[int]:
    if len(data) < 2 or data[0] != BBT_OPCODE_END:
        return None
    (transfer_id,) = struct.unpack_from("<B", data, 1)
    return transfer_id


def build_abort(transfer_id: int) -> bytes:
    return bytes([BBT_OPCODE_ABORT]) + struct.pack("<B", transfer_id)


def parse_abort(data: bytes) -> Optional[int]:
    if len(data) < 2 or data[0] != BBT_OPCODE_ABORT:
        return None
    (transfer_id,) = struct.unpack_from("<B", data, 1)
    return transfer_id


def build_error(transfer_id: int, error_code: int) -> bytes:
    return bytes([BBT_OPCODE_ERROR]) + struct.pack("<B B", transfer_id, error_code)


def parse_error(data: bytes) -> Optional[dict]:
    if len(data) < 3 or data[0] != BBT_OPCODE_ERROR:
        return None
    transfer_id, err = struct.unpack_from("<B B", data, 1)
    return {"transfer_id": transfer_id, "error": err}


def build_complete(transfer_id: int) -> bytes:
    return bytes([BBT_OPCODE_COMPLETE]) + struct.pack("<B", transfer_id)


def parse_complete(data: bytes) -> Optional[int]:
    if len(data) < 2 or data[0] != BBT_OPCODE_COMPLETE:
        return None
    (transfer_id,) = struct.unpack_from("<B", data, 1)
    return transfer_id


def build_start_ack(transfer_id: int, status: int, accepted_chunk_size: int) -> bytes:
    return bytes([BBT_OPCODE_START_ACK]) + struct.pack("<B B H", transfer_id, status, accepted_chunk_size)


def parse_start_ack(data: bytes) -> Optional[dict]:
    if len(data) < 5 or data[0] != BBT_OPCODE_START_ACK:
        return None
    transfer_id, status, accepted_chunk_size = struct.unpack_from("<B B H", data, 1)
    return {"transfer_id": transfer_id, "status": status, "accepted_chunk_size": accepted_chunk_size}


# NACK: new bitmap segmented format
def build_nack(transfer_id: int, flags: int, bitmap: bytes) -> bytes:
    header = struct.pack("<B B H", transfer_id, flags, len(bitmap))
    return bytes([BBT_OPCODE_NACK]) + header + bitmap


def parse_nack(data: bytes) -> Optional[dict]:
    if len(data) < 5 or data[0] != BBT_OPCODE_NACK:
        return None
    transfer_id, flags, bitmap_len = struct.unpack_from("<B B H", data, 1)
    total_needed = 1 + 4 + bitmap_len
    if len(data) < total_needed:
        return None
    bitmap = data[5:5 + bitmap_len]
    return {"transfer_id": transfer_id, "flags": flags, "bitmap": bitmap}


# Frame extraction from a byte stream

def get_frame_length_from_buffer(buf: bytes) -> Optional[int]:
    """Given a buffer starting at a packet boundary, return the full packet length
    if available, otherwise None if more bytes are needed.
    """
    if len(buf) < 1:
        return None
    opcode = buf[0]
    if opcode == BBT_OPCODE_START:
        return BBT_PACKET_START_FIXED_SIZE if len(buf) >= BBT_PACKET_START_FIXED_SIZE else None
    if opcode == BBT_OPCODE_CHUNK:
        if len(buf) < BBT_PACKET_CHUNK_HEADER_SIZE:
            return None
        _, _, _, payload_len, _ = struct.unpack_from(BBT_PACKET_CHUNK_HEADER_FORMAT, buf, 0)
        total = BBT_PACKET_CHUNK_HEADER_SIZE + payload_len
        return total if len(buf) >= total else None
    if opcode == BBT_OPCODE_NACK:
        if len(buf) < 5:
            return None
        _, _, bitmap_len = struct.unpack_from("<B B H", buf, 1)
        total = 1 + 4 + bitmap_len
        return total if len(buf) >= total else None
    if opcode == BBT_OPCODE_START_ACK:
        return 5 if len(buf) >= 5 else None
    if opcode == BBT_OPCODE_END:
        return 2 if len(buf) >= 2 else None
    if opcode == BBT_OPCODE_ERROR:
        return 3 if len(buf) >= 3 else None
    if opcode == BBT_OPCODE_COMPLETE:
        return 2 if len(buf) >= 2 else None
    if opcode == BBT_OPCODE_ABORT:
        return 2 if len(buf) >= 2 else None
    if opcode == BBT_OPCODE_PULL_REQ:
        return 4 if len(buf) >= 4 else None
    # For unknown opcodes, treat as a single byte frame to resync
    return 1