# Recipe: ZX-Evo BaseConf (`ATM3`)

Model `ATM3` (4096 KB fixed): the ZX-Evo with the BaseConf FPGA configuration and the AVR keyboard / PS/2 controller.
It is **not** an ATM Turbo 2 board: [`ATM450`](atm450.md) and [`ATM710`](atm710.md) are different machines. `ATM3` uses
the same `#FF77` / `#FFF7` / `#EFF7` registers as the 7.10 (read [atm710.md](atm710.md) first) with the differences
below. Common material for all ATM models (creating, ports, video modes, CP/M mode, pitfalls): [README.md](README.md).

## Port decode differences from the 7.10

- Every mainboard port decodes the
  full low byte (BaseConf FPGA rules, `PortDecoder_ATM3::ClassifyPort`):
  - **shadow** = TR-DOS active or `#BF` bit 0. The FDC (`#1F/#3F/#5F/#7F/#FF`),
    `#xx77` and the pager (`#xFF7`, `#x7F7`) answer only in shadow; outside it
    `#1F` is the Kempston joystick and `#xx77` the Z-Controller chip select.
    The joystick reads the `Joystick` device (idle `0x00`, active high: D0 right, D1 left, D2 down, D3 up, D4 fire);
    the host keypad drives it by default (`kp8` up, `kp2` down, `kp4` left, `kp6` right, `kp0` fire; `[INPUT]
    JoystickKeys=` overrides, empty disables, `Joystick=NONE` unfits it) and the same keys still reach the PS/2 log.
    To press buttons from automation (MCP `joystick_input`, WebAPI `/joystick/*`, CLI, Lua, Python) see
    [input/joystick.md](../../input/joystick.md). The ROM service menu is outside shadow about 60 frames after reset.
  - **CMOS** (Gluk clock): data `#BFF7` / address `#DFF7` outside shadow, but
    only after `OUT (#EFF7),#80`; `#BEF7` / `#DEF7` in shadow (always on).
    `#EFF7` itself is ignored in shadow and cannot be read.
  - **Evo registers**: read on `#xxBD` (index = A12..A8: pages, `#7FFD`,
    `#EFF7`, `#xx77` state, border, breakpoint, `#13BD` virtual-drive mask);
    `#xxBE` is a write-only exit strobe. `[EVO] Fpga=legacy` switches to the
    old tree (readback on `#xxBE`) for the older `rom/zxevo.rom` image.
  - **ROM**: the shipped image is the official `rom/zxevo-fe.rom`; its EVO
    Reset Service idles in the main menu at PC `#6117` about 60 frames after
    reset.
  - **`#BF` bits** (shadow ports): bit 2 = every memory write also writes the
    text-mode **font RAM** (`A & #7FF`, address `code * 8 + row`; read the byte
    under the beam on `#0EBD`; the font survives a reset); bit 5 = **4:4:4
    palette** (a `#FF` write takes the low bit of each channel from A15..A8,
    `#0DBD` then reads the low bit pair instead of the high one).
  - **`#xBF7`** (shadow, window = A15:A14, D0): per-window **write protect**
    for the map `#7FFD` bit 4 selects; read back on `#12BD` (bit i = window i
    of map 0, bit 4+i = of map 1). Window 0 under RAM 0, the NMI page or the
    virtual TR-DOS page is never protected.
  - **TR-DOS entry stall**: with `contention` on, an opcode fetch from `#3Dxx`
    of a window that holds the DOS ROM in map 1 takes half a 3.5 MHz T longer
    (the chipset holds the clock 4 x 28 MHz so the ROM chip can answer).

## Hard disk and CD

The IDE board is the NemoIDE (`[HDD] Scheme=NEMO-DIVIDE`). The units are the media slots `ide0.master`
(alias `hd`) and `ide0.slave`; ZX-Evo ships a CD drive on the slave (`CD1=1`, alias `cd`). In the ERS menu,
"B. HDD boot" boots the hard disk and "D. CD boot" runs the disc's `AUTORUN.ZX`; with the drive empty it keeps retrying until
a disc is inserted. Verbs and formats:
[use-media-slots.md](../../media/use-media-slots.md),
[docs/features/media.md](../../../docs/features/media.md).

```text
media {"action":"insert","slot":"hd","path":"/home/me/zx/nedoos.img"}  # hard disk (insert while paused)
media {"action":"insert","slot":"cd","path":"/home/me/zx/disc.iso"}    # CLI: media insert cd <iso>
inspect_state {"aspects":["ide"]}                                         # board, latches, units, sense data
```

## SD cards: which slot, and why a reset is mandatory

`ATM3` has two SD slots; put a card in the one the software will look at:

| Slot (alias) | Guest sees | Use it for |
|---|---|---|
| `sd.zc` (`sd`) | `E:` in the ERS and NedoOS (the card's first FAT partition) | NedoOS, the ERS "5. SDcard boot", any ZX-Evo software on a card |
| `sd.ngs` | the NeoGS card's SD slot | `NEOGS.ROM`, MP3 and module players (NeoGS add-on) |

`sd` without an id always means `sd.zc`; for the NeoGS card say `sd.ngs` (or `tag:sd+neogs`).
`GET /emulator/{id}/media` lists both with their `guestName`.

**Reset after every card change.** The ERS (the BIOS) reads the card's descriptors (the
partition table, the FAT layout, the boot files) **only at start** and never refreshes them.
A card inserted or swapped into a running machine stays invisible to it: the menu still shows
the old card (or none) and "5. SDcard boot" fails or boots stale data. So always:

1. insert the card (pause first, so nothing is half-way through a read);
2. **reset** the machine (`POST /emulator/{id}/reset`, MCP `emulator_manage` reset, CLI `reset`);
3. only then drive the ERS menu (`5` for SD boot).

```bash
BASE=http://localhost:8090/api/v1
curl -s -X POST $BASE/emulator/$EMU_ID/pause
curl -s -X POST $BASE/emulator/$EMU_ID/media/sd.zc/insert -H 'Content-Type: application/json' \
     -d '{"path":"/home/me/zx/nedoos-sd"}' | jq '{ok, slot, report}'
curl -s -X POST $BASE/emulator/$EMU_ID/reset            # mandatory: the BIOS rereads the card only at start
curl -s -X POST $BASE/emulator/$EMU_ID/resume
```

A card inserted right after `emulator/start` is not safe either: the BIOS may already have
read the slot. Reset anyway.

The insert `report` may warn that "sector 0 holds an MBR partition table": for a host folder
(`folder-fat16`) the card is still mounted; if the boot then fails, that warning is the first
suspect (this SD boot path reads FAT from sector 0, no MBR support).

## NedoOS from the SD card

`ATM3` boots NedoOS from the Z-Controller SD slot `sd.zc` (insert, then **reset**, see above): an SD image, or a host
folder with the NedoOS release files (`SD_BOOT.$C`, `bin/term.com`, `bin/cmd.com`;
a minimal set is `testdata/machines/zxevo/nedoos/sdcard/`). In the ERS menu "5.
SDcard boot" starts it; the shell prompt is `M:/bin>`.

The NedoOS ZX-Evo kernel reads the keyboard **only** from the AVR's PS/2 scan code
log, never from the ZX matrix. Host keys reach it as physical PC keys (the
emulator window sends them), and so does automation typing: text is typed as
US-layout PC keys, and key names accept PC keys too (`f1`, `home`, `esc`,
`pc.up`).

```text
media      {"action":"insert","slot":"sd.zc","path":"/home/me/zx/nedoos-sd"}   # a folder or an image
emulator_manage {"action":"reset"}                                           # mandatory: the BIOS reads the card only at start
type_input {"action":"tap","key":"5"}                                           # ERS: SD card boot
type_input {"action":"type","text":"free\n"}                                    # a shell command
type_input {"action":"tap","key":"f1"}                                          # a PC key with no ZX key
```

## NedoOS from a floppy or a hard disk

Three things in the ERS look like faults and are not (checked 2026-10-04):

- **A real floppy needs the virtual drive moved away from A.** The ERS makes drive A a virtual
  drive by default; "TR-DOS boot" then reports "Virtual drive not formatted or image not loaded"
  (or NEO-DOS shows `Virtual Drive: A`) whatever is in `fdd.a`. Press `Y` once (virtual drive B).
- **"TR-DOS boot" takes two `Enter` presses.** The first lists the disk's BASIC files in a
  window (`boot` highlighted), the second runs it. NedoOS `osatm3.trd` then reaches `A:/>`.
- **"HDD boot" needs a boot block on the disk.** The ERS reads 24 KB from LBA 2 and jumps to it.
  A freshly partitioned disk (the NedoOS `hdd_nedo.vhd` template) has zeros there, so nothing
  starts. NedoOS writes the block itself: run `hddfdisk.com`, `0` (Nemo master), the partition
  number, `b`, `y`; copy the HDD kernel `osatm3hd.$C` to the partition as `sd_boot.$C` and `bin/`
  next to it. Then "B. HDD boot" (4th entry: down x3, Enter) shows "1.NedoOS"; `1` boots it. The
  whole procedure and its limits:
  [hdd-images/README.md](../../../testdata/machines/baseconf/hdd-images/README.md).

```text
media {"action":"insert","slot":"fdd.a","path":"scratch/osatm3.trd"}
type_input {"action":"tap","key":"y"}            # virtual drive A -> B
type_input {"action":"tap","key":"enter"}        # 2. TR-DOS boot: the list of BASIC files
type_input {"action":"tap","key":"enter"}        # run "boot"; the prompt is "A:/>"
```

**A swapped SD card.** If the card is changed while NedoOS runs, the first reset ends in "SD card
lost, Press RESET" (the ERS asks the new, uninitialised card for its OCR with CMD58 and gets
silence); reset again. A card inserted before the first reset never shows it (and a big card
such as the whole release with `nedogame/`, 6245 files, boots fine).
