# TDD — storage: floppy, IDE, CMOS, media slots, DSS boot profile

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Review round 1 done (2026-09-28): `ide0.slave` empty by default (Q5, §1); `Ds12887` is the shared CMOS core (§4). **§2 floppy built in S3a** (2026-10-01, branch `sprinter-s3a`; as-built notes in §2.6, outcome in [roadmap-and-plan.md](roadmap-and-plan.md) §8). **§1 slots and §3 IDE built in S3b** (2026-10-02, branch `sprinter-s3b`; as built in §3.4, outcome in roadmap §9) |
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

The WD1793 side is **built** (commits `64756638`, `f304dde1`; model and worked examples in
[WD1793_Clock_And_Data_Rate.md](../../WD1793/WD1793_Clock_And_Data_Rate.md)). The Sprinter only
wires its latch to it in S3a.

| Item | Design | Source |
|---|---|---|
| Clock policy | the Sprinter decoder returns `FdcClockPolicy::Latched` from `PortDecoder::DefaultFdcClockPolicy()`: STEP and DRQ never change the clock, only the latch does; `[Beta128] TurboVG=` cannot override it | landed API |
| Density latch | codes `#16` (DD) / `#17` (HD), i.e. `OUT (#BD),A` with A = `#01` / `#21` (A13 selects), call `WD1793::SetLatchedClock(FdcClock, FdcDataRate)`: DD = `Clock1MHz` + `Rate250Kbps`, HD = `Clock2MHz` + `Rate500Kbps`. Machine reset = DD (ZXMAK2 resets to "720", `SprinterFdd.cs:318`) | HW §10; PLD `SP2_MAX.TDF:272, 285, 396-402` |
| WD1793 clock | at HD the chip runs at 2 MHz: step rates 3/6/10/15 ms, head settle 15 ms; the chip also writes (WRITE TRACK / WRITE SECTOR) at 500 kbit/s | datasheet p.6, p.19 |
| Medium density | from each track's raw length (images have no density field): HD when the track is at least 1.5x the DD nominal, i.e. 9 375 MFM bytes or more; `LoaderRawPcFloppy` builds 1.44 MB images with 12 500-byte tracks (§2.4), 720 KB and TRD with 6 250 | `DiskImage::RawTrack::RecordedDataRate()` |
| **Mismatch** | the data-rate check is **always on** in the WD1793, for every model (no option): a track at the other rate shows no address mark, so READ ADDRESS / READ SECTOR end in Record Not Found after the normal index count, verify in Seek Error. This is what the BIOS density probe relies on (`FDD_DRIVER.asm:626-650`). Other models are unaffected because their separator is 250 kbit/s and their disks are DD | FR-21 |
| FDC off | value bit 1 = 1 on a density write disables the FDC ports (MAME `:730-733`); keep it, **unverified** in the PLD | MAME |
| DD-mode turbo VG | in DD the PLD also runs the chip at 2 MHz while positioning (`TURBING` set by STEP, held until the read/write strobe). **Not modeled** in v1: `Latched` keeps 1 MHz in DD, so seeks take the standard time; the data is unaffected. A follow-up in [TODO.md](TODO.md) | PLD `SP2_MAX.TDF:272-306` |

Why the КР1818ВГ93 can do HD at all: the chip has no "HD mode", but its datasheet has two clocks,
1 MHz for 5.25"/3.5" drives and 2 MHz for 8" drives. The 8" MFM rate is 500 kbit/s, the same bit
rate as a 3.5" HD 1.44 MB disk. At 300 rpm that is 500 000 bit/s x 0.2 s = 12 500 raw bytes per
track, room for 18 x 512-byte sectors; the chip does not know the rpm. The Sprinter doubles the
clock **and** switches its separator from 7 MHz to 14 MHz, so reading and writing both run at
500 kbit/s. Doubling only the clock (as "turbo VG" on other clones does) would not read HD disks
and would ruin DD disks on write.

Why the BIOS probe works: READ ADDRESS at the current rate; on Record Not Found the BIOS flips the
latch and retries. A DD disk answers at DD, an HD disk at HD, and the emulated controller gives the
same answers.

CPU budget: an HD byte arrives every 16 µs. That is 56 T-states at 3.5 MHz, less than the 58 T of
the fastest TR-DOS transfer loop (Lost Data on every sector), but **336 T at 21 MHz**, six times the
need (and 112 T at 7 MHz, still enough). Check in S3a that the FDC timebase stays in
real time when the CPU runs at 21 MHz (research open question 7,
[DONE.md](../2026-09-29-fdc-clock-and-data-rate/DONE.md)).

### 2.4 Raw PC floppy images

> **Built (PLAN #60(f), 2026-09-29, branch `infra-60`):** format id `rawpc`, probed by size before the
> TR-DOS rule; `.img` saves go to MGT for a +D disk and to this writer otherwise; `.ima` added. Layout and
> worked totals: [rawpc.md](../../file-formats/disk-images/rawpc.md). Tests: `loader_rawpc_test.cpp` (both
> sizes through the WD1793, HD only at 500 kbit/s, a FAT12 boot sector), `floppyformats_test.cpp`,
> `mediaformatregistry_test.cpp`. The DSS 1.62 image is not in `testdata` yet (test plan:
> `testdata/machines/sprinter/`), so the real-image check waits for it.

New loader + writer `LoaderRawPcFloppy` (`core/src/loaders/disk/loader_rawpc.{h,cpp}`), registered in
the media format registry by size (storage technical design §4 already reserves it):

| Size | Geometry | Density |
|---|---|---|
| 737 280 | 80 cylinders × 2 sides × 9 sectors × 512 bytes, IDs 1-9, N = 2 | DD |
| 1 474 560 | 80 × 2 × 18 × 512, IDs 1-18 | HD |

MFM tracks are generated with standard PC gaps (GAP4a 80, GAP1 50, GAP2 22, GAP3 84 for DD / 108 for
HD); a HD track fits the 12 500-byte raw track (`core/src/emulator/io/fdc/diskimage.h:362`) and
is at the same time what marks it HD for the WD1793 (§2.3). Save
writes the sectors back when the layout is still regular; otherwise Export offers UDI. The +3 and
Profi raw formats (review round 2, G9 of the storage manager) share this loader.

### 2.5 The `#1F` operand rewrite

[tdd-accel-sound-input.md](tdd-accel-sound-input.md) §5.3 (it lives in the M1 hook with the
accelerator opcode snooping). Consequence here: TR-DOS 5.04Em and Spectrum programs use `#0F`
(or have `#1F` rewritten), and the table pattern `000x x111` routes both to code `#10`.

### 2.6 As built (S3a, 2026-10-01)

| Item | As built |
|---|---|
| Ports | as §2.1: codes `#10-#13` / `#14` / `#15` through `PeripheralPortIn/Out` with the canonical Beta ports (the WD1793 still registers them; the table decides when they are reached). Code `#15` = WD1793 `#FF` bits 7-6 or'ed with the Kempston bits 5-0 (`HasKempstonJoystick() = true`) |
| Drive select | the WD1793 now follows Beta `#FF` bits 1-0 (it used drive A for every value before: a shared fix). A blank CMOS boots floppy **B** (SETUP default CMOS `#10` = `#12`) |
| DOS signal | built in S1 (the decoder's `BeforeMachineM1`), test T-FDD-7 added in S3a |
| Density | as §2.3. Every PLD reset = 720 KB, FDC on. The latch writes `SetLatchedClock` whatever the off bit |
| Rate change mid-command | the probe flips the latch while its READ ADDRESS still runs (the chip ignores the new command while busy). The WD1793 re-runs a pending READ ADDRESS / Type I verify search at the new rate, keeping the first deadline (`WD1793::retrySearchAtNewRate`) |
| Time base at 21 MHz | research question 7 answered: it was **not** real time (Z80::t counts 6 CPU clocks per base T-state inside the frame, and `t_states` adds the base frame). `WD1793::SetBaseClockTimeBase(true)` (set by the Sprinter decoder) scales the frame part back; opt-in so the other turbo machines' TTD captures stay as they are |
| `#1F` rewrite | at the I/O cycle: when the port's low byte is `#1F`, the instruction at `m1_pc` is an unprefixed `#D3` / `#DB` with operand `#1F` and the operand's window holds RAM, the port becomes `#xx0F` (MAME `check_accel`). MEMPTR's visible high byte is A either way |
| Media | slots `fdd.a`-`fdd.d` from the generic `FloppyDriveSlots`; `.img` 1.44 MB / 720 KB through `LoaderRawPcFloppy`, TRD as usual |

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

### 3.4 As built (S3b, 2026-10-02)

| Item | As built | Deviation from §3.1-§3.2 |
|---|---|---|
| Adapter | the shared `IdeAdapter` (`core/src/emulator/io/ide/ideadapter.{h,cpp}`) got a `SPRINTER` region: `SprinterIn(code, port)` / `SprinterOut(code, port, value)`, called by `PortDecoder_Sprinter::StandardReadCode` / `StandardWriteCode` for codes `#20-#29` and `#2A` / `#2B` (the way the Scorpion decoder hands `#xxBE` to `SmucIn` / `SmucOut`) | no `IdeAdapterSprinter` class and no `idelatch.h`: rollout 1 built one adapter class for every board (IDE implementation plan §5, "Board"); the files live in `io/ide/`, not `io/hdd/adapters/` |
| Latch (pattern e) | one register, `IdeAdapterState::readLatch` (the PLD's HDDR): read A8 = 0 fetches the word, returns the low byte, latches the high byte; read A8 = 1 returns the latch; write A8 = 0 stores the low byte there; write A8 = 1 sends `value << 8 | latch` | the latch and the channel select live in `IdeAdapterState` (the `AtaChannel` TTD blob), not in `SprinterPldState` (its two bytes are now `reservedIde`) |
| Task file | reads `#21-#27` with A8 = 0, writes with A8 = 1; `#28` = alternate status (read) / device control (write); `#29` (drive address) reads `#FF`: the shared core has no drive-address register | `#29` floats (MAME reads its ATA device's CS1 register 7) |
| Channels | `IdeController` owns up to two `AtaChannel`s (`ChannelCount()`: 2 for `IDE_SPRINTER`, else 1); units are numbered across channels (`unit = channel * 2 + position`, the order of `config.ide[4]` and of BIOS drive codes `#80-#83`); `IdeAdapter::Channel()` returns the channel `#2A` / `#2B` selected | as designed ("`Core` owns an array of up to 2 channels"), inside `IdeController` |
| Config | `[HDD] Scheme=SPRINTER` (fits `MM_SPRINTER` only; Nemo / DivIDE schemes no longer fit the Sprinter: its PLD decodes every port); `CHS2` / `CHS3`, `CD2` / `CD3`, `Image2` / `Image3`, `HD2RO` / `HD3RO` and `[MEDIA] ide1.master` / `ide1.slave` for the secondary channel; `configs/sprinter` ships `Scheme=SPRINTER` with four empty hard-disk units | - |
| Slots | `ide0.master`, `ide0.slave`, `ide1.master`, `ide1.slave`, labeled "IDE primary master (hard disk)" ... and tagged `primary` / `secondary`; alias `hd` / `cd` = the first unit of each kind. `ide0.slave` is an empty hard-disk unit (no device on the bus, Q5); `CD1=1` (or `device=cdrom` on an insert) makes it an ATAPI CD unit: the shared core's `AtapiCdrom` already serves it, BIOS 3.04 and 3.06 identify it as "UNREAL-NG CD-ROM" (T-IDE-8) | the CD option is in S3b, not S7 (it cost nothing); CD boot and CD audio stay for S7 |
| Reset | `IdeController::Reset` (machine reset) clears the latches and resets the units of both channels; every PLD reset (RESET button, `#2E` reload, soft reset) selects the primary channel (`IdeAdapter::SprinterReset`, MAME `:1582`) | - |
| TTD | the `AtaChannel` blob (id 17) appends the second channel (selected unit, unit kinds, both `AtaDeviceState`s) when the board has two; one-channel blobs are byte-identical to before (TTD corpus and CI gate unchanged). The Sprinter still refuses to record until S7 | instead of a Sprinter IDE blob |
| Report | `DeviceState::Ide` (WebAPI `state/ide`, CLI `state ide`, Lua / Python `ide_state`, MCP aspect `ide`): `channels`, `selected_channel`, `adapter.channel`, `adapter.data_latch`, and per unit `channel`, `selected`, `slot` | - |
| CPU | the Z84C15 library already drives B on A15-A8 the Z80 way (INI: before the decrement; OUTI: after it): `PortDecoderSprinterIde_Test.Z84C15_IniOutiPutBOnTheHighAddressByte`; no CPU change | - |

**What the BIOS does with a drive** (BIOS 3.04 `AUTODET` / `MASTERC`, SETUP `#852A-#96FF`; the same ATA commands in
MAME, [reference/hdd-boot-304.txt](../../../testdata/machines/sprinter/reference/hdd-boot-304.txt)):

| Unit | Bus answer | BIOS | Time |
|---|---|---|---|
| a disk | status `#50`; IDENTIFY | the model string ("UNREAL-NG HDD"), INITIALIZE DEVICE PARAMETERS (`#91`) | at once |
| no unit, the other unit on the channel present | the present unit answers for it (ATA): status `#00`, its sector count echoes | NOP (`#00`), then `WXREADY` waits for DRDY: `#118` HALTs | 280 frames (5.7 s), then "None" |
| an empty CD unit | ATAPI signature | IDENTIFY PACKET DEVICE: "UNREAL-NG CD-ROM" | at once |
| no unit on the channel at all | `#FF` (BSY: the bus floats, hardware-reference §9.1) | `CLRBUSY` waits for BSY: `#060E` HALTs | 1 550 frames (31.7 s) unless F4 |

BIOS 3.04 probes the primary channel only (two units); BIOS 3.06 probes all four (the secondary after
`OUT (#BC),#01`). So a disk on `ide0.master` boots BIOS 3.04 at frame ~475 with no key; BIOS 3.06 also waits on an
empty secondary channel unless a disk is there or F4 is pressed.

## 4. CMOS

`Ds12887` (`core/src/emulator/io/rtc/ds12887.{h,cpp}`) - **built (PLAN #60(c), 2026-09-28)**.

**Decision (review round 1): `Ds12887` is the shared MC146818 core** for every machine: clock,
NVRAM, NVRAM file, fixed time for tests, TTD. The existing clocks now run on it:

| Clock | Where | On `Ds12887` |
|---|---|---|
| ATM3 / ZX-Evo AVR | `EvoAvr` (`core/src/emulator/memory/atm/evoavr.{h,cpp}`) derives from it and serves A, C, D and `#F0-#FF` the AVR's way | yes, 256 cells |
| Profi | `PortDecoder_Profi::_rtc`, `[PROFI] NvramFile=` | yes, 256 cells, now in TTD |
| Scorpion SMUC | `SMUCNvram::GetRtc()` (the LC16 EEPROM stays in `SMUCNvram`) | yes, 256 cells |

The old `CMOS`, `ProfiCMOS` and `ds12885.h` are gone.

The Sprinter then only wires its ports and its CMOS file to the core:

| Item | Design |
|---|---|
| Ports | code `#1D` = address latch (`#DFBD`), `#1E` = data write (`#BFBD`), `#1C` = data read (`#FFBD`) |
| Registers | `#00-#0D` clock (BCD or binary per register B, 12/24 h), `#0E-#7F` NVRAM (114 bytes), century at `#32`: `Ds12887 rtc{128}` + `SetCenturyRegister(0x32)` |
| Time source | the chip's hybrid time base: host local time plus the guest's offset, emulated time while TTD records, `SetFixedTime` for tests |
| Persistence | `[SPRINTER] CmosFile=` (128 bytes) through `Ds12887::LoadNvram` / `SaveNvram`, read at power-on and written at shutdown like `[PROFI] NvramFile=` (`PortDecoder_Profi`); "written on change" would be an addition |
| Default contents | a file with valid BIOS settings and checksum so the first boot does not stop at "CMOS checksum error"; generated by the test fixture from BIOS-TT's defaults |
| TTD | the shared `PeripheralId::Ds12887` blob (id 18, `core/src/debugger/ttd/ttdds12887.h`): cells, address latch, time base; the decoder lists it in `GetTTDModelStateIds()` / `CreateTTDSerializers()` like ATM3, Profi and Scorpion do |

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
