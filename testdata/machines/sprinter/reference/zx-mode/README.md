# Sprinter ZX-mode reference captures (MAME, BIOS 3.06)

Made 2026-10-02 for the ZX-mode research
([research-zx-mode.md](../../../../../docs/inprogress/2026-09-28-sprinter/research-zx-mode.md) §9).

- **MAME:** 0.289 subset build `zxsp` with the `sprinter` driver, `-bios v3.06` (the owner's MAME-pack ROM set).
- **Disk:** the MAME pack's `sp_hdd_sys.chd` (DSS 1.71.57) rebuilt with a `SYSTEM.BAT` that stops at the prompt
  (`@echo off`, `set PATH=...`, `ver`; no `fn`). No floppy.
- **Tool:** [mame-zxsteps.sh](../../../../../tools/machines/sprinter/mame-capture/mame-zxsteps.sh) with
  `mame-zxsteps.lua`; each `*-steps.txt` is the session's step log (frame, action, PLD state).

| File | Session | What it shows |
|---|---|---|
| `mame-menu-sprinter.png` | trd2, frame 1 400 | `spectrum atarin.trd`: the 128 menu "Sprinter" (TR-DOS, Hardware, 128 BASIC, Calculator, 48 BASIC, Options) |
| `mame-trdos-703.png` | trd3, frame 1 550 | ENTER: "Sprinter TR-DOS v.7.03 (c) 2025 Sprinter Team", drive A = the RAM disk |
| `mame-atarin-ramdisk.png` | trd3, frame 2 600 | `RUN`: the ATARIN demo loaded from the RAM disk |
| `mame-bcity-scl.png` | scl1, frame 2 400 | `spectrum bcity.scl`, TR-DOS, `RUN`: the disk's boot menu |
| `mame-reset-back-to-dss.png` | scl1, frame 3 200 | MAME soft reset in the Spectrum mode: the launcher's reset intercept, "EXIT from Spectrum mode" |
| `mame-menu-origin.png` | tap2, frame 1 440 | `spectrum origin.zx`: the standard 128 menu (Tape Loader first) |
| `mame-tape-no-signal.png` | tap2, frame 14 000 | Tape Loader with `greenberet.tap` playing: nothing loads (MAME's `kbd_fe_r` never sets `#FE` bit 6) |
| `mame-action-sna.png` | sna1, frame 1 700 | `spectrum p128.zx`, then MAME's snapshot device loads `testdata/loaders/sna/action.sna`: it runs |

The step strings used (`ZXK_STEPS`; `{ENTER}` is the natural-keyboard code):

```
trd3: 5|kbdonly|ms_naturl;1100|keys|cd \trd{ENTER};1200|keys|spectrum atarin.trd{ENTER};1420|kbdonly|=:;1450|keys|{ENTER};1550|snap|trdos;1560|keys|r{ENTER};2600|end|
scl1: (as trd3 with bcity.scl);2400|snap|game;2410|reset|;3200|snap|after-reset;3220|end|
tap2: 5|kbdonly|ms_naturl;1100|keys|cd \zx{ENTER};1200|keys|spectrum origin.zx{ENTER};1420|kbdonly|=:;1440|snap|menu;1450|keys|{ENTER};1570|play|;14000|snap|final;14010|end|   (mame args: -cass greenberet.tap)
sna1: 5|kbdonly|ms_naturl;1100|keys|cd \zx{ENTER};1200|keys|spectrum p128.zx{ENTER};1450|load|<action.sna>;1700|snap|sna-250;1720|end|
```
