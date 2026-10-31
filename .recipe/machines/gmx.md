# Recipe: ZS Scorpion + GMX

The GMX card (Graphic Memory eXpander, MOA 1998) sits in the CPU socket of a ZS Scorpion and brings its own Z80,
2 MB of RAM, a 512 KB flash of eight 64 KB ROM planes and a register file on top of the host board. Model id `GMX`,
2048 KB only.

Ground truth:
[portdecoder_scorpiongmx.h](../../core/src/emulator/ports/models/portdecoder_scorpiongmx.h),
config [data/configs/gmx/unreal.ini](../../data/configs/gmx/unreal.ini),
design [docs/inprogress/2026-10-07-gmx/design.md](../../docs/inprogress/2026-10-07-gmx/design.md).
Everything not listed here is the host Scorpion: see [scorpion.md](scorpion.md).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"GMX"}

invoke_api {"method":"GET","path":"/emulator/{id}/ports"}
#   → the card rows #0000 (p00), #78FD, #7AFD, #7CFD, #7EFD, #DFFD with decoded fields

invoke_api {"method":"GET","path":"/emulator/{id}/state/paging"}
#   → banks[0].role names the plane and page: the loader hands over to "GMX plane 4: 128K Editor/Menu ROM"
#     (#7EFD = #C0); the v6.44 firmware then settles in plane 5 page 3 with 640x200 on (#7EFD = #D8),
#     waiting for a key with interrupts on
```

## WebAPI

```bash
EMU_ID=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "GMX"}' | jq -r '.id')

# Port map with the card rows and their decoded latches
curl -s "$BASE/emulator/$EMU_ID/ports" | jq '{model, live, rows: [.entries[] | {port, device, tags}]}'

# The ROM plane and page behind #0000
curl -s "$BASE/emulator/$EMU_ID/state/paging" | jq '.banks[0] | {type, page, name, role}'
```

## State, TTD, snapshots

```bash
# The card's register file (the same node on CLI `paging`, Lua / Python paging_state(), MCP inspect_state paging)
curl -s "$BASE/emulator/$EMU_ID/state/paging" | jq '.gmx | {port_00, port_7efd, page_8000, page_c000, scroll_start, magic}'

# TTD: the card rides as GmxPaging (70) next to ScorpionProfROM (6); seek restores the pages behind #78FD / #DFFD
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/start" | jq .
```

```text
inspect_state {"aspects":["paging"]}         # .paging.gmx: the card's latches decoded
time_travel  {"action":"seek","frame":N}    # the card's latches, the plane and the pages come back
```

- Snapshots: with the card's paging off (`#78FD` = `#DFFD` = 0, `#1FFD` bit 2 clear, 640x200 off) the machine
  saves as a Scorpion ZS 256; otherwise every format answers `needs: mode:scorpion256` and names what is in use.
- A Scorpion `.z80` / `.szx` (and any 48K / 128K snapshot) loads into the Scorpion scheme: ROM plane 4, card pages off.
- State transfer: a Spectrum / Scorpion state moves into a GMX; a GMX moves to a Scorpion 256 while its paging is off
  and pages 16-127 are empty; with `#78FD` / `#DFFD` / its own plane in use, only to another GMX.

## Video: 640x200 and Edge Grinder

`#7EFD` bit 3 turns the extended mode on: 640x200 in a 736x288 frame (48 px borders), 80 linear bytes per line from
page `#39` (`#3B` with `#7FFD` bit 3), the standard attribute byte per 8x1 pixels at the same offset in page `#79`
(`#7B`), scrolled by the start address `(#7CFD & #3F) << 8 | (#7AFD & #F0)` with the wrap at 16000 bytes.

```bash
# The monitor's fast test is already in 640x200 a few seconds after power-on
curl -s "$BASE/emulator/$EMU_ID/state/screen" | jq '{video_mode, framebuffer, gmx_640x200}'
curl -s "$BASE/emulator/$EMU_ID/capture/screen" | jq -r .data | base64 -d > gmx.png     # 736x288

# Edge Grinder: insert, wait for the 128 menu (#7EFD = #40, ~75 s: the monitor's IDE probe times out without a
# drive, as on the board and in Unreal), Enter = "128 TR-DOS", any key on the controls screen, then #7EFD = #C8
curl -s -X POST "$BASE/emulator/$EMU_ID/disk/A/insert" -H 'Content-Type: application/json' \
     -d "{\"path\": \"$PWD/testdata/machines/gmx/edgegrinder/EGGMX.TRD\"}"
curl -s "$BASE/emulator/$EMU_ID/state/paging" | jq -r '.gmx.port_7efd.value'               # wait for 0x40
curl -s -X POST "$BASE/emulator/$EMU_ID/keyboard/tap" -H 'Content-Type: application/json' -d '{"key":"ENTER"}'
```

`inspect_state aspects:["video"]` and the video map (`/video/layout`, `/video/pixel`, `/video/address`, family `gmx`) follow the same geometry: a pixel's
sources are the pixel and attribute bytes after the scroll. Verified 2026-10-30 (wt-gmx-impl): the monitor's test
screen, the Scorpion GMX 128 menu, Edge Grinder's attract mode in 640x200.

## Turbo, frame, Magic

- `#7EFD` bit 7: 7 MHz (`hw_turbo_ratio` 2). The card's own wait: an opcode fetch from RAM that starts on an odd
  7 MHz T waits one T (`even_M1` = 2, turbo only); ROM code runs exactly 2x, a byte copy loop from RAM 1.93x.
  No Turbo+ `IN` strobes, no host waits.
- `#00` bit 7 (ST/PENT): the Pentagon frame, 320 lines (71680 T) with INT 17988 T before the paper; 0 = the
  Scorpion's 312 lines (69888 T). `#00` survives RESET, power-off clears it. Check with
  `inspect_state aspects:["paging"]` (`gmx.port_00.pentagon_int`) and the frame length in the timing state.
- Magic (NMI, the CLI `nmi` / WebAPI `nmi` routes): ignored while `#7EFD` bit 2 is set; otherwise the read-backs of
  `#7AFD` / `#7EFD` stay frozen until an `IN #FF` (`gmx.magic.frozen`).

## The card at a glance

| Port | Write | Read |
|:--|:--|:--|
| `#xx00` (A7-A0 = 0) | CONF0-2, RECONF (bit 3: reload the scheme, CPU reset), FIXROM, BLKEXT, BLKEX2, ST/PENT | - |
| `#78FD` | RAM page at `#8000` (stored ^ 2) | BRD1 (bit 7), the page, bit 0 = the magic byte after RECONF |
| `#7AFD` | scroll start low (bits 7-4) | paging status |
| `#7CFD` | scroll start high (bits 5-0) | - |
| `#7EFD` | turbo, ROM plane (6-4), 640x200, MagEnable | card status |
| `#DFFD` | RAM page bits 4-6 for `#C000` | - |

- `#C000` page = `DFFD[2:0] << 4 | 1FFD[4] << 3 | 7FFD[2:0]` (128 pages).
- `#1FFD` bit 2 maps the plane's TR-DOS page at `#0000` with the Beta ports on.
- `#7EFD` bit 2 blocks the Magic button and lifts the `#7FFD` lock.
- RESET clears every card latch except `#00`; power-off (hard reset) clears `#00` as well.
- The host Turbo+ logic is cut off by the fitting: no IN strobes, no Even M1.
