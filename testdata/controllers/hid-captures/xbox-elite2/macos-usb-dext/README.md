# Xbox Elite Series 2, macOS 15 USB (XboxGamepad.dext)

The pad reported firmware 5.17 as the USB device version (0x0517) and 5.23.6.0 in `system_profiler` over
Bluetooth. Fixtures are keyed by OS and transport, not by firmware: a driver must parse any firmware that keeps the
report descriptor.

Recorded 2026-10-17 on Darwin 24.6 with `docs/inprogress/2026-10-17-controller-manager/probes/hiddump.c`
(IOHIDManager, every input report of 045E:0B00, consecutive duplicates dropped) and `sdlev.c`
(SDL3 3.4.18, `SDL_JOYSTICK_MFI=0`, HIDAPI driver) running at the same time.

Session (owner's sequence): started on onboard profile 1, P1-P4 one by one, marker, both triggers fully, profile
switch, P1-P4 again, marker, back; then free play.

- `session-2026-10-17-raw.txt`: `<ms> dev0 id=20 len=18 : <18 bytes>`. Every report is 18 bytes although the GIP
  header's length byte says 0x2f (47): the dext delivers the first 14 payload bytes only (buttons, triggers, sticks).
  The paddle byte and the profile byte of the 47-byte firmware 5.17 report are cut off.
- `session-2026-10-17-sdl3-hidapi.txt`: the SDL events of the same presses. No `paddle*` event; every paddle press
  arrives as the face button the onboard profile assigns.

Layout of the 18 bytes: `[0]` 0x20 GIP input, `[1]` flags, `[2]` sequence, `[3]` 0x2f length, `[4]` buttons
(0x04 menu, 0x08 view, 0x10 A, 0x20 B, 0x40 X, 0x80 Y), `[5]` D-pad / shoulders / stick clicks, `[6..7]` LT and
`[8..9]` RT (10-bit little-endian, 0x3ff full), `[10..17]` LX LY RX RY (signed 16-bit little-endian).

## GameController over USB

`session-2026-10-17-gc-events.txt` (`probes/gcev.m`, a later session on profiles 0-3): vendor "Controller",
`paddleButton1-4` nil, battery 0.00 state -1. Paddles on profile 0 give no event; on profiles 1-3 only the mirrored
face buttons (B Y X on profile 1, B Y A X on 2 and 3), the same as through IOHID and SDL.
