# Recipe: Peters Plus Sprinter Sp2000

The Sprinter is a PC-like Spectrum clone: a Zilog Z84C15 CPU (3.5 or 21 MHz),
4 MB RAM, 64 KB fast RAM, a PLD (ACEX EP1K30) that the BIOS loads at power-on,
a 256 KB flash BIOS, its own video (text 40 / 80 columns, 320 x 256 in 256
colors, 640 x 256 in 16 colors, chosen per 8 x 8 square by a mode table in
video RAM), a WD1793 floppy controller for 720 KB and 1.44 MB disks, a DS12887A
CMOS clock and an AT keyboard on the Z84C15's serial port. Its disk operating
system is Estex DSS; Spectrum programs run in a Pentagon 128 mode that DSS
starts.

The one thing to know first: **every port goes through a table in RAM.** The
BIOS writes a 16 KB table into RAM page `#40`; for each port access the PLD
reads one byte of it, the *internal code*, and that code picks the device
(`#10` = WD1793 command, `#C1` = `#7FFD`, `#2B` = IDE primary, ...). Which byte
is read depends on the port's address bits, the direction, the TR-DOS signal,
`#7FFD` bit 5 and the *map* (0-3) the CNF register selects. The automation
shows the table decoded and resolves single ports (see [Ports](#ports-the-table-and-the-codes)).

Ground truth:
[portdecoder_sprinter.h](../../core/src/emulator/ports/models/portdecoder_sprinter.h),
the state report [sprinterdevicestate.cpp](../../core/src/emulator/ports/models/sprinter/sprinterdevicestate.cpp),
the design and status in
[docs/inprogress/2026-09-28-sprinter/](../../docs/inprogress/2026-09-28-sprinter/)
(`hardware-reference.md` for the codes and the port table, `automation-outcome.md`
for what the automation offers, `bios-versions.md` for the BIOS images). MCP
resource `unreal://machine/sprinter` is the one-page summary.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. The
> [WebAPI](#webapi) section has the same steps as curl for host-side
> pipelines (policy: [_common/transports.md](../_common/transports.md)).
> Shared patterns: [_common/machines.md](../_common/machines.md).

Outputs below are real, from a build of branch `sprinter-automation`
(2026-10-02), trimmed.

## MCP (preferred)

### 1. Create the machine and pick the BIOS

```text
emulator_manage {"action":"list_models"}
#   → models[] has {"name":"SPRINTER","full_name":"Sprinter 2000","creatable":true,
#     "available_ram_sizes_kb":[4096],"default_ram_kb":4096}
emulator_manage {"action":"create","model":"SPRINTER"}                     # BIOS from [ROM] SPRINTER= (3.07 BETA 1)
emulator_manage {"action":"create","model":"SPRINTER","sprinter_bios":"3.07","sprinter_fast_start":true}
#   → Created and started emulator 4ff43195-...            (sprinter_bios: 3.04 | 3.06 | 3.07 | a file in rom/sprinter)
inspect_state {"aspects":["sprinter_bios"]}
#   → [sprinter_bios] loaded sp2k-3.07-beta1.rom, configured rom/sprinter/sp2k-3.07-beta1.rom, fast_start on, accel_int_suspend off
#       3.04 rom/sprinter/sp2k-3.04.rom
#       3.06 rom/sprinter/sp2k-3.06-hf2.rom
#       3.07 rom/sprinter/sp2k-3.07-beta1.rom [loaded]
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/sprinter/bios","body":{"bios":"3.06","fast_start":false,"reset":true}}
#   → {"loaded":"sp2k-3.06-hf2.rom","previous_rom_file":"rom/sprinter/sp2k-3.07-beta1.rom","reset_done":true,
#      "reload_pending":false,"options":{"fast_start":false,"accel_int_suspend":false}, ...}
```

**Choosing the BIOS** at runtime: at create (`sprinter_bios`; WebAPI body `"sprinter":{"bios":"3.06"}`, CLI
`create SPRINTER --sprinter-bios 3.06`) or on a running machine (`POST /sprinter/bios`, CLI `state sprinter
bios 3.06`, Lua / Python `sprinter_bios_select`). The selection goes into this instance's configuration and
the flash is reread at the next reset (`reset: true`, the default, resets now; `reset: false` leaves
`reload_pending: true` until the next reset). `loaded` names the image the flash holds (by CRC-32), so a
mismatch with `rom_file` is visible. A reset stops a TTD recording and a new image invalidates its session.
`[ROM] SPRINTER=` in `<build>/bin/configs/sprinter/unreal.ini` (macOS: `unreal-qt.app/Contents/Resources/configs/sprinter/unreal.ini`)
remains the default for new machines: shipped `rom/sprinter/sp2k-3.07-beta1.rom` (owner decision 2026-10-02;
3.04 and 3.06 Hotfix 2 stay selectable). DSS 1.71 needs 3.06 or 3.07. **Warning:** on 3.07 BETA 1, DSS 1.71.57
(the MAME pack's disk) cannot start programs from a floppy or copy files off it (`known_issues` in the BIOS report;
the beta's floppy driver changes IY): for floppy work select 3.06 Hotfix 2. Background:
[bios-versions.md](../../docs/inprogress/2026-09-28-sprinter/bios-versions.md).

**Full start vs fast start** (`fast_start` of the selection above, default `[SPRINTER] FastStart=`):
`0` (the default) runs the ROM's PLD loader first, as the real machine does -
473 720 configuration writes, about 1.9 s of emulated time with a black
screen; `1` starts with the PLD already configured (the tests use it). Either
way `sprinter.pld` ends `"state":"configured","module":"Standard"`; the
bitstream hashes name the image (3.07 BETA 1: `full_hash 0x29641AB3`, `head_hash 0x49861031`; 3.04:
`full_hash 0xFC0928F2`, `head_hash 0x78EDDFC6`).

### 2. Boot DSS from a floppy

SETUP boots the IDE master first, then the "alternative device": **floppy A on
the default BIOS 3.07 BETA 1 (and 3.06), floppy B on BIOS 3.04**. Checked live
2026-10-02 with a blank CMOS: `dss_1_62_92.img` in drive A boots DSS 1.62.92 on
3.07 BETA 1 straight into Flex Navigator 1.10 (`A:\FN\`); in drive B 3.07 prints
"Alternative Boot from Diskette fail". The steps and outputs below are BIOS 3.04
with the disk in B: create with `"sprinter_bios":"3.04"` to follow them as they
are, or keep the default and use drive A. With no hard disk every IDE unit reads
an empty channel and SETUP prints "None" for each at once (no F4 needed; [sprinter-hdd.md](../media/sprinter-hdd.md)).

The Sprinter has no ZX screen: read its text with the `sprinter_text` aspect; `video_text` and
`screen_ocr` read the same cells while most of the picture is text (Spectrum mode stays a ZX screen).

```text
load_software {"path":"/abs/path/testdata/machines/sprinter/dss_1_62_92.img","drive":"B"}
#   → Inserted disk dss_1_62_92.img (uploaded) into drive B of <id>
inspect_state {"aspects":["sprinter_text"]}          # poll until "B:\>"
#   → [sprinter_text] 1280 text squares, mode page 1
#       Model name: Sprinter                    Sprinter BIOS: ver 3.04.253
#       ...
#        Detecting IDE Primary Master   ... None
#        Detecting IDE Primary Slave    ... None
#       Start from Hard disk...fail
#       Alternative Start from Diskette...Ok
#       Starting DOS...
#       Estex DSS Version 1.62.92
#       B:\>fn
```

The shipped floppy's `SYSTEM.BAT` ends with `fn` (Flex Navigator, the DSS
file manager), which does **not** run yet: it draws its logo and hangs. To get
a usable prompt, boot a copy without that line (host-side, into `scratch/`):

```bash
python3 - <<'EOF'
# DSS 1.62 floppy without the Flex Navigator autostart: SYSTEM.BAT is root entry 3,
# its data cluster 49 = LBA 80; drop the last line "fn"
img = bytearray(open('testdata/machines/sprinter/dss_1_62_92.img', 'rb').read())
entry = 19 * 512 + 2 * 32
assert img[entry:entry + 11] == b'SYSTEM  BAT'
size = int.from_bytes(img[entry + 28:entry + 32], 'little')
assert img[80 * 512 + size - 4:80 * 512 + size] == b'fn\r\n'
img[entry + 28:entry + 32] = (size - 4).to_bytes(4, 'little')
open('scratch/dss162-nofn.img', 'wb').write(img)
EOF
```

With `scratch/dss162-nofn.img` in drive B the boot ends at
`Estex DSS Version 1.62.92` / `B:\>`, ready for commands.

**DSS 1.71** (the owner's MAME pack, `IMG/dss171u.img`; not in the repo) is
inserted the same way, but does not start yet: the BIOS loads it from drive B
("Alternative Start from Diskette...Ok"), then the screen says
`Fatal error! Press RESET to restart.` (checked 2026-10-02).

### 3. Type DSS commands

`type_input` sends the host's PC keys, which reach DSS through the Z84C15
serial port (the keyboard FIFO is in `sprinter.z84c15.keyboard`):

```text
type_input {"action":"type","text":"dir\n"}
inspect_state {"aspects":["sprinter_text"]}
#   →   B:\>dir
#       Volume in drive B: is SYSTEM  DOS
#       Volume Serial Number is 2D3A-96C2
#       Directory of B:\
#       SYSTEM   DOS         16 035  17.12.20  13:27
#       SYSTEM   EXE          7 424  22.03.21  16:39
#       ...
#       ZX            <DIR>          22.03.21  17:01
#               6 file(s)       364,060 bytes
#       B:\>
capture_media {"action":"screenshot","format":"png","area":"full","filename":"/abs/path/scratch/dss-dir.png"}
#   → Screenshot saved to .../scratch/dss-dir.png (736x288 ...)
```

`\` types as the PC backslash (step 4 runs `a:\zx\spectrum.exe`). Single keys: `type_input {"action":"tap","key":"f4"}`; PC-only keys take a
`pc.` prefix where a ZX name would win (`pc.esc`, `pc.delete`); Ctrl+Alt+Del
(CPU reset, the PLD stays configured):
`type_input {"action":"combo","keys":["lctrl","lalt","pc.delete"]}`. DEL on the
BIOS logo opens SETUP.

### 3a. Keys and mouse in Flex Navigator

Flex Navigator reads keys through DSS (Tab switches panels, arrows, PgUp / PgDn,
Home / End, Ins, F1-F10, Alt+F1 / Alt+F2 drives, Ctrl+F1-F12 views, Ctrl+U swap;
the list is `FN\README.ENG` on the disk). Send them as PC keys:

```text
type_input {"action":"tap","key":"tab"}              # the other panel
type_input {"action":"tap","key":"down"}
type_input {"action":"combo","keys":["lalt","f1"]}   # drive menu of the left panel
```

The mouse goes through DSS's mouse driver, which reads the board's mouse in one
of two views: DSS 1.62.9x the PLD's Kempston view (`#FADF` buttons, `#FBDF` X,
`#FFDF` Y; port code `#58`), DSS 1.71 the Microsoft serial packets on SIO B
(1 200 baud, 3 bytes synced on bit 6; no `M` identification is needed). Both
views read one set of board mouse counters, a device of the emulator's mouse
manager like the Kempston interface: `mouse_input`, the GUI's captured host
mouse and TTD replay all reach it, also with `[INPUT] Mouse=NONE` (the board's
mouse is part of the machine, not the optional Kempston interface; with `NONE`
`/mouse/status` reports `"present":false` for the interface and no warning):

```text
mouse_input {"action":"move","dx":100,"dy":-20}      # + right, + up (Kempston axes); FN: 1 pixel per count
mouse_input {"action":"click","button":"left"}       # inactive panel: activates it; on a row: the bar goes there
mouse_input {"action":"click","button":"right"}      # marks the file under the bar, the bar moves down
inspect_state {"aspects":["sprinter"]}
#   → sprinter.z84c15.mouse: {"buttons":"0xFF","framing_errors":0,"mouse_baud":1200,"packet_in_flight":false,
#      "sio_b_baud":1215.2777777777778,"sio_b_fifo":"","sio_b_in_tune":true,"x":31,"y":85}
```

SIO B receives the mouse with CTC ZC/TO0 as its clock: DSS 1.71 programs CTC 0
`#55` with 45 and SIO B x16 (WR4 `#44`), 875 kHz / 45 / 16 = 1 215 baud. A
program that leaves SIO B off 1 200 baud by more than 5 % gets no mouse
characters (`sio_b_in_tune: false`, `framing_errors` counts them), as on the
board.

Buttons: D0 left, D1 right, D2 middle (active low). The Kempston view shows all
three; the serial packet has left and right only; FN ignores the middle button.

WebAPI equivalent:

```bash
curl -s -X POST $BASE/emulator/$EMU_ID/mouse/move  -H 'Content-Type: application/json' -d '{"dx":38,"dy":-10}'
curl -s -X POST $BASE/emulator/$EMU_ID/mouse/click -H 'Content-Type: application/json' -d '{"button":"left","frames":10}'
curl -s "$BASE/emulator/$EMU_ID/state/sprinter" | jq -c .z84c15.mouse
```

**In the GUI** the toolbar's mouse button is live for the Sprinter with any
`Mouse=` setting: click the screen to capture (that click is not passed on),
Cmd+Esc (Ctrl+Esc elsewhere) releases; the button switched off keeps the host
mouse away from the machine (automation still works). Checked on the GPU
renderer with FN 1.15 (BIOS 3.07 beta 1, `sp_hdd_sys.chd`), 2026-10-02.

After a panel switch FN 1.10 re-reads the floppy for about 0.6 s with
interrupts off; keys sent in that time overrun the SIO's 3-byte FIFO
(`sprinter.z84c15.keyboard.overruns`). Wait a moment between keys.

**Keyboard overrun, as on the board.** Nothing holds the AT keyboard off: while
the CPU reads nothing (DI, a long ISR, a Spectrum-mode program), every byte
after the third overwrites the newest one in the SIO FIFO. Then
`sprinter.z84c15.sio[0]` shows `overrun_in_fifo: true` (a written-over byte
still queued) and, once that byte reaches the top, `overrun: true` (RR1 bit 5,
until the program's Error Reset). BIOS 3.04 / 3.05 and DSS 1.62 take one key
per frame and never clear it, so a lost `F0` turns a break into another make
there; BIOS 3.06 / 3.07 and DSS 1.71 empty the FIFO and forget the shift keys on
an overrun. The keyboard itself sends only what the keys did: it repeats a key only
while it is held (500 ms, then 10.9 per second).

**F12 and Ctrl+Alt+Del** act in the PLD, which reads the keyboard wire, not the
SIO: an overrun never switches the turbo. Each F12 make byte without Shift /
Ctrl / Alt flips the turbo switch (`clock` in `/state/sprinter`), so holding F12
past half a second flips it again with every typematic repeat, as on the board.
In the GUI, keys held when the screen loses focus are released (the PS/2 keys
and the ZX matrix keys alike).

### 3b. Demos paced by the CTC (Bad Apple, dontBlink)

Some DSS 1.71 programs run their playback on a Z84C15 CTC interrupt instead of
the frame INT: CTC 2 counts the board's 875 kHz TRG2 by 112, ZC/TO2 drives
TRG3, CTC 3 counts by 160 and interrupts at 48.83 Hz with vector `#06` (IM 2).
On the owner's DSS 1.71 disk (BIOS 3.06, `sp_hdd_sys.chd` as above), from
Flex Navigator (the left panel on the root of C:):

```bash
tap() { curl -s -X POST $BASE/emulator/$EMU_ID/keyboard/tap -H 'Content-Type: application/json' \
             -d "{\"key\":\"$1\",\"frames\":3}" >/dev/null; sleep 0.3; }
tap down; tap down; tap enter; sleep 2          # C:\DEMOS
tap down; tap enter; sleep 2                     # BADAPPLE (DNTBLINK: 4 x down)
for k in b a d a p p l e period e x e; do tap $k; done; tap enter
sleep 15
curl -s "$BASE/emulator/$EMU_ID/state/sprinter" | jq -c '.z84c15.ctc.channels[3] | {control, mode, count, zero_counts, zc_to_hz, trigger_input}'
#   → {"control":"0xD5","mode":"counter","count":28,"zero_counts":479,"zc_to_hz":48.828125,
#      "trigger_input":{"kind":"cascade","source":"ZC/TO2"}}
curl -s "$BASE/emulator/$EMU_ID/state/sprinter" | jq -c '.sound.covox_blaster | {mode, rate_hz, ticks, ring_writes}'
#   → {"mode":"covox-blaster","rate_hz":21875.0,"ticks":377447,"ring_writes":755072}
```

`zero_counts` grows by one per 20.48 ms frame; `count` is the live
down-counter. MCP: `inspect_state {"aspects":["sprinter"]}`, the same
`z84c15.ctc` tree (CLI `state sprinter`, Lua / Python `sprinter_state`).
Checked live 2026-10-02 (branch `sprinter-ctc-trg`).

### 4. Spectrum mode and TR-DOS

The community BIOS (3.06 / 3.07, the default) carries the ZX ROMs: ESC at its boot prompt starts the
Spectrum 128 menu "Sprinter" directly. BIOS 3.04 has no Spectrum ROMs; DSS's `SPECTRUM.EXE` loads them from
`A:\ZX\ROMS`. So put a second copy of the floppy in drive A **before the
boot** (a medium cannot be in two drives; a disk inserted into A after DSS
started was not seen by the launcher):

```text
load_software {"path":"/abs/path/scratch/dss162-a.img","drive":"A"}      # cp scratch/dss162-nofn.img scratch/dss162-a.img
load_software {"path":"/abs/path/scratch/dss162-nofn.img","drive":"B"}
# ... wait for "B:\>" as in step 2 ...
type_input {"action":"type","text":"a:\\zx\\spectrum.exe a:\\zx\\pent128.zx\n"}
inspect_state {"aspects":["screen_ocr"]}             # poll until the 128 menu
#   →        Sprinter?????
#            ?TR-DOS      ?
#            ?Hardware    ?
#            ?128 BASIC   ?
#            ...
#      1986 Sinclair Research Ltd
inspect_state {"aspects":["sprinter"]}
#   → [sprinter] PLD configured (Standard), map 1, DOS off, 21 MHz, 320 lines, text, 40 columns
#       window 0: vROM 0x42
#       window 1: RAM 0x05
#       window 2: RAM 0xF6
#       window 3: RAM 0x07
```

The Spectrum mode switches to port map 1, puts a Spectrum ROM image from RAM
into window 0 (`vROM`, read-only) and turns the ZX screen shadow on
(`registers.all_mode.zx_screen_shadow: true`). Now `screen_ocr` works; the
picture is the Spectrum screen drawn by 40-column squares. TR-DOS (Sprinter
TR-DOS 7.01) reads TRD images from drive A at 720 KB:

```text
load_software {"path":"/abs/path/testdata/loaders/trd/zx-format8.trd","drive":"A"}
type_input {"action":"tap","key":"enter"}            # the menu's first entry: TR-DOS
type_input {"action":"tap","key":"k"}                # LIST (keyword mode)
type_input {"action":"tap","key":"enter"}
inspect_state {"aspects":["screen_ocr"]}
#   → Title: AMD4ever  Disk Drive: A
#     20 File(s)      80 Track D. Side
#     ...
```

### 5. Inspect the machine

```text
inspect_state {"aspects":["sprinter"]}               # PLD, windows, registers, cells, clock, frame, video, Z84C15, fdc, BIOS
inspect_state {"aspects":["paging"]}                 # the four windows as banks (type = window kind)
#   → [paging] model Sprinter Sp2000, locked no, trdos inactive
#       bank0 0x0000-0x3FFF ROM p8
#       bank1 0x4000-0x7FFF RAM p5 ...
inspect_state {"aspects":["rtc"]}                    # the DS12887A CMOS (sprinter.cmos only links here)
inspect_state {"aspects":["fdc"]}                    # the WD1793 registers and drives
```

What `sprinter` carries (the WebAPI JSON is the same tree):

| Block | Fields |
|:--|:--|
| `pld` | `state` (unconfigured / loading / configured), `module`, `bitstream` (writes, `full_hash`, `head_hash`, `fast_start`), `dcp_open` (the BIOS opened the port decoder), `dcp_opened_frame`, `dcp_opened_pc` |
| `decoder` | `map` (0-3, CNF bits 4-3), `cnf`, `dos` (TR-DOS on), `pn5`, `port_7ffd`, `port_1ffd` (after the CNF clean rules) |
| `windows[4]` | `kind` (ROM, loader ROM, fast RAM, vROM, RAM, graphics, ISA, port table, RAM (reset page)), `page`, `writable`, `cell`, `note` |
| `registers`, `cells` | ROM_RG, SYS_PG, ALL_MODE decoded, PORT_Y, RGMOD (mode page), HOLD, SCALE; cells `#C0-#FF` as hex rows |
| `clock`, `frame` | turbo requested / front-panel switch, `ratio` 1 or 6, `mhz` 3.5 / 21; `lines` 320 / 312, `t_states` 71 680 / 69 888 |
| `clock.waits` | the 21 MHz rule, `active`, `windows_waiting[4]` (main RAM waits, ROM and fast RAM not), the taken clocks |
| `clock.original_waits` | the ZX mode's PLD `WAIT_ORIG` (ALL_MODE bit 2 = 0 at 3.5 MHz, ORIGIN.ZX): `active`, `all_mode_bit2`, `rule`, `period_t` 4, `phase_t` (a placeholder until a board is measured), `windows_waiting[4]` (window 1; window 3 while `#7FFD` bit 2 is set) |
| `tape` | `time_base`: `base_clock` - the tape input counts real time, so load tapes in a 3.5 MHz mode (P128.ZX, ORIGIN.ZX); at 21 MHz the ROM loader fails, as on the board |
| `video` | `picture_mode` (the dominant content kind of the 640 x 256 picture; `picture_mode_key`, e.g. `spectrum` in the Spectrum mode, `graphics_640` in Flex Navigator), `picture_mixed` (more than one content kind), `picture_brief` (the GUI status bar's words), `squares` by kind, HOLD offsets, `int_positions` (frame INTs the mode table places); per square: step 6 |
| `accelerator` | `enabled`, `mode_name`, `length`, `function`, `blocked`, `operations`, `buffer_crc32` ([sprinter-accelerator.md](sprinter-accelerator.md)) |
| `sound` | the AY (chips, clock, stereo from its config) and the Covox-Blaster (control, rate, indices, counters; the ring: `sprinter_sound_ring`) - [sprinter-sound.md](sprinter-sound.md) |
| `z84c15` | WCR / MWBR / CSBR / MCR, `wait_generator` (WCR / MWBR decoded), `daisy_chain` (priority order, IP / IUS per source), watchdog with `deadline_clock`, `ctc` (`time_base_hz`, `cpu_clock_hz`; per channel the mode, prescaler, edge, timer start, `trigger_input` - `clock` 875 kHz / `cascade` ZC/TO2 -, the live `count`, `zero_counts`, `zc_to_hz` and what ZC/TO drives), SIO A (keyboard) / B (mouse) with their FIFOs, PIO, `keyboard` (INT on, bytes on the way, overruns) |
| `fdc` | `density_latch` (720 KB code `#16` / 1.44 MB code `#17`), the WD1793 `clock` (1 / 2 MHz) and `data_rate` (250 / 500 kbit/s), `drive` |
| `cmos`, `ide` | links: the CMOS report is `rtc`; `ide` shows the selected channel and data latch, the drives are in `state/ide` (see [sprinter-hdd.md](../media/sprinter-hdd.md)) |
| `bios` | the configured ROM file, `loaded` (by CRC-32), the pages identified by signature, the shipped images, `options`, `reload_pending`, how to select (also `sprinter_bios`) |

### 6. Video: the mode table, palettes, video RAM, the change log

Verified on Flex Navigator 1.15 (DSS 1.71 from the HDD, BIOS 3.07) and on the BIOS 3.04 text screen:

```text
inspect_state {"aspects":["sprinter_video"]}         # one letter a square (squares[b][a] decoded: invoke_api GET /state/sprinter/video)
#   → [sprinter_video] page 1, RGMOD 0x01, HOLD 0x77, 320 lines, PORT_Y 0xC0, palettes 4 5 6 7
#       G graphics 320 (256 colors), g graphics 640 (16 colors), T text 40, t text 80, Z Spectrum screen cell (ZX-40), B border, . blank, * blank with the frame INT
#       tttttttttttttttttttttttttttttttttttttttt          (the BIOS: 40 x 32 squares of 80-column text; FN: all "g")
#       BBBBZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZBBBB          (the Spectrum mode: rows 4-27; rows 0-3 and 28-31 all "B";
#                                                     picture_mode "spectrum", the status bar "Spectrum 256x192, screen 5")
inspect_state {"aspects":["sprinter_palette"]}       # the palettes the picture uses, R G B as video RAM holds them
#   → [sprinter_palette] used by the picture (R G B per pen)
#       0 graphics 0: 000000 FF0000 008000 ...
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/video/pixel","query_params":{"x":"300","y":"150"}}
#   → {"colour_index":9,"rgb":"#000080", sources: mode bytes at vram 0x7F40, pixel byte 0x2187E, palette_entry 0x27E0}
inspect_state {"aspects":["memory_region"],"region":"vram","address":10208,"size":3}
#   → [memory_region] vram 0x027E0, 3 bytes
#       027E0: 00 00 80                                         (pen 9: R 00, G 00, B 80 - the blue background)
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/memory/region/vram","body":{"offset":"0x27E0","hex":"800000"}}
#   → {"bytes_written":3,"region":"vram","success":true}      (the panels turn dark red: the pen follows at once)
inspect_state {"aspects":["video_changes"]}          # every machine: latch changes with T / line / PC, table writes per frame
#   → [video_changes] (running: the last completed frame)
#       frame 8937: 28 latch changes, palette writes 0, mode table writes 0
#         T 64737 (line 289, T 1) PC 0x0B3D: port_y 0xC0 -> 0x80          (FN's vertical copies step PORT_Y)
#       paused right after the write above: frame 10037 (current): ..., palette writes 2 (first 0x027E0, last 0x027E2)
inspect_state {"aspects":["screen_digest"]}          # hashes the 256 KB video RAM (+ RGMOD page, HOLD, frame height)
#   → active_surface {"memory":"vram","bytes":262144, ...}; the palette write above made "changed": true
capture_media {"action":"framebuffer","format":"index"}
#   → Framebuffer 736 x 288 index (u16le pen per pixel ...), 423936 bytes; include_image:true for the base64 data
```

The square map reads the displayed mode page (`page=0|1` for the other, `all=1` for the whole 56 x 40
table). Palette layout: pen `k x 256 + n` = bytes R, G, B at video RAM row `n`, column `#3E0 + 4k`;
palettes 0-3 graphics, 4-7 text paper / ink / their flash phase (border and blank: palette 4). Video RAM
writes go through the PLD's own path (pens refresh, the frame INT follows); RAM pages `#50-#5F` (the CPU's
graphics copy) are not touched. `/video/address?space=vram&offset=` maps a video RAM byte to its pixels.

### Ports: the table and the codes

```text
inspect_state {"aspects":["sprinter_ports"]}         # the table for the current map / DOS / PN5
#   → [sprinter_ports] map 0, DOS off, PN5 0: 64 rows
#       0x15 r xxxx xxxx 000x x111  BetaState
#       0x1C r 1x1x xxxx 101x x101  CmosRead
#       0x20 r xxxx xxxx 010x x000  IdeData
#       ...
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/state/sprinter/ports/lookup","query_params":{"port":"21BC","rw":"w"}}
#   → structuredContent.body: {"port":"0x21BC","map":0,"dos":false,"pn5":false,"from_machine":true,
#      "results":[{"direction":"w","answered_by":"PLD","index":"0x043C","index_in_map":"0x043C",
#                  "address_bits":"0x003C","code":"0x2B","name":"IdePrimary"}]}
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/state/sprinter/ports","query_params":{"map":"0","dos":"1","rw":"w"}}
#   → rows[] {"code":"0x10","name":"FdcCommand","direction":"w","pattern":"xxxx xxxx 000x x111","example":"0x0007","addresses":8} ...
```

A pattern lists A15..A0, `x` = not decoded. `map`, `dos` (1 = TR-DOS on),
`pn5` default to the machine's current state; `rw` is `r`, `w` or `rw`. The
Z84C15 answers `#10-#13` (CTC), `#18-#1B` (SIO), `#1C-#1F` (PIO), `#EE/#EF`,
`#F0/#F1`, `#F4` itself (`answered_by: "Z84C15"`, trace code `#100` + low
byte). `IN A,(#1F)` / `OUT (#1F),A` executed from RAM reach the bus as
`#xx0F` (the lookup of `#1F` says so): that is how TR-DOS reaches the WD1793.

The port trace records the code of every access (`code`, `code_name`) and
filters on it - recipe [analysis/port-trace.md](../analysis/port-trace.md#internal-port-codes-zx-evo-sprinter):

```text
analyze_performance {"action":"porttrace","frames":10,"limit":8}
#   → structuredContent.events.events[]:
#     {"direction":"IN","raw_port":"0xFF19","value":"0x04","pc":"0x051E","code":"0x0119","code_name":"Z84 SIO A control"}
#     {"direction":"IN","raw_port":"0xFF89","value":"0xC0","pc":"0x360B","code":"0x00C4","code_name":"PortY"}
#     {"direction":"IN","raw_port":"0xFFDF","value":"0x55","pc":"0x34E7","code":"0x0058","code_name":"KempstonMouse"}
```

Side by side with MAME's `sprinter` driver: [analysis/sprinter-mame-compare.md](../analysis/sprinter-mame-compare.md).

## WebAPI

```bash
BASE=http://localhost:8090/api/v1
EMU=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' -d '{"model":"SPRINTER"}' | jq -r .id)
curl -s "$BASE/emulator/$EMU/state/sprinter" | jq '.pld.state, .decoder, .windows, .bios.images'
curl -s -X POST "$BASE/emulator/$EMU/disk/B/insert" -H 'Content-Type: application/json' \
     -d "{\"path\":\"$PWD/scratch/dss162-nofn.img\"}" | jq .status          # "success"

txt() { curl -s "$BASE/emulator/$EMU/state/sprinter/text" | jq -r '.lines[].text'; }
until txt | grep -q 'B:\\>$'; do sleep 1; done
curl -s -X POST "$BASE/emulator/$EMU/keyboard/type" -H 'Content-Type: application/json' -d '{"text":"dir\n"}' | jq -c .
sleep 3                                                                   # the keys take a few frames
txt | grep -v '^$' | tail -5
#   ESTEX    ZIP        336 804  24.03.21  19:14
#   INSTALL  TXT          3 083  24.03.21  19:15
#   GAMES         <DIR>          24.03.21  19:05
#   PICS          <DIR>          24.03.21  19:12
#           6 file(s)       364,060 bytes

curl -s "$BASE/emulator/$EMU/state/sprinter/ports?map=0&dos=1&rw=w" | jq -r '.rows[] | [.code, .name, .pattern] | @tsv' | head -3
#   0x10  FdcCommand  xxxx xxxx 000x x111
#   0x11  FdcTrack    xxxx xxxx 001x x111
#   0x12  FdcSector   xxxx xxxx 010x x111
curl -s "$BASE/emulator/$EMU/state/sprinter/ports/lookup?port=7FFD" | jq -c '.results[] | {direction, index, code, name}'
#   {"direction":"r","index":"0x06FD","code":"0xC1","name":"7FFD"}
#   {"direction":"w","index":"0x04FD","code":"0xC1","name":"7FFD"}
curl -s "$BASE/emulator/$EMU/state/paging" | jq -c '.banks[] | {bank, type, page}'      # the Sprinter windows
curl -s "$BASE/emulator/$EMU/state/memory/rom" | jq '.total_rom_pages'                  # 16
curl -s "$BASE/emulator/$EMU/ports" | jq '.live.sprinter_port_table'                    # map, DOS, PN5 now
curl -s "$BASE/emulator/$EMU/capture/screen?area=full&format=png&path=$PWD/scratch/sprinter.png" | jq -c .
#   {"file":".../scratch/sprinter.png","format":"png","height":288,"saved":true,"size":...,"status":"success","width":736}

# Video (step 6), BIOS (step 1), raw pixels
curl -s "$BASE/emulator/$EMU/state/sprinter/video?squares=0" | jq -c '{mode_page, counts, frame}'
#   {"mode_page":0,"counts":{"blank":0,"border":0,"graphics_320":0,"graphics_640":1280,...},"frame":{"lines":320,...}}
curl -s "$BASE/emulator/$EMU/state/sprinter/palette?k=0" | jq -c '.palettes[0].pens[9]'      # {"n":9,"rgb":"#000080","vram":"0x027E0"}
curl -s "$BASE/emulator/$EMU/memory/region/vram?offset=0x27E0&length=3" | jq -c .            # {"hex":"000080",...}
curl -s "$BASE/emulator/$EMU/video/changes?frames=1" | jq -c '.frames[0].writes[0]'
#   {"changes":{"port_y":"0xC0 -> 0x80"},"line":289,"pc":"0x0B3D","t":64737,"t_in_line":1}
curl -s -D - -o scratch/fb.idx "$BASE/emulator/$EMU/capture/framebuffer?format=index" | grep -i '^x-'   # X-Width: 736, X-Height: 288
curl -s "$BASE/emulator/$EMU/state/sprinter/bios" | jq -c '{loaded, rom_file, reload_pending, options}'
```

`/state/sprinter/text`, `/state/sprinter/ports[/lookup]` and `/state/sprinter`
answer 404 with "Not a Sprinter machine" on other models; a bad `map` / `dos`
/ `pn5` / `rw` / `port` is a 400 with the reason.

## CLI / Lua / Python

```text
state sprinter                          # the report (also: state sp)
state sprinter text                     # the screen text
state sprinter ports map=0 dos=1 rw=r   # one line per row:
#   Sprinter port table: map 0, DOS on, PN5 0, RAM page #40, offset #0000
#   code dir  A15..A0 (x = any)    example  ports  name
#   #10  r    xxxx xxxx 000x x111  #0007        8  FdcCommand
#   #C1  r    01xx xxxx 111x x101  #40E5        2  7FFD
#   ...
#   Unmapped (code #00, reads #FF): 375 of the address combinations
state sprinter port 21BC rw=w           # index: 0x043C, code: 0x2B, name: IdePrimary
state sprinter port 1F rw=r             # answered_by: Z84C15 (PIO B control) + the #0F operand-rewrite note
state sprinter video                    # "Sprinter mode table: page 1 (displayed), RGMOD 0x01, ..." then one letter a square
state sprinter palette 4                #   10: 0000A8 0000A8 ... (16 pens a line, R G B)
state sprinter bios 3.06 reset=1        # select; plain "state sprinter bios" prints the report
memory region read vram 0x17F0 6        # 0x17F0: 00 00 00 00 A8 00
video changes 1                         # the change log as text
digest                                  # "Mode: Sprinter, surface: vram (262144 bytes ...)"
state memory                            # windows as "Sprinter Paging (PLD)"; state rom: the 16 ROM pages and their roles
```

```lua
local s = sprinter_state(); print(s.pld.state, s.decoder.map, s.windows[1].kind)   -- configured 0 ROM
local t = sprinter_ports({map = 0, dos = 1, rw = "w"}); print(#t.rows)          -- 41
local l = sprinter_port(0x21BC, {rw = "w"}); print(l.results[1].code)          -- 0x2B
for _, line in ipairs(sprinter_text().lines) do print(line.text) end
print(sprinter_video{squares=false}.map[1])                                      -- tttttttt... (BIOS)
print(region_read("vram", 0x17F0, 6)[5], sprinter_bios().loaded)                -- 168  sp2k-3.04.rom (switched to 3.04)
local fb = framebuffer("index"); print(fb.width, fb.height, #fb.data)           -- 736 288 423936
```

```python
import unreal_emulator as unreal
emu = unreal.emu_get_selected()
s = emu.sprinter_state(); print(s["pld"]["state"], s["clock"]["mhz"])          # configured 21
t = emu.sprinter_ports(map=0, dos=1, rw="w"); print(t["rows"][0]["name"])     # FdcCommand
l = emu.sprinter_port(0x21BC, rw="w"); print(l["results"][0]["index"])        # 0x043C
print(emu.paging_state()["sprinter"]["windows"][3]["kind"])                    # RAM
v = emu.sprinter_video(squares=False); print(v["map"][0][:8])                  # tttttttt
print(emu.region_read("vram", 0x17F0, 6).hex())                               # 00000000a800
fb = emu.framebuffer("index"); print(fb["array"].shape)                        # (288, 736) with numpy
emu.sprinter_bios_select(bios="3.06", reset=True)
```

(The Python surface is in the build with `-DENABLE_PYTHON_AUTOMATION=ON`; the examples use the same names
and core reports as Lua.)

## What works / what doesn't

| Area | State |
|:--|:--|
| PLD load (full start) and fast start, the port table, the windows, fast RAM, graphics pages, the 21 MHz turbo with its waits | implemented |
| Video: every square kind, palettes, HOLD, the mode-table INT, 320 / 312 lines | implemented |
| WD1793 at 720 KB and 1.44 MB (the `#BD` density latch), DSS 1.62 from floppy B, Spectrum mode, TR-DOS | implemented |
| AT keyboard and serial mouse on the Z84C15 SIO; `type_input`, key taps and combos | implemented |
| DS12887A CMOS (`[SPRINTER] CmosFile=` keeps it) | implemented |
| Flex Navigator (DSS's `fn`): FN 1.10 from the DSS 1.62 floppy, FN 1.15 on DSS 1.71 (HDD, BIOS 3.06) | runs; keys and mouse work through the automation (step 3a). From the floppy the BIOS RESTORE is still too slow at 21 MHz (the S5 tests work around it) |
| DSS 1.71 | runs from the hard disk with BIOS 3.06 / 3.07 (MAME pack `sp_hdd_sys.chd`: Flex Navigator 1.15, verified 2026-10-02); the floppy `dss171u.img` **stops** with "Fatal error! Press RESET to restart." on BIOS 3.04. On BIOS 3.07 BETA 1, DSS 1.71.57 cannot start programs from a floppy ("Invalid EXE file"; the beta's FDD driver changes IY, MAME agrees): copying the file to C: inside the machine fails too (a 0-byte file); use BIOS 3.06 (`POST /sprinter/bios {"bios":"3.06"}` + reset) or the DSS of the 3.07 recovery disk; `state/sprinter/bios` reports it in `known_issues`, unreal-qt shows "BIOS: known issue" in the status bar ([bios-versions.md](../../docs/inprogress/2026-09-28-sprinter/bios-versions.md) §5.2) |
| IDE hard disks | implemented (two channels, [sprinter-hdd.md](../media/sprinter-hdd.md)); an empty channel reads `#7F`, so the BIOS reports "None" without waiting |
| Sound: one AY at 1.75 MHz (ABC), beeper, Covox, Covox-Blaster (ring, rates, INT, 16-bit stereo) | implemented (S6, [sprinter-sound.md](sprinter-sound.md)) |
| Accelerator | implemented (S5, [sprinter-accelerator.md](sprinter-accelerator.md)) |
| ISA cards (General Sound on the ZX-bus adapter, `PROPLAY.EXE` MODs) | not yet (S6b); the ISA view reads `#FF` |
| TTD (time travel) | implemented (S7, [analysis/sprinter-ttd.md](../analysis/sprinter-ttd.md)) |
| Automation of video modes, palettes, video RAM, the change log, BIOS selection, mixer | implemented (automation audit 2026-10-02: step 1, step 6, [sprinter-sound.md](sprinter-sound.md)) |

## Pitfalls

- **Text from graphics screens.** BIOS, SETUP and DSS screens are Sprinter
  text squares: `sprinter_text`, `video_text` and `screen_ocr` read them
  (while most of the picture is text). Flex Navigator draws its text as
  graphics: no text path; compare pictures with `screen_digest` or
  `capture_media framebuffer`. Graphics squares read as spaces.
- **F4 in the GUI** is the "speed 8x" shortcut (`unreal-qt` menu), so at the
  IDE wait F4 may not reach the machine there; the automation's
  `type_input` / `keyboard/tap` always does. On a Mac keyboard DEL is
  Fn+Delete.
- **Tab in the GUI** reaches the machine on both screen renderers (it used
  to move the Qt focus on the software renderer, so every other Tab was lost).
- **The GUI mouse** works on both screen renderers through the shared mouse
  manager (step 3a); a plain Esc reaches the machine, Cmd+Esc / Ctrl+Esc
  releases the capture.
- **The generic `#7FFD` latch fields are not the Sprinter's.** `#7FFD` and
  `#1FFD` live in the PLD: read `sprinter.decoder.port_7ffd` /
  `port_1ffd` (and `/state/paging`'s `sprinter` block), not
  `paging.port_7ffd` of other models.
- **The table can change.** The port table is RAM: a program can rewrite
  page `#40` or switch the map (CNF). Look ports up with the current state
  (the default) unless you ask about another map on purpose.
- **A disk in A after DSS started** was not read by `SPECTRUM.EXE`; insert
  both floppies before the boot.
- **Uploads**: MCP `load_software` uploads a file that exists on the MCP host;
  floppies up to 4 MB are accepted (1.44 MB PC images included).

### ZX DLSS de-flicker in the Spectrum mode

Verified 2026-10-03 (WebAPI on a spare port, BIOS 3.06, the MAME-pack disk): in Flex Navigator type
`\zx\spectrum.exe \zx\p128.zx \trd\across\0.trd`, ENTER, ENTER on TR-DOS, `R` ENTER, ENTER on ACROSS.

```bash
curl -s -X PUT $BASE/emulator/$EMU/video/temporal -H 'Content-Type: application/json' -d '{"algorithm":"mod-tpgwafsd"}' |
  jq -c '{active, applicable, inactive_reason, video_delay_frames}'
#   Spectrum mode → {"active":true,"applicable":true,"inactive_reason":"","video_delay_frames":7}
#   Flex Navigator → {"active":false,"applicable":false,
#                     "inactive_reason":"not applicable: Sprinter native mode (640x256 16c): ZX DLSS works in the Spectrum mode only", ...}
```

The processed picture is `GET /capture/framebuffer?format=rgba` (the presented frame; `/capture/screen?area=full` is the same
pixels, encoded). `/capture/planeb` is the Sprinter's 736 x 288 plane B: the ZX frame the algorithm gets is every second pixel
of its 704 x 288 window at (16, 0). CLI `video temporal`, MCP `capture_media` `temporal_status`, Lua / Python
`video_temporal()` report the same fields. Design: `docs/inprogress/2026-09-27-zxdlss-gigascreen/temporal-effects-manager.md` §7.

### ZX-mode timing: the zxtime program

Verified 2026-10-02 on a GUI build (WebAPI on a spare port, BIOS 3.06 HF2, the MAME-pack disk as a CHD with
`ZXTIME.TRD` in `C:\TRD`): `spectrum origin.zx zxtime.trd`, the standard 128 menu down to TR-DOS, `R` ENTER.
ORIGIN.ZX's TR-DOS 5.04Em reads only the real floppy: with no disk in `fdd.a` it says "Disc Error" (the launcher's
RAM disk is for the Sprinter TR-DOS 7.03 of SP / P128 / P512); insert the same TRD as `fdd.a` and `R` ENTER again.

```bash
BASE=http://localhost:8090/api/v1
tap() { curl -s -X POST $BASE/emulator/$EMU/keyboard/tap -H 'Content-Type: application/json' -d "{\"key\":\"$1\",\"frames\":3}"; sleep 1; }
curl -s -X POST $BASE/emulator/$EMU/keyboard/type -H 'Content-Type: application/json' -d '{"text":"spectrum origin.zx zxtime.trd"}'; tap enter
tap down; tap down; tap down; tap down; tap enter       # Tape Loader, 128 BASIC, Calculator, 48 BASIC, TR-DOS
curl -s -X POST $BASE/emulator/$EMU/media/fdd.a/insert -H 'Content-Type: application/json' -d "{\"path\":\"$PWD/zxtime.trd\"}"
tap r; tap enter                                        # RUN: about 5 s later the report
curl -s "$BASE/emulator/$EMU/state/sprinter" | jq -c '.clock.original_waits | {active, windows_waiting}, .tape.time_base'
#   → {"active":true,"windows_waiting":[false,true,false,false]}  "base_clock"
```

The screen then reads `FRAME 69888 T`, `RATE 50.08 frames/s`, `INT 50 in 50 frames`, `REPEAT 0`, and the screen
reads `#4000 996`, `#C000 page 5 996`, `#C000 page 1 0` (thousandths of a T: the original waits, MAME prints 0).
The same block on the other surfaces: MCP `inspect_state {"aspects":["sprinter"]}` adds the lines "original waits
(ALL_MODE bit 2 = 0) ..." and "tape in real time ...", CLI `state sprinter` prints `original_waits:` and `tape:`,
Lua `sprinter_state().clock.original_waits.active`, Python `emulator.sprinter_state()["clock"]["original_waits"]`
(the Python surface needs a build with `ENABLE_PYTHON_AUTOMATION=ON`). What each line means and the numbers of every
launcher mode: [testdata/machines/sprinter/zx-timing/README.md](../../testdata/machines/sprinter/zx-timing/README.md),
[tdd-zx-mode.md](../../docs/inprogress/2026-09-28-sprinter/tdd-zx-mode.md) §4.1.

After Ctrl+Alt+Del from any mode started with `/ret-fn` the machine is back in DSS at 21 MHz (the PLD presets its
turbo bit on the reset); `/state/sprinter` shows `registers.all_mode.value` `0xFF` and `clock.mhz` `21`.

### Which ZX mode runs, and who changed the PLD (CNF, turbo, `#1FFD`)

Two questions that used to take an investigation: is this Spectrum session "Sprinter ZX" (`SP.ZX`:
`/sprinter /turbo /7FFD /1FFD`) or "Pentagon 128" (`P128.ZX`: `/7FFD`), and which instruction turned turbo on or
wrote `#1FFD`. Both are one call on a live (paused or running) machine and on a TTD recording. Design:
[tdd-zx-mode.md](../../docs/inprogress/2026-09-28-sprinter/tdd-zx-mode.md) §12.

Verified 2026-10-03 (GUI build on spare ports, BIOS 3.06, the MAME-pack CHD, Flex Navigator): TTD recording on,
`\zx\spectrum.exe \zx\sp.zx \trd\across\0.trd` typed in Flex Navigator, ENTER on TR-DOS, `R` ENTER, ENTER on ACROSS.

```bash
BASE=http://localhost:8090/api/v1; B=$BASE/emulator/$EMU
curl -s "$B/state/sprinter/zx-mode" | jq -c '{summary, best: .config.best_match | {file, confidence}, opts: .config.option_line,
     clock: .clock.why, int: .frame.int | {kind, line}, rom: .rom.set, launcher: .launcher.mode_name}'
#  {"summary":"ZX: Sprinter ZX (turbo req, 21 MHz, /1FFD)","best":{"file":"SP.ZX","confidence":"certain"},
#   "opts":"/sprinter /turbo /7FFD /1FFD /ret-fn","clock":"21 MHz: the CNF turbo request is on and the front-panel
#   switch (F12) allows it","int":{"kind":"pentagon","line":287},"rom":"sprinter-community","launcher":"Sprinter ZX"}
curl -s "$B/state/sprinter/zx-mode" | jq -c '.ports.rows[] | select(.port=="0x01FD") | {out: .tr_dos_off.out, ttd_query}'
#  {"out":{"code":"0xC0","name":"1FFD","effect":"the #1FFD latch: Scorpion paging (bit 4: +8 pages in window 3, ...)"},
#   "ttd_query":{"port":"0x00E5","port_mask":"0xE0E7"}}        (P128.ZX: "stores cell #C0 only: CNF bit 6 'SC clean' ...")

# Who wrote #1FFD (live journal: on by default, every change with frame, T, PC and the port used)
curl -s "$B/state/sprinter/pld-journal?kinds=port_1ffd&limit=1" | jq -c '.events[] | {frame, t, pc, port, value, text}'
#  {"frame":1873,"t":65528,"pc":"0x88F1","port":"0x01FD","value":"0x17","text":"#1FFD <- #17 via port #01FD: latch #00 -> #17"}
# The same from the TTD recording (its OUT journal, decoded through the port table as it is now)
curl -s "$B/state/sprinter/pld-journal?source=ttd&kinds=cnf,port_1ffd&limit=3" | jq -c '.events[] | {frame, t, pc, kind, port, value}'
#  ... {"frame":868,"t":50052,"pc":"0x5B5C","kind":"cnf","port":"0x073C","value":"0x07"}
#      {"frame":1873,"t":65527,"pc":"0x88F1","kind":"port_1ffd","port":"0x01FD","value":"0x17"}
# Or the raw TTD query with the report's port / mask (every spelling the PLD treats as #1FFD)
curl -s -X POST $B/ttd/port-events -H 'Content-Type: application/json' \
     -d '{"event":"out","port":"0x00E5","port_mask":"0xE0E7","newest":true,"limit":3}' | jq -c '[.hits[] | {frame, port, value, pc}]'
```

- `config.best_match.confidence`: `certain` when the launcher's copy of the `.ZX` file in RAM (page `#FF`, community
  launcher; page `#41`, Peters Plus) agrees with the hardware; `high` from the hardware alone (CNF byte, ALL_MODE,
  frame length, ROM set by CRC). P128.ZX and ORIGIN.ZX share CNF `#4E`: ALL_MODE `#FA` and 312 lines tell ORIGIN.
- `launcher.option_table.flags` is the launcher's own parsed table (`ret-fn`, `ret-zx`, ...); `config.return` says
  what Ctrl+Alt+Del does.
- Journal kinds: `port_table` (page `#40` written: the key ZX port decodes it changed), `cnf`, `clock`, `port_7ffd`,
  `port_1ffd`, `all_mode`, `rgmod`, `hold`, `frame_lines`, `pld_load`, `pld_configured`, `f12`, `ctrl_alt_del`,
  `reset`. The frame's events also appear in `/video/changes` (`machine_events`). Off / clear:
  `POST $B/sprinter/pld-journal {"enabled": false}` / `{"clear": true}`.
- The TTD time (`t`) is taken at the start of the I/O cycle, the live journal's after it: they can differ by 1 T.
- Other surfaces: CLI `state sprinter zx`, `state sprinter journal kinds=port_1ffd [source=ttd]`; MCP `inspect_state`
  aspects `sprinter_zx_mode`, `sprinter_pld_journal` (`pld_journal_kinds`, `pld_journal_source`); Lua
  `sprinter_zx_mode()`, `sprinter_pld_journal{kinds="port_1ffd"}`; Python `emu.sprinter_zx_mode()`,
  `emu.sprinter_pld_journal(kinds="port_1ffd", source="ttd")`. The GUI status bar shows the summary line, the
  tooltip the report.
