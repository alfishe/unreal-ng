# DivIDE, DivMMC and esxDOS

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Machines** | any 48K / 128K / +2 / +2A / +3 / Pentagon-class machine with the add-on; built into Karabas-Pro (a Profi clone) and, in its own form, the ZX Next ([zx-next.md](zx-next.md)) |
| **unreal-ng now** | only the DivIDE **IDE ports** (`[HDD] Scheme=DIVIDE`, `IdeAdapter::DivideIn/Out` on `ide-atapi`). No `#E3` register, no DivIDE memory, no automap, no DivMMC SPI ports. So esxDOS cannot run |
| **Effort** | **L** in total: M-L for the shared paging + automap framework (8K granularity in `#0000-#3FFF`), then **S** for DivIDE, **S** for DivMMC, **S** for the esxDOS fixtures and tests |

## 1. What these interfaces are (plain words)

A DivIDE or DivMMC is a small board that plugs into the Spectrum's edge connector. It holds:

- an 8 KB **EEPROM** with firmware (esxDOS, or older ones: FATware, MDOS3, DeMFIR, ResiDOS);
- 32 KB to 512 KB of **RAM** in 8 KB banks;
- the storage port: an IDE task file (DivIDE) or an SPI port to an SD card (DivMMC);
- a **trap** ("automap") that watches the addresses the Z80 fetches instructions from. When the
  Spectrum ROM reaches one of a few well-known addresses (the reset vector, RST 8, the NMI, the tape
  loader entry), the board swaps its own EEPROM and RAM into `#0000-#3FFF` instead of the Spectrum
  ROM. The firmware then runs with the Spectrum knowing nothing about it.

This is why the storage part is the easy part: without the paging and the trap, the firmware never
gets control.

## 2. Hardware facts (consensus)

References: zxsp `Source/Uni/Items/Fdc/DivIDE.{cpp,txt}` (full DivIDE, ships esxDOS 0.8.5),
pico-spec `src/DivMMC.{h,cpp}` + `src/Ports.cpp` (DivMMC, DivIDE, DivSD), Karabas-Pro RTL (DivMMC
inside a Profi clone: [karabas-pro-hardware-analysis.md](../2026-09-21-profi/karabas-pro-hardware-analysis.md) §1.8, §2.6),
MAME / jnext (the Next variant, [zx-next.md](zx-next.md)), UnrealSpeccy (IDE ports only:
`nedopc/io.cpp:439, 1093-1107`). Paths are relative to the local reference tree
(`emulators/github/...`, `emulators/svn/pentevo/tools/unreal_fix/0.39.0/nedopc/`).

### 2.1 DivIDE IDE ports

| Port | Register | Source |
|---|---|---|
| `#A3` | data, 16 bits through a toggle (below) | zxsp `DivIDE.txt:24-36`, `DivIDE.cpp:18-24` |
| `#A7 #AB #AF #B3 #B7 #BB #BF` | registers 1-7: `%101r rr11`, register = `(port >> 2) & 7` | same; Unreal `io.cpp:439, 1095`; pico-spec `Ports.cpp:819-822` |
| none | no control block (no alternate status, no SRST) | zxsp `DivIDE.txt` |

Decode masks differ: Unreal `(port & #A3) = #A3`, pico-spec `(low & #E3) = #A3`, zxsp the full
`%101r rr11`. **The Unreal mask is too wide**: `#E3` also matches it and lands on register 0 (data),
`#E7` on register 1, `#EB` on register 2. So an `OUT (#E3)` would feed the IDE data toggle. The
consensus is A6 = 0: `(low & #E3) = #A3`. Branch `ide-atapi` uses it since 2026-09-28
(`IdeAdapter_Test.DividePagingPortsAreNotIde`).

**Data toggle (all three references agree).**

| Access | Effect | Source |
|---|---|---|
| 1st `IN (#A3)` | 16-bit read from the drive, returns the low byte, latches the high byte | zxsp `DivIDE.cpp:233-247`; Unreal `io.cpp:1097-1104`, `986-992` |
| 2nd `IN (#A3)` | returns the latched high byte | same |
| 1st `OUT (#A3)` | stores the low byte only | zxsp `DivIDE.cpp:291-302`; Unreal `io.cpp:441-451`, `226-232` |
| 2nd `OUT (#A3)` | sends `low \| high << 8` | same |
| any other IDE register, or a write to `#E3` | resets the toggle | zxsp `DivIDE.cpp:229, 264, 288`; Unreal `io.cpp:455, 1107` |
| power-on | undefined (zxsp randomizes it) | zxsp `DivIDE.txt:48`, `DivIDE.cpp:36, 67` |

**Worked example.** `LD BC,#00A3 : INIR : INIR` reads a 512-byte sector: bytes arrive low, high,
low, high, exactly as they sit in the image file. No byte swapping anywhere.

### 2.2 Control register `#E3` (DivIDE and DivMMC)

| Bit | Name | Meaning |
|---|---|---|
| 7 | CONMEM | map the board in **now**: EEPROM at `#0000-#1FFF`, the selected bank at `#2000-#3FFF`; EEPROM writable only with jumper E removed |
| 6 | MAPRAM | from now on, RAM bank 3 replaces the EEPROM at `#0000-#1FFF` (read-only) when the board maps in by the trap. **Sticky**: a write of 0 does not clear it; only power-off does (on the Next, NR `#09` bit 3) |
| 5..0 | bank | 8 KB RAM bank for `#2000-#3FFF`: 2 bits on the 32 KB board, 4 bits (128 KB) on common DivMMCs, 6 bits (512 KB) on large boards |

Sources: zxsp `DivIDE.txt:51-83`, `DivIDE.cpp:76-107, 260-266`; Karabas-Pro `TOP:1519-1521`
(sticky bit 6: `E3[6] or D6`); pico-spec `Ports.cpp:2266-2271`.

Write protection when mapped by the trap with MAPRAM = 1 and CONMEM = 0: `#0000-#1FFF` (bank 3) is
read-only, and `#2000-#3FFF` is read-only when the selected bank is 3 (zxsp `DivIDE.cpp:81-89`).

**Where references disagree** (consensus in bold):

| Point | zxsp | pico-spec | Karabas-Pro RTL | Next (MAME, jnext) | **Take** |
|---|---|---|---|---|---|
| `#E3` readable | no (reads go to IDE) | yes | no | yes, bits 5:4 read 0 | **write-only** on DivIDE / classic DivMMC; readable on the Next |
| Reset clears MAPRAM | no | yes | "DivMMC map reset" on reset | yes (`#E3` = 0) | **no** on DivIDE (the board has no reset input, zxsp `DivIDE.cpp:184-186`); **yes** on the Next |
| CONMEM overrides MAPRAM | yes | no | n/a | yes | **yes** |
| Bank 3 write-protect | yes | no | n/a | yes | **yes** |

### 2.3 Automap (the trap)

| Fetch address (M1) | Effect | Timing |
|---|---|---|
| `#0000`, `#0008`, `#0038`, `#0066`, `#04C6`, `#0562` | map in | **after** that instruction's opcode fetch: the instruction at the entry point comes from the Spectrum ROM, the next one from the board |
| `#3D00-#3DFF` | map in | **instantly**: the opcode at `#3Dxx` already comes from the board |
| `#1FF8-#1FFF` | map out | after the fetch |

Sources: zxsp `DivIDE.cpp:317-349` (entry points after the fetch, `#3Dxx` instant; its own
`DivIDE.txt:97-104` lists `#3Dxx` as delayed but the code, pico-spec `DivMMC.h:188-194` and MAME
`rom3_instant_on` all map it instantly), Karabas-Pro `TOP:1730-1775`.

Conditions: automap is enabled by jumper E (or, per the document, MAPRAM set); on a +2A / +3 jumper
A must be set so the trap reacts only while ROM 3 (48 BASIC) is paged (zxsp `DivIDE.txt:21, 94-95`).
The NMI button on DivMMC boards pulls /NMI; the fetch at `#0066` then maps the board in (Karabas-Pro
`NMI_n = mapcond`, `TOP:1197`).

**Worked example: `LOAD ""` with esxDOS.** The ROM's tape loader `LD-BYTES` starts at `#0556`, and
execution passes `#0562` a few instructions later. The opcode at `#0562` still comes from the
Spectrum ROM; the next fetch comes from the esxDOS EEPROM, whose code at the same address decides
whether a tape image (`.TAP`) is attached. If so it feeds the loader from the card; if not, it
returns through the off-area `#1FF8-#1FFF`, the board maps out, and the ROM loader carries on with
the real tape. (`#04C6` is the same trick inside `SA-BYTES` for saving.)

### 2.4 DivMMC SPI ports

| Port | Direction | Meaning | Source |
|---|---|---|---|
| `#E7` | write | chip select, active low: bit 0 card 0, bit 1 card 1 (on boards with two sockets); reading gives `#FF` | pico-spec `DivMMC.cpp:675-689`, `Ports.cpp:829-832`; Karabas-Pro `TOP:1704-1708` |
| `#EB` | write | send a byte over SPI | pico-spec `Ports.cpp:2282-2284` |
| `#EB` | read | the byte the card sent during the last exchange; on the Next it also starts an exchange sending `#FF` (the Z-Controller rule) | pico-spec `Ports.cpp:825-827`; MAME `specnext.cpp:1246-1278`; jnext `spi.cpp:176-215` |

This is the Z-Controller's `#77` / `#57` pair with other port numbers and the select on bit 0
instead of bit 1 ([zxevo-baseconf-tsconf.md](zxevo-baseconf-tsconf.md) §1.1). Whether a classic
DivMMC read also starts a new exchange is **unverified** (pico-spec emulates the card at protocol
level, not the shift register); esxDOS's driver works either way if it sends `#FF` itself.

## 3. What esxDOS needs from the machine

| Need | Detail | Source |
|---|---|---|
| Hardware | DivIDE, DivMMC (or DivSD, Next DivMMC) with `#E3` paging, banked RAM and the automap trap | zxsp, pico-spec |
| Firmware | the 8 KB `ESXMMC.BIN` / `ESXIDE.BIN` build for the interface, in the EEPROM | pico-spec `src/roms/esxdos/esxdos.c` ("v0.8.9-DivMMC"), `esxide.c` ("v0.8.9-DivIDE"); zxsp `Resources/Roms/esxdos085.rom` |
| Card | a FAT16 or FAT32 volume with `/SYS/` (`/SYS/CONFIG/ESXDOS.CFG` and the system files) and `/BIN/` (dot commands) | path strings in the ROM images above; FAT16 / FAT32 from the esxDOS distribution notes (esxdos.org, not in the local sources) |
| RAM | at least the 32 KB board; esxDOS keeps its buffers and the resident part of the system in the banks | zxsp (32 KB default) |
| Host ROM | the unmodified 48K ROM or a 128K ROM set; entry points sit at ROM addresses, so a patched ROM that moves `#0562` or `#04C6` breaks tape traps | zxsp patch list `DivIDE.cpp:162-174` |

Machines: 48K, 128K, +2, +2A / +3 (with jumper A), Pentagon-class clones. On a Pentagon with a Beta
128 interface both boards trap `#3Dxx`: the DivIDE maps instantly, so TR-DOS stops being reachable
through `RANDOMIZE USR 15616` while automap is on. That conflict is real hardware behavior, not an
emulation bug; the config should warn when both are fitted.

esxDOS support elsewhere: zxsp (DivIDE), pico-spec (DivMMC, DivIDE, DivSD), MAME and jnext (Next
only, through `enNxtmmc.rom`). UnrealSpeccy, Xpeccy, Spectral, ZXMAK2: no.

## 4. unreal-ng now vs gap

| Piece | Have | Need |
|---|---|---|
| DivIDE IDE ports | `IdeAdapter::DivideIn/Out` (`IDE_DIVIDE`), gated "TR-DOS ports off" (the UnrealSpeccy rule) (1) narrow the decode to `(low & #E3) = #A3`: today's `(port & #A3) = #A3` (the UnrealSpeccy mask) catches `#E3`, `#E7`, `#EB` as IDE registers 0-2; (2) reset the toggle on a write to `#E3` and on any other register access in **both** directions (today a register read resets only the read toggle, a write only the write toggle) |
| IDE disk, slots, formats | shared core on `ide-atapi`; `.hdf` (RS-IDE) is the common DivIDE image format and is supported | none |
| SD card | `SdCardSpi` (master), `HostFolderFat` (FAT16 default, FAT32 with the cluster minimum) | a slot `sd.divmmc` (the storage manager's naming rule already reserves it, [reuse-and-readiness.md](../2026-09-28-storage-manager/reuse-and-readiness.md) G1) |
| SPI port | `ZControllerSpi` (select on D1, read = previous byte + new exchange) | generalize to a `SpiPort` with a configurable select mask (DivMMC bit 0 / bit 1, Next bits 0-7) or add a `DivMmcSpi` twin; either way ~50 lines |
| `#E3` + memory | **nothing**. `Memory` maps in 16 KB windows (`_bank_read[4]`); DivIDE needs two 8 KB halves of window 0 from a foreign memory | see §5 |
| Automap | `Z80::machineM1Hook` (`BeforeMachineM1` / `OnMachineM1`, master, built for ZX-Evo E3) gives both "instant" and "after the fetch" points | one hook per machine today: the DivIDE needs to chain with the machine's own hook (ZX-Evo, TR-DOS `#3Dxx` rules in `memory.cpp`) |
| Persistent EEPROM | none | an 8 KB blob with session / persist access (storage-manager G11 "persistent blobs") |
| TTD | nothing | `#E3`, automap state, toggle: a small POD blob; DivIDE RAM (32-512 KB): a TTD v2 memory region (PLAN #40-V1) or, before V1, the whole RAM in the blob (the NeoGS precedent, `PeripheralId::NeoGS = 12`) |

## 5. The cross-cutting piece: 8 KB paging in `#0000-#3FFF`

Three ways to show 8 KB pages in window 0:

| Option | How | Verdict |
|---|---|---|
| A. `HostBusOverlay` (master, `core/src/emulator/memory/hostbusoverlay.h`) | the overlay replaces what the CPU reads in `#0000-#3FFF` | **no**: the normal access runs first, so a write with RAM paged at `#0000` (Pentagon RAM mode, +3 all-RAM) also lands in machine RAM; the debugger, memory views and TTD see the machine page, not the board; and only one overlay may be installed (NeoGS ZXDMA already uses it) |
| B. Split window 0 into two 8 KB halves in `Memory` (`_bank_read` of 8 entries, or 4 + a "split" flag for window 0) | the DivIDE sets the two halves to its EEPROM and RAM pages, which live in the memory buffer like ROM and cache pages | **yes**: the one correct design; every access path, the debugger, TTD dirty tracking and memory counters see the right page. It is also what the ZX Next needs (its MMU has eight 8 KB slots) |
| C. Map a 16 KB "composite" page (copy EEPROM + bank into a scratch page on every change) | copies on each bank switch | no: writes to the bank would need write-back, and TTD sees a scratch page |

**Recommendation: B**, as its own small core project ("8 KB windows"), with benchmarks before and
after (the fast read path is `*(_bank_read[addr >> 14] + (addr & #3FFF))`; an 8-entry table with
`addr >> 13` costs nothing more).

## 6. Software to test with

| Item | Where | Note |
|---|---|---|
| esxDOS 0.8.5 (8 KB, DivIDE) | zxsp `Resources/Roms/esxdos085.rom` | copy into `testdata/` with a notice |
| esxDOS 0.8.9 DivMMC / DivIDE | pico-spec `src/roms/esxdos/esxdos.c`, `esxide.c` (C arrays; extract to binary) | the current release is 0.8.9 |
| esxDOS system files (`SYS/`, `BIN/`) | the esxDOS distribution (esxdos.org) | a **folder** in `sd.divmmc` through `HostFolderFat`: no image needed |
| Test cards / disks | none on disk (no `.mmc`, `.hdf` anywhere in `testdata/` or the references) | build from a folder; pico-spec's `esxdos.mmc` is a superfloppy (no MBR), so the folder builder should offer both layouts |

## 7. Acceptance tests

1. **Paging truth table** (unit): every `#E3` value x trap state x MAPRAM history; reads and writes
   at `#0000`, `#1FFF`, `#2000`, `#3FFF`; bank 3 write-protect; MAPRAM survives reset but not power-on.
2. **Automap timing** (unit, M1 hook): a program jumps to `#0562`; the byte fetched at `#0562` is the
   ROM's, at `#0563` the EEPROM's; a jump to `#3D00` fetches the EEPROM's byte at once; a fetch at
   `#1FF8` maps out after the instruction.
3. **esxDOS boot** (real firmware, 48K and 128K): with a folder holding `SYS/` and `BIN/`, the
   48K screen shows the esxDOS banner, then `.ls` lists the folder (screen text).
4. **Tape trap**: `LOAD ""` with `.tapein game.tap` loads from the folder.
5. **DivIDE**: the same boot with `ESXIDE.BIN` and a `.hdf` in `ide0.master`.
6. **TTD**: record through an esxDOS `.ls`; replay reproduces the screen; the `#E3` blob round-trips
   mid-transfer.

## 8. Order and dependencies

1. 8 KB windows in `Memory` (core, benchmarked).
2. `DivPaging` device: `#E3`, automap on the M1 hook (chained), EEPROM blob, RAM pages, TTD blob.
3. DivMMC (`SpiPort` + `sd.divmmc`) - the most used variant today, and the Next reuses it.
4. DivIDE (paging + the existing `IDE_DIVIDE` adapter).
5. Karabas-Pro personality (DivMMC inside the Profi decoder, `#EB` shared with the Profi IDE family
   when CP/M is off, [karabas-pro-hardware-analysis.md](../2026-09-21-profi/karabas-pro-hardware-analysis.md) §1.7-1.8) - only if Karabas-Pro becomes a model.

## Glossary

| Term | Meaning |
|---|---|
| automap | the board's trap: it pages itself in when the CPU fetches from certain ROM addresses |
| M1 | the Z80 bus cycle that fetches an opcode; traps watch M1 so data reads do not trigger them |
| CONMEM / MAPRAM | bits 7 / 6 of `#E3`: "map the board now" / "use RAM bank 3 as the firmware area" |
| EEPROM | the board's 8 KB rewritable firmware chip |
| superfloppy | a FAT volume with no partition table (the boot sector is sector 0) |
| dot command | an esxDOS program in `/BIN`, run from BASIC as `.name` |
