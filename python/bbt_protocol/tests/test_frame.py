"""Unit tests for frame builders/parsers.

These tests are designed to run with pytest but are not executed here.
"""
from bbt_protocol.frame import (
    build_start,
    parse_start,
    build_chunk,
    parse_chunk,
    BBT_PACKET_CHUNK_HEADER_SIZE,
    bbt_crc16_ccitt,
    build_complete,
    parse_complete,
    transfer_id_base,
    transfer_id_is_receiver_role,
    transfer_id_with_receiver_role,
    transfer_id_with_sender_role,
)


def test_start_roundtrip():
    pkt = build_start(transfer_id=5, total_size=12345, chunk_size=256, total_chunks=49)
    meta = parse_start(pkt)
    assert meta is not None
    assert meta["transfer_id"] == 5
    assert meta["total_size"] == 12345
    assert meta["chunk_size"] == 256
    assert meta["total_chunks"] == 49


def test_chunk_roundtrip():
    payload = b"hello world" * 3
    pkt = build_chunk(transfer_id=2, seq=7, payload=payload)
    assert BBT_PACKET_CHUNK_HEADER_SIZE == 8
    assert len(pkt) == BBT_PACKET_CHUNK_HEADER_SIZE + len(payload)
    parsed = parse_chunk(pkt)
    assert parsed is not None
    assert parsed["transfer_id"] == 2
    assert parsed["seq"] == 7
    assert parsed["payload_len"] == len(payload)
    assert parsed["payload"] == payload
    # verify crc
    assert parsed["payload_crc"] == bbt_crc16_ccitt(payload)


def test_abort_roundtrip():
    from bbt_protocol.frame import build_abort, parse_abort
    pkt = build_abort(transfer_id=0x15)
    parsed = parse_abort(pkt)
    assert parsed == 0x15


def test_complete_roundtrip():
    pkt = build_complete(transfer_id=0x2A)
    parsed = parse_complete(pkt)
    assert parsed == 0x2A


def test_transfer_id_role_helpers():
    base_id = 0x12
    sender_id = transfer_id_with_sender_role(base_id)
    receiver_id = transfer_id_with_receiver_role(base_id)

    assert sender_id == base_id
    assert receiver_id == (base_id | 0x80)
    assert transfer_id_base(sender_id) == base_id
    assert transfer_id_base(receiver_id) == base_id
    assert transfer_id_is_receiver_role(sender_id) is False
    assert transfer_id_is_receiver_role(receiver_id) is True


def test_frame_length_abort_pull_req():
    from bbt_protocol.frame import get_frame_length_from_buffer, BBT_OPCODE_ABORT, BBT_OPCODE_PULL_REQ
    
    # ABORT packet is 2 bytes
    assert get_frame_length_from_buffer(bytes([BBT_OPCODE_ABORT])) is None
    assert get_frame_length_from_buffer(bytes([BBT_OPCODE_ABORT, 0x15])) == 2

    # PULL_REQ packet is 4 bytes
    assert get_frame_length_from_buffer(bytes([BBT_OPCODE_PULL_REQ])) is None
    assert get_frame_length_from_buffer(bytes([BBT_OPCODE_PULL_REQ, 0x15, 0x00])) is None
    assert get_frame_length_from_buffer(bytes([BBT_OPCODE_PULL_REQ, 0x15, 0xF0, 0x00])) == 4
