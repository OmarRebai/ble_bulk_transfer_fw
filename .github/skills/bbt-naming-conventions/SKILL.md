---
name: bbt-naming-conventions
description: "Apply ble_bulk_transfer naming conventions for C/C++ symbols: public API prefixes, static underscore, snake_case, and getter/setter naming."
---

# BBT Naming Conventions

## When to Use

- Adding or refactoring C/C++ symbols in this repo
- Creating public APIs or internal static helpers
- Auditing naming consistency

## Conventions

1. **Public members** start with `file_name_` and use snake_case.
   - Example: file `bbt_sender.c` -> `bbt_sender_init`
2. **Static/internal members** start with `_` and use snake_case, with no other prefix.
   - Example: `_next_packet`
3. **Abbreviations**: use `bbt_` as the module prefix where it appears in file names; it stands for ble_bulk_transport.
4. **Getters/Setters** use `file_name_get_*` and `file_name_set_*` for public APIs.

## Procedure

1. Identify the file base name (without extension) and the symbol visibility (public vs internal).
2. If the symbol is public, prefix it with `file_name_` and apply snake_case.
3. If the symbol is static/internal, prefix it with `_` and apply snake_case.
4. For getters/setters, use `file_name_get_*` or `file_name_set_*` with snake_case.
5. Update all references and headers to match the renamed symbols.

## Quality Checks

- Public symbols start with `file_name_`.
- Static/internal symbols start with `_` and have no other prefix.
- Names are snake_case and consistent with file/module naming.
- Getter/setter names follow `file_name_get_*` / `file_name_set_*`.
