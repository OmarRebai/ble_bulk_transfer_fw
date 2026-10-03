---
name: bbt-packet-little-endian
description: "Packet handling rule: assume little-endian on the wire, avoid per-field endian conversions, and allow direct struct casts for packet parsing/building."
argument-hint: "Apply little-endian packet rules to parsing/building"
---

# BBT Packet Little-Endian Rules

## When to Use

- Designing or reviewing packet parsing/building code
- Refactoring packet structures or layouts
- Adding new protocol fields

## Rules

1. Assume packets are little-endian on the wire.
2. Do not perform per-field endian conversions.
3. Arrays may be cast directly to packet structs instead of reading each field individually.

## Procedure

1. Define the packet struct to match the wire layout exactly.
2. Parse by casting the byte array to the struct type.
3. Build packets by writing to the struct fields directly.
4. Keep parsing/building logic minimal and avoid manual byte extraction.

## Decision Points

- If a packet layout changes, update the struct definition and keep parsing logic unchanged.
- If a struct layout cannot match the wire format, revisit the field ordering and sizes.

## Quality Checks

- No endian conversion helpers are used in packet parsing/building paths.
- Packet structs map 1:1 to the wire format.
- Parsing uses direct struct casts instead of per-field reads.
