---
name: bbt-embedded-c-clean-code
description: "Apply clean code and solid principles for embedded C in ble_bulk_transfer: no dynamic allocation, single responsibility per file, modularity, and reusability."
---

# BBT Embedded C Clean Code

## When to Use

- Designing or refactoring embedded C modules
- Creating new headers, C files, or public APIs
- Reviewing code for maintainability and reuse

## Core Rules

1. **No dynamic allocation**: do not use malloc/free/realloc or hidden allocators; fixed-size static pools are allowed.
2. **Single responsibility per file**: each .c/.h pair owns one module/feature responsibility.
3. **Modularity**: define clear interfaces; hide internals in .c or internal headers.
4. **Reusability**: prefer small, composable APIs and data structures.
5. **Principles**: apply SOLID (adapted for C) and align with MISRA-C style safety rules.

## Procedure

1. Define the module responsibility in one sentence.
2. Choose the file base name that matches the responsibility.
3. Design a minimal public API in the header; keep private helpers static in the .c.
4. Use only static or stack allocation; if buffers are needed, pass them in or use fixed-size storage.
5. Keep functions small and focused; split into helpers when a function grows or mixes concerns.
6. Check naming and visibility for consistency across the module.

## Decision Points

- If a feature needs dynamic memory, redesign to use fixed-size buffers, static pools, or caller-provided storage.
- If a file touches more than one responsibility, split it into separate modules.
- If an API is hard to reuse, reduce parameters, separate parsing from transport, or decouple hardware specifics.

## Quality Checks

- No dynamic allocation functions used anywhere in the module; only fixed-size static pools if needed.
- Each file has a single, clear module/feature responsibility and a matching name.
- Public API is minimal and stable; internal helpers are static.
- APIs are reusable across targets with minimal changes.
