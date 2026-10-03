# Plan: Make bbt_core.h the Public Header

## Purpose

Migrate the public API surface to `bbt_core.h`, remove `bbt_api.h`, and align README and includes with the final header layout.

## Scope

- Public header becomes `bbt_core.h`.
- `bbt_api.h` is removed entirely.
- `bbt_sender.h` and `bbt_receiver.h` include `bbt_core.h`.
- README includes the public header and reflects the final layout.
- Internal helper declarations are allowed to remain in the public header.

## Constraints

- Keep API names stable (`bbt_core_*` in `bbt_core.h`).
- Maintain naming conventions (public symbols prefixed by file base name).
- Do not introduce dynamic allocation or new dependencies.

## Tasks

1. Public header migration
   1.1 Move public API prototypes from `bbt_api.h` into `bbt_core.h`.
   1.2 Ensure `bbt_core.h` keeps all public API declarations alongside internal helpers.
   1.3 Delete `bbt_api.h`.

2. Update includes
   2.1 Replace `#include "bbt_api.h"` with `#include "bbt_core.h"` across headers and sources.
   2.2 Update `bbt_sender.h` and `bbt_receiver.h` to include `bbt_core.h` directly.
   2.3 Verify include order to avoid circular dependencies (core -> sender/receiver -> core).

3. Documentation alignment
   3.1 Update README to include `bbt_core.h` as the public header.
   3.2 Adjust any wording that refers to `bbt_api.h` or the public API location.

4. Validation and cleanup
   4.1 Re-scan for any remaining references to `bbt_api.h`.
   4.2 Confirm that all public APIs are declared only once in `bbt_core.h`.

## Risks

- External users may still include `bbt_api.h`; removal is a breaking change.
- Include cycles if `bbt_core.h` and `bbt_sender.h`/`bbt_receiver.h` reference each other in the wrong order.

## Open Questions

- `bbt_api.h` will be removed immediately (no compatibility stub).
- Any additional docs or examples outside README that need updates?
