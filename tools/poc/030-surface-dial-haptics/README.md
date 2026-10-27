# 030 — surface-dial-haptics

Feature: [docs/inprogress/2026-10-18-control-surfaces](../../../docs/inprogress/2026-10-18-control-surfaces/README.md)
(phase S0). Status: charter; first observations recorded, the felt measurements not done.

## Goals

1. What the Surface Dial (`045E:091B`, Bluetooth LE) gives an application on each OS: input reports, the report
   descriptor, feature and output reports, without seizing the device.
2. What each haptic field does when felt: steps per turn (Resolution Multiplier), Auto Trigger waveform, Repeat
   Count, Waveform Cutoff Time, Retrigger Period, Manual Trigger waveform.
3. Counts per turn at a given step setting: is one input count one step?
4. Latency: report intervals while turning fast (BLE connection-interval batching); output report → felt click.

## Requirements

- macOS first (the owner's machine), then Windows (with the system wheel on and off) and Linux (hidraw).
- Every write recorded with its exact bytes and the feature report read back after it.
- Felt results judged by the owner, one change at a time.

## Constraints

- No code from [daniel5151/surface-dial-linux](https://github.com/daniel5151/surface-dial-linux) (no license file);
  it is a reference for the layout only.
- Values inside the descriptor's logical ranges only (Cutoff 1..10, Retrigger 1..2000 ms, Auto Trigger 1..7).
- Restore the found state at the end: feature report 1 = `01 24 00 00 01 05 50 00`.

## Acceptance criteria / expected results

| # | Question | Expected | Decides |
|:--|:--|:--|:--|
| A1 | Input without seizing on macOS | yes | the IOHID backend can add usage `0x01:0x0E` |
| A2 | Steps = counts per turn at 12, 36, 72, 360 | equal | `counts_per_detent = 1` |
| A3 | Auto Trigger 3 at n steps: one click per count | yes | device-local detents usable |
| A4 | Repeat Count r: plays r or r + 1 times | to measure | the `tick` / `double` mapping |
| A5 | Waveform 4 (buzz continuous): length vs Cutoff | to measure | the `buzz` pattern; whether a limit buzz needs a stop |
| A6 | Free wheel without losing resolution: Auto Trigger 1 (none) with steps kept | no clicks, counts unchanged | `clicks: false` |
| A7 | Output report → felt click | < 30 ms | NFR-2 |

## Tools

- `dialhaptic.c` (here): `get`, `feature <steps> <auto> <cutoff> <retrigger> [repeat]`, `play <repeat> <waveform> <retrigger>`.
- `docs/inprogress/2026-10-17-controller-manager/probes/hiddump.c 300 091B`: descriptor and input reports. It drops
  consecutive duplicate reports, so same-direction turns collapse; A2 and A7 need a capture without that filter
  (a `hiddump` option or a copy here).
- Raw output: `scratch/2026-10-18-poc-030/`; the fixture kept: `testdata/controllers/hid-captures/surface-dial/macos-ble/`.

## Results

- [measurements-2026-10-18-macos.md](measurements-2026-10-18-macos.md): first observations on macOS (A1; the found
  state; two early writes).
