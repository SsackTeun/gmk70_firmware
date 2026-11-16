# Shared GMK70 library

VIA, Vial and ZMK compile these framework-independent GMK70 sources directly.
Portable policy and protocol helpers live in `core/`; compiled implementations
live in `battery/`, `power/`, `radio/`, `update/` and `wb32/`. Small core routines
are inline so each build can specialize its adapter policies and discard unused
paths within the fixed firmware slots.
There is no generated/copy tree, separate runtime, heap, or new worker.

| Common implementation | VIA / Vial consumer | ZMK consumer |
| --- | --- | --- |
| `core/board.h`: physical LED data and battery thresholds | board config and indicators | lighting and controls |
| `core/policy.h`: channel/status translation, timing, freshness, charger and critical/inactivity decisions | wireless, lowpower and board adapters | radio, battery and power adapters |
| `core/radio_commands.h`: stock external-radio wire ABI | module API | radio core and battery UART owner |
| `core/radio_protocol.h`: packet construction, checksum, streaming RX and bounded raw-tail quarantine | `drivers/wireless/module.c` | `battery/battery.c`, `radio/core.c` |
| `core/radio_tx.h`: ACK deadlines, finite retry decisions and task-phase helper | module task and native SMSG queue | radio service worker |
| `core/selection.h`: physical-selector debounce | `wls/wls.c` | `radio/selection.c` |
| `core/hold.h`: source-position ownership and hold deadline | pairing adapter | radio selector |
| `core/function_flip.h`: stock HID mappings and held/release state | native QMK keycode converter | native ZMK HID converter |
| `core/indicators.h`: battery segments, charging/radio colors, radio LED selection and blink phase | stock battery and radio indicators | battery view and controls |

The compiled shared sources are `battery/battery_holds.c`,
`power/stock_power.c`, `radio/radio_keys.c`, `wb32/wb32_stop*.c`, and
`update/image.c`, `update/protocol.c` and `update/trial.c`. There are no copies
or forwarding headers at their old QMK locations. Zephyr CMake and QMK/Vial
Make compile these actual files. QMK/ChibiOS adapters remain in
`firmware/qmk-common/`; ZMK/Zephyr adapters remain in `firmware/zmk/`.

## Adapter boundaries

Core routines use caller-owned state and explicit wrapping millisecond times.
They do not access devices, invoke native input/report functions, block, acquire
locks or start timers. The owning adapter serializes calls and performs effects
after the appropriate interrupt/spinlock boundary.

QMK's native direct HID send path, threaded control FIFO and deferred callbacks
remain its adapter. ZMK's report mailbox/FIFO, complete-frame UART ownership,
native endpoint listeners, connection barriers and work deadlines remain its
adapter. Both use the same protocol and finite retry implementation. Queue
saturation, partial transmission, stale ACK attribution and native report
clearing still require their existing adapter tests.

Preserved policies are explicit:

- QMK drops an invalid raw second byte; ZMK may resynchronize on that byte.
  ZMK requires the exact ACK trailer; QMK also accepts an additive-valid
  nonstandard `0x61` notification. Neither can dispatch quarantined raw tails.
- QMK advances retry over task passes at a strict timeout. ZMK retries at its
  inclusive service deadline. Battery inquiry keeps its one-retry budget.
- VIA/Vial battery segments use intensity 150; ZMK uses 80. Native LED-index
  conversion, RGB effects, DMA/SPI lifecycle and rail ownership remain adapters.
- QMK keeps unchanged Windows F-key processing native. ZMK intercepts it and
  retains a release-cancellation marker after native HID clears. Both use the
  same stock mapping and held-input transition algorithm.
- Keycode values, native Studio behavior IDs, layout definitions and saved
  configuration formats remain native. Flash authorization, native Vial unlock,
  resident ABI and image/settings bounds stay with their established owners.

Framework-provided matrix debounce, USB controllers, native RGB engines,
settings storage and RTOS suspend/resume are not replaced by common substitutes.
The shared STOP primitive still needs each owner to manage wake sources,
clocks, timers and framework state. Brief runtime STOP remains unqualified;
this refactor does not enable it or change radio firmware.

## Verification

Load `scripts/project.sh`, then run `make test-common`, `make test test-zmk
check-tools`, and the affected ARM builds. Common tests exercise all 256 raw
lengths, delayed/faulted tails, timer wrap, retry limits and both adapters'
policies. Compiled traces link the actual QMK/Vial converter and ZMK converter
and check held Alt, settings/OS changes, media, brightness and native clear
semantics. Existing adapter tests cover the hardware-facing/native boundaries.
Host tests and build/image checks do not establish physical power savings or
hardware stability and never flash/reset/write a keyboard.

The completed refactor passed `make test test-zmk check-tools`, cross-framework
traces against both pinned QMK variants, five QMK/VIA builds, three Vial builds,
and both canonical ZMK builds with pinned xPack GCC 14.2.1. The ZMK updater uses
106,488/106,496 application bytes and 28,020/28,544 RAM bytes; standalone uses
101,288/114,688 application bytes and 27,164/28,544 RAM bytes. These RAM figures
are linker reservations, not measured peak usage. Resident/settings boundaries,
dependency revisions and the VIA definition are preserved.

The neutral-directory extraction moved all 24 shared C/header files unchanged,
along with this README. All ten canonical builds and the full regression/tooling
checks passed again. The pinned-Zephyr permanent/qualification/relay receiver
compile check also passed. The resident retains CRC `0x698B3EC1`; the canonical
ZMK updater application is byte-identical to the pre-move image.
