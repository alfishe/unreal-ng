# Phase E4 — virtual TR-DOS drives (BaseConf "trdemu")

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Done. Plan: [implementation-plan.md](implementation-plan.md) phase E4 |
| **Gaps closed** | ST-5 (trdemu trap), ST-6 (legacy RAM-disk latches) of [gap-analysis.md](gap-analysis.md). P-2 (FDC only in shadow) was already closed in E0 |
| **Design** | [tdd-virtual-trdos.md](tdd-virtual-trdos.md) ("As built" notes in §3) |

## 1. The user-visible result

The ERS RAM disk works. On the shipped ROM with a blank NVRAM, the ERS makes drive A a RAM disk.
Choosing "S. TR-DOS" in the ERS menu starts NEO-DOS with `Virtual Drive: A`, and then:

```
A>LIST                      -> Title: RAMDISKO  Disk Drive: A
                               0 File(s)   80 Track D. Side   Free Sector 2544
A>SAVE "a" CODE 32768,256
A>LIST                      -> 1 File(s) ... Free Sector 2543
                               a       <C>  1  32768   256
A>LOAD "a" CODE 40960       -> the 256 bytes at 40960 equal the ones saved
```

Every one of these disk accesses runs the unmodified NEO-DOS ROM. The disk itself is emulated
by the ERS's own WD1793 emulator, which lives in RAM page `#FE`. The emulator provides only the
FPGA part: it swaps that page in at the right moment.

Before E4, NEO-DOS talked to the real WD1793 model, which had no disk in drive A, so every
command above failed.

## 2. What changed

| Piece | Change | Evidence |
|---|---|---|
| Trap | An FDC access in shadow (`#1F/#3F/#5F/#7F/#FF`) for a drive masked in `#13BD` sets *pending* when TR-DOS is on (`CF_TRDOS`), window 0 is ROM, and palette-write mode is off (`#xx77` A14 = 1). For `OUT (#FF)` the drive being written counts, otherwise the latched one (`evoVgDrive`) | `fpga/base_trdemu/trunk/z80/zdos.v:61`, `zports.v` `vg_rdwr_fclk` |
| Chip deselect | For a masked drive, `#1F-#7F` reads return `#FF` and writes are dropped. `#FF` always reaches the WD1793 model (our model owns the `#FF` latch, so the read keeps INTRQ/DRQ and the drive bits) | `zports.v` `vg_cs_n` |
| Swap in | At the start of the **next** opcode fetch (new `IMachineM1Hook::BeforeMachineM1`, called before the fetch), *pending* becomes *in* and window 0 becomes RAM `#FE`. The trapping instruction itself still runs with the ROM, so an `INI` cannot write into `#FE` (the RTL's `trdemu_wr_disable`) | `zdos.v`, `atm_pager.v:114-168` |
| Swap out | `OUT (#xxBE)` clears *in* at once, so the fetch right after the `OUT` comes from the ROM. Inside an NMI the `#BE` write ends the NMI only (E3 rules) | `zdos.v` `clr_nmi && !in_nmi` |
| NMI on top | Window 0 is RAM `#FF` while both are active, `#FE` again after the NMI leaves | `atm_pager.v` `page <= {7'h7F, in_nmi}` |
| No trap in the NMI page | The trigger checks the *effective* window 0 (ROM). Inside the NMI page or the trdemu page itself, FDC accesses by ERS code never re-trap | `zdos.v:61` `romnram` is the pager output |
| DOS stays on | Already in E3: `IsDosLeavingBank` looks at the *programmed* window type, so running the stub in page `#FE` keeps TR-DOS on | `atm_pager.v:251-256` |
| Legacy FPGA | `[EVO] Fpga=legacy` has no mask and no trap. Instead `#2F/#4F/#6F/#8F` are four read/write bytes in shadow (`EmulatorState::wd_shadow`) | `fpga/baseconf/trunk/zports.v:186-189` |
| Z80 | `IMachineM1Hook` gained `BeforeMachineM1(address)` next to `OnMachineM1`. It costs nothing when no hook is attached, and the decoder attaches it only while a swap is pending | — |
| TTD | `AtmPagingState` 132 → 136 bytes: `evoTrdemu` (bit 0 in, bit 1 pending), `evoVgDrive`, 2 reserved. A restore re-runs the paging decode, which re-attaches the M1 hook a pending swap needs | — |

## 3. Tests

| Test | Design ID | Pins |
|---|---|---|
| `PortDecoder_ATM3_Test.FddMask13BD_ReadWriteResetAndLegacyAbsent` (E1) | TRD-1 | `#13BD` read-back, reset, absent on legacy |
| `ZXEvoTrdemu_Test.MaskedDriveDeselectsTheChip` | TRD-2, TRD-9 | masked drive: `#1F` reads `#FF`, a command never reaches the WD1793; `#FF` still answers |
| `ZXEvoTrdemu_Test.SystemWriteUsesTheNewDriveNumber` | TRD-3 | `OUT (#FF)` judged by the drive being written |
| `ZXEvoTrdemu_Test.TrapNeedsEveryCondition` | TRD-4 | remove DOS / ROM in window 0 / palette-write off / mask bit, one at a time: no trap |
| `ZXEvoTrdemu_Test.SwapForNextFetchAndImmediateExit` | TRD-5, TRD-8 | NEO-DOS `IN A,(#1F)` at `#1FDD`: next fetch from `#FE`, `OUT (#BE),A` returns to ROM page 29 at once, DOS stays on |
| `ZXEvoTrdemu_Test.TrappingIniCannotWritePageFE` | TRD-6 | NEO-DOS `INI` at `#3FEC` with HL = `#0100`: page `#FE` unchanged |
| `ZXEvoTrdemu_Test.NmiOverTrdemuMapsPageFF` | TRD-7 | NMI over trdemu shows `#FF`, its `#BE` keeps trdemu |
| `ZXEvoTrdemu_Test.RestoreBetweenTrapAndSwapStillSwaps` | TRD-12 | state captured between the trap and the swap, restored after the stub has finished: the M1 hook comes back and the next fetch is again from `#FE` |
| `ZXEvoTrdemu_Test.LegacyFpgaLatchBytes` | TRD-11 | legacy latches read back in shadow, float outside |
| `PortDecoder_ATM3_Test.Fdc_OnlyInShadow_JoystickOutside` (E0) | TRD-10 | FDC gating |
| `ZXEvoErs_Test.FpgaSuitabilityProbePassesOnTrdemu` (E1) | ERS-FPGA-1 | the ERS accepts the FPGA |
| `ZXEvoErs_Test.RamDiskSaveListLoadThroughVirtualTrdos` | ERS-RD-1 (and more) | real ROM, keys typed into NEO-DOS: catalog of the fresh RAM disk, `SAVE`, catalog again, `LOAD` compares all 256 bytes (§1). With the trap disabled the test fails at the first `LIST` |
| `TtdAtmPaging_*` (updated) | — | blob layout 136, trdemu state round trip |

ERS-RD-1 in the design formats the RAM disk from the ERS Services menu. The shipped ROM already
creates and formats it at reset (drive A, 2544 free sectors), so the test uses that disk.

## 4. Not done here (tracked)

| Item | Where |
|---|---|
| ERS-RD-2 (RAM disk loaded from a TRD), ERS-MNT-1/2 (mount a TRD, `IMAGE.MNT` automount): they need an SD card or an HDD to hold the image | E5 (SD), E6 (NemoIDE) |
| `evo` automation state (`fdd_mask`, `in_trdemu`) | E10 |

## 5. Performance

The new pre-fetch call costs one pointer test per M1, the same as E3's post-fetch call. Measured
against HEAD `979574e0` in isolated worktrees (Release, three interleaved rounds of 5 repetitions,
medians): `BM_Z80_DecodeOverhead_NOP` −1.0 %, `DD_Prefix` −0.3 %, `MixedInstructions_Block` −0.5 %.
The machine was heavily loaded (base-against-itself spread 55-112 %, `BM_Frame_PureCPU` too noisy
to read), so the only conclusion is that there is no measurable slowdown.

## 6. Verification (2026-09-28)

Isolated worktree = HEAD `979574e0` + only the E4 files: `core-tests` **4039 passed, 0 failed**, zero
compiler warnings. Main tree: full build clean, 4058 tests passed. The real-ROM test fails with the
trap disabled (mutation check).
