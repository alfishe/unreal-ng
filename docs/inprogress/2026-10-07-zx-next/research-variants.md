# Research: Next board variants

**Date:** 2026-10-07 · part of [README.md](README.md) · feeds `NextBoard` in [design.md](design.md) D3

The task asked for faithful support of real variants (Issue 2, Issue 4, KS2, Mini). This is what the sources
read say. Everything else is open.

## 1. What is established

| Fact | Source |
|:--|:--|
| MAME's header lists "TBBlue 1.2, Issue 0, Issue 1, Issue 2, Issue 2B (Kickstarter 1), Issue 2D, Issue 2E, Issue 2H, Issue 4 (Kickstarter 2), Issue 5 (Kickstarter 3)" and says the implementation is based on Issue 5 | [MAME `specnext.cpp`](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/next/specnext.cpp) head |
| MAME has four machines: `tbblue` (id `#08`, "Emulators ID", board issue 0, RAM default 2M, options 1M and 4M), `specnext_ks1` (id `#0A`, issue 1, 1M default, option 2M), `specnext_ks2` (id `#0A`, issue 2, 2M only), `specnext_ks3` (id `#0A`, issue 3, 4M only) | same file, `tbblue()`, `ks1()` .. `ks3()` |
| The boot ROM comes in four images per MAME: Next core 3.01.00, anti-brick 3.02.00, Next core 3.02.04, anti-brick 3.02.04; each is 8K, mirrored twice into 16K; the anti-brick variant is a different first 8K | same file, `ROM_START(tbblue)` |
| jnext targets Issue 2 behavior as the reference where the VHDL says the issues differ | jnext `FPGA-REPO-ANALYSIS.md` ("target Issue 2 behavior (Spartan 6) as the reference unless targeting Issue 5 features") |
| Issue 2 uses a Xilinx Spartan-6 FPGA; the Issue 4 board moved to an Artix-7; KS2 machines all ship with 2 MB; the Accelerated model has the Accelerator board with the full 2 MB | press and shop pages found by search: [The Register, 2024-01-19](https://www.theregister.com/2024/01/19/return_of_spectrum_next), [specnext.com Accelerated product page](https://www.specnext.com/product/zx-spectrum-next-accelerated-computer/) (search summaries, pages not read in full) |
| The VHDL tree has three tops: `zxnext_top_issue2.vhd` (Spartan 6), `zxnext_top_issue4.vhd`, `zxnext_top_issue5.vhd` (Artix 7, "XADC + XDNA") | jnext `FPGA-REPO-ANALYSIS.md` |
| The NextREG list has an "Issue 2 keyboard" bit (NR `#08` bit 0, "Implement issue 2 keyboard") and the anti-brick machine id `#FA`, ZX-DOS platform id `#EA` | `nextreg.txt` |
| XADC registers NR `#F8`-`#FA` exist in MAME (the Artix-7 on-chip ADC) | MAME register cases |
| Unexpanded Next: 768K total; expanded: 2 MB | wiki Memory map (fetched) |
| FPGA flash: 16 MB, 32 slots of 512 KB on the older boards (slot 0 anti-brick, slot 1 Next core); press says KS2 uses 256 Mbit with up to 15 cores of 17.5 Mbit | storage-manager integration doc (FPGA repo README, not re-read); The Register summary |

## 2. What is not established

| Open | Why it matters |
|:--|:--|
| "Next Mini" | none of the sources read (MAME, jnext, the wiki pages fetched, a web search) mentions a product by that name. It may be a Pi-Zero-sized or a different community board; it cannot be modelled without a hardware description. Q1 |
| What an application can detect between Issue 2 and Issue 4 | the core is the same VHDL (register map, ports); differences read so far are RAM size defaults, flash layout, XADC, HDMI timing. NR `#00` reports `#0A` on all of them in MAME. Whether NR `#0E` (sub-minor) or another register differs is open |
| KS3 4 MB | one source (MAME). Q3 |
| Whether Issue 2 without the Accelerator limits NR `#07` speeds or RAM pages | the Accelerated product page suggests that the Accelerator adds RAM and speed; the unexpanded 768K memory map is in the wiki. Q4 |
| Keyboard issue-2 / issue-3 matrix differences | NR `#08` bit 0 selects; the rule is not in the sources read (`config.ini` calls it "Keyboard Issue 2(1)/Issue 3(0)") |

## 3. Design consequence

`NextBoard` is a small struct returned by `NextBoard::For(boardName)`, like `ProfiBoard::For(model)`
([design.md](design.md) D3):

| Field | Meaning | Source status |
|:--|:--|:--|
| `machineId` | NR `#00` value | `#0A` hardware, `#08` an "emulator" board for software that wants to know |
| `issue` | 2, 4 (KS2) or 5 (KS3) | MAME numbering differs: its `board_issue` counts 1, 2, 3 for KS1..KS3. We use the printed issue numbers and map |
| `ramKb` | 1024 / 2048 / 4096 | R1, Q4 |
| `hasXadc` | NR `#F8`-`#FA` present | Artix-7 boards |
| `flashSlots`, `flashKb` | persistent flash blob size | storage doc, Q3 |
| `rtcFitted` | DS1307 present (optional on the board) | storage doc |
| `keyboardIssue2Default` | default for NR `#08` bit 0 | open |

Only fields that change behavior a program can observe are added. The default board is **Issue 4 / KS2 with
2 MB**, because that is the machine the NEXT row in the model table already promises (2048K). The config key is
`[NEXT] Board=issue2|issue4|ks3|emulator` and the first phases implement `issue4` only (phase N12 adds the rest).
