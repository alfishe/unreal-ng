# ZX-Evo TSConf Machine Support

**Created:** 2026-09-27 · **Status:** design complete, review round 1 applied —
ready for phase 0 (see [TODO.md](TODO.md))

Make the ZX-Evo **TS-Conf** configuration a creatable machine (model key `TSL`,
alias `TSCONF`) in unreal-ng: memory windows + FM window + cache, palette VDU
with 4 modes × 4 geometries, TSU tiles/sprites, DMA, 4-source interrupt
controller, switchable CPU clock, SD card (image or host folder) + Beta-128 with
virtual drives, AY + beeper/Covox DAC + GS — with **full debugging, TTD
capture/restore and all automation frontends** (WebAPI/MCP/CLI/Lua/Python).

## Documents

| File | Content |
|:--|:--|
| [hardware-spec.md](hardware-spec.md) | The behavioral contract: every register, bit, reset value and timing rule with Verilog/XLS evidence; firmware build matrix; reconciliation of the four reference implementations (§12); corrections from v1 (§13) |
| [technical-design.md](technical-design.md) | I — what TS-Conf is; II — the machine block by block (diagrams); III — unreal-ng implementation: current state, the four new generic extension points (interrupt source, memory write intercept, M1 hook, ungated step hook), state isolation, decoder, engine scheduling, video, storage, TTD, debugger, automation, decisions D1-D7, risks |
| [implementation-plan.md](implementation-plan.md) | Phases 0-8 with dependencies and test-first work lists (test IDs, fixtures, expected values traced to the spec) |
| [references.md](references.md) | Sources with upstream links, local clones, what each is authoritative for |
| [boot-and-storage-notes.md](boot-and-storage-notes.md) | Field notes from the real firmware: BIOS Setup options and boot devices, Wild Commander (panel drives, PS/2-only keyboard), IDE on TS-Conf (ports, no MBR needed, WC's disk detection, TTD), test data |
| [TODO.md](TODO.md) | Status marker + progress |

## Key findings

- The authoritative sources are TS-Labs' own: the TS-Conf Verilog
  (`tslabs/zx-evo`, `pentevo/fpga/current`) and the `TSconf.xls` register
  workbook. The ancestor Unreal fork (`tslabs/zx-evo-unreal`) is the porting
  blueprint but diverges from the hardware in a dozen places (hardware-spec §12).
- unreal-ng carries an unfinished, non-isolated skeleton of the ancestor's port
  (state struct, a never-instantiated renderer, placeholder video modes, ROM
  config). The creatability gate is the missing port decoder; the real work is
  the engines - the four generic extension points (interrupt source, write
  intercept as a host bus overlay, M1 hook, per-step hook) are built.
- The ROM image is already in the repo: `data/rom/zxevo.rom` pages 0-3 are
  TS-BIOS / TR-DOS 5.04T / 128 / 48 (the same file serves ATM3 from pages 28-31).
- The shared SD card model (`SdCardSpi`) already exists on the NeoGS branch.
- `pentevo/fpga/baseconf` is the ZX-Evo **Base Configuration** (the existing
  `ATM3` machine), not TS-Conf.
