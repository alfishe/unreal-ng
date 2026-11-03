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
  In the loader's Pentagon scheme (`#00` = `#E4`) it saves as a Pentagon 128 (`.sna` included).
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

# Edge Grinder: insert, wait for the 128 menu (#7EFD & #7F = #40, frame 317 = 6.4 s on 6.44), Enter = "128 TR-DOS",
# any key on the controls screen, then #7EFD = #C8 (a disk inserted before the reset boots on its own, see below)
curl -s -X POST "$BASE/emulator/$EMU_ID/disk/A/insert" -H 'Content-Type: application/json' \
     -d "{\"path\": \"$PWD/testdata/machines/gmx/edgegrinder/EGGMX.TRD\"}"
curl -s "$BASE/emulator/$EMU_ID/state/paging" | jq -r '.gmx.port_7efd.value'   # 0x40 / 0xC0 (bit 7 = turbo)
curl -s -X POST "$BASE/emulator/$EMU_ID/keyboard/tap" -H 'Content-Type: application/json' -d '{"key":"ENTER"}'
```

`inspect_state aspects:["video"]` and the video map (`/video/layout`, `/video/pixel`, `/video/address`, family `gmx`) follow the same geometry: a pixel's
sources are the pixel and attribute bytes after the scroll. Verified 2026-10-30 (wt-gmx-impl): the monitor's test
screen, the Scorpion GMX 128 menu, Edge Grinder's attract mode in 640x200.

## Disks at power-on, firmware choice, hard disk

- **Autostart.** A disk the media manager boots (`/disk/A/insert` with autostart, the Qt drop, `--disk`) at power-on
  waits for ProfROM to finish: the loader, the monitor's logo and tests run first, then the deferred TR-DOS start
  resets into the plane's TR-DOS with `#7FBA` D7 set (the real drive A). The end of the boot is the first 48 BASIC
  KEY-SCAN (`IN #FE` at pc `#0296` on the plane's 48 ROM page) from the menu's IM 1 interrupt: frame 338 on 6.44,
  313 on v5.00. Paging writes are no sign (the monitor-call stub writes `#7EFD = #40` from `#0050` at frame 112).
  A disk inserted into a booted machine starts TR-DOS at once. Tests: `GmxAutostart_Test.*` in
  `core/tests/emulator/gmx_boot_test.cpp`.
- **Firmware.** `data/configs/gmx/unreal.ini` `GMX=rom\gmx644.rom` (ProfROM + GMX 6.44, the default) or
  `GMX=rom\gmx.rom` (Boot ROM V 5.00). There is no WebAPI switch: change the ini and restart. Programs that need v5
  RAM variables (GMX Commander) hang on 6.44; programs that check for v5.XX.40+ (Phanes) refuse 5.00 with a message.
  Each firmware keeps its own CMOS / NVRAM file (signature `#64` for 5.00, `#65` for 6.44), so switching does not
  re-initialize the other's settings. Compatibility per program:
  [software-compatibility.md](../../docs/inprogress/2026-10-07-gmx/software-compatibility.md).
- **Hard disk.** An image on `ide0.master` (media slot), ProfROM sees it after a cold reset with Caps Shift + 5 held;
  the 6.44 Partition manager formats MFS. `zxdisk` and the unreal-disk `DiskFs` read and write MFS partitions
  (`docs/emulator/design/disk-file-systems.md`, "ProfROM MFS layout").
  CP/M 2.2 (CPMZSGMX) wants an MFS subpartition `CPMZS0` of type MicroDOS (Hard disk utility: Magic, `H`, `P`;
  type the name with `keyboard/combo ["CAPS", <letter>]`), then its `INSTALL` item 3 to fill it; steps in
  [software-compatibility.md](../../docs/inprogress/2026-10-07-gmx/software-compatibility.md#media-used).

## Turbo, frame, Magic

- `#7EFD` bit 7: 7 MHz (`hw_turbo_ratio` 2). The card's own wait: an opcode fetch from RAM that starts on an odd
  7 MHz T waits one T (`even_M1` = 2, turbo only); ROM code runs exactly 2x, a byte copy loop from RAM 1.93x.
  No Turbo+ `IN` strobes, no host waits.
  Check: Edge Grinder in game, key R shows the frame rate: 25 (the real board's figure; 16 = too slow, 50 = too fast).
- `#00` bit 7 (ST/PENT): the Pentagon frame, 320 lines (71680 T) with INT 17988 T before the paper, and the ZX
  screen on the Pentagon raster and border (video mode `Pentagon128K`; 640x200 keeps its own); 0 = the Scorpion's
  312 lines (69888 T). `#00` survives RESET, power-off clears it. Check with `inspect_state aspects:["paging"]`
  (`gmx.port_00.pentagon_int`) and the frame length in the timing state.

## Pentagon timings: the loader's schemes

Hold Delete (Caps Shift + 0) at power-on: the loader V2.00 Set Up menu lists the FPGA schemes. The firmware itself
writes `#00`; nothing to configure in the emulator.

| Key | Scheme | `#00` | What runs |
|:--|:--|:--|:--|
| 2 | Scorpion (the default) | `#02` | the GMX: Scorpion frame, card ports, plane 4 |
| 4 | Pentagon | `#E4` | a Pentagon 128 (manual, "the Pentagon scheme"): no `#xxFD` ports, `#7FFD` at A1 = A15 = 0, 320 lines, 80 from INT to the screen, plane 1 ROMs |
| 7 | Pentagon7 | `#A7` | the Scorpion ports without the card's (BLKEXT) on the Pentagon frame, plane 3 "Reset Rom Loader" menu |

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/keyboard/press" -H 'Content-Type: application/json' -d '{"key":"cs"}'
curl -s -X POST "$BASE/emulator/$EMU_ID/keyboard/press" -H 'Content-Type: application/json' -d '{"key":"0"}'
curl -s -X POST "$BASE/emulator/$EMU_ID/reset" -H 'Content-Type: application/json' -d '{"hard":true}'
sleep 3; curl -s -X POST "$BASE/emulator/$EMU_ID/keyboard/release_all"
curl -s -X POST "$BASE/emulator/$EMU_ID/keyboard/tap" -H 'Content-Type: application/json' -d '{"key":"4"}'
curl -s "$BASE/emulator/$EMU_ID/state/paging" | jq '.gmx.port_00'      # value 0xE4, pentagon_int true
```

The choice lasts until power-off (RESET keeps `#00`). Verified 2026-10-31 (wt-gmx-impl): a border effect timed from
the INT draws the same frame on scheme 4 as on the `PENTAGON` model (`GmxBoot_Test.LoaderSchemeFourIsAPentagon128`).
- Magic (NMI, the CLI `nmi` / WebAPI `nmi` routes): ignored while `#7EFD` bit 2 is set; otherwise the read-backs of
  `#7AFD` / `#7EFD` stay frozen until an `IN #FF` (`gmx.magic.frozen`).

## SD card (Z-Controller) and slots

The card's Z-Controller (the 6.44 monitor's SD driver, `#77` D1 = /CS, `#57` the SPI byte, `#77` reads `#00`) is the
media manager's slot `sd.zc` (alias `sd`, tags `sd zcontroller primary boot`, FAT32 only, 500 ms swap delay), so every
plane's media verbs reach it: CLI `media insert sd <path>`, Lua / Python `media_insert("sd", path)`, MCP
`media action=insert slot=sd`, WebAPI and WebSocket `media/{slot}/insert`, the Qt Media menu.

```bash
# OSZ: the system disk in A:, the SD folder (a FAT32 volume) in sd, reset, Enter on "128 TR-DOS"
curl -s -X POST "$BASE/emulator/$EMU_ID/media/sd/insert" -H 'Content-Type: application/json' \
     -d "{\"path\": \"$PWD/testdata/machines/gmx/osz/sdcard\"}"
curl -s -X POST "$BASE/emulator/$EMU_ID/media/A/insert" -H 'Content-Type: application/json' \
     -d "{\"path\": \"$PWD/testdata/machines/gmx/osz/OS.TRD\"}"
curl -s "$BASE/emulator/$EMU_ID/media/sd.zc" | jq '{present, format, source}'      # folder-fat32
curl -s -X POST "$BASE/emulator/$EMU_ID/reset"
# wait for the 128 menu (#7EFD = #40), tap ENTER: "Found FAT: E:SD0,0 ... OS ver 2025.10.08" in 640x200 (#7EFD = #C8);
# Symbol Shift + Enter switches from the OS console to the None Commander with the card's files
curl -s -X POST "$BASE/emulator/$EMU_ID/keyboard/combo" -H 'Content-Type: application/json' \
     -d '{"keys":["ss","enter"],"frames":6}'
```

A folder is a FAT32 volume behind an MBR (the monitor finds the first partition, as the web Unreal's `osz.vhd` at
LBA 63). Verified 2026-10-30 (wt-gmx-impl): OSZ boots from the card into the None Commander 2025.09.10
(`GmxBoot_Test.OszBootsFromTheSdCard`, long tier).

Slots (`/slots`, MCP `slots`): the built-ins are the Scorpion's AY, Beta 128, Kempston joystick and `sd-zc`.
The config fits a NeoGS on `zxbus.1`; a classic General Sound replaces it as on any Scorpion
(`slots plug zxbus.1 gs ram=512k`, forced: same ports). History only:
Apollonov's 1999 article (City #18, ZX-Pilot #31) said the classic GS did not work with the card; the official site
then named the firmware of 20.04.99 as the fix, and Puzzle Bobble (2024) plays its samples through a GS on a GMX.

## The card at a glance

| Port | Write | Read |
|:--|:--|:--|
| `#xx00` (A7-A0 = 0) | CONF0-2, RECONF (bit 3: reload the scheme, CPU reset), FIXROM, BLKEXT, BLKEX2, ST/PENT | - |
| `#78FD` | RAM page at `#8000` (stored ^ 2) | BRD1 (bit 7), the page, bit 0 = the magic byte after RECONF |
| `#7AFD` | scroll start low (bits 7-4) | paging status |
| `#7CFD` | scroll start high (bits 5-0) | - |
| `#7EFD` | turbo, ROM plane (6-4), 640x200, MagEnable | card status |
| `#DFFD` | RAM page bits 4-6 for `#C000` | - |
| `#xx77` | Z-Controller: bit 1 = SD /CS | `#00` |
| `#xx57` | SPI byte to the SD card | SPI byte from the card |

- `#C000` page = `DFFD[2:0] << 4 | 1FFD[4] << 3 | 7FFD[2:0]` (128 pages).
- `#1FFD` bit 2 maps the plane's TR-DOS page at `#0000` with the Beta ports on.
- `#7EFD` bit 2 blocks the Magic button and lifts the `#7FFD` lock.
- RESET clears every card latch except `#00`; power-off (hard reset) clears `#00` as well.
- The host Turbo+ logic is cut off by the fitting: no IN strobes, no Even M1.
