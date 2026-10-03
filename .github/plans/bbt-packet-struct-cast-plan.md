# Plan: Packet Struct Cast Refactor (STM32 LE)

## Goal
Refactor packet parsing/building to use direct buffer-to-struct casting (no field-by-field assignments), with all message structs packed, opcode excluded, and STM32 little-endian assumed.

## Scope
- **In-scope:** `Inc/bbt_types.h`, `Src/bbt_packet.c`
- **Out-of-scope:** All other headers/sources, tests, build scripts

## Constraints
- New struct allowed only for chunk header to support polymorphic layout
- Packed structs, opcode stored separately in the frame
- STM32-only (little-endian, unaligned access acceptable)
- Zero field-by-field assignments in packet parse/build paths

## Tasks
1. **Align `bbt_types` message structs with wire layout**
   1.1. Add a packed-struct macro for the existing `bbt_*` message structs in `bbt_types.h`.
   1.2. Ensure each `bbt_*` message struct matches on-wire fields only (no opcode fields).
   1.3. Introduce a packed `bbt_chunk_header_t` for the on-wire chunk header.
   1.4. Update `bbt_chunk_packet_t` to embed `bbt_chunk_header_t` as the first field and keep a payload pointer (polymorphic layout).

2. **Refactor `bbt_packet.c` to cast buffers to `bbt_*` structs**
   2.1. Add a cast macro like `PROTOCOL_CAST_PAYLOAD(type, buffer, offset)` in `bbt_packet.c`.
   2.2. Replace overlay structs with direct casts to `bbt_start_meta_t`, `bbt_pull_req_t`, `bbt_range_t`, and `bbt_chunk_header_t`, using offset `1` to skip opcode.
   2.3. Update size checks to use `sizeof(bbt_*)` plus payload length where applicable.
   2.4. In chunk parsing, cast to `bbt_chunk_header_t`, then set `bbt_chunk_packet_t.payload` to the trailing buffer region (single pointer assignment).
   2.5. Ensure builders write opcode first and then assign through the casted `bbt_*` struct (no per-field assignments for header fields).

3. **Documentation alignment (limited to scope files)**
   3.1. Add brief comments in `bbt_types.h` describing packed, opcode-free wire layout expectations for each `bbt_*` message struct.

## Dependencies and Ordering
1 → 2 → 3. Struct layout must be finalized before updating casting logic.

## Risks
- **API break risk:** Changing `bbt_chunk_packet_t` layout to include `bbt_chunk_header_t` may affect any external users that treat it as a flat struct.
- **Alignment risk:** Packed casts assume unaligned access is acceptable on STM32 targets.
- **Portability risk:** STM32-only assumption removes endianness abstraction.

## Open Questions
- None.
