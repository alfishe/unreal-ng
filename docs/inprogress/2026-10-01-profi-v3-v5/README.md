# Profi v3 and v5 as two machines

**Created:** 2026-10-01 · **Status:** implemented on branch `profi-v3-v5` (phases 1-7) · see [TODO.md](TODO.md)

## What this is

The ZX Profi came as two board families:

- **v3.x** (TOO "Profi" / JV Kramis, 1990): 512K, monochrome 512x240, the Kramis BIOS with TR-DOS 5.03.
- **v5.0x** (Kondor, 1993-94): 1024K, colour hi-res with a 256-colour palette, the Micco BIOS with TR-DOS 5.04T,
  and with controller v4.0 and later an extended port map with a clock chip, an IDE port and a COM port.

unreal-ng has one Profi, which behaves like a v5. xpeccy-plus split its Profi into v3 and v5 on 2026-10-01. This
folder designs the same split for unreal-ng. It differs from xpeccy-plus in one way: every difference is checked
against independent sources first. Those sources are the other emulators, the Karabas-Pro RTL, the Black_Cat
port table, the factory ROMs, and the board manuals and PROM dumps from
[speccy4ever](https://speccy4ever.speccy.org/_PR.htm).

**Can both be done correctly? Yes.**

- **Ready to build now (phases 1-3):** the port and device differences, the memory, the firmware, the monochrome
  hi-res and the frame timing are all settled.
- **Frame timing:** our own decode of the sync PROMs shows that Profi boards shipped with different sync PROMs.
  The emulator consensus (69888 T) and xpeccy-plus's measured 71680 T are both right, for different PROMs. So the
  PROM becomes a board option.
- **v5 timing:** settled. Its sync PROM dumps first gave a 216 T line, which turned out to be our mistake: v5 boards
  reload the line counter with 61. The frame is 69888 T with INT 14368 T before the paper, and the board adds a
  video WAIT in Spectrum mode.
- **Wait states and the floating bus:** the v3 turbo wait rule and the v3 floating bus come from the v3.2
  schematic (E3); the v5 video WAIT comes from a gate-level model of the v5.06 netlist (E4). Both are implemented
  ([design.md](design.md) sections 4.4 and 6).

## Documents

| File | Contents |
|:--|:--|
| [cross-check.md](cross-check.md) | every v3/v5 claim, with each source's answer and a verdict; our sync-PROM decode |
| [research-profi-v3-turbo-floatbus.md](research-profi-v3-turbo-floatbus.md) | E3: the v3.2 turbo wait rule, the HLD hold and the floating bus, read off the schematic |
| [research-profi-v5-wait.md](research-profi-v5-wait.md) | E4: the v5 video WAIT, from a gate-level model of the 5.06 netlist |
| [research-profi-v5-open-items.md](research-profi-v5-open-items.md) | phase 7: the palette gate, the CP/M switch, the #DFFD decode of 5.0 and 5.06, the third crystal |
| [research-profi-keyboard.md](research-profi-keyboard.md) | the keyboards: X9 / KEYB per board, the PROFI-XT controller (8035 firmware, its reconstruction), how BIOS 2.0 reads EXT, other emulators; implemented in [design.md](design.md) section 9 |
| [test-programs.md](test-programs.md) | the emulated test programs (Tact Meter, TEST 4.30, the border demos) and what they show against real boards |
| [tools/machines/profi/](../../../tools/machines/profi/README.md) | the sync-PROM decoder (`syncprom/`), the port decoder PROM tool (`profidecoder/`), the v5 wait model (`waitmodel/`), the v3 turbo model (`turbomodel/`) and the PROFI-XT 8035 simulator (`xtkbd/`) |
| materials (not in the repository) | every file the analysis used: manuals, PROM and ROM dumps, articles, forum pages, other emulators' sources, working reports. Kept outside the repository by decision (2026-10-01), with its own index `materials/README.md`; every external file has its source URL there, and the public ones are linked from these documents |
| [decoder-prom.md](decoder-prom.md) | both boards' port decoder PROMs: wiring, port map per mode, what they settle, and the check against unreal-ng (no difference) |
| [roms.md](roms.md) | the factory firmware (now in `data/rom/profi/`), which BIOS needs which board, our non-factory images |
| [requirements.md](requirements.md) | goals, requirements per board with confidence, acceptance, open questions |
| [design.md](design.md) | two models and one decoder with a board profile, ports, timing, turbo, TTD, automation, the phase plan |
| [tdd-plan.md](tdd-plan.md) | tests per phase |

## Where xpeccy-plus was right, and where not

| xpeccy-plus says | Here |
|:--|:--|
| v3 has no palette, no extended ports, no RTC, no IDE | **confirmed** by the manuals (the v3.2 port decoder takes ADR15 where v4/v5 take ROM14) |
| v3 is monochrome in hi-res | confirmed (v3.2 manual) |
| v3 Kramis BIOS + TR-DOS 5.03, v5 Micco BIOS | confirmed (speccy4ever labels, ZXMAK2 romsets) |
| `#FE` bit 7 always reads 1 | right for v3, **wrong for v5** (ZXMAK2, Karabas and pico-spec read a palette bit there) |
| YM2149 on v5 | **wrong**: both manuals say AY-8910 / 8912 |
| no floating bus on v3 | **wrong** per the v3.2 manual ("чтение пиксела экрана во время прямого хода луча") |
| v3 frame 71680 T, INT 47 T before paper | **right for one PROM** (`FB0579B6`, our decode gives 71680 / 48 T), but not for the original PROM of a 3.2 board (`0A1DFAFD`, dumped by the MDESK project), which gives the 69888 / 12580 every other emulator uses; that one is the v3 default ([cross-check.md](cross-check.md) section 4) |
| v5 216 T line, 67392 T frame, 13860 T to paper | **wrong**: a decoding mistake we first made too. v5 boards reload the line counter with 61, not 63 (Kondor's fix list, the 5.06 netlist). With that, the v5 PROM gives 224 T x 312 = 69888 T and INT 14368 T before paper, the 48K's position |
| (not in xpeccy-plus) v5 has a video WAIT in Spectrum mode | the v5 board holds CPU RAM accesses for the video, like a contended Sinclair bus (Gromov, the 5.06 album: jumper SB8 "торможение спектрум-режима") |
| v3 turbo wait rule, HLD drops turbo | **confirmed** by the v3.2 schematic (E3); its Tact Meter figure (88208 T) is reproduced |
| v3 256K / 768K with `#FF` holes | no other source; open |
