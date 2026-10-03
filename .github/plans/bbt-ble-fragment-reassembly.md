# BBT BLE Fragment Reassembly Plan

## Goal
Handle BLE stack callbacks that split a single protocol frame across multiple `bbt_core_enqueue_packet()` calls, so packets are reassembled before parsing and CRC checks.

## Scope
- In: receiver-side stream reassembly for incoming BLE/UART byte fragments before packet dispatch.
- In: support for packets that span queue items, including header/body splits and multiple frames per item.
- Out: wire-format changes, CRC algorithm changes, transport API changes.

## Plan

1. Define the reassembly boundary
   1. Implement reassembly inside receiver processing, not in the transport or core queue layer.
   2. Confirm that enqueue items are byte fragments, not guaranteed packet boundaries.
   3. Keep parsing separate from transport delivery.

2. Add a stream reassembly buffer
   1. Introduce a fixed-size receiver-side accumulator for partial bytes.
   2. Append each incoming queue item to the accumulator.
   3. Reject only the current fragment on overflow and keep the active transfer state intact.

3. Extract complete frames from the stream
   1. Determine packet length from the opcode and header fields after the first frame bytes are available, then use the fixed header to decide how much more is needed.
   2. Handle partial headers, partial payloads, and multiple frames in one accumulator.
   3. Preserve leftover bytes for the next queue item.

4. Wire the reassembly into receiver dispatch
   1. Feed reconstructed frames into the existing parse/handle functions.
   2. Keep sender and receiver role selection unchanged.
   3. Ensure CRC is computed only on complete payloads.

5. Validate against BLE split-packet cases
   1. Add tests for 248-byte payloads split into 245 + 3 bytes.
   2. Add tests for header split across two items.
   3. Add tests for multiple frames packed into one callback.

## Dependencies
- Existing packet size constants and fixed-size buffers.
- Existing opcode-based frame length rules.

## Risks
- Reassembly buffer size may be too small for worst-case transport bursts.
- A naive accumulator could duplicate data or lose leftovers if frame extraction is not iterative.
- If the receiver still assumes item == frame, some handlers will need to move one layer lower.

## Open questions
- What is the maximum fragment burst size the STM32 BLE stack can emit for one logical frame?
