# 030 — measurements 2026-10-18, macOS 15 (Darwin 24.6), Dial firmware 29d51170

- A1 yes: `IOHIDManagerOpen(kIOHIDOptionsTypeNone)` and `IOHIDDeviceOpen(kIOHIDOptionsTypeNone)` succeed; reports
  arrive (`testdata/controllers/hid-captures/surface-dial/macos-ble/`). No system UI reacted to turns.
- Feature report 1 as found: `01 24 00 00 01 05 50 00` (36 steps, repeat 0, Auto Trigger 1 = none, cutoff 5,
  retrigger 80 ms): no local clicks by default.
- An early probe wrote output `01 03 03 00 00` (repeat 3, waveform 3 = click, retrigger 0, below the logical
  minimum): accepted (`kIOReturnSuccess`); not yet confirmed by feel.
- It wrote feature `01 0c 00 00 03 00 00 00` (12 steps, Auto Trigger 3, cutoff 0 and retrigger 0, both below the
  logical minimum): accepted; read back `01 0c 00 00 03 05 50 00`, so the out-of-range fields were ignored and the
  rest applied. Restored the same day with the probe: feature `01 24 00 00 01 05 50 00` written and read back.
- The capture filter dropped repeats: the fixture shows only direction changes, not every step, so it cannot show
  counts per turn or report intervals (A2, A7 open).
- Report `32 02` (System Control, Wake bit) arrives about 2 ms after each press report (`01 03`), not only on wake;
  a driver ignores it.
