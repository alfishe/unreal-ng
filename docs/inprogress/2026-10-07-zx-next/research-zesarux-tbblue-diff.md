# ZEsarUX `tbblue.c` against the FPGA and unreal-ng

Source: ZEsarUX commit `2d8dba1` (`src/machines/tbblue.c`, 9005 lines, and `tbblue.h`), read 2026-11-20. Reference: the FPGA
sources `ZX_Spectrum_Next_FPGA/cores/zxnext/src` (`zxnext.vhd` 7595 lines, `device/*.vhd`, `video/*.vhd`). unreal-ng: master at
`c6a70f8b3` (`core/src/emulator/io/z80n/`, `core/src/emulator/memory/next/`, `core/src/emulator/ports/models/portdecoder_next*`,
`core/src/emulator/video/next/`).

What was read of `tbblue.c` is listed per area below (line ranges); the summary of coverage is at the end. Only differences are
listed. When ZEsarUX and unreal-ng differ, the VHDL decides. Paths: `zxnext.vhd` is
`cores/zxnext/src/zxnext.vhd`; unreal-ng paths are relative to `core/src/emulator/`.

Verdicts: **ok** = unreal-ng follows the VHDL (ZEsarUX differs); **BUG** = unreal-ng contradicts the VHDL (numbered in the list at
the end); **known** = an open item of [TODO.md](TODO.md) already; **unverified** = no VHDL evidence found either way.

## 1. NextREG read-back (`tbblue_get_value_port_register`, tbblue.c 5531-5735)

ZEsarUX reads back the stored byte for every register except NR `#00`, `#01`, `#07`, `#0E`, `#0F`, `#18`-`#1B`, `#1E`, `#1F`,
`#41`, `#44`. The FPGA has an explicit read multiplexer (`zxnext.vhd` 5881-6285) with `when others => (others => '0')`
(6282-6283): every register without a case reads `#00`, and many cases are narrower than eight bits.

| Area | FPGA (file:line) | ZEsarUX (tbblue.c:line) | unreal-ng (file:line) | Verdict |
|:--|:--|:--|:--|:--|
| Registers without a read case (`#04`, `#0C`, `#0D`, `#29`-`#2B`, `#35`-`#39`, `#60`, `#63`, `#75`-`#79`, unused numbers) | `others => 0` (`zxnext.vhd` 6282-6283) | stored byte (5726) | `#04`, `#35`-`#39`, `#60`, `#63`, `#75`-`#79` read 0 (io/z80n/nextboard.cpp 150-151, 183-188); every other number falls to `return _regs[reg]` (nextboard.cpp 266), and `Write`'s `default` stores any byte (nextboard.cpp 637-639): NR `#29`, `#2A`, `#2B`, `#0C`, `#FF`... read back what was written | **BUG 1**, fixed 2026-11-20 |
| NR `#0B` | `joy_iomode_en & '0' & iomode(1:0) & "000" & iomode_0` = mask `#B1` (5914-5915) | stored byte | raw `_regs` (nextboard.cpp 266) | **BUG 1**, fixed 2026-11-20 |
| NR `#12` / `#13` | `'0' & bank(6:0)` (5929-5933; written `nr_wr_dat(6 downto 0)` 5220-5223) | stored byte | raw `_regs` (write `_regs[reg] = value`, nextboard.cpp 452-456) | **BUG 1**, fixed 2026-11-20 |
| NR `#2F` | `"000000" & scrollx(9:8)` (6017-6018) | stored byte | raw | **BUG 1**, fixed 2026-11-20 |
| NR `#4C` | `"0000" & index` (6056-6057) | stored byte | raw | **BUG 1**, fixed 2026-11-20 |
| NR `#68` bit 1 | reads `'0'`; bit 3 is `port_ff3b_ulap_en` (6092-6093, 4543-4553) | stored byte | raw: bit 1 reads as written (bit 3 is kept equal to the ULA+ flop, portdecoder_next.cpp 1110) | **BUG 1**, fixed 2026-11-20 |
| NR `#6A`, `#70`, `#71` | `"00" & ...` (6098-6099), `"00" & res & offset` (6113-6114), `"0000000" & msb` (6116-6117) | stored byte | raw | **BUG 1**, fixed 2026-11-20 |
| NR `#8F` | `"000000" & mapping_mode` (6161-6162) | stored byte | raw | **BUG 1**, fixed 2026-11-20 |
| NR `#D8` | `"0000000" & io_trap_fdc_en` (6265-6266) | stored byte | raw | **BUG 1**, fixed 2026-11-20 |
| NR `#07` | `"00" & cpu_speed & "00" & nr_07` (5902-5903): bits 5:4 the speed in force | bits 5:4 = programmed speed (5604) | `ActiveCpuSpeed()` (nextboard.cpp 157-160) | ok |
| NR `#1C` | the four clip indices (5979-5980) | falls through to the stored byte (5638: `break` without return) | `ReadClipControl()` (nextboard.cpp 173-174) | ok |
| NR `#1E` / `#1F` | `cvc`, the line counter after NR `#64` (5982-5986) | `t_scanline` minus the paper start (5510-5528) | `CurrentLine()` of the interrupt source (nextboard.cpp 201-204) | ok |
| NR `#22` bit 7 | `not pulse_int_n`: the ULA INT pulse is being driven (5991-5992) | stored byte | `_lineControl` only, bits 2:0 (io/z80n/nextinterrupts.cpp 99-101); bit 7 always 0 | **BUG 2**, fixed 2026-11-20 |
| NR `#C0` bits 2:1 | `z80_im_mode`, the CPU's current `IM` (6229-6230; `o_im_mode` 1905) | stored byte | `_control` written `value & 0xE9` (nextinterrupts.cpp 47, 105-106): bits 2:1 always 0 | **BUG 3**, fixed 2026-11-20 |
| NR `#41` | `nr_palette_dat(8 downto 1)` (6038-6039) | `(colour >> 1) & 0xFF` (5686) | `ReadPaletteValue8` | ok (same) |
| NR `#44` | `dat(10 downto 9) & "00000" & dat(0)` (6047-6048): bits 7:6 are stored for every palette | bit 7 only for the Layer 2 priority (5715-5717) | keeps bits 7:6 (test `NextReadBack_Test.TheSecondPaletteByteKeepsItsPriorityBitsInEveryPalette`) | ok |
| NR `#28` | `nr_stored_palette_value` (6003-6004) | stored byte | `StoredPaletteValue()` (nextboard.cpp 214-215) | ok |
| NR `#8E` | computed from the ports (6158-6159) | stored byte | `ReadMemoryMapping()` (portdecoder_next.cpp 2041-2048) | ok |
| NR `#2C`-`#2E` | the Pi I2S samples (6006-6014) | stored byte (written as DAC mirrors) | `PiAudioLeft/Right` (nextboard.cpp 238-245) | ok |
| NR `#69` | `port_123b_layer2_en & port_7ffd_shadow & port_ff_reg(5:0)` (6095-6096) | stored byte; writes propagate (`tbblue_sync_display1_reg_to_others`, 4607) but port writes do not update NR `#69` | composed (nextboard.cpp 189-190) | ok |
| NR `#02` bits 3:2 after a refused NMI | set when the request is accepted by the NMI state machine (`nmi_accept_cause`, 3843-3862), independent of NR `#06` bits 3 / 4 (the enables gate only `nmi_assert_*`, 2090-2091) | cleared when refused (5007, 5023) | set only when `GenerateDriveNmi` / `GenerateMultifaceNmi` succeed, which needs NR `#06` bit 4 / 3 (nextboard.cpp 525-533, ports/models/portdecoder_next.cpp 2188-2213) | **BUG 4**, fixed 2026-11-20 |

## 2. Interrupts: line, ULA, IM2, NR `#20`, NMI (tbblue.c 8850-8937; `video/screen.c` 14976-15110; `cores/core_spectrum.c` 388-470, 760-830)

ZEsarUX models the line interrupt (shared with the ZX-Uno code), NR `#22` bit 2 (ULA interrupt off), the stackless NMI and the
NR `#02` NMIs. It has no hardware IM2 mode, no CTC, no UART interrupts and no NR `#20` (a grep of `tbblue_registers[0xC`
finds only NR `#C0` bit 3, tbblue.c 8890, 8912).

| Area | FPGA (file:line) | ZEsarUX (tbblue.c:line) | unreal-ng (file:line) | Verdict |
|:--|:--|:--|:--|:--|
| Line interrupt position | `hc_ula = 255` of the line before the target, `cvc` = target - 1, target 0 -> `c_max_vc` (`video/zxula_timing.vhd` 563-580) | T-state 128 to 159 of the line, counted from the paper start, only with `iff1` set (`video/screen.c` 15042-15100) | `LineStartT`: 128 T into the line before (io/z80n/nextinterrupts.cpp 267-280) | ok |
| Line interrupt with interrupts disabled | `line_int_pulse` is raised whatever IFF1 (`zxula_timing.vhd` 577); the CPU takes it later only if `/INT` is still low | not raised while `iff1` = 0 (`screen.c` 15042) | raised by position | ok |
| NR `#22` bit 7 (INT flag) | `not pulse_int_n` (`zxnext.vhd` 5991-5992) | set for 32 T after a line interrupt (`screen.c` 15090-15110), stored in NR `#22` | always 0 | **BUG 2**, fixed 2026-11-20 |
| NR `#20` write (generate interrupts) | `im2_int_unq`: bit 7 line, bit 6 ULA, bits 3:0 CTC 0-3 become unqualified requests, the enables not needed (`zxnext.vhd` 1946-1947, `device/im2_peripheral.vhd` 56, 160, 172, 186-192) | not modelled | not modelled: no code reads NR `#20` (only `nextregtable.inc` 31); the write is stored and does nothing | **BUG 5**, fixed 2026-11-20 |
| NR `#20` read | `im2_int_status(0) & (11) & "00" & (6 downto 3)` (`zxnext.vhd` 5988-5989) | stored byte | stored byte (nextboard.cpp 266) | **BUG 5**, fixed 2026-11-20 |
| NR `#C8`-`#CA` status with the source disabled or in pulse mode | `int_status <= (int_req or i_int_unq) or (int_status and not clear)` in both modes, no enable term (`im2_peripheral.vhd` 160); `int_req` of the CTC (`ctc_zc_to`) and the UARTs is not gated by NR `#C5` / `#C6` (`zxnext.vhd` 1941-1944) | not modelled | `Raise` returns for a disabled source and sets `_pending` only in the hardware mode (io/z80n/nextinterrupts.cpp 188-202); `Latch` runs only in the hardware mode (nextinterrupts.cpp 208-212, 324, 370); the NR `#C0` write clears `_pending` when leaving it (nextinterrupts.cpp 51-52) | **BUG 6**, fixed 2026-11-20 |
| NR `#C0` bits 2:1 | the CPU's `IM` (`zxnext.vhd` 6230) | stored byte | 0 | **BUG 3**, fixed 2026-11-20 |
| NR `#02` bit 2 (DivMMC NMI) | needs NR `#06` bit 4 (`nmi_assert_divmmc`, `zxnext.vhd` 2091) | no NR `#06` check (tbblue.c 5012-5025); refused only when DivMMC is paged | `DriveNmiRefusal` checks NR `#06` bit 4 (ports/models/portdecoder_next.cpp 2188-2200) | ok |
| NR `#02` bit 3 (Multiface NMI) | needs NR `#06` bit 3 (`zxnext.vhd` 2090) | checks NR `#06` bit 3 and the machine type (tbblue.c 4993-5009) | checks NR `#06` bit 3 (portdecoder_next.cpp 2202-2213) | ok |
| NR `#02` NMIs in config mode | no machine-type term in `nmi_assert_*` (`zxnext.vhd` 2090-2091) | refused when NR `#03` type is 0 or 7 (tbblue.c 4994, 5012) | no type check | ok |
| Stackless NMI / `RETN` | NR `#C0` bit 3 (`zxnext.vhd` 6230) | `tbblue_handle_nmi` / `tbblue_retn` (tbblue.c 8850-8937) | `onStacklessNmi` (nextinterrupts.cpp 47-50) | ok (same) |

## 3. Reset values (tbblue.c 3897-4176)

`tbblue_reset_common` (3905-3999) serves the soft and the hard reset; `tbblue_hard_reset` (4057-4174) clears the whole register
array first. The FPGA's list is the `if reset = '1'` block of `zxnext.vhd` 4925-5111 (soft and hard alike) plus the declared
initial values for the power-on load.

| Area | FPGA (file:line) | ZEsarUX (tbblue.c:line) | unreal-ng (file:line) | Verdict |
|:--|:--|:--|:--|:--|
| NR `#42` (ULANext format) | `X"07"` (`zxnext.vhd` 5002) | 15 (3926) | `_ulaNext = 0x07` (io/z80n/nextvideoregs.cpp 30) | ok |
| NR `#4A` (fallback colour) | `X"E3"` (5014; the `X"00"` line is commented out, 5013) | 0 (3928) | table `0xE3` (io/z80n/nextregtable.inc 60) | ok |
| NR `#68` on a soft reset | every field reset: ULA on, blend 00, cancel-extended-keys 0, fine scroll 0, stencil 0 (5026-5030); ULA+ enable 0 (4547) | `&= 0x13` keeps bits 4, 1, 0 (3943) | `_regs[0x68] = 0` (io/z80n/nextboard.cpp 50-51) | ok |
| Clip windows | L2 / sprites / ULA `0,FF,0,BF`, tilemap `0,9F,0,FF`, indices 0 (4959-4981) | same (3968-3986) | same (nextvideoregs.cpp 17-25) | ok (same) |
| NR `#0B`, `#14`, `#26`, `#27`, `#2F`-`#31`, `#6C`, `#6E`, `#6F`, `#C0`, `#C4`, `#C6`, `#CC`-`#CE` on a soft reset | reset (4932-5107) | not reset (`tbblue_reset_common` lists none of them) | reset (nextboard.cpp 50-73, the table loop 74-77, `NextInterruptSource::Reset`) | ok |
| NR `#05`, `#08`, `#0A` on a hard reset | `nr_05` joy0 `001`, scandoubler; NR `#08` speaker on; NR `#0A` mouse DPI 01 (declared values, research-fpga-vhdl.md section 4) | NR `#05` = 1 (joystick 1 = `000`), NR `#08` = 16, NR `#0A` = 1 (4094-4099) | NR `#05` = `#41`, NR `#08` = `#10`, NR `#0A` = `#01` (nextboard.cpp 35-38) | ok |
| Hard reset and SRAM | NR `#02` bit 1 reloads the FPGA (`zxnext_top_issue5.vhd` 1095); the SRAM is not cleared by the core | clears SRAM `#020000`-`#1FFFFF` (4086, a work-around comment) | RAM survives (TODO.md "Tests" item) | ok |
| NR `#02` soft reset | `reset` runs the block above | only `reg_pc = 0` (4981-4988); the register reset comes from the CPU reset path | `NextBoard::AfterInstruction` -> `PortDecoder_Next::PerformReset(false)` (nextboard.cpp 665-692, ports/models/portdecoder_next.cpp 1677) | ok |
| Default palettes | the palette RAMs have no init; the firmware writes them | Layer 2 / sprite colour = index with the low blue bit = bit 1 (1537-1550), ULA 16 colours repeated, entry 11 of every 16 replaced by `#1CF` (1595-1604) | measured firmware state (nextvideoregs.cpp 37-52) | ok |

## 4. Memory paging, ROM, alt ROM, boot ROM (tbblue.c 2920-3700, 4639-4741)

| Area | FPGA (file:line) | ZEsarUX (tbblue.c:line) | unreal-ng (file:line) | Verdict |
|:--|:--|:--|:--|:--|
| Boot ROM window | `cpu_a(15 downto 14) = "00" and bootrom_en = '1'` selects `bootrom_do` (`zxnext.vhd` 1856-1857), the ROM's address is `cpu_a(12 downto 0)` (3199-3204): the 8K image shows at `#0000` **and** `#2000`, ahead of the MMU, Multiface and DivMMC; writes still go to the SRAM the pre-decode chose (NR `#04`'s bank in config mode, 3044-3050, `sram_pre_rdonly` 0) | boot ROM 16K at `#0000-#3FFF` (3645-3646) | slot 0 only, and only while MMU0 is a ROM value (memory/next/nextmemory.cpp 438, 465-471); slot 1 shows NR `#04`'s bank; a write to slot 0 goes to the trash page (nextmemory.cpp 468-469). research-fpga-vhdl.md section 7 also says `#0000-#1FFF` | **BUG 7, fixed**: `MapSlot` puts the boot ROM's 8K on the read side of slots 0 and 1 over whatever the decode chose, the write side stays (tests `NextBoot_Test.BootRomIsEightKMirroredAtTwoThousandHex`, `WritesUnderTheBootRomLandInTheNr04Bank`) |
| Config mode, NR `#04` | `nr_04_romram_bank & cpu_a(13)` (3044-3050) | `registers[4] & 127` (3631) | 7 bits, 8 on Issue 5 (nextboard.cpp 614-618) | ok |
| MMU page past the installed RAM | `mmu_A21_A13` (2964); a page beyond the fitted chips has no chip select (`zxnext_top_issue2.vhd` 1300-1308) | clamped to the last page (`tbblue_get_limit_sram_page`, 2985-2992) | unmapped (nextmemory.cpp 515-526) | ok (the floating value is unverified) |
| MMU `#E0`-`#FF` above `#3FFF` | `mmu_A21_A13(8) = 1`: no SRAM access (`sram_pre_active`, 3060-3062) | only 255 is special, and only in slots 0 / 1 (3172-3213) | `MmuIsRom` (test `NextVhdlParity_Test.N1MmuPagesFromE0ShowTheRomOnlyInSlotsZeroAndOne`) | ok |
| +3 all-RAM modes | the four maps from `port_1ffd_reg(2:1)` (`zxnext.vhd` 4623-4632) | same maps (3215-3342) | same table (nextmemory.cpp 625) | ok (same) |
| Leaving all-RAM | `port_1ffd_special_old`: MMU2-5 back to `#0A`, `#0B`, `#04`, `#05`, MMU6/7 the bank (4652-4681) | `tbblue_set_mmu_128k_default` resets all eight (3380-3405, 3504-3507) | MMU2-5 defaults, MMU6/7 the bank, MMU0/1 ROM (nextmemory.cpp 633-646) | ok |
| A port write over NR `#56` / `#57` | `port_memory_ram_change_dly` is 1 for every paging port write (3814): MMU6/7 take the `#7FFD` bank again | `tbblue_set_memory_pages` keeps the stored NR values | `changeBank` true for the ports (nextmemory.h 103) | ok |
| `#7FFD` bank bits 6:3 | `port_dffd_reg(3:0)` (3764-3766) | not modelled (`disabled_tbblue_mem_get_ram_page`, 3344, is dead code) | `_dffd` (nextmemory.cpp 613, 643) | ok |
| NR `#8F` mapping modes, `#EFF7` | Pentagon 512 / 1024 bank bits and lock (3764-3769, 3796-3801), `#EFF7` bit 3 maps RAM 0 at `#0000` (4636-4640), the write is a paging change (3812) | not modelled | stored only | known (TODO.md "Not planned yet") |
| Alt ROM select | `sram_rom` from the locks or `port_1ffd_rom` per machine type (2980-3006) | lock bits checked before the port, the machine type ignored (3023-3152) | `EffectiveRom` / `AltPage` per machine type (nextmemory.cpp 296-322) | ok |
| Alt ROM write mode (NR `#8C` bit 6) | `sram_pre_rdonly <= not (altrom_en and altrom_rw)`: with bit 6 the ROM area is writable into the alt ROM, reads stay on the normal ROM (3056) | only `(#8C & #C0) = #80` (replace reads) handled; bit 6 ignored (3187) | writes to the alt ROM page (nextmemory.cpp 498-507) | ok |
| NR `#8C` on a soft reset | `(7 downto 4) <= (3 downto 0)` (2254-2255) | same (4039-4048) | same (nextboard.cpp 78-80) | ok (same) |
| NR `#8E` write | `port_7ffd_reg(2:0)` only with bit 3, `port_7ffd_reg(4)` only with bit 2 = 0, `#1FFD` bits from 2:0 (3662-3670, 3727-3734); `#DFFD` bits 2:0 = `"00" & bit 7` with bit 3 (3696-3700) | `tbblue_paging_128k_reg_142` (4639-4740) | `WriteMemoryMapping` (ports/models/portdecoder_next.cpp 2018-2039) | ok |

## 5. Copper (tbblue.c 379-882)

Section 22 of [research-fpga-vhdl.md](research-fpga-vhdl.md) has the mode names and the WAIT threshold. New in this pass:

| Area | FPGA (file:line) | ZEsarUX (tbblue.c:line) | unreal-ng (file:line) | Verdict |
|:--|:--|:--|:--|:--|
| Mode `11` restart point | `copper_en_i = "11" and vcount_i = 0 and hcount_i = 0` (`device/copper.vhd` 80), `vcount_i => cvc` (`zxnext.vhd` 3950); `cvc` is loaded with NR `#64` at the first paper line and wraps at `c_max_vc` (`video/zxula_timing.vhd` 457-469), so with an offset the restart is where `cvc` passes 0, `offset` lines before the paper | restart at the vsync interrupt (`tbblue_copper_handle_vsync`, 747-753), no offset | `_t` back to 0 (the paper-relative frame start) restarts `_pc` (io/z80n/nextcopper.cpp 92-104), where `cvc` = NR `#64` | **BUG 8**, fixed 2026-11-20 (`NextCopper::RestartClock`) |
| WAIT compare | `vcount_i = line and hcount_i >= H*8 + 12` (`copper.vhd` 94) | `current_raster == line and hc >= H` with `hc` = T * 2 / 8, no `+12`, no NR `#64` (693-728, 935-962) | `cvc == vpos && hc >= threshold` (nextcopper.cpp 138-143) | ok |
| MOVE to NR `#00` | no write pulse when `data(14 downto 8) = 0`, whatever the value byte (`copper.vhd` 104-105) | writes register 0 (read only) (558) | `reg != 0` (nextcopper.cpp 182) | ok |
| NR `#60` / `#63` storage | `#60` writes the MSB at an even address, the LSB at an odd one; `#63` keeps the even byte in `nr_copper_data_stored` and writes both at the odd one (`zxnext.vhd` 3977-3998, 5418-5437) | `#63` uses the previous NR `#63` value from the register array (426-443, 5395) | same as the FPGA (nextcopper.cpp 33-69) | ok |
| Copper time base | 28 MHz, MOVE 2 clocks, WAIT / NOP 1 (`copper.vhd`) | `delta * 8` of 3.5 MHz T (608-666) | per 28 MHz clock (nextcopper.cpp 107-190) | ok (same rates) |

## 6. Timing, contention, turbo (tbblue.c 3747-3782, 4200-4455)

| Area | FPGA (file:line) | ZEsarUX (tbblue.c:line) | unreal-ng (file:line) | Verdict |
|:--|:--|:--|:--|:--|
| NR `#03` timing remap | `000` -> 48K, `101`-`111` -> +3, only with bit 7, the lock open and bit 3 = 0 (`zxnext.vhd` 5124-5135) | bit 7 checked, the lock ignored, `000` and `101`-`111` fall to 48K (4390-4393, 4919-4928) | `kTimingRemap` (io/z80n/nextboard.cpp 545-556) | ok |
| Timing change under turbo | no speed term in the NR `#03` path | ignored while `cpu_turbo_speed != 1` (4444, 4304) | taken | ok |
| Memory contention | per 8K page of the MMU, banks 0-7 only, off for Pentagon timing and any speed above 3.5 MHz (`zxnext.vhd` 4481, 4489-4493) | per 16K window through `contend_pages_128k_p2a`, the window from NR `#56` (3429, 3481) | per slot from the MMU value (memory/next/nextmemory.cpp 364-386) | ok |
| NR `#07` speed in force | `cpu_speed` follows `nr_07_cpu_speed` unless the expansion bus is on (read 5902-5903) | immediate, plus the emulator's own caps `tbblue_deny_turbo_rom` (PC < `#4000` capped to 4x) and `tbblue_deny_turbo_everywhere` (3747-3782) | `ActiveCpuSpeed` / `ApplyCpuSpeed` (nextboard.cpp 643-656) | ok (the caps are ZEsarUX options) |

## 7. Sprites (tbblue.c 1275-1330, 1831-2805)

| Area | FPGA (file:line) | ZEsarUX (tbblue.c:line) | unreal-ng (file:line) | Verdict |
|:--|:--|:--|:--|:--|
| Port `#57` attribute stepping | after byte 3 with bit 6 = 0, or after byte 4, the index steps to the next sprite; byte 4 is not written (`video/sprites.vhd` 639, 650-662) | writes a 0 into byte 4 and steps (1928-1934) | skips byte 4, no write (io/z80n/nextsprites.cpp 43-61) | ok |
| Port `#303B` read | returns the flags and clears them on the read edge (`sprites.vhd` 983-991) | clears bits 0-1 on read (2858) | `ReadStatus` clears (nextsprites.cpp 28-33, 486-500) | ok |
| Too many sprites | `sprites_overtime`: the engine is still busy when the next line starts (`sprites.vhd` 977) | a count: `MAX_SPRITES_PER_LINE` = 100 (2223, 2796; tbblue.h 148) | time-based, the S_PROCESS clocks per sprite (nextsprites.cpp 190-353) | ok |
| Collision flag | set when a sprite pixel lands on one already written in the line (`spr_line_data_o(8) and spr_line_we`, `sprites.vhd` 991) | the same, in the renderer (2040) | the same (nextsprites.cpp 328-335) | ok |
| NR `#15` bit 6, sprite 0 on top | the write is dropped when the target pixel is taken (`sprites.vhd` 972) | the same (2046) | `!zeroOnTop \|\| !p.opaque` (nextsprites.cpp 333, 401) | ok |
| Clip window | applied at the output: with over-border and NR `#15` bit 5 = 0, the whole 0-319 x 0-255; with bit 5, `x1*2 .. x2*2+1`; without over-border, each coordinate + 32 and line < 224 (`sprites.vhd` 1042-1066) | applied during rendering; over-border without bit 5 uses the full range; the non-border y range is capped at 32+191 (2146-2172) | the same as the FPGA (nextsprites.cpp 422-451) | ok |
| Relative sprites | visible only if the anchor is visible (`sprites.vhd` 784); 4-bit N6 from attribute 4 bit 5 | the same (2307, 2325, 2447) | `anchor.visible && (a[3] & 0x80)`, N6 from bit 5 (nextsprites.cpp 183-187) | ok |
| 4-bit transparency | the nibble compares with NR `#4B` bits 3:0 (`sprites.vhd` 971) | `transparency_colour &= 0xF` (2462) | (nextsprites.cpp 224-231) | ok |

## 8. Layer order and blending (tbblue.c 6918-7500)

| Area | FPGA (file:line) | ZEsarUX (tbblue.c:line) | unreal-ng (file:line) | Verdict |
|:--|:--|:--|:--|:--|
| Orders `000`-`101` | `zxnext.vhd` 7218-7284, including the ULA-border exception for LUS / USL / ULS (7256, 7266, 7278) | priority chain without the border exception (7295-7480) | `ComposeOrder` (video/next/nextvideorenderer.cpp 674-) with `borderException` (721-722) | ok |
| Mode `110` / `111` blue channel | `mixer_b_t := layer2_rgb(2 downto 0) + mix_rgb(2 downto 0)` on the 3-bit blue of the 9-bit colour, clamp to 7 (`110`), `<= 4` -> 0, `>= 12` -> 7, else -5 (`111`) (`zxnext.vhd` 7201-7203, 7286-7341); the result keeps all three blue bits | 3-bit channels, clamps to 7 before subtracting 5 (6918-7010) | blue taken as `(c >> 1) & 3` (2 bits), summed, clamped to 3, the `111` subtract run on the 2-bit sum, LSB rebuilt as `b ? 1 : 0` (nextvideorenderer.cpp 690-691, 776-798) | **BUG 9**, fixed 2026-11-20 |
| Stencil blue channel | `stencil_rgb <= ula_rgb and tm_rgb` on all 9 bits (`zxnext.vhd` 7113) | not emulated (no stencil in the renderer) | `b2(ula) & b2(t)` with the LSB rebuilt (nextvideorenderer.cpp 708) | **BUG 9**, fixed 2026-11-20 |
| Mode `111` clamp | sums of 8-11 give 3-6, sums of 12-14 give 7 (`zxnext.vhd` 7312-7341) | clamps to 7 first, so 8-14 all give 2 (6955-6965, 6997-7007) | the same as the FPGA for red and green (nextvideorenderer.cpp 787-798) | ok (red / green) |
| Blend source `00` with the ULA disabled | `mix_rgb <= ula_mix_rgb`, transparent only by the global colour or the clip, no `ula_en` term (`zxnext.vhd` 7100-7101, 7142-7144); `ula_en` (NR `#68` bit 7) only gates `ula_rgb` (7103-7104) | the ULA layer is empty when disabled (1461) | the ULA line is empty when NR `#68` bit 7 is set (nextvideorenderer.cpp 201-202), so the blend sees no ULA | **BUG 10** (from the VHDL; unverified on a board), fixed 2026-11-20 (`NextVideoRenderer::UlaMixApart`) |
| Fallback colour | `rgb_out_2 <= fallback_rgb_2` (NR `#4A`) when nothing is opaque (`zxnext.vhd` 7214) | the same, the paper only in modes `000`-`101` (7472) | `fallback` (nextvideorenderer.cpp 684, 723) | ok |

## 9. Layer 2, tilemap, LoRes, ULA colours (tbblue.c 1322-1478, 5739-5760, 6070-6918, 7504-8340)

| Area | FPGA (file:line) | ZEsarUX (tbblue.c:line) | unreal-ng (file:line) | Verdict |
|:--|:--|:--|:--|:--|
| Layer 2 Y scroll, 256 x 192 | `y(7:6) + 1` once `y_pre` reaches 192, not a modulo: a scroll above 191 reaches rows 192-254 of the bank (`video/layer2.vhd` 156-158) | `% 192` (7555) | the VHDL form (video/next/nextvideorenderer.cpp 365-369) | ok |
| Layer 2 X scroll, 320 / 640 wide | `x(8:6) + 3` past 319 (`layer2.vhd` 152-154) | `% 320` (7578) | `Layer2WideColumn` (nextvideorenderer.cpp 396) | ok |
| Layer 2 palette offset | added to bits 7:4 of the pixel, wrapping in 8 bits (`layer2.vhd` 203) | `pixel + offset * 16`, no wrap (7657) | `(base[address] + offset) & 0xFF` (nextvideorenderer.cpp 378, 400) | ok |
| Layer 2 address | `(bank(6:4) + 1) & bank(3:0)` 16K pages from SRAM `#40000`, transparent when bit 21 is set (`layer2.vhd` 172-175) | `bank * 16K + #40000`, masked to 2 MB (1340-1370, 7639) | bank from the start of RAM, transparent past the fitted RAM (nextvideorenderer.cpp 349-357) | ok |
| Tilemap base in bank 7 | the BRAM of bank 7 is 8K: address bits 12:0 (`video/tilemap.vhd` 402-403; `zxnext.vhd` 6663) | 16K from bank 7 (1431-1450) | 8K wrap for bank 7 (nextvideorenderer.cpp 446-453) | ok |
| Tilemap transparency | 4-bit index against NR `#4C`; text mode against the global colour (`zxnext.vhd` 7109) | NR `#4C` (6621) | (nextvideorenderer.cpp 442, 525) | ok |
| LoRes memory | bank 5 only (`zxnext.vhd` 4267, 6625) | bank 5 (5739-5760) | bank 5 (nextvideorenderer.cpp 294-296) | ok |
| LoRes Y scroll | `+1` on `y(7:6)` from 192 (`video/lores.vhd` 84-87) | (8252-8340) | the VHDL form (nextvideorenderer.cpp 305-306) | ok |
| ULANext paper with NR `#42` = `#FF` or a non-solid mask, and the ULANext border with `#FF` | `ula_select_bgnd` (`video/zxula.vhd` 500-502, 525); the ULA pixel then **is** the fallback colour NR `#4A` (`zxnext.vhd` 6987-6991), opaque unless NR `#4A` equals the global transparency NR `#14` (7100) | paper index 128 (6235-6237); the border is the fallback colour (6088-6090) | the pixel is left transparent: `-1` (nextvideorenderer.cpp 245-249) and `UlaBorderPixel` returns false (171-172), so a layer below the ULA shows through instead of the fallback colour | **BUG 11**, fixed 2026-11-20 |
| ULA+ border | `11 & attr(7:6) & 1 & attr(5:3)` = `#C8` + colour (`zxula.vhd` 530-541) | `192 + 8 + colour` (6084) | `0xC8` + colour (nextvideorenderer.cpp 182) | ok |
| Timex hi-res border with ULANext | `128 + attr(5:3)` of the `01 PPP III` border attribute (`zxula.vhd` 503) | `128 + paper` (6075-6078) | `128 + colour` (nextvideorenderer.cpp 179-182) | ok |
| Blend of ULA and tilemap in `110` / `111` with NR `#68` bits 6:5 = `10` | one mix layer: `ula_final_rgb`, the tilemap over or under the ULA, not an average (`zxnext.vhd` 7149-7151, 7115-7116) | averages the ULA and tile colours (6918-6950, 7262-7275) | `mixRgb = u` (nextvideorenderer.cpp 745-747) | ok |

## 10. Ports, DAC, UART (tbblue.c 8765-9005)

| Area | FPGA (file:line) | ZEsarUX (tbblue.c:line) | unreal-ng (file:line) | Verdict |
|:--|:--|:--|:--|:--|
| DAC ports | each port is gated by its NR `#84` enable (soundrive 1 / 2, Profi covox, covox, Pentagon `#FB` only when soundrive 2 is off, GS `#B3`, Specdrum `#DF`) (`zxnext.vhd` 2429-2435, 2658-2664) | all ports always decoded; NR `#08` bit 3 checked only at the mix (8939-8975, 8977-9005) | `DacChannelsAt` (ports/models/portdecoder_next.cpp 1299-1311) | ok |
| `#7FFD` / `#1FFD` and the MMU | rewrite the MMU slots by the paging rules (`zxnext.vhd` 4606-4700) | rewrites NR `#50`-`#57` (8765-8805) | section 4 above | ok |
| UART status | TX and RX flags (`zxnext.vhd`, port `#133B`) | only "data ready" from the host bridge (8829-8840) | not compared further (no ZEsarUX model) | no action |

## unreal-ng bug candidates

Each one contradicts the VHDL; none is confirmed on a board. Paths as above; the proposed test is one line each.

1. **NextREG read-back of narrow and unknown registers.** `zxnext.vhd` 5881-6285 (`others => 0` at 6282-6283; masks at 5914-5915,
   5929-5933, 6017-6018, 6056-6057, 6092-6093, 6098-6099, 6113-6117, 6161-6162, 6265-6266) against io/z80n/nextboard.cpp 266
   (`return _regs[reg]`) and 637-639 (`default` stores any byte). Test: write `#FF` to NR `#0B`, `#12`, `#2F`, `#4C`, `#68`, `#6A`,
   `#70`, `#71`, `#8F`, `#D8`, `#29` and read back `#B1`, `#7F`, `#03`, `#0F`, `#FD` (bit 1 clear, bit 3 = ULA+ flop), `#3F`,
   `#3F`, `#01`, `#03`, `#01`, `#00`.
2. **NR `#22` bit 7 never set.** `zxnext.vhd` 5991-5992 (`not pulse_int_n`) against io/z80n/nextinterrupts.cpp 99-101. Test: read
   NR `#22` inside the ULA INT pulse; bit 7 = 1, and 0 outside it.
3. **NR `#C0` bits 2:1 do not show the CPU's IM.** `zxnext.vhd` 6229-6230 (`z80_im_mode`, `o_im_mode` 1905) against
   nextinterrupts.cpp 47 (`value & 0xE9`) and 105-106. Test: after `IM 2`, NR `#C0` bits 2:1 read `10`; after `IM 1`, `01`.
4. **NR `#02` bits 3:2 lost when the NMI is refused by NR `#06`.** `zxnext.vhd` 2090-2091, 3843-3862 against nextboard.cpp
   525-533 and ports/models/portdecoder_next.cpp 2188-2213. Test: NR `#06` = 0, write NR `#02` = `#04`: no NMI, NR `#02` bit 2 reads 1.
5. **NR `#20` (generate interrupt) not modelled.** `zxnext.vhd` 1946-1947, 5988-5989, `device/im2_peripheral.vhd` 56, 160, 172,
   186-192 against nextboard.cpp 266 (stored, no effect). Test: line interrupt disabled, write NR `#20` = `#80`: an INT is raised
   and NR `#C8` bit 1 reads 1.
6. **Interrupt status not set for a disabled source or in pulse mode.** `im2_peripheral.vhd` 160, `zxnext.vhd` 1941-1944 against
   nextinterrupts.cpp 51-52, 188-202, 208-212. Test: pulse mode, NR `#C9` CTC 0 enable off, let CTC 0 reach zero: NR `#C9` bit 0 = 1.
7. **Boot ROM window is 8K, mirrored at `#2000`.** `zxnext.vhd` 1856-1857, 3199-3204, 3044-3050 against
   memory/next/nextmemory.cpp 438, 465-471. Test: config mode with the boot ROM on, the byte at `#2000` equals the byte at `#0000`;
   write `#0100`, NR `#03` = 0 (leave), and `#0100` of NR `#04`'s bank holds the written byte. **Fixed** (2026-11-20): the boot ROM
   is the read side of slots 0 and 1 whatever the MMU holds, the writes keep the decode under it (Multiface, DivMMC, Layer 2, MMU RAM,
   NR `#04` bank). Open: `sram_req` has no `bootrom_en` term, so a read under the boot ROM at 28 MHz should wait a clock too; unreal-ng
   keeps it wait-free (with the wait the TTD seek test over the boot diverges, to be looked at separately).
8. **Copper mode `11` restart ignores NR `#64`.** `device/copper.vhd` 80, `zxnext.vhd` 3950, `video/zxula_timing.vhd` 457-469
   (restart where `cvc` = 0) against io/z80n/nextcopper.cpp 92-104 (restart at the paper top, where `cvc` = NR `#64`). Test:
   NR `#64` = 10, mode `11`, a list of one MOVE: the MOVE lands 10 lines before the first paper line, not on it.
9. **Blue channel of the blend modes `110` / `111` and of the stencil uses 2 bits.** `zxnext.vhd` 7203, 7286-7341 (3-bit blue sum,
   clamp, minus 5), 7113 (9-bit AND) against video/next/nextvideorenderer.cpp 690-691, 708, 776-798 (`(c >> 1) & 3`, clamp 3,
   LSB rebuilt). Test: mode `110`, Layer 2 blue `011` + ULA blue `011` (9-bit palette entries) gives blue `110`; stencil of blue
   `001` and `001` gives `001`; the existing AdditiveBlendMixesLayer2WithTheUlaAndClamps test masks blue out.
10. **Blend source `00` drops the ULA when NR `#68` bit 7 disables it.** `zxnext.vhd` 7100-7104, 7142-7144 (`ula_mix_rgb` has no
    `ula_en` term) against nextvideorenderer.cpp 201-202. Test: mode `110`, NR `#68` = `#80`, ULA paper red, Layer 2 blue: the
    pixel is the sum, not Layer 2 alone. Derived from the VHDL only; worth a board check before changing.
11. **ULANext full-ink / non-solid-mask paper and the full-ink border are transparent instead of the fallback colour.**
    `video/zxula.vhd` 500-502, 525 (`ula_select_bgnd`), `zxnext.vhd` 6987-6991 (the ULA pixel becomes NR `#4A`), 7100 (transparent
    only if NR `#4A` = NR `#14`) against nextvideorenderer.cpp 171-172, 245-249. Test: ULANext on, NR `#42` = `#FF`, NR `#4A` = `#1C`,
    NR `#14` = `#E3`, order SUL with an opaque Layer 2 pixel under the paper: the output is `#1C`, not the Layer 2 colour.

## ZEsarUX-only differences (no action)

unreal-ng follows the VHDL in all of these; ZEsarUX does not.

- NextREG reads: every register reads its stored byte (5726); the narrow masks and `others => 0` are not modelled.
- No IM2 hardware mode, no CTC, no NR `#20`, no NR `#C0`-`#CF` beyond storage; the line interrupt is in `screen.c` and needs IFF1.
- NR `#02`: a refused NMI clears the bits (5007, 5023).
- NR `#03`: the timing lock is ignored and the remap differs (4390-4393, 4919-4928); timing changes are ignored under turbo (4304, 4444).
- Contention per 16K window instead of per 8K MMU page (3429, 3481).
- Optional turbo caps `tbblue_deny_turbo_rom` / `tbblue_deny_turbo_everywhere` (3747-3782).
- Boot ROM 16K at `#0000-#3FFF` (3645-3646).
- Copper: mode `11` restarts at the vsync interrupt (747-753); the WAIT ignores NR `#64` and the `+12` (693-728, 935-962); a MOVE to
  NR `#00` is executed (558); NR `#63` takes its first byte from the register array (426-443); mode `01` stops at the end of the
  list, where the VHDL's 10-bit address wraps.
- Sprites: byte 4 written as 0 when attribute 3 bit 6 is clear (1928-1934); a per-line count of 100 instead of the time budget (2223,
  2796); the clip window is applied while drawing (2146-2172).
- Layer order: no ULA-border exception in LUS / USL / ULS (7295-7480); mode `111` clamps to 7 before the minus 5 (6955-6965,
  6997-7007); the ULA / tilemap mix source averages the two colours (6918-6950, 7262-7275).
- Layer 2: Y scroll `% 192` (7555), X scroll `% 320` (7578), palette offset not wrapped (7657).
- Tilemap in bank 7 read as 16K (1431-1450).
- ULANext full-ink paper is palette entry 128 (6235-6237).
- DAC ports are always decoded, not gated by NR `#84` (8939-8975).

## Coverage of `tbblue.c`

Read through: 60-1100 (palettes, copper, raster, clip windows), 1100-2920 (reset of sprites, palettes, sprite ports and overlay,
Layer 2 port), 2920-4180 (memory tables, paging, contention, reset), 4196-4470 (timing, turbo), 4600-4660, 4742-5505 (NextREG
writes), 5505-5740 (NextREG reads), 5739-5800 (LoRes pointer, layer names), 6070-6250 (border and ULA colours), 6252-6918 (tilemap,
skimmed for registers, clip, transparency and scroll), 6918-7500 (blend, layer order), 7504-7740 (Layer 2), 8252-8340 (LoRes),
8765-9005 (ports `#7FFD` / `#1FFD`, UART, NMI, DAC). Also `screen.c` 14960-15112 (line interrupt), `core_spectrum.c` 388-470 and
760-830 (interrupt acceptance), `tbblue.h` (constants). Not read line by line: 4470-4600 and 4660-4742 (debug and menu helpers),
7740-8250 and 8339-8760 (the ULA and screen-store renderers: ZEsarUX's own frame buffer, checked only for the colour rules above).
DivMMC, DMA and the Multiface live in other ZEsarUX files and were not part of this diff.
