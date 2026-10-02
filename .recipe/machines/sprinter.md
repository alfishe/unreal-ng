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
#   → models[] has {"name":"SPRINTER","full_name":"Sprinter Sp2000","creatable":true,
#     "available_ram_sizes_kb":[4096],"default_ram_kb":4096}
emulator_manage {"action":"create","model":"SPRINTER"}
inspect_state {"aspects":["sprinter"]}
#   → structuredContent.sprinter.bios:
#     {"rom_file":"rom/sprinter/sp2k-3.04.rom",
#      "identified":{"page_8":"Sprinter BIOS 3.04 page 8 (BIOS proper, EXP)", ...},
#      "images":[{"file":"rom/sprinter/sp2k-3.04.rom","version":"Sprinter BIOS 3.04 (Peters Plus, 17.06.2003; the default)",
#                 "crc32":"1729cb5c","present":true,"active":true},
#                {"file":"rom/sprinter/sp2k-3.06-hf2.rom","version":"Firmware v3.06 Hotfix 2 (community build, 19.01.2026)", ...},
#                {"file":"rom/sprinter/sp2k-3.07-beta1.rom","version":"Firmware v3.07 BETA 1 (community build, 24.09.2026)", ...}],
#      "select":"[ROM] SPRINTER=rom/sprinter/<file> in configs/sprinter/unreal.ini beside the binary ..."}
```

**Choosing the BIOS** is a config setting, not an API call: edit
`[ROM] SPRINTER=` in the Sprinter config the binary reads
(`<build>/bin/configs/sprinter/unreal.ini`; on macOS
`<build>/bin/unreal-qt.app/Contents/Resources/configs/sprinter/unreal.ini`),
then create the machine again. With `SPRINTER=rom/sprinter/sp2k-3.07-beta1.rom`
the next machine reports `"identified":{"page_8":"Sprinter BIOS 3.07 BETA 1 page 8 (EXP)", ...}`
and boots "Firmware v3.07 BETA 1". Background: [bios-versions.md](../../docs/inprogress/2026-09-28-sprinter/bios-versions.md).

**Full start vs fast start** (`[SPRINTER] FastStart=` in the same file):
`0` (the default) runs the ROM's PLD loader first, as the real machine does -
473 720 configuration writes, about 1.9 s of emulated time with a black
screen; `1` starts with the PLD already configured (the tests use it). Either
way `sprinter.pld` ends `"state":"configured","module":"Standard"`; the
bitstream hashes name the image (3.04: `full_hash 0xFC0928F2`, `head_hash 0x78EDDFC6`).

### 2. Boot DSS from a floppy

SETUP boots the IDE master first, then the "alternative device", floppy B.
With no hard disk both IDE units read an empty channel and SETUP prints
"None" for each at once (no F4 needed; [sprinter-hdd.md](../media/sprinter-hdd.md)).

The Sprinter has no ZX screen: read its text with the `sprinter_text` aspect
(`screen_ocr` reads Spectrum mode only).

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
capture_media {"action":"screenshot","format":"png","mode":"full","filename":"/abs/path/scratch/dss-dir.png"}
#   → Screenshot saved to .../scratch/dss-dir.png (736x288 ...)
```

`\` types as the PC backslash (step 4 runs `a:\zx\spectrum.exe`). Single keys: `type_input {"action":"tap","key":"f4"}`; PC-only keys take a
`pc.` prefix where a ZX name would win (`pc.esc`, `pc.delete`); Ctrl+Alt+Del
(CPU reset, the PLD stays configured):
`type_input {"action":"combo","keys":["lctrl","lalt","pc.delete"]}`. DEL on the
BIOS logo opens SETUP.

### 4. Spectrum mode and TR-DOS

BIOS 3.04 has no Spectrum ROMs; DSS's `SPECTRUM.EXE` loads them from
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
| `video` | `picture_mode` (the dominant square kind of the 640 x 256 picture), `squares` by kind, HOLD offsets, `int_positions` (frame INTs the mode table places) |
| `z84c15` | WCR / MWBR / CSBR / MCR, watchdog, CTC channels, SIO A (keyboard) / B (mouse) with their FIFOs, PIO, `keyboard` (INT on, bytes on the way, overruns) |
| `fdc` | `density_latch` (720 KB code `#16` / 1.44 MB code `#17`), the WD1793 `clock` (1 / 2 MHz) and `data_rate` (250 / 500 kbit/s), `drive` |
| `cmos`, `ide` | links: the CMOS report is `rtc`; `ide` shows the selected channel and data latch, the drives are in `state/ide` (see [sprinter-hdd.md](../media/sprinter-hdd.md)) |
| `bios` | the ROM file, the pages identified by signature, the shipped images, how to choose |

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
curl -s "$BASE/emulator/$EMU/capture/screen?format=png&mode=full&path=$PWD/scratch/sprinter.png" | jq -c .
#   {"file":".../scratch/sprinter.png","format":"png","height":288,"saved":true,"size":...,"status":"success","width":736}
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
state memory                            # windows as "Sprinter Paging (PLD)"; state rom: the 16 ROM pages and their roles
```

```lua
local s = sprinter_state(); print(s.pld.state, s.decoder.map, s.windows[1].kind)   -- configured 0 ROM
local t = sprinter_ports({map = 0, dos = 1, rw = "w"}); print(#t.rows)          -- 41
local l = sprinter_port(0x21BC, {rw = "w"}); print(l.results[1].code)          -- 0x2B
for _, line in ipairs(sprinter_text().lines) do print(line.text) end
```

```python
import unreal_emulator as unreal
emu = unreal.emu_get_selected()
s = emu.sprinter_state(); print(s["pld"]["state"], s["clock"]["mhz"])          # configured 21
t = emu.sprinter_ports(map=0, dos=1, rw="w"); print(t["rows"][0]["name"])     # FdcCommand
l = emu.sprinter_port(0x21BC, rw="w"); print(l["results"][0]["index"])        # 0x043C
print(emu.paging_state()["sprinter"]["windows"][3]["kind"])                    # RAM
```

## What works / what doesn't

| Area | State |
|:--|:--|
| PLD load (full start) and fast start, the port table, the windows, fast RAM, graphics pages, the 21 MHz turbo with its waits | implemented |
| Video: every square kind, palettes, HOLD, the mode-table INT, 320 / 312 lines | implemented |
| WD1793 at 720 KB and 1.44 MB (the `#BD` density latch), DSS 1.62 from floppy B, Spectrum mode, TR-DOS | implemented |
| AT keyboard and serial mouse on the Z84C15 SIO; `type_input`, key taps and combos | implemented |
| DS12887A CMOS (`[SPRINTER] CmosFile=` keeps it) | implemented |
| Flex Navigator (DSS's `fn`) | **hangs** after its logo - boot a floppy without it |
| DSS 1.71 (`dss171u.img`) | **stops** with "Fatal error! Press RESET to restart." after the BIOS loaded it |
| IDE hard disks | implemented (two channels, [sprinter-hdd.md](../media/sprinter-hdd.md)); an empty channel reads `#7F`, so the BIOS reports "None" without waiting |
| Sound: one AY at 1.75 MHz (ABC), beeper, Covox, Covox-Blaster (ring, rates, INT, 16-bit stereo) | implemented (S6, [sprinter-sound.md](sprinter-sound.md)) |
| Accelerator | implemented (S5, [sprinter-accelerator.md](sprinter-accelerator.md)) |
| ISA cards (General Sound on the ZX-bus adapter, `PROPLAY.EXE` MODs) | not yet (S6b); the ISA view reads `#FF` |
| TTD (time travel) | refuses to record this machine until phase S7 |

## Pitfalls

- **No text from `screen_ocr` outside Spectrum mode.** BIOS, SETUP and DSS
  screens are Sprinter text squares: use `sprinter_text`
  (`/state/sprinter/text`). Graphics squares read as spaces.
- **F4 in the GUI** is the "speed 8x" shortcut (`unreal-qt` menu), so at the
  IDE wait F4 may not reach the machine there; the automation's
  `type_input` / `keyboard/tap` always does. On a Mac keyboard DEL is
  Fn+Delete.
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
