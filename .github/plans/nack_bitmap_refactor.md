# NACK Bitmap Refactor Plan

## Purpose and Scope
This plan details the steps to refactor the NACK (Negative Acknowledgement) message in the BLE Bulk Transfer protocol. The new approach replaces range-based NACKs with a fixed-length bitmap, always starting from chunk 0, and includes the bitmap length in the message. The goal is to reduce protocol overhead and simplify sender/receiver logic while standardizing message size.

## Tasks
1. Update NACK message structure to include:
   - `transfer_id`
   - `bitmap_length` (set to BBT_BITMAP_SIZE_BYTES)
   - `bitmap[]` (fixed-length, always from chunk 0, zero-padded as needed)
2. Refactor receiver logic to generate the fixed-length bitmap for missing chunks.
3. Update sender logic to interpret the fixed-length bitmap, handling missing and received chunks.
4. Update protocol documentation and API references to reflect the new NACK format.
5. Test with various transfer sizes and loss patterns to validate efficiency and correctness.

## Decisions
- No offset or range fields; bitmap always starts at chunk 0.
- No special handling for the case where all chunks are missing.
- No backward compatibility with range-based NACKs.
- Bitmap is always sent at fixed length (BBT_BITMAP_SIZE_BYTES), zero-padded if necessary.

## Risks and Considerations
- NACK message size is predictable and consistent, simplifying parsing and protocol handling.
- Slightly higher overhead for small or sparse transfers, but improved protocol uniformity.

## Open Questions
- None (fixed-length approach selected).

---

**Validated against project naming and modularity rules.**
