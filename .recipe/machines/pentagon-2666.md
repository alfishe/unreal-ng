# Recipe: Pentagon 2.666 LE

A Pentagon 1024 (71680-T frame, no contention, `#7FFD` / `#EFF7`) with the ATM Turbo 2 shadow layer (`#xx77`, two
window maps chosen by `#7FFD` bit 4, ATM video modes, the `#xxFF` palette), 2 MB, 3.5 / 14 / 28 MHz, Gluk CMOS,
Z-Controller SD, Nemo IDE, two YM2149, Covox, a 16550 UART and a Beta Disk emulator over a RAM disk. Model id
`P2666` (`P2666LE` matches by prefix), 2048 KB only. The model is the board as built: its bugs are copied
(even Nemo IDE registers read `#FE`, the 8-bit sound sum wraps).

Ground truth:
[portdecoder_p2666.h](../../core/src/emulator/ports/models/portdecoder_p2666.h),
config [data/configs/p2666/unreal.ini](../../data/configs/p2666/unreal.ini),
hardware reference [docs/inprogress/2026-11-11-pentagon-2666-le/hardware-reference.md](../../docs/inprogress/2026-11-11-pentagon-2666-le/hardware-reference.md),
real software [testdata/machines/p2666/software/README.md](../../testdata/machines/p2666/software/README.md).

## Boot

The board always starts in Gluk Reset Service (DOS on, `#7FFD` = 0) at 28 MHz, whatever `RESET=` says. The MCU's
power-on upload puts the ROMs into write-protected RAM pages `#48..#4C`, the logo into page 5, the default palette
and the 64 CMOS cells (`[P2666] CmosFile`). A CPU reset keeps RAM, the palette and the CMOS.

Gluk menu: cursor down 7 times + Enter = "trdos" (TR-DOS from the RAM disk, drive A); key 5 = SD boot (NedoOS).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"P2666"}
media {"action":"insert","slot":"fdd.a","path":"<abs>/testdata/machines/p2666/software/pentagon-tests.trd"}
media {"action":"insert","slot":"sd","path":"<abs>/testdata/machines/p2666/software/nedoos/sdcard"}
inspect_state {"aspects":["paging"]}   # .paging.p2666: #xx77, #EFF7, clock, DOS, the window map
```

A disk in drive A goes into the RAM disk (pages `#76` down; images over 41 pages are refused). `[P2666]
BetaDisk=wd1793` gives the plain WD1793 with drives A..D instead.

The board has no floppy controller (manual 2.9). `inspect_state {"aspects":["fdc"]}` (CLI `state fdc`, WebAPI
`/state/fdc`, Lua / Python `fdc_state()`) reports the FPGA's WD1793 imitation on the RAM disk: registers, status,
`phase` (every level-set command flag: a step written during a restore reads `restore+step`), INTRQ / DRQ, the
system register `#FF` (drive, reset, side), the head cylinder, the RAM-disk pages and drive A.

## WebAPI

```bash
EMU_ID=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "P2666"}' | jq -r '.id')

# The board: video mode, 28 / 14 / 3.5 MHz, DOS, the active map with each window's kind and page
curl -s "$BASE/emulator/$EMU_ID/state/paging" | jq '.p2666 | {port_xx77, port_eff7, cpu_mhz, dos, map, windows}'

# TR-DOS disk into the RAM disk, then Gluk's "trdos" entry
curl -s -X POST "$BASE/emulator/$EMU_ID/disk/A/insert" -H 'Content-Type: application/json' \
     -d "{\"path\": \"$PWD/testdata/machines/p2666/software/pentagon-tests.trd\"}"
for i in 1 2 3 4 5 6 7; do
  curl -s -X POST "$BASE/emulator/$EMU_ID/keyboard/tap" -H 'Content-Type: application/json' -d '{"key":"DOWN"}'
done
curl -s -X POST "$BASE/emulator/$EMU_ID/keyboard/tap" -H 'Content-Type: application/json' -d '{"key":"ENTER"}'
```

The same `p2666` node is on CLI `paging`, Lua / Python `paging_state()` and MCP `inspect_state` aspect `paging`.

## State, TTD, snapshots

- TTD: the board rides as P2666Board (71: the FPGA latches, the window registers, the palette words, the INT pulse,
  the CMOS RAM), P2666Sound (72: both YM2149, Covox, the beeper), P2666BetaDisk (73, only with the RAM disk; the
  disk bytes ride with the RAM), P2666Uart (74), next to the MCU clock (Ds12887, 18) and the SD card (EvoSdCard,
  15). Every recorded frame replays to the next checkpoint (`P2666Ttd_Test`).
- A 48K / 128K `.sna` / `.z80` / `.szx` loads as on a Pentagon 128: every window in Pentagon mode, DOS off, the ZX
  picture, `#EFF7` = 128 mode. The clock select stays (a snapshot has no clock); set 3.5 MHz first with `#EFF7` bit 4
  for timing-exact programs. A locked `#7FFD` (a 48K `.z80` writes `#30`) maps page 32 + bank at `#C000` on this
  board (D-3): the load puts the bank there too, so the program sees its RAM.
- A snapshot is taken only while the board is that Pentagon 128 (every window in Pentagon mode, the ZX picture,
  `#EFF7` bit 2 set, bit 0 clear); otherwise every format answers `needs: mode:pentagon128` with the reason. It
  saves as a Pentagon 128.
- State transfer: a Spectrum 128K state moves into the board the same way; the board in its own paging moves only to
  another P2666.

## Pitfalls

- 28 MHz at power-on: a test that counts T-states sets `#EFF7` bit 4 (turbo0, 3.5 MHz) and clears `#xx77` bit 3.
- The frame is 49.70 Hz (base clock 3.5625 MHz), not 50 Hz.
- Even Nemo IDE registers read `#FE` (the board's read mux): IDE drivers fail as on the board.
- `#FE` = `#20` with `#EFF7` bit 2 clear also clears the INT flag (FE bit 5).
