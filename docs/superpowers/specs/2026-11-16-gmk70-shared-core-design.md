# GMK70 shared implementation

The user approved one maintained GMK70 implementation for VIA, Vial and ZMK,
with adapters for framework operations. This records that approved direction.

## Scope and architecture

Keep authoritative code under `firmware/qmk-common`, as required by AGENTS.md.
Add a framework-independent C core there. Both QMK Make and Zephyr CMake compile
the same sources directly. There is no copied source tree, generated duplicate
implementation, dependency edit, new runtime, or common prebuilt binary.

The shared core owns board protocol formats and parsing, timing and battery
rules, output-selection and hold decisions, stock HID mapping, indicator
calculations, and reusable hardware algorithms. Reuse the existing shared
updater, STOP, battery-hold and radio-key implementations rather than replacing
them with another copy. Framework adapters own native input/report conversion,
thread and interrupt synchronization, scheduling, native USB and RGB APIs,
settings serialization, authorization and peripheral lifecycle.

Preserve existing behavior at adapter boundaries. Existing intentional native
differences must be explicit inputs or policies, not copied algorithms. In
particular, VIA/Vial RGB index conversion remains separate; saved keycodes,
behavior IDs, settings formats, resident ABI, flash regions, native Vial unlock,
and ZMK's permanent no-watchdog policy remain unchanged. This refactor does not
enable unqualified brief runtime STOP or program a physical keyboard.

## Alternatives

Only sharing constants leaves the repeated implementations and parity failures.
A single framework-dependent implementation with pervasive QMK/Zephyr branches
entangles kernels and native APIs. The approved approach is common C algorithms
with narrow adapters, extending cross-framework reuse that already exists.

## Execution and verification

1. Record baseline source/build contracts and current tests.
2. Extract shared radio framing, battery and timing rules, preserving bounded
   raw-frame quarantine, checksum and ACK handling and command ownership.
3. Consolidate stock mappings, display calculations, hold/selection decisions,
   and power admission algorithms with native conversion outside the core.
4. Audit remaining overlap, including split/CRC/image and hardware helpers.
   Document native boundaries instead of treating framework-specific types or
   lifecycle operations as common algorithms.
5. Exercise the common implementations through both adapters, including damaged
   packets, held/released modifiers, physical versus selected output, cable
   transitions, stale battery, sleep admission and settings/updater boundaries.
6. Format edited C. Run `make test test-zmk check-tools`, then all supported
   affected QMK/VIA and Vial variants, ZMK Studio updater and standalone.
   Check linked image budgets and resident/settings compatibility. Builds and
   tests must never flash, reset or write a physical keyboard.

The ZMK updater application slot is already full. Avoid new threads, heap,
duplicate buffers and large callback tables; verify sizes rather than assuming
that a source refactor saves flash or RAM. Native hardware behavior and current
consumption remain unverified by host fixtures.

## Review resolutions

The builder check confirmed the architecture and required an actual consumer
inventory, preserved malformed-packet/rendering/release policies, explicit
execution/time contracts and attention to LTO/image size. The core README
records each VIA/Vial and ZMK consumer and its native boundary. Common routines
have caller-owned state/time and do not block or call device APIs. Golden
adapter tests and cross-framework traces retain native differences; all physical
hold/unlock/settings contracts remain with the native adapters. Shared inline
code stays inside the existing translation units, preserving their audited LTO
and linker-wrap boundaries rather than adding misclassified library objects.
