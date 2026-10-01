# ZX-Evo BaseConf: where the reference sources come from

Written 2026-10-01 so that a later audit does not have to ask. Every hardware statement in this folder and in
`docs/inprogress/2026-09-29-machine-waits/` rests on the upstream projects below, taken from their public
repositories (not from a local edit), at the commits listed. Local checkouts live in the reference directory
next to the project (`emulators/github/`); they are plain clones and were unmodified when this was written
(`git status` clean for `pentevo`).

## Upstream sources and pinned revisions

| What | Upstream | Commit used | Used for |
|---|---|---|---|
| ZX-Evo FPGA (RTL), AVR firmware, ERS ROM sources | [`alfishe/pentevo`](https://github.com/alfishe/pentevo) (mirror of the NedoPC / tslabs project, canonical home [`tslabs/zx-evo`](https://github.com/tslabs/zx-evo/tree/master/pentevo), folder `pentevo`) | [`c24723db`](https://github.com/alfishe/pentevo/tree/c24723db5f8f272ae00c515dac73b49501d7fe2e) (2026-06-19) | **ground truth**: `fpga/base_trdemu/trunk` (released tree), `fpga/baseconf/trunk` (frozen legacy tree), `avr/baseconf/trunk`, `rom/` |
| xpeccy-plus (fork) | [`dotkoval/xpeccy-plus`](https://github.com/dotkoval/xpeccy-plus) | [`84e627d3`](https://github.com/dotkoval/xpeccy-plus/tree/84e627d3bf182ccf58271623033ca418fd66b3c3) (2026-10-01) | second opinion: `src/libxpeccy/hardware/pentevo.c` |
| Xpeccy (upstream of the fork) | [`samstyle/Xpeccy`](https://github.com/samstyle/Xpeccy) | `3a311278` (2026-07-08) | confirms what is the fork's own |
| Karabas Pro (a Profi-class board, not an Evo clone) | [`andykarpov/karabas-pro`](https://github.com/andykarpov/karabas-pro) | `c210d6cc` (2026-02-17) | other-board check only: its `#FE` read is `GX0 & TAPE_IN & kb_do_bus` (profi `karabas_pro.vhd:1787`), so it does not decide Evo behavior |
| Unreal Speccy family | [`alfishe/unreal-speccy`](https://github.com/alfishe/unreal-speccy) | `a685b6f9` (2020-04-02) | original-Unreal behavior |
| NedoOS | [`alfishe/NedoOS`](https://github.com/alfishe/NedoOS) | `44049473` (2026-09-27) | what software does with the ports |

Direct links into the RTL at the pinned commit (all return HTTP 200 when checked on 2026-10-01):
[`mem/atm_pager.v`](https://github.com/alfishe/pentevo/blob/c24723db5f8f272ae00c515dac73b49501d7fe2e/fpga/base_trdemu/trunk/mem/atm_pager.v),
[`z80/zports.v`](https://github.com/alfishe/pentevo/blob/c24723db5f8f272ae00c515dac73b49501d7fe2e/fpga/base_trdemu/trunk/z80/zports.v),
[`z80/zclock.v`](https://github.com/alfishe/pentevo/blob/c24723db5f8f272ae00c515dac73b49501d7fe2e/fpga/base_trdemu/trunk/z80/zclock.v),
[`z80/zkbdmus.v`](https://github.com/alfishe/pentevo/blob/c24723db5f8f272ae00c515dac73b49501d7fe2e/fpga/base_trdemu/trunk/z80/zkbdmus.v),
[`vg93/vg93.v`](https://github.com/alfishe/pentevo/blob/c24723db5f8f272ae00c515dac73b49501d7fe2e/fpga/base_trdemu/trunk/vg93/vg93.v),
[`video/video_palframe.v`](https://github.com/alfishe/pentevo/blob/c24723db5f8f272ae00c515dac73b49501d7fe2e/fpga/base_trdemu/trunk/video/video_palframe.v),
[`avr/baseconf/trunk/src/joystick.c`](https://github.com/alfishe/pentevo/blob/c24723db5f8f272ae00c515dac73b49501d7fe2e/avr/baseconf/trunk/src/joystick.c).

## What was simulated and what was only read

| Statement kind | How it was established |
|---|---|
| Wait states at 14 MHz (`zmem.v` fetch/data words, `zclock.v` I/O wait), frame and INT geometry (`video_sync_h/v.v`) | **Run**: the actual RTL modules in Verilator 5.052 with a behavioral Z80 bus model; bench saved in [`../2026-09-29-machine-waits/tools/zxevo-rtl-sim/`](../2026-09-29-machine-waits/tools/zxevo-rtl-sim/), results and method in [`../2026-09-29-machine-waits/research-zxevo.md`](../2026-09-29-machine-waits/research-zxevo.md) (marks [H] / [M] / [L]). The in-source wait table is wrong in places; the simulated numbers replace it |
| Port decode, `#BF`/`#BD` registers, write protect, font RAM, 4:4:4 palette, `#FE`/`#FF` read format, DOS-entry clock stall, clock-select switch time | **Read** in the RTL (file and line in the design documents). Not simulated; the stall length (4 fclk) and the switch on the falling edge of /RFSH are straight from the source |
| AVR behavior (joystick, keyboard, rasters) | **Read** in the AVR C sources |

Gaps that follow from this and are recorded, not hidden: the 3.5 and 7 MHz quantization of the stall (the clock
edge phase), and everything marked [M] in the research document.

## The 2026-10-01 comparison report

`xpeccy-vs-unreal-ng-zx-evo-baseconf.md` (the xpeccy-plus / unreal-ng matrix) compared code at xpeccy-plus
`84e627d3` and unreal-ng `e02d8f4d`. Its rows on pal444, write protect, font RAM and the `#3Dxx` hold were
correct that day and have been closed since ([tdd-e8-wrprot-font-pal444-dosstall.md](tdd-e8-wrprot-font-pal444-dosstall.md));
its row on 14 MHz wait tables calls the two numerically different: ours are the Verilator-checked ones above;
xpeccy-plus' table has not been compared with the simulation line by line, so that difference is open on its side.
