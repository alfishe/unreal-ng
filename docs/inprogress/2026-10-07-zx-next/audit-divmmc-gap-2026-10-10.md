# Audit of the DivMMC emulation against the RTL (2026-10-10)

Trigger: [findings-nex-launch-2026-10-10.md](findings-nex-launch-2026-10-10.md) (Next3D Elite Wireframe, Space and Cube misbehave when started
through NextZXOS; at one sample MMU slot 0 held `NEXTREG #50 = #17` while its kind was `divmmc rom`).

Method: our DivMMC (`core/src/emulator/io/z80n/nextdivmmc.{h,cpp}`, the slot view in `core/src/emulator/memory/next/nextmemory.cpp`, the
ports / reset / NMI in `portdecoder_next.cpp`, NR #09 / #0A in `nextboard.cpp`, the loader in `core/src/loaders/nex/loadernex.cpp`) read against
the official core 3.02.03 (`device/divmmc.vhd`, `zxnext.vhd`, `device/im2_control.vhd`, `serial/spi_master.vhd`), `nexload.asm`, the wiki mirror,
jnext, ZEsarUX and MAME; then the real programs of the collection through the NextZXOS Browser (the env-gated `NexBrowserRun_Test`, a card folder
of `testdata/machines/zxnext/card` plus the collection) and the direct loader, with an ESX call trace (`RST #08` + function byte + path), an SD
read trace and a per-M1 trace of the DivMMC ROM. One emulator is never the only reference: where the RTL and an emulator disagree the RTL wins.

RTL paths: `R/` = `/Volumes/TB4-4Tb/Data/zx-spectrum/zx-next/fpga/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`; `zx` = `R/zxnext.vhd`, `dm` = `R/device/divmmc.vhd`.

## The questions, answered

**(a) Which register disables the automap; does NEXLOAD turn it off?**
`NR #0A` bit 4 (`nr_0a_divmmc_automap_en`, `zx` 5196, reset value 0, kept over a soft reset). `divmmc_automap_reset = port_divmmc_io_en = 0 or nr_0a_divmmc_automap_en = 0`
(`zx` 4112) and `automap_reset` clears `automap_hold`, `automap_held` and `button_nmi` and forces the combinational `automap` to 0 (`dm` 108-148). `NR #06` bit 4 is
`nr_06_button_drive_nmi_en` (`zx` 5165, 2091): the DRIVE button's NMI enable. The comment "DivMMC automatic paging off" on `res 4,a` of `PERIPHERAL_2_REGISTER` (NR #06)
in `nexload.asm` 323-349 is a leftover of the cores before 3.01.10, when NR #06 bit 4 controlled the automap (ZEsarUX `tbblue.c` 3706-3716 says so: "Los cores antiguos
controlaban el automapeo con el bit 4 del registro 06. Desde el core 3.01.10 se movio al bit 4 del registro 0A"). **On the current core NEXLOAD does not touch NR #0A**:
what it clears is the DRIVE button NMI (and it sets the M1 button NMI enable). The automap stays enabled for the program NEXLOAD starts; the registers we read at the end
of a Browser run: `NR #0A = #11`, `NR #B8 = #82`, `#B9 = #00`, `#BA = #00`, `#BB = #F2` (NextZXOS's own entry points: `RST 8` and `RST 38` and `#3Dxx`, `#056A`, `#04D7` only
with ROM 3 paged), `NR #06 = #AC`, `NR #08 = #4A`.

**(b) What leaves the automap latch.** In the RTL only these: (1) reset (`dm` 108/126/139 `i_reset`), (2) `automap_reset` (NR #0A bit 4 = 0, or the DivMMC port disabled by NR #83 bit 0),
(3) `retn_seen` (exactly `ED 45`, `R/device/im2_control.vhd` 137, 236; not while the Multiface is active, `zx` 4111), (4) an M1 fetch at `#1FF8-#1FFF` while NR #BB bit 6 = 1
(`divmmc_automap_delayed_off`, `zx` 2896; the latch drops after that fetch, `dm` 131). There is no timeout and no release when a program starts. Delayed entries set `automap_hold` at the
M1 and `automap_held` copies it when MREQ rises (`dm` 126-143): the DivMMC answers from the next memory cycle on (operands of the same instruction included). Instant entries are
also in the combinational `automap` for the fetch itself (`dm` 148). The ROM-3-only entries (`NR #B9` bit = 0, `#3Dxx`, `#056A/#04D7/#0562/#04C6`) need `sram_divmmc_automap_rom3_en`
(`zx` 3138): the fetch is in slot 0/1 **and the MMU maps ROM there** (`sram_pre_override(0)`, `zx` 3036-3057: MMU value `#FF`; RAM in the slot gives `110`, config mode `110`, Multiface `000`),
no Layer 2 read mapping, no ROMCS, and the 48K BASIC ROM (or the alternate 48K ROM) selected. The NMI entry `#0066` needs `button_nmi` and NR #BB bit 1 (instant) / bit 0 (delayed).

**(c) The esxDOS `RST #08` flow.** Verified on the trace of a NEXLOAD run (the load of `elite.nex` through the Browser: 30 delayed entries: `#0038` x20, `#0008` x9, `#0000` x1, 357 instant `#3Dxx`
entries, 362 map-out fetches at `#1FF8-#1FFF`): the `RST #08` fetch with ROM 3 paged and `B9` bit 0 = 0 (ROM-3 entry, delayed): the opcode at `#0008` comes from ROM, the latch is set, the DivMMC
ROM answers from `#0009` (the hook byte is read from the return address through the mapped window), the code switches the OS pages with `NEXTREG`, and leaves through a fetch in `#1FF8-#1FFF`
(`automap` back to 0). The ESX call trace of Cube shows the calls NEXLOAD makes (`F_OPEN` = `#9A` with `"CUBE.NEX"`, `F_READ` = `#9D`, `F_CLOSE`, `M_GETERR` = `#93` on the error path). Our slot view
matches the RTL here (`NextSkeleton_Test.DivMmcMapsRomAndBankAndTheAutomapFollowsTheRegisters`).

**(d) Do the loaders end in the hardware's DivMMC state?** NEXLOAD from NextZXOS: yes by construction (it is real NextZXOS code: the last call leaves through `#1FF8`; `NR #0A = #11`, E3 = 0, not mapped:
read at the end of Cube / Space / Elite through the Browser). The direct `LoaderNex`: on a bare machine NR #0A is 0 (no OS, no automap - fine); into a running NextZXOS (`UNREAL_NEX_BOOT`, the app's "open a
.nex") it keeps whatever DivMMC state it found (latch, CONMEM) and leaves NR #06 / #08 as NextZXOS had them - gaps D9 and D10.

**(e) Anything else.** The NextZXOS failures of Cube, Space and Elite are **not** a DivMMC rule: the SD card's SPI byte timing was not rescaled when NextZXOS switches the CPU to 14 MHz in the middle of
a sector, so three bytes of the directory sector were read twice and the file was not found (gap D11). The DMA never waited for the SPI byte (gap D12). See "What was seen".

## Gap list

Severity: **real** = breaks software that runs on the board; **minor** = a rarely reached or cosmetic difference.

| # | RTL | Ours | Others | Severity | Proposed test | Status |
|:--|:--|:--|:--|:--|:--|:--|
| D1 | A write of NR #0A with bit 4 = 0 (or the DivMMC port disabled) is `automap_reset`: `automap_hold`, `automap_held`, `button_nmi` cleared, `automap` = 0 at once (`zx` 4112, `dm` 108-148) | `Classify` returns `None` while the bit is off (`nextdivmmc.cpp` 51-53) but a held latch stays and keeps the DivMMC mapped over slots 0 / 1; a pending DRIVE `button_nmi` stays | jnext `apply_enabled_transition_` clears all three; MAME `automap()` | real (The Scholar writes `NR #0A = 1`; a program started with the latch held keeps the OS ROM in slot 0 whatever it writes) | `NextDivMmc_Test.ClearingNr0aBit4DropsTheHeldAutomapAndTheButtonLatch` | fixed |
| D2 | the ROM-3 entries need the MMU to map ROM at the fetch address, not config mode (`zx` 3036-3057, 3138: `override(0)`) | `rom3 = BasicRomVisible() && !Layer2ReadsAt()`: only the ROM selection is looked at (`nextdivmmc.cpp` 53, `nextmemory.cpp` 243) - RAM in slot 0 / 1 (NR #50 / #51) with ROM 3 selected still traps `#0008`, `#0038`, `#3Dxx`, `#056A`, `#04D7` | jnext `rom3_path_eligible` has `sram_pre_override_0`; MAME `m_automap_rom3_active` | **real, seen on a program**: BubNext (`games/platform/BubNext/BUBNEXT.nex`) through the Browser: its own IM 1 vector at `#0038` in RAM (MMU slot 0 = `#5E`) trapped, and with the old rule the run ended with `mapped 1 automapped 1 slot0 divmmc rom` (the symptom of the findings); with the new rule it ends unmapped. 128 of the 183 NEX of the collection contain `NEXTREG #50 / #51` (byte pattern, data matches possible); NextZXOS runs with `B9 = 0`, so an interrupt vector `#0038` or a `RST` inside such a program's own RAM, or code at `#3Dxx`, maps the OS DivMMC ROM over it | `NextDivMmc_Test.RomThreeEntriesNeedTheMmuToMapRomAtTheFetch` (RAM in slot 0: `#0038`, `#0008`; RAM in slot 1: `#3D00`; ROM: all trap) | fixed |
| D3 | `rom_en = page0 and (conmem or automap) and not mapram`, `ram_en = page0 and (conmem or automap) and mapram or ...` (`dm` 94-95; 3.02.00: bank 3 fills the lower 8K even with CONMEM): CONMEM does not beat MAPRAM | `bank3AtZero = Mapram() && !CONMEM` ("CONMEM beats MAPRAM: the ROM", `nextdivmmc.cpp` 29): with E3 = `#C0..` the ROM shows, and slot 1 bank 3 stays writable (`nextmemory.cpp` 365-374) | jnext `is_rom_mapped = active && !mapram`; MAME `specnext_divmmc.cpp` 38-43; [esxdos-and-sd.md](esxdos-and-sd.md) 3.1 table has it right; the classic board does the opposite (CONMEM wins, [research-divmmc-original-rtl.md](research-divmmc-original-rtl.md) section 3) | real (esxdos writes bank 3 through CONMEM before it sets MAPRAM; with both set the board shows RAM, read-only, and its bank 3 slot-1 view is read-only) | `NextDivMmc_Test.MapramBankThreeFillsTheLowerEightKilobytesEvenWithConmem` | fixed |
| D4 | the `#0066` entry maps only for `button_nmi and NR #BB bit 1` (instant) or `bit 0` (delayed) (`zx` 2907-2908, `dm` 120-121) | `#0066` maps after the fetch whatever NR #BB says (`nextdivmmc.cpp` 59-60) | jnext checks both bits; MAME `automap_nmi_*_on` | real (Atic Atac Remake and parallax.nex write NR #BB = `#02` / `#40`: the DRIVE button must not enter the OS menu over the game; ours does; the instant bit has to map for the fetch itself) | `NextDivMmc_Test.TheNmiEntryFollowsNrBbBitsOneAndZero` | fixed |
| D5 | NR #02 resets (soft and hard) are `reset`: E3 = 0, latches cleared (`zx` 4175-4177, `dm` 108-139) | `PerformReset` (NR #02) leaves the DivMMC and Multiface as they are; only the power-on `reset()` calls `_divMmc->Reset()` (`portdecoder_next.cpp` 108, 541) | jnext `reset()` | real (a soft reset with the DivMMC held starts the personality ROM at `#0000` through the DivMMC ROM; E3 CONMEM / MAPRAM survive) | `NextDivMmc_Test.AnNr02ResetClearsThePortAndTheLatches` | fixed (the DivMMC and the Multiface, D14) |
| D6 | `port_e3 = lsb and NR #83 bit 0`, `port_e7 / port_eb = lsb and NR #83 bit 3` (`zx` 2412, 2419, 2608, 2620); the memory enables are `and i_en` (`dm` 98-99) and `automap_reset` follows the bit | #E3 / #E7 / #EB always answer (`portdecoder_next.cpp` 274, 340, `portdecoder_next_reports.cpp` 340 "enforced: false"); the port enable does not unmap or reset | jnext `set_port_io_enable`; MAME | minor (software that turns internal ports off before it runs on bare hardware) | `NextDivMmc_Test.ADisabledDivMmcPortIsNotDecodedAndUnmapsTheMemory` | fixed (E3, E7 / EB; `next_ports` reports them enforced) |
| D7 | NR #0A bits 7:6 (Multiface type) and bit 5 (SD swap) are written only in config mode (`zx` 5192-5195) | the whole byte is stored (`nextboard.cpp` default branch) | jnext | minor-real (The Scholar / Trainyard / Ping Pong write NR #0A: a byte with bit 5 = 0 would swap the card lines of a swapped board) | `NextDivMmc_Test.Nr0aBitsSevenToFiveAreConfigModeOnly` | fixed |
| D8 | `retn_seen` only for `ED 45` (`im2_control.vhd` 137) | `ED 55 / 5D / 65 / 6D / 75 / 7D` share the body and fire the callback (`unreal-next-z80/opcodes-ed.inc` 395-696) | jnext (all aliases; see its "G87" note) | minor (no program is known that uses an alias with the automap held) | `Z80nOpcodes_Test.OnlyEd45ReportsRetnSeen` | open: the library reports every alias through one callback; needs the opcode passed along |
| D9 | NEXLOAD ends in an unmapped DivMMC with E3 = 0 (the last `RST #08` leaves through `#1FF8`) | `LoaderNex::Load` into a running system leaves the latch and CONMEM as found (`loadernex.cpp` 160-176) | n/a | minor-real (the app's "open a .nex" while NextZXOS is inside a DivMMC call: the program would start with the OS ROM in slot 0) | `LoaderNex_Test.LoadingDropsAHeldAutomapAndConmem` | fixed |
| D10 | `nexload.asm` 323-349 also writes NR #06 (`res 4`, `set 3`) and NR #08 (`set 7, 6, 3, 2, 1`, `res 5, 0`) | `LoaderNex::ResetRegisters` does not (`loadernex.cpp` 50-88): the DRIVE button NMI stays enabled over a loaded program, NR #08 bit 6 (contention off) is not set | n/a | minor (F10 during a NEX would open the OS NMI menu on top of the program; a program that drops to 3.5 MHz keeps contention) | `LoaderNex_Test.ResetRegistersWriteNr06AndNr08LikeNexload` | fixed |
| D11 | the SPI byte is 16 CPU clocks and `spi_master.vhd` counts the CPU clock (`i_CLK => i_CLK_CPU`, `zx` 3284): a speed change inside the byte changes the time that is left | the end of the byte in flight was computed once in base T-states from the speed of its start (`portdecoder_next.cpp` 589-640); the new speed is taken over at a frame start, a byte that began before it was too long in the new clocks, the following `IN (#EB)` of `INIR` came too early and returned the same byte again | the SPI docs of the DMA audit | **real, data corruption**: NextZXOS switches to 14 MHz around the sector of a directory (`mult 4` at `F_OPEN`); one read in a thousand duplicated bytes, the directory entry `..` was shifted by three bytes and `F_OPEN` answered "No such file or dir" - Cube, and a NEX file read this way arrives with duplicated bytes | `NextBoot_Test.ASpiByteInFlightWhenTheSpeedChangesIsRescaled` | fixed |
| D12 | `dma_wait_n = z80_wait_n and spi_wait_n` (`zx` 1844): the DMA waits for the byte | `waitSpi` compared `Now28()` (28 MHz clocks) with the byte's end in base T-states (`portdecoder_next.cpp` 390-395): never waited, DMA reads of #EB returned duplicates | the DMA audit table claims "the DMA waits" | real (DMA streaming from the card) | `NextBoot_Test.DmaReadsOfThePortEbWaitForTheSpiByte` | fixed |
| D13 | a read that comes before the byte is done returns the old `miso_dat` and starts nothing (`spi_master.vhd` 108-132) | returns the byte of the transfer that is in flight (the answer is computed at the start) | n/a | minor | none | open |
| D14 | the Multiface is reset by `reset` as well (`R/device/multiface.vhd` 120-190) | not reset by NR #02 either (D5) | n/a | minor | `NextDivMmc_Test.AnNr02ResetClearsThePortTheLatchesAndTheMultifaceSession` | fixed with D5 |

## What was seen with the real programs

All through `NexBrowserRun_Test` (boot chain, SPACE, `B`, `H` + the name, ENTER, ENTER; 28 MHz off after the navigation), card = `testdata/machines/zxnext/card` plus the collection, 300 frames,
then the register and picture dump; and through the direct loader (`LoaderNexRun_Test`).

| Title | Before the fixes | After the fixes (the card with the whole collection, 300 frames) |
|:--|:--|:--|
| Next3D Demo Cube (`demos/tech-demo/Next3D Demo Cube/cube.nex`) | NEXLOAD: `No such file or dir, 0:2`, with the whole collection on the card (FAT16, 32 K clusters, 40067 clusters); fine on a smaller card, fine again on the same card after a file was added to the folder (the layout moved, the speed switch fell elsewhere) and failing again after it was removed: the failure follows where the speed switch falls inside a sector, not the file. Trace: `F_OPEN "CUBE.NEX"` reads the directory sector, the buffer at `#2540` holds `.`, 32 bytes, **three extra `00`**, then `..`; `SPI TOOFAST` three times at 14 MHz right after the frame start that applied the speed | NR #12 written 144 times (`9 12 9 12 ...`), PC in the program's loop (`#E395`), a rotating cube (yellow, violet and blue faces), `e3 0`, DivMMC not mapped, NR #0A `#11` |
| Next3D Demo Space | Browser: NR #12 stuck / no flip (the findings); direct: flips | Browser: NR #12 flips `9 12 9 12 ...` (73 writes), a filled ship in front of the yellow sun, Layer 2 banks 9 / 12 / 13 filled in turn |
| Next3D Elite Wireframe | "black through both loaders", slot 0 `divmmc rom` | not reproduced as black here: direct and Browser give the same picture (blue background, green filled triangles and blue edges), Layer 2 banks 9-11 are filled (16381 / 16384 / 16384 non-zero bytes), the DivMMC is not mapped at the end (`e3 0`, `automapped 0`, slot 0 `ram`). The program draws a static scene every frame (364 `NEXTREG #50` per frame, three frames in a row leave the 64K the CPU sees byte-identical, the keys Q A O P W S change nothing) and waits for the interrupt flag at `#FE14` (IM 2, vector table at `#FD00`, handler `JP #FE0B`); what the board shows for it is not known to us |
| Next3D Demo Circle | works | works (NR #12 286 writes, the white circle on black) |

The `slot 0 = divmmc rom` sample of the findings could not be reproduced as a stuck state: the end state of every Browser run is `e3 0`, not mapped. Two causes
fit it and are fixed: the SPI corruption (D11) feeds a program or a loader with duplicated bytes, and with RAM in slot 0 the ROM-3 entries trapped (D2: NextZXOS runs with `NR #B9 = 0`, so an
interrupt at `#0038` or a `RST 8` in a program's own RAM page mapped the OS ROM over it and nothing but `#1FF8`, a RETN or NR #0A released it).

## Sources read

`device/divmmc.vhd` (whole), `zxnext.vhd` 1844, 2091-2118, 2412-2420, 2606-2622, 2846-2908, 3020-3145, 3280-3300, 4103-4190, 5160-5200, 5585-5595, 6349; `device/im2_control.vhd`;
`serial/spi_master.vhd`; `firmware/tbblue-firmware-sources/asm/nexload/nexload.asm` 270-760; wiki mirror pages NextReg `$0A`, `$B8`, `$BB`, Divmmc Entry Points 0 / 1; jnext
`src/peripheral/divmmc.cpp`; ZEsarUX `src/machines/tbblue.c`; MAME `specnext_divmmc.cpp`; [research-fpga-vhdl.md](research-fpga-vhdl.md) 424-494, [esxdos-and-sd.md](esxdos-and-sd.md) 3-4.
