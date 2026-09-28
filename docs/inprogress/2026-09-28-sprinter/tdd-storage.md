# TDD — storage: floppy, IDE, CMOS, media slots, DSS boot profile

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Review round 1 done (2026-09-28): `ide0.slave` empty by default (Q5, §1); `Ds12887` is the shared CMOS core (§4) |
| **Hardware** | [hardware-reference.md](hardware-reference.md) §9, §10, §12, §14 |
| **Plugs into** | media manager [technical-design.md](../2026-09-28-storage-manager/technical-design.md) (PLAN #58), [integration-ide-cd.md](../2026-09-28-storage-manager/integration-ide-cd.md), [integration-floppy.md](../2026-09-28-storage-manager/integration-floppy.md); shared IDE core [2026-09-25-ide-hdd-design.md](../2026-09-21-profi/2026-09-25-ide-hdd-design.md) (PLAN #13a) |

## 1. Slots

| Slot id | Kind | Controller | Default |
|---|---|---|---|
| `fdd.a` … `fdd.d` | Floppy | WD1793 (existing) | empty; 3.5" HD drives |
| `ide0.master` | Block | primary channel, unit 0 | empty |
| `ide0.slave` | Block, or Optical when configured `cdrom` | primary channel, unit 1 | **empty, no device** (review round 1, Q5). Once ATAPI exists (S7), an empty CD unit here, as MAME wires it (`sprinter.cpp:1967-1968`), is a config option |
| `ide1.master`, `ide1.slave` | Block | secondary channel | empty |

Naming trap: the BIOS constant for the **primary** channel is called `IDE_CHANEL_2` (code `#2B`) and
the secondary `IDE_CHANEL_1` (`#2A`) (INC `SP2000.inc:1341-1342`). Slot ids follow the ATA names:
`ide0` = primary (selected with `OUT (#BC),#21`), `ide1` = secondary. BIOS drive codes: `#80`
= `ide0.master`, `#81` = `ide0.slave`, `#82` = `ide1.master`, `#83` = `ide1.slave` (bit 0 unit,
bit 1 channel, HW §9.2).

The BIOS probes every unit at start-up. A test (T-IDE-8) runs that probe with `ide0.slave` empty and
with an empty CD unit there, and checks what the BIOS reports in both setups.

## 2. Floppy

### 2.1 Port wiring

The decoder calls the WD1793 with the canonical Beta port for the code: `#10`→`#1F`, `#11`→`#3F`,
`#12`→`#5F`, `#13`→`#7F`, `#14`/`#15`→`#FF` (`WD1793::portDeviceInMethod/OutMethod`,
`core/src/emulator/io/fdc/wd1793.h:1162-1163`). The WD1793's own port registration
(`attachToPorts`, `wd1793.cpp:3452-3465`) is **not** used on this model: the table decides.
Code `#15` returns `WD1793 #FF status & joystick bits` (MAME `sprinter.cpp:605-607`).

### 2.2 DOS signal

The DOS signal is PLD state (it is an index bit of the port table and an input of the window-0
formula), so the Sprinter M1 hook owns it, as the TSConf design does for its own DOS:

```text
OnMachineM1(pc):
    if pc in #3D00..#3DFF and dosOff and #7FFD bit 4: dosOff = 0; UpdateBanks()   // MAME :1383-1391
    if pc >= #4000 and not dosOff:                      dosOff = 1; UpdateBanks()   // MAME :1392-1400
```

The generic `CF_SETDOSROM` / `CF_LEAVEDOS*` flags stay off for this model
(`core/src/emulator/memory/memory.cpp:903-923`).

### 2.3 Density and media

| Item | Design | Source |
|---|---|---|
| Density latch | codes `#16` (DD, 250 kbit/s) / `#17` (HD, 500 kbit/s) set `FdcDataRate`; reset = DD (ZXMAK2 resets to "720", `SprinterFdd.cs:318`) | HW §10 |
| WD1793 clock | at HD the WD1793 runs at 2 MHz: step rates and settle times halve (MAME `beta_m.cpp:196-202`) | MAME |
| Medium density | from the image: 18 sectors × 512 on a track = HD, ≤ 10 = DD, TR-DOS 16 × 256 = DD | geometry |
| **Mismatch** | new WD1793 option `rateCheck` (off for every other model): when the drive's data rate differs from the medium's, the controller never sees an ID address mark, so READ ADDRESS / READ SECTOR end in Record Not Found after the WD1793's normal index count. This is what the BIOS density probe relies on (`FDD_DRIVER.asm:626-650`) | FR-21 |
| FDC off | value bit 1 = 1 on a density write disables the FDC ports (MAME `:730-733`); keep it, **unverified** in the PLD | MAME |

The unreal-ng WD1793 today derives byte timing from the raw track size and has no explicit rate
(`wd1793.h:944-957`); `rateCheck` adds one comparison in the ID search, nothing else.

### 2.4 Raw PC floppy images

New loader + writer `LoaderRawPcFloppy` (`core/src/loaders/disk/loader_rawpc.{h,cpp}`), registered in
the media format registry by size (storage technical design §4 already reserves it):

| Size | Geometry | Density |
|---|---|---|
| 737 280 | 80 cylinders × 2 sides × 9 sectors × 512 bytes, IDs 1-9, N = 2 | DD |
| 1 474 560 | 80 × 2 × 18 × 512, IDs 1-18 | HD |

MFM tracks are generated with standard PC gaps (GAP4a 80, GAP1 50, GAP2 22, GAP3 84 for DD / 108 for
HD); a HD track fits the 12 500-byte raw track (`core/src/emulator/io/fdc/diskimage.h:362`). Save
writes the sectors back when the layout is still regular; otherwise Export offers UDI. The +3 and
Profi raw formats (review round 2, G9 of the storage manager) share this loader.

### 2.5 The `#1F` operand rewrite

[tdd-accel-sound-input.md](tdd-accel-sound-input.md) §5.3 (it lives in the M1 hook with the
accelerator opcode snooping). Consequence here: TR-DOS 5.04Em and Spectrum programs use `#0F`
(or have `#1F` rewritten), and the table pattern `000x x111` routes both to code `#10`.

## 3. IDE adapter

### 3.1 Position in the IDE design

`IdeAdapterSprinter` (`core/src/emulator/io/hdd/adapters/ideadapter_sprinter.{h,cpp}`) is one more
adapter on the shared core (IDE design §5), with two differences from every board listed there:

1. **Two channels.** The adapter owns a channel latch and talks to `AtaChannel` 0 or 1. The IDE
   design has `Core` own one `AtaChannel` (§5 "Ownership"); the Sprinter needs a second one. Change:
   `Core` owns an array of up to 2 channels, created per model (`ide_scheme` says how many). Only the
   Sprinter asks for 2.
2. **A new latch pattern (e): "A8 half, low-byte latch on write".** Reads: A8 = 0 fetches the word,
   returns the low byte, latches the high byte; A8 = 1 returns the latch. Writes: A8 = 0 stores the
   byte in the latch; A8 = 1 sends `value << 8 | latch`. One latch register for both directions
   (PLD `SP2_1K30.TDF:181`, `:360-373`). Added to `idelatch.h` as `A8HalfLatch`.

### 3.2 Pseudocode

```cpp
uint8_t IdeAdapterSprinter::Read(uint8_t code, uint16_t port)
{
    const bool a8 = port & 0x0100;
    AtaChannel& ch = _channels[_selected];
    switch (code)
    {
        case 0x20:
            if (a8) return _latch;
            { uint16_t w = ch.ReadData(); _latch = w >> 8; return w & 0xFF; }
        case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
            return a8 ? 0xFF : ch.ReadRegister(code & 7);      // MAME :623-626
        case 0x28: return a8 ? 0xFF : ch.ReadAltStatus();       // #3F6
        case 0x29: return a8 ? 0xFF : ch.ReadDriveAddress();    // #3F7
    }
    return 0xFF;
}

void IdeAdapterSprinter::Write(uint8_t code, uint16_t port, uint8_t v)
{
    const bool a8 = port & 0x0100;
    AtaChannel& ch = _channels[_selected];
    switch (code)
    {
        case 0x20: if (a8) ch.WriteData(uint16_t(v << 8 | _latch)); else _latch = v; break;
        case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
            if (a8) ch.WriteRegister(code & 7, v); break;
        case 0x28: if (a8) ch.WriteDeviceControl(v); break;
        case 0x2A: _selected = 1; break;                        // secondary
        case 0x2B: _selected = 0; break;                        // primary
    }
}
```

Reset: `_selected = 0` (MAME `:1582`), `_latch = 0`, both channels hard-reset.

**Worked example: how the BIOS alternates A8 without changing the port.** The sector loop is
`LD BC,#0050` followed by 512 `INI` (BIOS-TT `ATA_DRV.ASM:350-366`, BC = `IDE.Read.Data`). `INI` puts B on A15-A8 and
decrements B *after* the input, so B runs `#00, #FF, #FE, #FD…`: the first `INI` has A8 = 0 (reads a
word, returns the low byte), the second has A8 = 1 (returns the latched high byte), and so on. The
write loop uses `OUTI` (`ATA_DRV.ASM:409-413`), where B is decremented *before* the output, with the
same alternation. Two consequences: the adapter must look at A8 of the
actual bus address, and the Z80 core must drive B on the address bus with the documented
pre/post-decrement timing for `INI`/`INIR`/`OUTI`/`OTIR` (a CPU-level test in the test plan §2.5).

### 3.3 What the BIOS and DSS need from the drive

- ATA commands used by BIOS-TT: `#90`, `#91`, `#20`, `#30`, `#40`, `#70`, `#EC`, `#C4/#C5/#C6`
  (INC `constants/ATA.inc:67-112`); all are in the IDE design's rollout-1 list (§6.3).
- LBA and CHS: the BIOS stores CHS geometry per unit in CMOS (`#12-#19`, `#37-#3E`) and uses LBA
  when IDENTIFY word 49 says so (`HD_IDF_ADR.LBA_CHS`, INC `SP2000.inc:888-893`).
- Interrupts: none; the BIOS polls status (HW §9.1).
- ATAPI: BIOS-TT `ATAPI_DRV.ASM` (packet commands, 2048-byte blocks, eject/close through `#5E`);
  the IDE design's `AtapiCdrom` (rollout 1, R1-7) serves it unchanged.

## 4. CMOS

`Ds12887` (`core/src/emulator/io/rtc/ds12887.{h,cpp}`) on the shared register map
`core/src/emulator/io/rtc/ds12885.h`.

**Decision (review round 1): `Ds12887` is the shared MC146818 core** for every machine: clock,
NVRAM, CMOS file, fixed time for tests, TTD. It is built by extracting the most complete existing
implementation, the ATM3 `CMOS` (`core/src/emulator/memory/atm/cmos.{h,cpp}`). Moving the existing
clocks onto it is a **separate shared-infrastructure task** (PLAN #60), done **before** the
Sprinter program, with tests that pin each machine's current behavior before and after:

| Existing clock | Where | Migrates onto `Ds12887` |
|---|---|---|
| ATM3 `CMOS` | `core/src/emulator/memory/atm/cmos.{h,cpp}` | the source of the extraction |
| `ProfiCMOS` | `core/src/emulator/memory/profi/proficmos.h` | yes (gains TTD capture) |
| `SMUCNvram` | `core/src/emulator/io/rtc/smucnvram.{h,cpp}` | yes |
| the RTC inside `EvoAvr` | `core/src/emulator/memory/atm/evoavr.{h,cpp}` | yes |

The Sprinter then only wires its ports and its CMOS file to the core:

| Item | Design |
|---|---|
| Ports | code `#1D` = address latch (`#DFBD`), `#1E` = data write (`#BFBD`), `#1C` = data read (`#FFBD`) |
| Registers | `#00-#0D` clock (BCD or binary per register B, 12/24 h), `#0E-#7F` NVRAM (114 bytes), century at `#32` |
| Time source | host time (default) or `FixedTime=` for tests, like the ATM3 `CMOS` fixed-time mode (`core/src/emulator/memory/atm/cmos.h:16`) |
| Persistence | `[SPRINTER] CmosFile=` (128 bytes, written on change and at shutdown), the `EvoAvr` NVRAM precedent (`core/src/emulator/memory/atm/evoavr.h:77-78`) |
| Default contents | a file with valid BIOS settings and checksum so the first boot does not stop at "CMOS checksum error"; generated by the test fixture from BIOS-TT's defaults |
| TTD | whole 128 bytes + address latch in the blob (Profi's RTC has no TTD capture today; this does it right from the start) |

Worked example: writing `#0A` to port `#DFBD` selects register `#0A`; a read of port `#FFBD`
returns it. The same two
steps on an ATM3 reach the same `Ds12887` code through ATM3's own ports; only the port wiring
differs.

## 5. The DSS boot profile for folder volumes

### 5.1 What DSS needs on a hard disk

From the loader source (DSS `utils/BOOT/DOSBOOT4.ASM`) and the installer (`SYS.ASM:130-140`):

| LBA | Contents | Why |
|---|---|---|
| 0 | MBR, signature `#AA55`, **partition entry 0** of type `#06` (FAT16), `#04`, `#0E` or `#01` | the loader checks entry 0 only (HW §9.3) |
| 1-3 | the 1 536-byte DSS loader, starting with `Starting...\0` | the BIOS reads LBA 1 and jumps to `#800C`; the loader reads LBA 2-3 |
| partition start | FAT16 boot sector with the BPB, `FAT16   ` at `+#36`, media `#F8` | `DOSBOOT4.ASM:351-375` |
| root directory | `SYSTEM.DOS` (kernel), `SYSTEM.EXE` (shell) | `DOSBOOT4.ASM:650`, `:213` |

The storage manager's layout already puts the partition at LBA 2048 (storage technical design
§6.3), so LBA 1-3 are free. The profile only has to (1) choose type `#06`, (2) fill LBA 1-3.

### 5.2 Profile

`HostFolderFat` gets a small `BootProfile` hook (a strategy, the same shape as the folder disk
builder's format strategies): `SprinterDssBootProfile` fills reserved sectors 1-3 and sets the
partition type. Where the loader bytes come from, in order:

1. `BOOT.EXE` in the folder root or `CMD\BOOT.EXE`: its **last 1 536 bytes**, accepted only if they
   start with `Starting...` (in DSS 1.60R `BOOT.EXE`, 2 453 bytes, the loader starts at offset
   `#395`);
2. `[SPRINTER] DssBootLoader=<file>` (a raw 1 536-byte file);
3. none: the volume is still mounted, not bootable; the report says "no DSS boot loader found".

Profile choice: automatic for a folder in an `ide*` slot of a `SPRINTER` machine, overridable by
`<slot>.boot = dss | none`.

### 5.3 Worked example

Folder `~/sprinter/c/` holds `SYSTEM.DOS`, `SYSTEM.EXE`, `SYSTEM.BAT`, `CMD/BOOT.EXE`,
`GAMES/…` (copied from the DSS 1.62 floppy). Insert into `ide0.master` (Session access):

| LBA | Generated from |
|---|---|
| 0 | MBR: entry 0 type `#06`, start 2048, size = volume |
| 1-3 | last 1 536 bytes of `CMD/BOOT.EXE` |
| 2048 | FAT16 boot sector, 8 sectors per cluster (4 KB) |
| … | FATs, root (`CMD`, `GAMES`, `SYSTEM.BAT`, `SYSTEM.DOS`, `SYSTEM.EXE`), data |

CMOS boot drive = HDD 0 → BIOS reads LBA 1 → loader → `SYSTEM.DOS` → DSS prompt on `C:`. Guest
writes stay in the session map; the folder is untouched until a commit (storage design §6.5).

### 5.4 Limits

- Cluster size: the loader keeps the cluster length in 16 bits and reads up to 32 sectors at a time
  (`DOSBOOT4.ASM` `FLOAD`); the profile caps FAT16 clusters at 16 KB (volume ≤ 1 GB) until DSS is
  tested with 32 KB clusters (**unverified**).
- FAT32 is never offered on this model (DSS reads FAT12/FAT16 only, HW §9.3): `fs=fat32` on a
  Sprinter IDE slot is refused with an error.
- A floppy folder (a FAT12 folder image in `fdd.*`) is not in the storage manager's plan (its folder
  floppy builder is TRD first); a DSS FAT12 floppy builder can follow the same profile later.

## 6. TTD and snapshots

The media manager's common rules apply (storage technical design §9): the media set is fixed while
recording; a guest write is a replay barrier. The adapter latch and channel latch are in the
Sprinter IDE blob (or the shared `AtaChannelState.adapterLatch[4]`, IDE design §10.1). CMOS is in
its own blob (§4).

## 7. Tests

See [test-plan.md](test-plan.md) §2.5-2.7: adapter truth table (every code × A8 × direction),
word order (`#ABCD` → image bytes `CD AB`), channel select by `OUT (#BC),#21/#01`, density
mismatch, raw PC loader round trip, CMOS persistence, the boot profile's LBA 0-3 bytes, and the
ROM-gated boots (ACC-3, ACC-4, ACC-5).
