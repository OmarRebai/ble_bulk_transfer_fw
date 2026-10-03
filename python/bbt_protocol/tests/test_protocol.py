"""Integration test for protocol + MockTransport.

This test verifies that when the receiver signals missing chunks via a NACK
(bitmap) the sender retransmits the missing sequences.
"""
import asyncio
import time

from bbt_protocol.transports.mock_transport import MockTransport
from bbt_protocol.protocol import BbtProtocol
from bbt_protocol.frame import (
    build_complete,
    build_nack,
    build_start_ack,
    transfer_id_with_receiver_role,
)


def test_sender_retransmit_on_nack():
    async def _run():
        t1 = MockTransport(mtu=64, name="t1")
        t2 = MockTransport(mtu=64, name="t2")
        t1.set_peer(t2)
        t2.set_peer(t1)

        sender = BbtProtocol(t1, role="sender")
        receiver = BbtProtocol(t2, role="receiver")

        received = {}
        lost_once = {1}
        total_chunks = 4
        chunk_size = 4
        transfer_id = 7

        async def on_start(meta):
            await t2.send(build_start_ack(transfer_id_with_receiver_role(meta["transfer_id"]), 0, chunk_size))

        end_seen = False

        def on_chunk(chunk):
            seq = chunk["seq"]
            # simulate loss of chunk 1 only on the first receipt
            if seq in lost_once:
                lost_once.remove(seq)
                return
            received[seq] = chunk["payload"]
            if end_seen and len(received) == total_chunks:
                asyncio.create_task(t2.send(build_complete(transfer_id_with_receiver_role(chunk["transfer_id"]))))

        def on_end(transfer_id):
            nonlocal end_seen
            end_seen = True
            # build bitmap of received chunks (bit=1 -> received)
            bitmap = bytearray((total_chunks + 7) // 8)
            for i in range(total_chunks):
                if i in received:
                    bitmap[i // 8] |= (1 << (i % 8))
            pkt = build_nack(transfer_id_with_receiver_role(transfer_id), 0, bytes(bitmap))
            # send nack back to sender
            asyncio.create_task(t2.send(pkt))
            if len(received) == total_chunks:
                asyncio.create_task(t2.send(build_complete(transfer_id_with_receiver_role(transfer_id))))

        receiver.register_callbacks(on_start=on_start, on_chunk=on_chunk, on_end=on_end)
        await receiver.start()
        await sender.start()

        # 4 chunks exactly
        data = b"ABCDEFGHIJKLMNOP"[:chunk_size * total_chunks]

        await sender.send_file(transfer_id, data, chunk_size, timeout=1.0)
        # give some time for retransmit to arrive and be processed
        await asyncio.sleep(0.5)
        assert 1 in received

    asyncio.run(_run())


def test_sender_uses_accepted_chunk_size_from_start_ack():
    async def _run():
        t1 = MockTransport(mtu=64, name="t1")
        t2 = MockTransport(mtu=64, name="t2")
        t1.set_peer(t2)
        t2.set_peer(t1)

        sender = BbtProtocol(t1, role="sender")
        receiver = BbtProtocol(t2, role="receiver")

        received_lengths = []
        end_seen = False

        async def on_start(meta):
            await t2.send(build_start_ack(transfer_id_with_receiver_role(meta["transfer_id"]), 0, 4))

        def on_chunk(chunk):
            received_lengths.append(len(chunk["payload"]))
            if end_seen and len(received_lengths) == 3:
                asyncio.create_task(t2.send(build_complete(transfer_id_with_receiver_role(chunk["transfer_id"]))))

        def on_end(transfer_id):
            nonlocal end_seen
            end_seen = True
            if len(received_lengths) == 3:
                asyncio.create_task(t2.send(build_complete(transfer_id_with_receiver_role(transfer_id))))

        receiver.register_callbacks(on_start=on_start, on_chunk=on_chunk, on_end=on_end)
        await receiver.start()
        await sender.start()

        ok = await sender.send_file(transfer_id=9, data=b"ABCDEFGHIJ", chunk_size=8, timeout=2.0)
        assert ok is True
        await asyncio.sleep(0.2)
        assert received_lengths == [4, 4, 2]

    asyncio.run(_run())


def test_sender_ignores_larger_accepted_chunk_size():
    async def _run():
        t1 = MockTransport(mtu=64, name="t1")
        t2 = MockTransport(mtu=64, name="t2")
        t1.set_peer(t2)
        t2.set_peer(t1)

        sender = BbtProtocol(t1, role="sender")
        receiver = BbtProtocol(t2, role="receiver")

        received_lengths = []
        end_seen = False

        async def on_start(meta):
            await t2.send(build_start_ack(transfer_id_with_receiver_role(meta["transfer_id"]), 0, 16))

        def on_chunk(chunk):
            received_lengths.append(len(chunk["payload"]))
            if end_seen and len(received_lengths) == 2:
                asyncio.create_task(t2.send(build_complete(transfer_id_with_receiver_role(chunk["transfer_id"]))))

        def on_end(transfer_id):
            nonlocal end_seen
            end_seen = True
            if len(received_lengths) == 2:
                asyncio.create_task(t2.send(build_complete(transfer_id_with_receiver_role(transfer_id))))

        receiver.register_callbacks(on_start=on_start, on_chunk=on_chunk, on_end=on_end)
        await receiver.start()
        await sender.start()

        ok = await sender.send_file(transfer_id=10, data=b"ABCDEFGHIJ", chunk_size=8, timeout=2.0)
        assert ok is True
        await asyncio.sleep(0.2)
        assert received_lengths == [8, 2]

    asyncio.run(_run())


def test_sender_accumulates_segmented_nack_bitmap():
    async def _run():
        t1 = MockTransport(mtu=64, name="t1")
        sender = BbtProtocol(t1, role="sender")

        sender._pending_transfers[7] = {
            "chunks": {seq: bytes([seq]) for seq in range(16)},
            "total_chunks": 16,
            "missing": [],
            "last_nack": time.time(),
            "start_ack": None,
            "start_status": 0,
            "accepted_chunk_size": 1,
            "nack_bitmap": bytearray(),
        }

        retransmitted = []

        async def fake_send_chunk(transfer_id, seq, payload):
            retransmitted.append(seq)

        sender.send_chunk = fake_send_chunk  # type: ignore[assignment]

        sender._handle_frame(build_nack(transfer_id_with_receiver_role(7), 0x01, bytes([0x0F])))
        await asyncio.sleep(0)
        assert retransmitted == []

        sender._handle_frame(build_nack(transfer_id_with_receiver_role(7), 0x00, bytes([0xF0])))
        await asyncio.sleep(0)
        assert retransmitted == [4, 5, 6, 7, 8, 9, 10, 11]

    asyncio.run(_run())


def test_protocol_abort_error_callbacks():
    from bbt_protocol.frame import build_abort, build_error

    async def _run():
        t1 = MockTransport(mtu=64, name="t1")
        proto = BbtProtocol(t1, role="receiver")

        abort_id = None
        error_dict = None

        def on_abort(transfer_id):
            nonlocal abort_id
            abort_id = transfer_id

        def on_error(err_dict):
            nonlocal error_dict
            error_dict = err_dict

        proto.register_callbacks(on_abort=on_abort, on_error=on_error)
        await proto.start()

        # Inject ABORT
        proto._handle_frame(build_abort(0x42))
        assert abort_id == 0x42

        # Inject ERROR
        proto._handle_frame(build_error(0x42, 0x08))
        assert error_dict is not None
        assert error_dict["transfer_id"] == 0x42
        assert error_dict["error"] == 0x08

        await proto.stop()

    asyncio.run(_run())
