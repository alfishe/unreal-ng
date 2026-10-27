# Xbox Elite Series 2, macOS 15 Bluetooth LE (045E:0B22)

The pad reported firmware 5.17 as the USB device version (0x0517) and 5.23.6.0 in `system_profiler` over
Bluetooth. Fixtures are keyed by OS and transport, not by firmware: a driver must parse any firmware that keeps the
report descriptor.

Recorded 2026-10-17 on Darwin 24.6 with `docs/inprogress/2026-10-17-controller-manager/probes/hiddump.c 180 0b22`
(IOHIDManager, every input report, consecutive duplicates dropped; the report descriptor is the third line of the raw
log) and `sdlev.c` (SDL3, `SDL_JOYSTICK_MFI=0`, HIDAPI driver) running at the same time. The owner pressed buttons and
the paddles freely; the onboard profile was not switched during the session.

Second session (`session-2026-10-17-profiles-*.log`), owner's sequence: profile 0, P1 P2 P3 P4 (upper right, lower
right, upper left, lower left); profile button; P1-P4 on profile 1; the same on profiles 2 and 3; profile button back
to 0.

- `session-2026-10-17-raw.txt`: `<ms> dev0 id=01 len=20 : <20 bytes>`; the 464-byte report descriptor.
- `session-2026-10-17-sdl3-hidapi.txt`: the SDL events of the same presses, with `paddle1`..`paddle4`.

Input report 0x01, 20 bytes, from the descriptor:

| Bytes | Usage | Content |
|:--|:--|:--|
| `[0]` | | report id 0x01 |
| `[1..4]` | Generic Desktop X, Y | left stick, 16-bit unsigned little-endian |
| `[5..8]` | Z, Rz | right stick, 16-bit unsigned |
| `[9..10]` | Simulation Brake | LT, 10-bit |
| `[11..12]` | Simulation Accelerator | RT, 10-bit |
| `[13]` | Hat switch | low nibble, 1..8, 0 idle |
| `[14..15]` | Button 1..15 | buttons |
| `[16]` | Consumer 0xB2 (Record) | bit 0: share |
| `[17]` | Consumer 0x85 (Order Movie) | **onboard profile**, 0-3; follows every press of the profile button (0 → 1 → 2 → 3 → 0) |
| `[18]` | Consumer 0x99 | low nibble; 0x05 throughout |
| `[19]` | Consumer 0x81 (Assign Selection) | **paddles**, low nibble: 0x01 = P1 (upper right), 0x02 = P2 (lower right), 0x04 = P3 (upper left), 0x08 = P4 (lower left); SDL names them `paddle1`, `paddle3`, `paddle2`, `paddle4` |

Chords are ORed (0x0a, 0x03, 0x07, 0x06 seen).

**The paddle bits are reported on every profile.** On profiles 1-3 the same report also carries the button the
profile assigns, in `[14]` (0x01 A, 0x02 B, 0x08 X, 0x10 Y):

| Profile | P1 | P2 | P3 | P4 |
|:--|:--|:--|:--|:--|
| 0 | paddle only | paddle only | paddle only | paddle only |
| 1 | + B | + Y | nothing (unassigned) | + X |
| 2 | + B | + Y | + A | + X |
| 3 | + B | + Y | + A | + X |

SDL3 reports `paddle*` on profile 0 only and the mirrored face button on profiles 1-3 (it zeroes the paddle bits when
the profile byte is non-zero).

## GameController next to raw IOHID

`session-2026-10-17-gc-events.txt` (`probes/gcev.m`: `pressedChangedHandler` on every element of
`physicalInputProfile`) and `session-2026-10-17-gc-raw.txt` (`hiddump 180 0b22`) were recorded at the same time, in
two processes. Owner's sequence: P1-P4 on profile 0, then P1-P4 on profiles 1, 2 and 3.

- GameController reports `GCXboxGamepad` with `paddleButton1-4` and `buttonShare` nil, vendor "Xbox Wireless
  Controller", haptics with the handle and trigger localities, battery 0.00 with state -1 (unknown) although
  `system_profiler SPBluetoothDataType` reports "Battery Level: 100%" and "Firmware Version: 5.23.6.0" for the same
  pad.
- Profile 0: the raw log has P1-P4 (`[19]` 0x01, 0x02, 0x04, 0x08); GameController has **no event at all**.
- Profiles 1-3: GameController has only the mirrored face buttons (B Y X on profile 1, B Y A X on 2 and 3).
- Raw IOHID input keeps arriving while GameController holds the device: the two coexist.
