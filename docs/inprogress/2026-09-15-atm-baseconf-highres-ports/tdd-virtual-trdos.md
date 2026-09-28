# Virtual TR-DOS drives (BaseConf "trdemu") — technical design

| | |
|---|---|
| **Date** | 2026-09-27 |
| **Status** | Design, ready for review. Nothing implemented |
| **Closes** | gaps **ST-5**, **ST-6**, **P-2** (FDC gating) of [gap-analysis.md](gap-analysis.md); user items "RAM disk" and "mount TR-DOS from file / SD / HDD" |
| **Depends on** | [tdd-evo-control-and-avr.md](tdd-evo-control-and-avr.md) (`#xxBD`, `#xxBE`, NMI, FPGA variant, M1 hook); for images on SD/HDD: [tdd-storage-sd-ide-cd.md](tdd-storage-sd-ide-cd.md) |
| **Hardware source** | [baseconf-hardware-reference.md](baseconf-hardware-reference.md) §A.8 (FPGA), §C 3 (ERS software) |

## 0. Summary

On a real Evo the **ROM does the disk emulation, not the FPGA**. The FPGA only provides a trap:
when the unmodified TR-DOS ROM touches the floppy controller for a drive marked "virtual", the
FPGA silently swaps RAM page `#FE` into `#0000-#3FFF` for the next instruction. The ERS has
unpacked a WD1793 emulator there (`dos_fe.a80`) with a stub at each TR-DOS I/O address. The stub
serves the request from a RAM disk (pages `#F5` and down) or from a TRD file on an SD/HDD FAT
volume, then executes `OUT (#BE),A`, which swaps the ROM back in. TR-DOS continues after its
`IN`/`OUT` and never notices.

So the emulator implements **only the trap** (about 100 lines in the ATM3 decoder). The RAM disk,
TRD mounting, SCL/FDI conversion, `IMAGE.MNT` automount and the menus all come for free from the
real ERS ROM, exactly as on hardware.

## 1. Worked example

The ERS has loaded a TRD into the RAM disk as drive B and written `#13BD ← %0010`.
TR-DOS reads a sector:

```
TR-DOS ROM #3FD5  OUT (#FF),A    ; select drive B → drive B is masked: real VG93 not selected
                                 ; trap condition true → next fetch from RAM #FE
RAM #FE    #3FD7  JP ADR_3FD7     ; the stub table entry for "read sector" (was IN A,(#FF) in ROM)
             ...                  ; WORKER copies 256 bytes from RAM disk to the caller's HL
RAM #FE    #2A53  OUT (#BE),A     ; exit strobe: window 0 is ROM again from the next fetch
TR-DOS ROM #2A55  RET             ; leaves the TR-DOS sector routine with "success"
```

## 2. Decisions

| # | Decision | Reason |
|---|---|---|
| D1 | Emulate the FPGA trap only; no host-side WD1793 virtualization | Faithful: the ERS code is the product users run; our host floppy images for **real** drives stay unchanged. A later host-side accelerator is possible (the ERS copies whole sectors already, so there is no speed problem to solve) |
| D2 | trdemu exists only when `[EVO] Fpga=trdemu` (default). `Fpga=legacy` provides the four RAM-disk latch bytes `#2F/#4F/#6F/#8F` instead | The two FPGA trees differ; the ROM image decides which one software expects (control design D1) |
| D3 | The page swap takes effect at the **next instruction start** (the M1 hook of TSConf phase 0, `CF_MACHINEM1`) and is undone **during** the `OUT (#BE)` | Matches the RTL: `in_trdemu` is set by the trapped access and seen by the following fetch; `clr_nmi` clears it at once (not after 2 M1 like NMI). Instruction granularity is exact here because the trapped access is always the last bus cycle that matters for the swap |
| D4 | The "write disable until next M1" of the RTL is satisfied by D3 | Inside the trapping instruction (e.g. `INI`) window 0 is still the ROM, so a memory write there already has no effect. A test pins this (TRD-6) |
| D5 | DOS stays on while executing from page `#FE` / `#FF` | RTL `ram_exec_stb` uses the **programmed** window type (`ramnrom[pent1m_ROM]`), not the NMI/trdemu override (`fpga/base_trdemu/trunk/mem/atm_pager.v:251-256`). Our "leave DOS on RAM execution" check must look at the programmed pager entry for ATM3 (TRD-8) |

## 3. Hardware behavior to implement

State (added to the ATM3 decoder, all in the TTD blob):

| Field | Meaning | Reset |
|---|---|---|
| `fddMask` (4 bits) | `#13BD` D3..D0: drive n is virtual | 0 |
| `vgDrive` (2 bits) | drive number from the last `OUT (#FF)` | 0 |
| `inTrdemu` | page `#FE` forced into window 0 | 0 |
| `trdemuPending` | set by the trap, applied at the next instruction start | 0 |
| `ffLow` | last written `#FF` bits (side, HLT, /RESET) for the reconstructed read | 0 |

Rules:

1. **Mask register.** `OUT (#13BD)` stores D3..D0; `IN (#13BD)` returns them (upper bits 0). Only
   with `Fpga=trdemu`. Decode: low byte `#BD`, index A12..A8 = `#13` (control design §3).
2. **Chip-select suppression.** For an FDC port access in shadow (`#1F/#3F/#5F/#7F/#FF`), let
   `drive` = the new D1..D0 for `OUT (#FF)`, otherwise `vgDrive`. If `fddMask[drive]` is set, the
   WD1793 is **not** accessed: reads of `#1F-#7F` return `#FF` (bus not driven), writes are dropped.
   `OUT (#FF)` still latches drive/side/HLT/reset into `vgDrive`/`ffLow` (the real latch is in the
   FPGA), but does not reach the WD1793 model.
3. **`#FF` read.** Always `{INTRQ, DRQ, 1, side, HRDY, /RES, drive}`: INTRQ/DRQ are the WD1793 model's
   current output lines (the real chip keeps driving them even while it is deselected for a masked
   drive), the rest comes from `ffLow`/`vgDrive`.
4. **Trigger.** On an FDC port access (any direction) when all hold: `Fpga=trdemu`, shadow,
   `CF_TRDOS` (DOS signal), window 0 **programmed** as ROM, palette-write mode off (`#xx77` A14 = 1),
   `fddMask[drive]`. Then `trdemuPending = 1`.
5. **Swap in.** At the next instruction start: `inTrdemu = 1`, `trdemuPending = 0`,
   `UpdateZ80Banks()`. Window 0 = RAM page `#FE`, or `#FF` when an NMI is also active.
6. **Swap out.** `OUT (#xxBE)` clears `inTrdemu` immediately unless an NMI is active (then the NMI
   exit rules apply first, control design §5). `UpdateZ80Banks()`.
7. **Priority of window 0:** pager off (ROM 31) > NMI (RAM `#FF`) / trdemu (RAM `#FE`) > `#EFF7`.3
   (RAM 0) > normal pager.
8. **Gating of the FDC itself (gap P-2):** VG93 ports answer only in shadow; outside shadow `#1F`
   is the Kempston joystick (8 bits) and `#3F/#5F/#7F/#FF` float. Applies to both FPGA variants.
9. **Legacy variant only:** `#2F/#4F/#6F/#8F` are four plain read/write bytes in shadow (BC
   `zports.v:186-189`).

## 4. Code placement

| File | Change |
|---|---|
| `ports/models/portdecoder_atm3.{h,cpp}` | trdemu state, FDC arm (gate + suppression + trigger + `#FF` read), `#13BD` in the `#xxBD` table, `#BE` exit, M1-hook callback |
| `ports/models/portdecoder_atm710.cpp` `updateMemoryBanks` | window-0 priority rule 7 for ATM3 (a hook the ATM3 class overrides, not an `if (model == ATM3)` in shared code) |
| `cpu/z80.cpp` instruction-start hooks | reuse `CF_MACHINEM1` + `pMachineM1Hook` from TSConf phase 0 (INF item); ATM3 registers its decoder as the hook. If ATM3 lands first, phase E3 builds the hook exactly as specified in TSConf technical-design §3.6 |
| `memory.cpp` leave-DOS check | ATM3 asks the decoder whether the **programmed** window is RAM (D5) |
| `debugger/ttd/atm/ttdatmpaging.{h,cpp}` | add the five fields; bump the blob version; `static_assert` size |
| automation | `evo` state: `fdd_mask`, `in_trdemu` (control design §9) |

## 5. Tests

File: `core/tests/emulator/ports/models/portdecoder_atm3_trdemu_test.cpp` (unit, synthetic memory)
and `core/tests/emulator/machines/zxevo/zxevo_trdemu_test.cpp` (real ROM, skips if
`zxevo_fe.rom` is absent).

| ID | Asserts |
|---|---|
| TRD-1 | `#13BD` reads back D3..D0; upper bits 0; reset clears it; absent (`#FF`) with `Fpga=legacy` |
| TRD-2 | masked drive: `IN (#1F)` returns `#FF`, WD1793 register untouched (command written via `OUT (#1F)` does not reach the model); unmasked drive: normal WD1793 |
| TRD-3 | `OUT (#FF),#01` with mask `%0010`: no trap (drive A); `OUT (#FF),#02`: trap uses the **new** drive number |
| TRD-4 | trigger needs every term: remove shadow / DOS / ROM-in-window-0 / palette-write-off / mask bit one at a time → no swap |
| TRD-5 | after a trapped `IN A,(#1F)` at `#1FDD`, the next fetch at `#1FDF` reads page `#FE`; `OUT (#BE),A` placed at `X` makes the fetch at `X+2` come from ROM |
| TRD-6 | trapped `INI` with HL in `#0000-#3FFF`: RAM page `#FE` is unchanged afterwards (D4) |
| TRD-7 | trap during NMI maps `#FF`, not `#FE`; `OUT (#BE)` inside NMI does not clear trdemu |
| TRD-8 | executing the stub in page `#FE` keeps `CF_TRDOS` set; returning to ROM `#3Dxx` code continues in TR-DOS (D5) |
| TRD-9 | `#FF` read = `{INTRQ,DRQ,1,side,HRDY,RES,drive}` for masked and unmasked drives |
| TRD-10 | outside shadow: `IN (#1F)` = joystick byte, `IN (#3F)` = `#FF`, WD1793 untouched (P-2) |
| TRD-11 | legacy variant: `#2F/#4F/#6F/#8F` read back in shadow, float outside |
| TRD-12 | TTD: capture while `inTrdemu = 1` (inside a stub), restore, continue → same bytes delivered to the TR-DOS buffer and same final PC/T-state as the uninterrupted run |
| **ERS-RD-1** | real ROM: ERS Services → "Format ramdisk 640k" on drive B (driven by key injection), exit to TR-DOS, `CAT "B"` lists an empty disk labelled `RAMDISKO` with 2544 free sectors |
| **ERS-RD-2** | real ROM: RAM disk B loaded from a TRD (test places the image on the SD/HDD fixture, ERS "TRD to: Ramdisk B"), TR-DOS `RUN "boot"` from B reaches a known screen hash |
| **ERS-MNT-1** | real ROM: "Mount B:" of a TRD file on the SD fixture (ST-1), TR-DOS reads and **writes** a file on B; the image file on the fixture changes accordingly (write path through `RDWR_MOUNT`) |
| **ERS-MNT-2** | real ROM: `IMAGE.MNT` automount with CMOS `#EC` bit 5 set mounts at reset without menu interaction |
| **ERS-FPGA-1** | real ROM: the ERS header does **not** show "Incorrect FPGA zxevo_fw.bin" with `Fpga=trdemu`, and does with `Fpga=legacy` + `zxevo_fe.rom` (proves the `#13BD` read-back check) |

## 6. Risks

- **ROM-version coupling.** The stub table is keyed to TR-DOS 5.03-family addresses in page 29.
  That is the ROM's business, not ours, but it means tests must pin the exact ROM image (md5 in the
  test) and a different DOS ROM in page 29 fails in the ERS, just as on hardware.
- **Instruction-start hook availability.** If TSConf phase 0 has not landed, this phase builds the
  hook (small, specified in TSConf technical-design §3.6); both machines then share it.
