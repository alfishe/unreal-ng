# Surface Dial, macOS 15 Bluetooth LE (045E:091B)

Firmware 29d51170 (`system_profiler SPBluetoothDataType`). Recorded 2026-10-18 on Darwin 24.6 with
`docs/inprogress/2026-10-17-controller-manager/probes/hiddump.c 300 091B` (IOHIDManager, every input report,
consecutive duplicates dropped: same-direction steps collapse into one line, so the file shows direction changes,
not every step). The owner touched, turned both ways and pressed the dial freely.

- `2026-10-18-descriptor-and-reports.txt`: `added` line (usage page 1, usage 14, max input 73), the 488-byte report
  descriptor, then `<ms> dev0 id=<id> len=<n> : <bytes>`.

Input report 0x01, 9 bytes: byte 1 bit 0 push, bit 1 touch; bytes 2-3 the rotation since the last report (int16 LE);
bytes 4-8 constants. Report 0x32 is System Control (sleep / wake); `32 02` follows each press report by about 2 ms. Layout, haptics and touch:
[surface-dial.md](../../../../../docs/inprogress/2026-10-17-controller-manager/surface-dial.md).

Touch latency (2026-10-19, `docs/inprogress/2026-10-17-controller-manager/probes/dialtouch.c`; `dialfeat.c` beside it reads the feature reports, a raw report log beside the owner's hand): the touch bit
comes on and goes off a second or more after the hand, both ways; the release always arrives (11 of 11 touches
in a minute), only late. No other report carries the hand: the device sends input reports 0x01 and 0x32 only, and
vendor page 0xFF07 (reports 0x2A-0x31) is Microsoft's CFU firmware update channel - feature 0x2A holds the firmware
version (`70 11 d5 29` = 29d51170), feature 0x02 the haptic waveform list (03 04 05); nothing is written there. The
backends therefore infer the hand from a turn or a press at once (`DialTouch` in `hid/drivers/surfacedial.h`) and
leave the release to the sensor; a touch the sensor never confirmed ends 3 s after the last action.
