# BBT Code Review Audit

## Title and scope

Review of the C sender/receiver path and the Python protocol mirror for wire-format compatibility, chunk sizing, and CRC correctness.

## Findings

- High (resolved): `python/bbt_protocol/protocol.py:203-214` reads `ack.get("chunk_size", ...)`, but `parse_start_ack()` returns `accepted_chunk_size`. The negotiated size is therefore ignored and the sender keeps slicing chunks at the original size, which can produce length mismatches and CRC failures when the peer accepts a different size.
- High: `Src/bbt_sender.c:523-547` assumes `read_fn()` always fills the full `payload_len`, but `bbt_export_read_fn_t` only returns a status and no byte count (`Inc/bbt_types.h:337-341`). A short read cannot be detected, so the sender can emit frames whose advertised length and CRC no longer match the actual payload, matching the observed "received smaller than expected" symptom.
- Medium (resolved): `python/bbt_protocol/protocol.py:229-249` processes every NACK segment immediately and never waits for the final segment flag, while the C side explicitly sends segmented bitmap NACKs. For transfers whose bitmap spans multiple packets, Python will retransmit from incomplete bitmaps and can resend the wrong chunk set.
- High (resolved): Receiver processing assumed each queue item contained a full packet, so BLE fragmentation (e.g., 245 + 3 bytes) could yield truncated parses and CRC errors. Reassembly now buffers fragments and dispatches only complete frames (implemented in bbt_receiver stream reassembly).

## Recommendations

- Change the export read contract to report bytes actually read, or require the callback to guarantee a full fill and validate that contract explicitly.

## Residual risks or open questions

- None noted beyond the export read contract constraint.
