# E8b: `#FE` / `#FF` read formats and the clock-select switch time — design and verification

| | |
|---|---|
| **Date** | 2026-10-01 |
| **Status** | Implemented 2026-10-01 (not committed) |
| **Source** | RTL at the pinned commit of [sources-and-provenance.md](sources-and-provenance.md); xpeccy-plus as the second opinion |
| **Origin** | rows 32, 39 and 20 of the xpeccy-plus / unreal-ng comparison report |

## 1. `#FE` / `#F6` read: bit 5 = 0

`z80/zports.v` (`dout` case): `PORTFE` and `PORTF6` return `{1'b1, tape_read, 1'b0, keys_in}`: bit 7 = 1, bit 6 = tape,
**bit 5 = 0**, bits 4:0 = keys. A plain ULA reads bits 7 and 5 as 1. The legacy tree has the same lines
(`fpga/baseconf/trunk/z80/zports.v:392-395`). xpeccy-plus: `evoInFE = xInFE & ~0x20`. Karabas Pro is a Profi-class
board with its own format (`GX0 & TAPE_IN & kb_do_bus`, six key lines) and says nothing about the Evo.

Change: the ATM3 `KeyboardBorder` arm masks bit 5 of the standard `#FE` read. Test FE-1.

## 2. `#FF` read: `{intrq, drq, 1, last write D4..D0}`

`zports.v` `VGSYS` case: `{vg_intrq, vg_drq, 1'b1, ~vg_side, vg_hrdy, vg_res_n, vg_a}`; `vg93/vg93.v:177` loads
`{vg_side, vg_hrdy, vg_res_n, vg_a} <= {~din[4], din[3], din[2], din[1:0]}` on every `#FF` write in shadow. So the
read shows D4..D0 of the last write (`~vg_side` = D4) and bit 5 = 1. The legacy tree keeps a plain six-bit latch
(`vgFF <= din[5:0]`, `dout = {intrq, drq, vgFF}`). xpeccy-plus: `(res & 0xC0) | 0x20 | (regFFW & 0x1F)`.

What was wrong: unreal-ng returned the WD1793 model's register, which is not written when the write is a controller
reset (bit 2 low), so the read lost bits after such a write, and bit 5 was the written one.

Change: ATM3 keeps its own latch of the last `#FF` write (`EmulatorState::evoVgSys`, D5..D0; it replaces the
drive-only `evoVgDrive`, the drive is D1..D0 of it) in `TrdemuFdcAccess`, for both trees; the read arm builds the
value (status bits 7:6 from the chip). Test FF-1 (both trees).

## 3. Clock-select switch time

`zclock.v`: `always @(posedge fclk) if(zpos) begin old_rfsh_n <= rfsh_n; if (old_rfsh_n && !rfsh_n) int_turbo <= turbo; end`.
The select takes effect on the falling edge of /RFSH, the refresh half of the **next M1** after the `OUT`. unreal-ng
queued it and applied it at the next frame start, so a program that selected 14 MHz ran a whole frame at the old
rate.

Change: `PortDecoder_ATM3::updateTurboMode` sets `EmulatorState::evoTurboPending` (kept in the `AtmPaging` blob) and
attaches the machine M1 hook; `OnMachineM1`, called after the refresh, clears it and calls
`Z80::ApplyHardwareTurboNow()` (the mid-frame path Scorpion and TSConf already use). The hook is attached only while
a switch is pending, which is a handful of instructions per switch. Reset takes the same path. Test CLK-1.

Effect on recorded data: the ATM3 golden row (T-states per 150 frames 9 434 880 -> 9 504 768, because the BIOS's
14 MHz now starts inside the frame that selects it) and four TTD CI-gate rows were re-recorded.

## 4. A harness bug this exposed: `Emulator::RunNFrames`

`RunNFrames(n)` fixed its budget at the start as `frameLimit * n` T-states. With a clock switch inside the run
(now at the next M1, earlier than the next frame) the frame length doubles or quadruples while the budget stays,
so "N frames" became half or a quarter of that in emulated time. That showed up as three unrelated failures:
`ZXEvoErs_Test` (a typed `SAVE` arrived too early), two `ComPort_Test` cases (the run stopped before the UART echo)
and a shifted golden row. `RunNFrames` now rescales its counters when the frame length changes during a step
(`emulator.cpp`), and the three failures are gone without test edits. The ATM3 golden row is now 150 x 69888 T like
the 48K row; before, it undercounted.

## 5. Verification

Full build zero warnings; `test-parallel` all green (see the session notes in [TODO.md](TODO.md)); mingw
`-fsyntax-only -Werror` clean for the changed files.
