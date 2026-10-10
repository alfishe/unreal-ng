# Recipe: ZX Spectrum Next

The Next is a Z80N (Z80 + NEXTREG, MUL, ...) at 3.5 / 7 / 14 / 28 MHz with 2 MB RAM, a 256-register control file ("NextREG"),
layered video (ULA, Layer 2, tilemap, 128 sprites, a copper), three AY chips, a DMA, a DivMMC with an SD card, and a Multiface-style
NMI. Its software is NextZXOS, loaded by the firmware (`TBBLUE.FW`) from the SD card; the emulator runs that real chain.

Ground truth: [docs/inprogress/2026-10-07-zx-next/](../../docs/inprogress/2026-10-07-zx-next/README.md) (`design.md`,
`TODO.md` for the status, `design-nextreg-journal.md` for the register journal), the port decoder
[portdecoder_next.h](../../core/src/emulator/ports/models/portdecoder_next.h), the reports
[portdecoder_next_state.cpp](../../core/src/emulator/ports/models/portdecoder_next_state.cpp). Scripts:
[tools/machines/next/](../../tools/machines/next/README.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred; the [WebAPI](#webapi) section has the same steps as curl and
> as the Python scripts under `tools/machines/next/`. Policy: [_common/transports.md](../_common/transports.md).

Outputs below are real, from a build of 2026-10-09 (sections 3b: 2026-10-10), trimmed.

## MCP (preferred)

### 1. Create the machine on a card

The machine boots the real chain only when the model's config names the boot ROM and a card (folder or image):
`configs/next/unreal.ini` - `[ROM] NEXTBOOT=rom/next/nextboot.rom` and `[NEXT] SdCard=<folder>`. A card folder needs `TBBLUE.FW`,
`machines/next/*` (the personality ROMs, `menu.def`, `keymap.bin`), `nextzxos/`, `sys/`, `dot/` - about 3 MB (the repository's
`testdata/machines/zxnext/card` is a starting point; the full NextZXOS distribution works as it is). Without them the machine
starts in the bare personality (128K ROM, no SD).

```text
emulator_manage {"action":"create","model":"NEXT","ram_power_on":"zero"}
#   → {"id":"dbd6edec-...","message":"Emulator created and started"}
```

NextZXOS reaches its menu **about 6 seconds** after the reset at normal speed (measured: `reset` to the key-wait loop `#0C8F`
5.6 s). Do not enable `turbo`: it only makes the picture stutter.

```text
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/state/next"}
#   → machine {"cpu_clock_hz":28000000,"speed_ratio":8,"machine_type":3,"timing":"+3", ...}, divmmc {...}, mmu {...}, spi {...}
```

NextZXOS runs its menus, Browser and editor at **28 MHz** (the guide says so: "the operating system runs at the maximum speed when
possible"); the "3.5MHz >" in its menu is the speed it gives user programs. The status bar shows the real clock.

### 2. Drive the menu and the Browser

The keyboard tools press real keys. SPACE leaves the welcome page, `B` opens the Browser, `H` searches by name, ENTER opens a
directory or runs the file under the cursor, EDIT goes up, BREAK backs out. Wait for the machine to be idle between keys (the
CPU halted at `#0C8F`: `inspect_state registers` shows `halted: true, pc: 3215`).

```text
type_input {"action":"tap","key":"space","frames":6}
type_input {"action":"tap","key":"b","frames":6}
type_input {"action":"tap","key":"h","frames":6}
type_input {"action":"type","text":"tests","delay_frames":5}
type_input {"action":"tap","key":"enter","frames":6}      # accept the search
type_input {"action":"tap","key":"enter","frames":6}      # open the directory
```

`tools/machines/next/browser-drive/drive.py` does this with the waits: `drive.py boot space browser go:tests go:base go:copper enter`.

### 3. Who wrote this register? (the NextREG journal)

The port trace, port breakpoints and the TTD I/O journal see port cycles only; the `NEXTREG` instruction and the copper write
registers without one. The NextREG journal sees every write. It is off by default.

```text
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/next/reg-journal","body":{"enabled":true,"clear":true}}
#   → {"available":true,"capacity":65536,"enabled":true,"evicted":0,"last_seq":0,"size":0}
... run the program ...
inspect_state {"aspects":["next_reg_journal"],"nr_journal_regs":"02,07","nr_journal_limit":6}
#   → [next_reg_journal] on, 3371 held, 0 evicted
#       #4 f1214 pc 0x3DBD nextreg NR0x07 0x00 -> 0x03  (CPU speed 28 MHz)
#       #422 f1217 pc 0x099C port NR0x07 0x03 -> 0x00  (CPU speed 3.5 MHz)
#       #2308 f1222 pc 0x0AB6 port NR0x02 0x00 -> 0x08  (multiface NMI)
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/state/next/reg-journal?regs=02,03&sources=nextreg,port&limit=20"}
```

`source` says the door: `nextreg` (the instruction), `port` (`OUT #253B`), `copper`, `internal`. The same report is `state next
journal` on the CLI, `next_reg_journal{...}` in Lua, `emu.next_reg_journal(...)` in Python.

### 3b. What did the program configure? (the debugger reports)

Guessing from port traces and one register at a time found the DMA writing into ROM, the tilemap reading bank 7 as 16K and a card
that answers as SDSC. Seven reports show the state at once; **none of them has a side effect on the machine** (reading the DMA does
not step its read sequence, reading a palette does not move the index, a register read does not touch the select latch).

```text
inspect_state {"aspects":["next_dma","next_video","next_palette","next_ports","next_nextreg"],
               "nr_palette":"sprites_1","nr_range":"0-3","nr_port":"6B","nr_access":"w","nr_reg":"07"}
invoke_api {"method":"GET","path":"/api/v1/emulator/{id}/state/next/dma"}                  # also /video /palette /ports /nextreg
```

CLI (`state next ...`, `next dma` is the same), Lua `next_dma()`, Python `emu.next_dma()`. The outputs below are real: they are produced by
the core test `CliNextReports_Test.RecipeStory` (`UNREAL_PRINT_NEXT_RECIPE=1 core-tests --gtest_filter=CliNextReports_Test.RecipeStory`) -
a program that copies 256 bytes from `#8000` into the ROM at `#0100` through the DMA, puts the tilemap in bank 7, and a disabled port.

**`next_dma`** - the copy into the ROM is visible as `dst_kind: rom` before the first byte moves:

```text
$ state next dma
mode: zxn                  # the port the last access used: #6B = zxn, #0B = z80
enabled: false             # WR6 ENABLE not given yet
burst: continuous
status: 0x3A               # the byte the next read of the read sequence returns
a:  address 0x8000, type memory, step inc, timing 1
b:  address 0x0100, type memory, step inc, timing 1
direction: a_to_b
block_length: 256
counter: 0
src: 0x8000   (src_kind: ram)
dst: 0x0100   (dst_kind: rom)       # <- the past find: a transfer into ROM
holds_bus: false    dma_delay: false
interrupt_enables: nr_cc 0x00, nr_cd 0x00, nr_ce 0x00
```

**`next_video`** - layer order, ULA / Layer 2 / tilemap / sprites switches, clip windows, raster. The tilemap base `#A0` is bank 7 at 8K
offset `#2000`: the video side of bank 7 is an 8K RAM, so the map is read from `#0000` of the bank (`map_wraps_8k: true`):

```text
$ state next video          (excerpt)
layer_order: value 2, name SUL, raw_nr_15 0x08
ula: enabled true, mode standard, port_ff 0x00, clip 0,255,0,191
layer2: enabled false, resolution 256x192x8, bank 8, shadow_bank 11
tilemap: enabled true, columns 40, control 0xA0, map_base 0xA0, map_bank 7, map_offset 0x2000, map_wraps_8k true,
         tile_base 0x0C, tile_bank 5, tile_offset 0x0C00, transparent_index 15
raster: frame 0, frame_t 3, tstates_per_line 228, vc 0, hc 6, int_line 0, current_line 239
timing: family 128K, hz 50
```

**`next_palette`** - the eight 9-bit palettes (`ula_1 layer2_1 sprites_1 tilemap_1 ula_2 layer2_2 sprites_2 tilemap_2`), the selected one
(NR `#43`), the index, the transparent indexes; `palette=` and `range=` choose:

```text
$ state next palette sprites_1 0-3
Palette ula_1 selected, index 0, auto-increment on
transparent: global 0xE3, sprites 0xE3, tilemap 15, fallback 0xE3

[2] sprites_1
  0	0x000  R0 G0 B0
  1	0x003  R0 G0 B3
  2	0x005  R0 G0 B5
  3	0x007  R0 G0 B7
```

**`next_ports`** - which device answers a port, and whether NR `#82`-`#85` switch it off. `enforced` tells whether the emulator gates the
port by that bit (today the DAC ports and the Multiface; the other ports decode regardless of the enable word):

```text
$ state next ports 6B w          (after NR #82 <- #DF)
Port 0x006B write: DMA (zxnDMA)
  decoded by: A7:A0 = #6B
  enabled: off (NR 0x82 bit 5, gated by the emulator: off)
  side effect: the next byte of the DMA's WR0-WR6 sequence; the zxn mode latch is set

Internal port enable word 0xFFFFFFDF (NR #82-#85; restored by soft reset)
  bit 0  NR0x82  on  #FF  (not gated)
  ...
```

**`next_nextreg` and the write** - one register with decoded bits, no side effects; the write goes through the board's single write choke
point, so the journal sees it. `door` is `nextreg` (the `NEXTREG` instruction: the select latch stays), `port` (`OUT #243B`, `OUT #253B`) or `internal`:

```text
$ next nextreg 07 03                       # POST /api/v1/emulator/{id}/next/nextreg {"reg":"07","value":"03","door":"nextreg"}
NR #07 <- #03 (was #00, reads #33) through nextreg (stopped)
$ state next nextreg 07
reg: 0x07   name: CPU Speed   access: RW   value: 0x33   stored: 0x03   reset: 0x00   decoded: CPU speed 28 MHz
$ state next journal regs=07
NextREG journal on: 1 event(s) of 1 held, 0 evicted
#1 frame 0 T 3 PC 0x0000 nextreg NR0x07 0x00 -> 0x03  CPU Speed (CPU speed 28 MHz)
```

The write runs where nothing else drives the machine (paused, or between two frames of a running one) and answers 409 on a machine that is
not a Next. The JSON of every report is the same tree as the text (`GET .../state/next/dma` and so on; the OpenAPI manifest lists the
query parameters).

**`next_copper`** - the copper list as a disassembly. `pc` and the `>` marker show where the copper is; the write address is the NR `#60`-`#63`
pointer and a report never steps it:

```text
$ state next copper count=3
Copper stopped (mode 0), write address 6 (word 3, msb next), pc 0, line offset 0, 3 word(s) in the list
> 0	0x9864  WAIT line 100 hpos 12
  1	0x1505  MOVE NR #15 <- #05  (Sprite and Layers System)
  2	0xFFFF  HALT
```

**`next_sprites`** - the sprite attributes decoded (a relative sprite is resolved against its anchor), the switches, the collision flags (the
report does not clear them, a port `#303B` read does) and the pattern memory in use:

```text
$ state next sprites
Sprites off, over border off, 1 visible; collision off, too many off
#0 basic x 64 y 64 pattern 0 palette +0 scale 1x1  [40 40 00 80 00]
pattern memory: 256 non-zero byte(s) of 16384, 1 8-bit pattern(s) in use
```

MCP: `inspect_state {"aspects":["next_copper","next_sprites"],"nr_from":"0","nr_count":"16"}`; WebAPI `GET .../state/next/copper?from=0&count=16&raw=true`,
`GET .../state/next/sprites?all=true`; Lua `next_copper{...}`, `next_sprites{...}`; Python `emu.next_copper(...)`, `emu.next_sprites(...)`.

### 4. Starting a snapshot (`.snx`, `.sna`, `.z80`) from the Browser

NextZXOS starts a snapshot in a way worth knowing, because every piece must be exact or the screen stays black:

1. the OS loads the file, builds a register image, and writes **NR #02 = 8** - the Multiface NMI (NR #06 bit 3 on); the code after
   the `OUT` is a `NOP` and the Multiface ROM checks that the NMI returned to `#0AB8` / `#0AB9`;
2. NR #C0 bit 3 is on (**stackless NMI**): the return address goes to NR #C2 / #C3, not the stack; the CPU reads it back at `RETN`;
3. the Multiface ROM (system page 5, `enNextMf.rom`) is at `#0000-#1FFF`, its RAM at `#2000-#3FFF`, from the fetch at `#0066` to
   the `RETN`; **the DivMMC's automap is inactive while it is in**;
4. the ROM sets the machine to 48K mode (`#7FFD` = `#30`), restores the registers and **NR #C2/#C3 = `#2313`** - a `RET` of the 48K
   ROM - so the `RETN` lands on `RET`, which pops the snapshot's PC from its stack.

```text
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/feature/debugmode","body":{"enabled":true}}   # breakpoints need it
control_execution {"action":"bp_add","address":2742,"page":"rom2"}      # the OUT of NR #02 (ROM page 2 - an address alone also hits ROM 0)
... run the Browser entry ...
control_execution {"action":"step"}  ×2
#   → pc 0x0AB8, then pc 0x0066 with slot 0 = "multiface rom", NR #C2/#C3 = #B8 #0A
```

### 5. The real-board test programs

`tools/machines/next/browser-drive/suite.py --card <card>` runs every program of `tests/<area>/<test>/` the way a person does (reset,
Browser, run) and photographs the result beside the board photograph; `tools/machines/next/realboard/run-all.py` runs them straight
through the snapshot loader with automatic verdicts for the four programs that paint their own pass / fail.

### 6. Sound: is the DAC alive? (the DMA sample demo)

```text
load_software {"path":".../DMA Sample Engine Demo/zxnext_dma_sample.nex"}      # then tap a key
invoke_api POST /feature/porttrace {"enabled":true};  POST /profiler/porttrace/filter {"include":[{"direction":"out"}]}
#   OUT #FFDF should come at ~8000 per second; OUT #183B (CTC 0) is the timer
capture_media {"action":"audio_capture","seconds":2,"wav":true}              # left / right peak, rms, zero_crossing_rate
```

`audio_capture` leaves the emulator **paused** afterwards: resume it (`POST /resume`) before the next step.

## WebAPI

The same steps; ids come from `GET /api/v1/emulator`.

```bash
BASE=http://localhost:8090/api/v1/emulator; ID=$(curl -s $BASE | jq -r '.emulators[0].id')
curl -s -X POST $BASE/$ID/next/reg-journal -H 'Content-Type: application/json' -d '{"enabled":true,"clear":true,"capacity":262144}'
curl -s "$BASE/$ID/state/next/reg-journal?regs=02&limit=5" | jq '.events[] | {seq,frame,pc,source,value,decoded}'
curl -s -X POST $BASE/$ID/keyboard/tap -H 'Content-Type: application/json' -d '{"key":"enter","frames":6}'
curl -s -X POST $BASE/$ID/step                                  # one instruction (debug mode on)
curl -s "$BASE/$ID/capture/screen" | jq -r .data | base64 -d > screen.png
```

## Pitfalls

- `turbo` (the feature) is not a speed-up here; leave it off.
- Execution breakpoints are silent unless the `debugmode` feature is on; an address alone matches in every ROM page - add `page`
  (`rom2`, `rom5`, `ram5`).
- The Browser remembers the directory it was last in (the card is the user's NextZXOS state): press EDIT to the root first
  (`drive.py root`).
- NextZXOS writes to the card folder (browser preferences, `autoexec.1st` renamed by the welcome page): keep a pristine copy.
- `screenshot` files go where the *server* process runs; with `filename` prefixes the path may be URL-encoded into one file name.
- A black screen after starting a snapshot is almost always the NMI chain of section 4: check the journal for the `NR #02`
  write, the DivMMC mapping (`state/next` `divmmc.mapped`) and where `NR #C2/#C3` point.
