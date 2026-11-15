# Read the radio's battery state without another firmware flash

Use this when the keyboard and Windows keep showing an old battery percentage
in Bluetooth mode. It works with the existing stock radio and any main-MCU
firmware, including QMK/VIA, Vial and ZMK.

1. Leave the keyboard connected over Bluetooth with the symptom present.
   Keep the same profile and cable state. Close Bluetooth LE Explorer and other
   OTA/configuration inspection tools.
2. From this repository on Windows, run `tools\radio-ota\run-battery.cmd`.
   It uses built-in **Windows PowerShell 5.1**; no MSVC, administrator access or
   Python installation is required. It discovers the connected GMK70 profile.
3. Keep using the keyboard normally for about 30 seconds. Send the resulting
   `gmk70-battery-radio-*.json` beside the launcher. Do not power-cycle or switch
   to dongle first.

If Windows exposes more than one connected GMK70, supply its Bluetooth address:

```powershell
.\tools\radio-ota\run-battery.cmd -BluetoothAddress 'your-current-profile-address'
```

The collector takes three snapshots, twelve seconds apart. It reads the radio's
published/retained percentages, downward-filter counter, charging latch,
external-percentage override flag, sampler/ADC-read timer descriptors, power
phase, selected profile and connection state. It never reads ADC registers or
claims to measure battery voltage or current.

The sender accepts only eight fixed code/sensor read regions. It sends existing
`READ_DATA`/`READ_MEM` queries over the stock OTA service and consumes the response
using FF00 uncached reads; enabling notifications is unnecessary. It rejects
flash/RAM writes, erase, commit, code execution, reset, pairing, mode selection
and reads outside that allowlist. No firmware, settings or service is installed.
The older RAM trial remains restricted to its original verified device and
write scopes; this collector opens an additional read-only mode.

Four bounded flash/RAM code fingerprints must match the captured radio before
any state is decoded. These are layout checks, not verification of the complete
live application. A mismatch or timeout ends acquisition without retry. The JSON
contains decoded fields and request addresses/lengths, not raw RAM, Bluetooth
addresses, bonding keys or HID paths. Other bytes fetched alongside the selected
profile are discarded.

Interpretation requires the snapshots, not just one unchanged percentage:

- A growing downward counter while the displayed value stays fixed proves that
  the radio's filter is withholding lower estimates.
- A sampler remaining unarmed across snapshots points to sampling/recovery;
  a periodic timer between expiry and dispatch is reported separately. An armed
  timer alone does not establish a successful ADC conversion.
- An unexpected charging/full latch or external-percentage override points to a
  different control-state problem.

Bluetooth access itself can wake the radio. The first battery block is read
before the other checks to reduce that disturbance, and timestamps distinguish
it from the later timer/profile reads. Fields are sequential reads, not an atomic
snapshot. This is evidence collection; it does not certify a hardware diagnosis
or repair the estimator.

Offline tests never contact a keyboard:

```sh
source scripts/project.sh
pwsh -NoProfile -File tools/radio-ota/battery-tests.ps1
pwsh -NoProfile -File tools/radio-ota/ram-trial-tests.ps1
pwsh -NoProfile -File tools/radio-ota/tests.ps1
pwsh -NoProfile -File tools/radio-ota/winrt-tests.ps1
```
