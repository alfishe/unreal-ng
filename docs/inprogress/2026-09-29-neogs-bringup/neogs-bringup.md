# NeoGS bring-up: from the task to master

- **Date:** 2026-09-29
- **What this is:** the story of how the NeoGS sound card emulation got into
  unreal-ng. It starts with the task and ends with the final polish. It
  covers each step: how it was done, what was hard, what did not work at
  first, and which bugs came up and how they were fixed. At the end are a
  session log with every commit, and an account of how the time was spent.
- **Sources:** git history (branch `neogs`, merged in `d0359af3`); the design
  documents in [`../2026-09-19-general-sound/`](../2026-09-19-general-sound/)
  (mainly [`neogs-tdd.md`](../2026-09-19-general-sound/neogs-tdd.md) §13-§14
  and [`neogs-zxdma-design.md`](../2026-09-19-general-sound/neogs-zxdma-design.md))
  and their earlier git revisions; [`PLAN.md`](../PLAN.md) row #45; the
  Claude Code session transcripts (timestamps and user messages). Every
  number below comes from one of these, and the method is given where the
  number is derived rather than read.

## Contents

1. [Summary](#1-summary)
2. [Glossary](#2-glossary)
3. [Timeline](#3-timeline)
4. [Phase by phase](#4-phase-by-phase)
5. [Bugs and hard problems](#5-bugs-and-hard-problems)
6. [What did not work at first](#6-what-did-not-work-at-first)
7. [Session log](#7-session-log)
8. [Time accounting](#8-time-accounting)
9. [Driving the emulator through MCP / WebAPI](#9-driving-the-emulator-through-mcp--webapi)
10. [Lessons learned](#10-lessons-learned)

---

## 1. Summary

**NeoGS** is a modern clone of the General Sound card for the ZX Spectrum,
made by NedoPC (2008 onward). Like the original card it has its own Z80
processor and runs its own firmware. An FPGA (a programmable logic chip) is
built around that Z80 and adds:

- 4 MB of RAM and 512 KB of rewritable flash;
- four switchable memory windows;
- eight sound channels instead of four;
- an interrupt controller;
- an SD card slot;
- a hardware MP3 decoder chip;
- DMA engines that move data without the CPU.

**What was built.** unreal-ng now emulates the full card. It runs the real
flash firmware unchanged: the loader, then the GS-compatible main ROM v1.11.
The emulation includes:

- all four CPU clocks (10/12/20/24 MHz), switchable while the card runs;
- the interrupt controller, the 8-channel mixer and the analog output stage;
- SPI (the serial bus to the SD card and the MP3 chip) with an SD card model;
- a VS1001/VS1011 MP3 decoder built on the minimp3 library;
- the SD and MP3 DMA engines, and the rarely used ZX-DMA (the Spectrum
  reading and writing card memory directly);
- flash programming with the 29F040B command set;
- time travel debugging (TTD);
- the SD card as a slot (`sd.ngs`) of the emulator's media manager;
- every automation surface (CLI, WebAPI, MCP, Lua, Python), Audio Settings
  and HUD labels in the Qt GUI.

The authors' own test programs pass on an emulated Pentagon, the NedoPC flasher
reprograms the flash, Neo Player Light plays MP3 files from the SD card in
real time, and the demo *The Link* (which uses the card as a graphics
accelerator through ZX-DMA) runs to its end credits.

**Key numbers.**

| Item | Value |
|---|---|
| From the first request (2026-09-18 23:58) to the merge into master (2026-09-28 20:33) | 9 days 21 hours of calendar time |
| From the start of the design rewrite (2026-09-27 12:01) to the merge | 32.5 hours of calendar time |
| Life of the `neogs` branch (created 2026-09-27 20:08, merged 2026-09-28 20:33) | 24.4 hours |
| Active session time on NeoGS (idle gaps over 30 minutes removed) | **about 13.6 hours** (12.8 h to 16.1 h depending on the gap threshold, §8) |
| Commits in the session log (§7) | 28 non-merge commits, plus 6 merges of master into `neogs` |
| Card code (`core/src/emulator/sound/chips/neogs/`) | 17 files, 4,429 lines |
| NeoGS test files (`core/tests/emulator/sound/chips/neogs/` + `ttdneogs_test.cpp`) | 17 files, 5,301 lines |
| Main feature commit `50b3ad35` | 101 files, +13,973 / −642 lines |
| Core test suite size, start of the branch → merge | 3,772 → 4,592 tests, all passing at the merge |
| Design document `neogs-tdd.md`, 2026-09-19 sketch → merge | 423 → 1,968 lines |

**Where the time went** (30-minute gap threshold, details in §8): coding 28%,
tests 23%, porting to master 20%, design 12%, verification 11%, analysis of
the sources 6%, proof of concept 0%.

## 2. Glossary

| Term | Meaning here |
|---|---|
| **GS** | General Sound, the original 1990s sound card with its own Z80. |
| **host** | The ZX Spectrum itself, as seen by the card. |
| **card CPU** | The Z80 on the card. It is a real chip on NeoGS, not part of the FPGA. |
| **FPGA, Verilog, RTL** | The FPGA is a chip whose logic is loaded from a description. Verilog is the language of that description; "RTL" means those source files. For NeoGS they are the ground truth for how the card behaves. |
| **firmware** | The programs the card CPU runs: the **loader** (in flash, starts first) and the **main ROM** (copied into RAM by the loader). |
| **flash** | 512 KB of memory that keeps its contents without power and can be rewritten by a program (a "flasher"). |
| **SPI** | A simple serial bus. The card sends one byte and receives one byte at the same time. The SD card and the MP3 chip hang on it. |
| **DMA** | Moving data without the CPU. NeoGS has three engines: SD card → RAM, RAM → MP3 chip, and **ZX-DMA** (Spectrum ↔ card RAM). |
| **TTD** | Time travel debugging: the emulator records a session and can go back to any moment. It stores **checkpoints** (full saved states) at intervals, and each device writes its state into a **blob** (a block of bytes) in each checkpoint. **v1** is the current format; **v2** will store large memories as "regions" that save only the parts that changed. |
| **TDD** | Technical design document (here `neogs-tdd.md`). |
| **POC** | Proof of concept: a small throwaway program built to test an idea before the real code. |
| **golden test** | A test that pins exact outputs (memory hashes, CPU state, sample streams) so that any change in behavior is caught. |
| **worktree** | A second checkout of the same git repository in another folder, so two branches can be worked on side by side. |
| **merge, fast-forward** | Merging joins two branches. A fast-forward is the simple case: the target branch just moves to the newer commit, with no merge commit. |
| **personality** | Which card sits in the emulator's single GS slot: `LLE` (classic GS running its firmware), `LW` (a lightweight player), `NGS` (NeoGS). |
| **mailbox** | The ports the Spectrum and the card use to pass commands and data one byte at a time. |

## 3. Timeline

| Date (local time, UTC−4) | Phase | What happened |
|---|---|---|
| 2026-09-18 23:58 | Task received | The user asks for a design of General Sound and NeoGS support: enable by config, load the ROM, wire the ports, cross-check with ZXMAK2, Unreal Speccy and Xpeccy. Answers the next day: separate designs, GS priority P0, **NeoGS P2**. |
| 2026-09-19 13:05-14:31 | Sources, first design | Materials downloaded (documents, schematics, FPGA sources, datasheets). The user points out that NeoGS has a *physical* Z80 and a hardware MP3 chip. Three review rounds. The 423-line NeoGS sketch is committed with the GS design (`5107e773`). |
| 2026-09-19 → 09-27 | (GS first) | The classic GS card is built and debugged (P0). Its coprocessor moves to the unreal-z80 core on 09-27 (`4ecf980f`). No NeoGS work in this period. |
| 2026-09-27 12:02-14:18 | Source analysis, design rewrite | The GS/NeoGS memory paging is checked against primary sources. Two real bugs are found in the classic card (`65de3029`, `df11e887`). The NeoGS design is rewritten from scratch as a full TDD (`2918ad83`). |
| 2026-09-27 16:24-17:26 | Review round 4 | Four parallel reviewers check every claim against the Verilog, the firmware, the datasheets and the emulator code. Nine things that would have been built wrong are corrected (`7167d0e8`). |
| 2026-09-27 19:56-20:08 | Test assets | Test MP3s recorded from the emulator; SD card images generated. |
| 2026-09-27 20:08 | Implementation starts | Worktree `scratch/wt-neogs`, branch `neogs`, from master `47c40db2`. |
| 2026-09-27 20:08-22:43 | Phases 0-4, 6 | Shared GS infrastructure, the card, SD, flash, MP3, DMA, switching, automation; then TTD on v1 full blobs. One commit, `50b3ad35`. |
| 2026-09-27 22:13-22:43 | Phase 5 design | ZX-DMA design document with the "zero cost when unused" requirement. |
| 2026-09-27 22:49-01:24 | Phase 5a-5c | Host bus overlay, ZX-DMA model, its TTD state and automation. Three master merges along the way. |
| 2026-09-28 01:42-02:15 | Player verification | The Neo Player Light v0.44 hang is explained (a bug in the player); player tests on every card layout. |
| 2026-09-28 02:25-04:05 | Polish | SD requests on the machine thread; configuration fixed while TTD records; all 14 shipped configs get NeoGS; GUI labels; the stereo complaint leads to the AC-coupling fix and a schematic study; a clock test suite finds a bug. |
| 2026-09-28 09:02-11:41 | Real software, GUI | *The Link* demo analyzed and passing; HUD indicators for DMA and transfers; a stereo mode option; a floppy controller crash found in manual testing and fixed. |
| 2026-09-28 12:10 | Merge postponed | A trial merge shows that master now has its own, more general SD card model; the user decides to wait for the media manager. |
| 2026-09-28 19:42-20:33 | Port and merge | Master merged into `neogs`; the SD card moves onto the media manager (`3afc231a`); a second master merge (`d0359af3`); master fast-forwarded to `d0359af3` and pushed. |
| 2026-09-28 21:03 | Cleanup | Worktree removed; branch `neogs` kept. |
| 2026-09-29 01:27-01:58 | TTD follow-up | By user decision the card's RAM and flash leave the TTD v1 checkpoints (uncommitted, in `scratch/wt-ttd-o1`). |

## 4. Phase by phase

### 4.1 The task

The first request (2026-09-18 23:58) covered both cards at once:

> write technical design how to implement GeneralSound / NeoGS cards -
> enable/disable via config, no runtime switching. load ROM, wire ports etc.
> do research and cross analysis with zxmak2, unreal speccy, xpeccy-plus.

The follow-up answers set the frame: two separate designs; the classic GS is
P0 and NeoGS P2; ports go through the existing dynamic port decoder "for all
configurations, Pentagon, ATM, ZX Evo, Scorpion"; the card integrates with the
HUD "like MoonSound".

On 2026-09-19 the user corrected the first research: "neogs contains physical
Z80 - correct documents. seems also it contains hardware mp3 codec - research
more information about BOM ... how we'll implement that mp3 codec in our
emulation?" From then on the requirements grew in the conversation rather
than in one document. The ones that shaped the result most:

| When | Requirement (the user's words, translated where needed) | Effect |
|---|---|---|
| 09-19 | Physical Z80, hardware MP3 chip | The card CPU runs real firmware; the MP3 chip needs a software decoder |
| 09-27 12:40 | "minimp3 into 3rdparty ... treat everything without a license as MIT. FPGA questions - look in the sources" | minimp3 vendored; every hardware question answered from the Verilog |
| 09-27 12:55 | Levels between devices "we'll tune experimentally later"; a cold boot resets everything; no separate Spectrum/card reset for now | §11 decisions 7 and 10 of the TDD |
| 09-27 20:08 | "Set up worktree from master, do full development for neogs there" | Branch `neogs` in `scratch/wt-neogs` |
| 09-27 21:53 | "phase 6 - let's implement it per v1 now. yes it will eat memory, but it will work. then we'll migrate everyone to v2" | NeoGS records TTD with full blobs |
| 09-27 22:13 | ZX-DMA: "a detailed design ... this switching must not reduce performance, and when there is no neogs or ZX-DMA is inactive - zero cost" | The host bus overlay design |
| 09-28 02:26 | "we will not write any external media into TTD v1, only the reaction on the ports, which is fixed data for the time of the recording"; "during a TTD recording nothing in the configuration may change" | SD insert/eject refused while recording |
| 09-28 03:11 | Card chosen "only from the config" | No GUI card switch |
| 09-28 11:10 | Stereo options "separated, legacy GS (50% cross-feed), mono ... also through automation and in audio settings" | `[NGS] StereoMode` |
| 09-29 01:29 | "Large memory blobs are deliberately not written in v1, because a format with a full snapshot every second is very inefficient" | Card RAM and flash leave the v1 checkpoints |

### 4.2 Analysis of the primary sources

**Sources used.** All of them were local copies, checked on 2026-09-27
(`neogs-tdd.md` §1.2):

| Source | What it answered |
|---|---|
| NeoGS FPGA Verilog, current (git-svn mirror of NedoPC `ngs`, SVN r191) | Ports, memory map, interrupts, sound, SPI, DMA timing |
| fpgaD Verilog (the older 2 MB board) | What older boards lack |
| Firmware sources: loader, main ROM v1.11, FPGA boot program, flasher | Boot sequence, clock per stage, what the firmware relies on |
| Host-side programs: `test_ngs`, `test_emu_ngs`, Neo Player Light | Acceptance tests |
| VS1001 datasheet, MA8201 DAC datasheet, ACEX FPGA datasheet | Decoder registers and protocol |
| Board BOM and schematic (`revC-VS`) | Flash chip type, decoder alternatives, reset jumper, analog output |
| `spi_doc.txt`, `dma_zx_doc.txt` (Russian, CP1251) | Timing rules for software |
| Unreal Speccy `gsz80.cpp`, `vs1001.cpp`, `sdcard.cpp` | A working emulation, used for comparison only |
| Original GS schematic, GS port documents, Xpeccy | The classic card's paging |

**How conflicts were resolved.** The rule became: *the current FPGA sources
win*. Where sources disagreed, the Verilog or the schematic decided, and where
two readings of the Verilog disagreed, the file was read again directly. The
main cases:

| Question | Sources disagreeing | Resolution |
|---|---|---|
| Classic GS: which RAM does the fixed window `#4000-#7FFF` show? | GS port doc and Xpeccy: lower half of a page. Schematic, NeoGS FPGA, Unreal Speccy, the firmware's own RAM probe: upper half of MPAG 1 | Schematic plus the majority. The classic card was wrong and was fixed (BUG-10, `65de3029`). |
| Classic GS port `#0A` status bit | Emulator vs GS port doc | GS doc: bit 7 = NOT MPAG bit 0 (BUG-11, `df11e887`) |
| DMA address width | Two reviewers disagreed; the old notes said a 6-bit high register | `dma_sequencer.v`: only 21 address bits reach memory, so DMA covers 2 MB even on a 4 MB card |
| Where DMA behavior comes from | 2026-09-19 sketch: "fpgaD has no DMA, follow Unreal's DMA logic" | The current FPGA has DMA; its Verilog is followed. Unreal Speccy differs in 13 places (`neogs-tdd.md` §3.11) |
| Card clock | Unreal Speccy: always 24 MHz | FPGA: CKSEL honored, 10 MHz at reset |
| Stereo channel layout | GS programmer's manual: channels 1,4 left, 2,3 right | Schematic and FPGA: 1,2 left, 3,4 right |
| Flash chip | Earlier design: Am29F040 | BOM: 29F040**B** (ST part fitted, ID `20`/`E2`), whose unlock addresses differ |
| Is the MP3 decoder a VS1001 or a VS1011? | Software reads the chip version and treats them differently | Both, as a setting (`Mp3Chip`, default `vs1001`) |

**A useful discovery:** nothing needed to be disassembled. The firmware
search found sources for every part of the flash image, and the build script
that assembles `full_ngs.rom` from them. The assembler is AS (`asw`/`asl`),
not sjasmplus. The same repository has GS 1.04, 1.05a, 1.05b and 1.08 as
buildable disassemblies, which match the original ROMs by SHA-256.

**The first analysis (2026-09-19) was shallow.** It was done together with the
classic GS design, and NeoGS was P2. The sketch that came out of it had
several claims that the 2026-09-27 analysis overturned (see §6).

### 4.3 Design: from a sketch to a reviewed TDD

The NeoGS design went through four versions:

| Revision | Commit | Lines | What changed |
|---|---|---|---|
| Sketch (2026-09-19) | `5107e773` | 423 | "Extend `SoundChip_GeneralSound`" as a subclass; ports, GSCFG0, DMA from Unreal Speccy; a short TTD and test section |
| Full TDD (2026-09-27) | `2918ad83` | 1,009 | Rewritten from the sources: NeoGS as a third *personality* of the one GS slot, a sibling class instead of a subclass, shared parts extracted from the classic card first |
| Review round 4 (2026-09-27) | `7167d0e8` | 1,645 | Nine wrong claims fixed, many gaps filled, seven phases (0-6) with "done when" gates |
| As built (2026-09-28, at the merge) | `d0359af3` | 1,968 | §14 as-built record, analog output, clocks, SD via the media manager |

**Review round 4** was the turning point. The user asked "are we ready to
implement neogs? review designs, cover all gaps found, prepare for
implementation". Four reviewers ran in parallel for 7-12 minutes each: one
checked the hardware claims against the FPGA, one checked the design against
the emulator code, one checked the SD, VS1001, flash and DMA specs, and one
checked the firmware and host programs. Their reports found nine things that
would have been built wrong:

1. **SPI restart.** A write during a byte transfer *restarts* the transfer;
   the design said it was ignored. The loader's back-to-back SD accesses are
   exactly 16 cycles apart, so this would have hung the SD boot.
2. **CMD59.** Every NeoGS driver sends CMD59 (CRC off) to the SD card. The
   design did not list it; without it the loader hangs.
3. **DMA reaches 21 address bits** (2 MB), not the whole 4 MB.
4. **The flash chip is a 29F040B**, and the flasher ends by writing CPLD port
   `#80` to restart the card, which the emulator must handle.
5. **VS1001 vs VS1011** differ in ways software checks. Also, minimp3 cannot
   be fed an arbitrary byte stream without losing state, so a small frame
   parser feeds it one MP3 frame at a time.
6. **The shared card loop.** A virtual bus interface would add an indirect
   call to every classic GS memory access. Moving the classic card to a finer
   time unit would change its rounding, reorder mailbox accesses and break
   the recorded TTD fixtures. The fix: a template loop in which each card
   keeps its own time unit.
7. **Module replay after a card switch** would have fired during the NeoGS
   loader. It now waits for the main ROM's command loop (`COMINT_`, `#026E`).
8. **WebAPI `?ram=1`** would have returned the wrong bytes on NeoGS.
9. **Two plans that could not work:** rebuilding the flash image byte for byte
   in CI (the tools exist only as Windows 32-bit binaries), and adding the
   card image to the host ROM table.

Five more small items were found on a second pass through the reviewers'
lists and fixed before the commit.

**Key design decisions and why** (`neogs-tdd.md` §11):

| Decision | Why |
|---|---|
| NeoGS is a third personality of the single GS slot | GS and NeoGS cannot both be fitted, and nothing else has to enforce that |
| Sibling class plus shared extracted components, not a subclass | The classic card's memory, ports and mixing are private; a subclass would have forced them open and coupled the two cards |
| Per-card time unit: classic card stays at 12 MHz cycles, NeoGS counts 1/120,000,000 s "base ticks" | 120 MHz is the least common multiple of 10, 12, 20, 24 MHz and the 24 MHz crystal, so every NeoGS event lands on an exact tick; the classic card stays bit-identical |
| minimp3, fed one frame at a time by our own parser | Deterministic, sample-exact, and TTD can save its state |
| MP3 audio as its own mixer source at the stream rate | Not squeezed through the card's 37.5 kHz DAC path |
| ZX-DMA by swapping the host CPU's memory interface only while needed | Zero cost otherwise (phase 5 below) |
| One shared SD card model for NeoGS, TS-Conf and Z-Controller | Written once. In the end master generalized it before NeoGS merged (§4.9) |

**Worked example: the time base.** At 20 MHz one card CPU cycle is 6 base
ticks (120 / 20). The card timer runs from the 24 MHz crystal: 24 MHz / 5 /
128 = 37.5 kHz, which is 3,200 base ticks per interrupt. If the firmware
switches to 24 MHz, a CPU cycle becomes 5 ticks, but the timer still fires
every 3,200 ticks. So the sound timing does not move when the CPU clock
changes, exactly as on the board.

### 4.4 Proof of concept

**There was none.** `tools/poc/` has no NeoGS entry, and no session built a
throwaway prototype. The risky parts were handled in other ways:

- **Performance of the shared loop:** decided on paper in review round 4, then
  measured with the classic card's golden fingerprints and A/B benchmarks
  during phase 0.
- **The MP3 decoder:** minimp3 went straight into the real code, with a test
  that compares the output sample for sample against minimp3 run directly.
- **ZX-DMA:** a full design document first, then a groundwork commit with no
  user (5a), measured before the model was added (5b).

Two small tools were written that are close to a POC in spirit, but they are
kept as tooling: `tools/neogs/pack_flash.py` (checks the flash image layout)
and `tools/neogs/make_sd_image.py` (builds FAT test cards).

### 4.5 Implementation: phases 0-4 and 6

Everything up to phase 4 and phase 6 went into one commit, `50b3ad35`, made
at the user's request after the code was stable. The per-phase "done when"
gates in the TDD were checked before it.

**Test assets first (19:56-20:07).** The user asked to "generate sd card
image, create test mp3 file by recording eyeache1.sna to mp3". The snapshot was
run on an emulated Pentagon, 30 seconds of its AY music were captured through
the WebAPI, and encoded three ways: 44.1 kHz 128 kbit/s CBR, 22 kHz mono VBR
with an ID3v2 tag and a Xing header (to test that the parser skips them), and
MPEG Layer II. SD images for FAT16 with and without a partition table and FAT32
were generated. Later the user asked "why do we need such a big image for
tests? ... maybe generate it in scratch at run time", so the committed images
were replaced by `core/tests/_helpers/fatimagebuilder.h`, which builds sparse
images at run time and deletes them afterwards.

**Phase 0: shared infrastructure, classic card unchanged.**
- What: `GSCardRunner<Card>` (the catch-up loop that runs the card CPU up to
  the Spectrum's time), `GSAudioOut`, `GSModuleReplay`, `GSHostClock`.
- How it was proven: `soundchip_gs_golden_test.cpp` records the classic
  card's RAM hash, DAC sample stream and port trace before the change and
  compares after.
- **Difficulty:** the first extraction was **24% slower**. The DAC-window
  check had moved into a function the compiler no longer inlined. The fix put
  the check inline in the read path and kept the next-event time in the
  runner. Result: the classic card became **4-8% faster** than before, still
  bit-identical. Because the machine was loaded by other work (CLion at
  1600% CPU), the A/B runs had to be interleaved against a master build.

**Phase 1: the card core.** Memory windows (PG0-PG3, MPAG/MPAGEX, ROM mode,
RAMRO write protection), ports, the interrupt controller, the 8-channel DAC,
the clocks, and boot from the real flash image `data/rom/neogs/full_ngs.rom`.

**Phase 2: SPI, SD card, flash.** `SdCardSpi` (SDSC and SDHC, CRC rules,
session/persist/off write modes), `NeoGSSpi`, `Flash29F040B` programming, the
CPLD port `#80` restart and flash persistence.

**Phase 3: MP3 and DMA.** `Vs10xxDecoder` over minimp3; the SD DMA (with
token wait, error token, no timeout) and the MP3 DMA (burst, then paced by the
decoder's DREQ signal), both stalling the card CPU as on the board.

**Phase 4: switching and automation.** Runtime switching between LLE, LW and
NGS with the uploaded module handed over; NeoGS state and SD/flash commands on
CLI, WebAPI, MCP, Lua and Python; a guard that refuses to load a TTD session
recorded with a different GS card.

**Acceptance on an emulated Pentagon** (all passed on 2026-09-27):
`test_ngs` (2 and 4 MB), `test_emu_ngs`, the NedoPC flasher updating the flash
from the SD card, and Neo Player Light v0.60 playing `EYEACHE.MP3` in real
time. Findings from these runs are in §5.

**Phase 6: TTD on v1 full blobs.** The design had first planned to *refuse*
recording while NeoGS is fitted (4 MB per checkpoint was too much). Phases
0-4 shipped with that refusal. At 21:53 the user decided otherwise ("yes it
will eat memory, but it will work"), and phase 6 was built at once:

- the SD protocol, decoder and DMA state were added to the card's blob
  (layout 2, later 3 with ZX-DMA);
- the recording veto was removed;
- `ttdneogs_test` replays the SD boot exactly from the session start, from a
  per-frame checkpoint and from a mid-frame seek.

Measured: about 55 KB a checkpoint (compressed) with the firmware idle,
because the RAM is mostly zeros. The SD card's sectors never go into TTD: an
SD image can be gigabytes. An SD write is a replay barrier instead.

**Gate at the end of the day:** 3,772 core tests run, 3,768 pass, and the 4
others are the same 4 that do not run on master. NeoGS costs 1.8× the
classic card idle and 1.75× while playing, inside the 2× / 2.5× budgets.

### 4.6 Phase 5: ZX-DMA

**What it is** (the user first asked: "is it just another expansion port to
address more memory in layers?"). No. While the card runs its ZX module, it
**takes over the Spectrum's memory cycles** in `#0000-#3FFF`.

*Worked example.* The card program selects the ZX module and sets the card
address to `#100000`. The Spectrum runs `LDIR` from `#0000` into its own
RAM at `#8000`:

1. The first Spectrum read returns junk: each read returns the byte that the
   *previous* read fetched.
2. Each later read returns the next card byte. After each byte, the card
   address moves on by one.
3. If the card has not finished the previous byte, it holds the Spectrum
   with `/WAIT`.

Writes go the other way, into card RAM, and also into Spectrum RAM if RAM is
paged there.

**The problem:** the Spectrum's memory accesses are the hottest path in the
emulator. An `if` there would cost every machine, with or without NeoGS. The
user's requirement was zero cost when NeoGS is absent or ZX-DMA is inactive.
They also asked: "maybe as soon as DMA mode starts we react faster, and as
soon as the DMA transaction ends, go back to the cheaper mode?"

**The design** ([`neogs-zxdma-design.md`](../2026-09-19-general-sound/neogs-zxdma-design.md)):
- a **host bus overlay** slot: extra memory interfaces that call the normal
  access and then the overlay. They are selected **only while an overlay is
  installed**, so without one the Z80 uses the very same interface objects as
  before;
- **one selector** for the memory interface instead of four places that
  assigned it;
- **three modes**: Off (no overlay), Watch (a short window around a transfer,
  renewed by activity, closing after `ZxDmaWatchFrames` quiet frames) and
  Divert (a transfer is running).

**Sub-phases:**

| Sub-phase | Commit | Content |
|---|---|---|
| 5a | `38bbe5ce` | Overlay slot and one selector, no user yet. Golden test of 8 models + a demo in fast and debug mode, pinned before the change. An installed pass-through overlay costs about 7% of a Pentagon frame; no overlay: within noise |
| 5b | `537badd7` | `NeoGSZxDma`: the modes, the one-byte read lag, writes at the grant, `/WAIT` in host T-states, arbitration with SD/MP3 bursts, abort |
| 5c | `78b06100` | TTD state (blob layout 3) and automation, GS port trace side `zxdma` |
| 5d | — | Debugger integration: waits for the GS debugger |
| 5e | — | `Fpga=D` (old boards): **skipped by decision**, the notes kept (§3.12) |

"No known program uses ZX-DMA", the design said. The next day *The Link*
turned out to use it heavily (§4.8).

### 4.7 Verification against real software and hardware

**Neo Player Light v0.44: the hang that was not ours.** On 2026-09-27 v0.44
(with and without DMA) found no files on any test card. The user deferred it.
On 2026-09-28 01:42 it was taken up again ("try to figure out neo player").
It was solved by reading the player's FAT variables and stack in the hung card,
then its source. The chain:

1. `FINDMP3` builds the MP3 list in one memory page and the directory list in
   another. With fewer than two files it returns *before* switching back to
   the MP3 page.
2. `SET_MP3` then reads a file descriptor from the wrong page and gets a null
   pointer. It takes the "directory cluster" from card address `#0000`, which
   holds the main ROM: `F3 C3 48 01` (`DI : JP #0148`).
3. It follows cluster `#0148C3F3` past the end of the card and waits for a
   data token without a timeout.

Every test card had exactly one MP3. With a second file (`EYE22K.MP3`),
v0.44 plays in real time. The DMA build `npl044_dma` cannot play on real
hardware either: its CMD17 call is commented out in the NedoPC sources. SD and
MP3 DMA were therefore proven with our own card-side player instead
(`soundchip_neogs_dmaplayer_test`: 200 sectors, exact bytes, paced by DREQ).

**Player keys.** Both players read keys through the 48K ROM's interrupt scan
and poll the card with interrupts off, so a very short key press can be
missed, on the board too. The tests hold a key for 12 frames and release it
for 5.

**"Stereo falls apart on NeoGS" (user, 2026-09-28 03:28).** A careful
measurement played one MOD note in each channel on both cards. It found two
separate things:

- **A real bug: a standing DC offset.** The firmware only touches channels
  that play. Idle channels keep whatever the boot's memory test last read
  (`#2C`, `#2C`, `#02` on channels 2-4). The output sat at about −6,000 (L)
  and −13,000 (R) out of ±32,767 and jumped when channels started or stopped.
  On the board, output capacitors remove that DC. Fix: a 5 Hz high-pass per
  side (`4671ce6c`).
- **Not a bug: hard panning.** The user asked "how did the real GS mixer
  sound? no panning at all?" The schematics answered it
  ([`neogs-tdd.md` §3.6](../2026-09-19-general-sound/neogs-tdd.md)). The
  classic GS has about 47% cross-feed built into its op-amp output stage
  (L ≈ 1.29·a + 0.61·b), and Unreal's 50% models that circuit. NeoGS has
  hard stereo: separate op-amps and coupling capacitors, no cross-feed. The
  emulation was right. At the user's request a `[NGS] StereoMode` option
  came later (`separated` default, `gs`, `mono`; `5119d36e`).

**Clocks.** The user asked whether the other documented clocks had been
tested for synchronization. The honest answer was: only 20 MHz end to end. A
new suite (`soundchip_neogs_clocks_test.cpp`, 43 tests, `e4e4cc7c`) runs
everything at 24/12/20/10 MHz and across switches. It found a bug: SPI bytes
and DMA steps in flight kept the old clock after a GSCFG0 change. A new
`applyClockChange` now rescales their remaining time.

**The Link (2026-09-28 09:02-10:12).** The user reported that the demo loads,
plays FM music and hangs. The analysis extracted and detokenized the 20 ALASM
sources from the author's disk and documented the protocol in
`docs/disasm/demo/thelink/`. It found:

- **Nothing was wrong in the emulation.** The user's running binary was a
  master build with the classic GS, and the demo needs NeoGS: it uploads 20
  pages through ZX-DMA writes and fetches every effect frame through ZX-DMA
  reads.
- On the `neogs` branch it runs to its end credits.

A regression test was added (`efb2acc5`). The demo made the card's work visible
as a gap: it uses NeoGS as an accelerator, not for sound, so nothing showed
it was active. This led to the HUD indicators "NeoGS DMA" and "NeoGS <->"
(`0ebfc667`).

**Manual testing in the GUI** found:
- the Audio Settings label still said "GS";
- a mislabeled HUD entry (the YM2203 FM entry was called "Moonsound
  Activity");
- a crash when changing disks (a WD1793 use-after-free, not NeoGS; fixed in
  `0d484d4d` and cherry-picked to master as `94219b58`).

### 4.8 TTD: three decisions about the card's memory

TTD is where the design changed direction most often:

| Date | Decision | Reason |
|---|---|---|
| 2026-09-27 (review round 4) | Refuse recording while NeoGS is fitted, until TTD v2 regions exist | Whole RAM per checkpoint ≈ 1.2 GB per 300 frames uncompressed |
| 2026-09-27 21:53 | Record on v1 with RAM and flash in every checkpoint | User: "it will eat memory, but it will work"; ~55 KB compressed when idle |
| 2026-09-28 02:26 | No external media in TTD; the configuration is fixed while recording | User: TTD records only what the card answers on its ports |
| 2026-09-29 01:29 | Card RAM and flash leave the v1 checkpoints | User: "a format with a full snapshot every second is very inefficient"; large memories wait for v2 regions |

The 2026-09-29 change was made during a separate TTD task (persisting the
input journal) in `scratch/wt-ttd-o1`, branch `ttd-o1-journals`, and is **not
committed yet**. Its effects:

- the NeoGS blob is layout 4, about 21 KB before compression;
- a 300-frame `.ttd` file shrank from 19.57 MB to 2.68 MB (−86%, almost all
  from this change);
- the price: after a rewind the card firmware runs over later RAM, so the
  NeoGS card itself does not replay exactly until TTD v2;
- `TTD_NeoGS_Test.SdBootReplaysExactlyFromEveryKindOfRestorePoint` is skipped
  with that reason.

The change also exposed two things:
- all shipped configs fit NeoGS (`GSType=NGS`, since `1dfcab32`), while
  comments in three replay tests said "classic GS". Those tests now fit the
  classic card explicitly.
- a limit bug: a `.ttd` file with a NeoGS whose RAM held poorly compressible
  data would not load ("implausible peripheral blob size"); the read limit was
  lower than the write limit.

### 4.9 Porting: merges with master

The branch lived only 24 hours, but master moved fast: several other sessions
were changing the ZX-Evo machine, the CPU contention model, the GS control path
and storage at the same time. `neogs` absorbed master six times:

| Merge | Commit | What it brought, and what had to be adapted |
|---|---|---|
| 1 | `d34f2c56` | Master since the branch point; clean |
| 2 | `ff53480a` | The CMOS zero-init fix (`5ded62c9`); ATM3 went back into the golden host test (`0574e5b2`) |
| 3 | `a34395f7` | Conflict in `ttdserializable.h` (master's `Plus3Paging = 13` next to the NeoGS id); ATM3 golden row re-recorded after ZX-Evo BaseConf E0 (`5786aa4d`) |
| 4 | `1e85a981` | Master moved GS automation to TTD live input (`8dacbc82`); NeoGS additions re-applied on top. ATM3 row re-recorded after E1 (`bc8881df`) |
| 5 | `3afc231a` | Media manager, unified `SdCardSpi`, contention rework, TTD rule FR-4. 17 conflicted files. The big one (below) |
| 6 | `d0359af3` | Multi-point I/O contention, floating bus fix, tape as a media slot. Clean; 4,592/4,592 tests |

**The golden host test and the ATM3 detour.** `core_golden_test` pins the
host machines' behavior. When a row changes, it must be proven to come from
master, not from NeoGS. That caused one mistake. The phase 5a commit
`38bbe5ce` picked up another session's uncommitted CMOS work, which sat in the
shared worktree. It was reverted (`2226270a`) and the test restored
(`c05f3c4e`). Another session then found the root cause of ATM3's
non-determinism and fixed it on master (`5ded62c9`). ATM3 came back into the
test after merge 2.

**The media manager migration (2026-09-28 19:42-20:22).** At 12:10 the user
asked if the branch was ready to merge. A trial merge showed that master had
created its own `SdCardSpi` for the ZX-Evo Z-Controller (`ed703577`). It was
not a different model but a generalization of the NeoGS one: the medium sits
behind an `IBlockDevice` interface (image, RAM disk, host folder), and it keeps
every call NeoGS uses. The user decided to wait until master finished the
media manager. At 19:42: "check what is ready, do the migration, retest
everything completely, and when you are ready and I have done the review and
manual tests, we bring it all into master". The adaptations:

- **SD card as slot `sd.ngs`.** It is registered while NeoGS is fitted and
  accepts an image or a host folder (built into a FAT volume), with a 500 ms
  swap delay. Card detect and write protect come from the SSTAT bits. Writes
  are TTD barriers (`MediaManager::NoteWrite`), and insert/eject are refused
  while recording by the manager itself.
- **A card fitted after start-up** gets its configured medium: the manager now
  remembers the applied media set.
- **Legacy keys:** `[NGS] SDWrite` / `SDWriteProtect` map onto the slot.
- **One memory-interface selector** over three inputs (debug × contention ×
  bus overlay = 8 interfaces). The NeoGS overlay wraps the contended access,
  so ULA waits come first. A new test checks that an overlay on a contended
  48K keeps the contention.
- **Master's rule FR-4:** a GS card switch is refused while recording, and a
  stopped session is dropped. The NeoGS tests were adapted ("if master
  introduced new rules, we just adapt").
- **A bug introduced by the merge:** the TTD GS-slot guard silently stopped
  working, because it read `_timeline` instead of the staged timeline. It was
  found and fixed before the commit.
- **CoreGolden rows** for 48K, 128K, +3, Profi, ATM710 and ATM3 differed. A
  clean master build in a separate worktree gave exactly the same values, so
  the change came from master's contention work. The rows were re-recorded.
  That worktree first failed to build: its submodules (`googletest`,
  `benchmark`) were empty.

Master gained three more commits while the merge was still uncommitted. They
were applied on top and `MERGE_HEAD` was moved to the new master, so the
merge commit has the fresh master as its parent.

**The final merge (2026-09-28 20:22-20:33).** After the user's manual tests
("everything works, merge into master and push to both remotes"):

1. The merge was committed as `3afc231a`.
2. Master had moved again, so it was merged once more (`d0359af3`, clean).
3. Master was **fast-forwarded** to `d0359af3` and pushed to both remotes.

The last piece of master (new contention and the tape slot) had not been
checked by hand with NeoGS; the report said so. At 21:03 the worktree was
removed and the branch kept.

### 4.10 After the merge

- NeoGS state appears in the shared GS device report on every automation
  surface (`c6f9b8aa`, another session).
- The ZX-DMA overlay moved to the new multi-overlay chain when TS-Conf needed
  a second overlay (`f8eedf52`, another session, 2026-09-29).
- The TTD memory change of §4.8 is pending in `scratch/wt-ttd-o1`.
- Still open: 5d (debugger integration, with the GS debugger), the `neogs`
  debugger target, and RAM/flash as TTD v2 regions.

## 5. Bugs and hard problems

| # | Bug or problem | Symptom | Root cause | Fix | Commit |
|---|---|---|---|---|---|
| 1 | Classic GS fixed window (BUG-10) | `#4000-#7FFF` showed the wrong RAM page | Emulator followed the GS port doc; the schematic shows the upper half of MPAG 1 | New window constant, tests through the card CPU | `65de3029` |
| 2 | Classic GS port `#0A` (BUG-11) | Wrong status bit | Copied MPAG bit 7 instead of NOT bit 0 | Per GS doc, with a test | `df11e887` |
| 3 | Phase 0 slowdown | Classic card 24% slower | Extraction stopped the compiler from inlining the DAC-window test | Check inline in the read path; next event kept in the runner. Now 4-8% faster | `50b3ad35` |
| 4 | Decoder crash | Crash in the PCM queue | `pcmQueued()` ignored the read position | `(size − read) / 2` | `50b3ad35` |
| 5 | SD card answered a bad CMD0 | Wrong protocol at power-on | Card in PowerOn state accepted a CMD0 with a bad CRC | Silent unless a valid CMD0 | `50b3ad35` |
| 6 | Neo Player Light rejected the test cards | "No files" on generated images | Partition type `#04` not accepted by the player | FAT16 typed `#06` at every size, largest cluster size | `50b3ad35` |
| 7 | `test_emu_ngs` failed on small cards | Reads landed 32 MB apart from 8 GiB up | Its driver packs sector numbers as {15-8, 7-0, 31-24, 23-16} | Test uses a sparse 17 GiB SDHC image | `50b3ad35` |
| 8 | Authors' SCL files did not load | Checksum missing in four of six | Files as published | Checksum appended in fixture copies | `50b3ad35` |
| 9 | `mp3dec_t` could not be forward-declared | Build error | It is an anonymous typedef | Wrapped in `struct Vs10xxMp3State` | `50b3ad35` |
| 10 | Worktree Qt build used the main tree's headers | Wrong headers, confusing errors | `#include "../../../core/..."` in the floppy widget | Includes through `emulator/...` | `50b3ad35` |
| 11 | Stray files in a commit | Phase 5a included another session's CMOS work | Shared worktree, broad staging | Reverted and golden restored; stage files by name | `2226270a`, `c05f3c4e` |
| 12 | ZX-DMA overlay window | Overlay saw the whole address space | Window defaulted to full range | Default `#0000-#3FFF` | `537badd7` |
| 13 | Test SD image replaced by itself | New image deleted | Same path reused by a new builder instance | Unique path per instance | `fc4a8a3b` |
| 14 | Neo Player Light v0.44 hang | "search MP3 files" forever | Bug in the player with exactly one MP3 file (§4.7) | Test cards carry two MP3s; documented | `ca04ea8f` |
| 15 | Standing DC offset | Output at −6,000 / −13,000, jumps when channels start | Output capacitors not modeled | 5 Hz high-pass per side | `4671ce6c` |
| 16 | Clock switch in the middle of a transfer | SD block finished at the old clock | SPI/DMA times not rescaled on GSCFG0 change | `applyClockChange` | `e4e4cc7c` |
| 17 | HUD label | Two "Moonsound Activity" entries | The YM2203 FM entry was mislabeled | Renamed; "General Sound / NeoGS Activity" | `0ebfc667` |
| 18 | Stale HUD test | `FeatureGatingZeroCostWhenDisabled` failed | HUD now tracks events while hidden | Test follows the new contract | `5119d36e` |
| 19 | WD1793 use-after-free (not NeoGS) | Crash in weak-byte lookup after a disk change | Controller kept pointers into a released disk image | Controller drops pointers on disk change | `0d484d4d` (master `94219b58`) |
| 20 | GS-slot TTD guard disabled by the merge | Sessions from another GS card loaded silently | Read `_timeline` instead of the staged timeline | Reads the staged timeline | `3afc231a` |
| 21 | Golden rows changed after merge | CoreGolden failed on 6 models | Master's contention change, not NeoGS | Proven against a clean master build, re-recorded | `3afc231a` |
| 22 | Large NeoGS blob would not load | "implausible peripheral blob size" | Read limit below write limit | One 16 MB limit both ways | uncommitted (`wt-ttd-o1`) |

Some mistakes only affected the tests during development: wrong flash bit
direction, volumes over 63, a JR offset, status bits read from two different
reads, and a handshake needing 10 ms because of the LDIR copy. Two more came
from tooling: stale test binaries (the ALL build target does not rebuild
`core-tests`), and a zsh `$b:c` modifier eating a variable.

## 6. What did not work at first

| First approach | Why it failed or was dropped | What replaced it |
|---|---|---|
| NeoGS as a subclass of the classic card (2026-09-19 sketch) | The classic card's internals are private; the cards differ too much | Sibling class plus shared components (phase 0) |
| "Follow Unreal's DMA logic" (sketch) | The current FPGA has DMA; Unreal differs in 13 places | The Verilog, everywhere |
| A virtual card bus for the shared loop | An indirect call on every classic GS memory access | A template runner |
| One 120 MHz time unit for both cards | Changes classic rounding and invalidates recorded TTD data | Per-card units |
| CI rebuild of the flash image byte for byte | Tools only as Windows 32-bit binaries | A packer check (`pack_flash.py`) |
| Refuse TTD recording with NeoGS | The user preferred "works now, optimize later" | v1 full blobs, then (09-29) blobs without RAM and flash |
| SD insert/eject as TTD events | User: no external media in TTD | Config fixed while recording |
| Committed SD card images (8 MB and 36 MB) | Too large | Built at run time in scratch |
| Proposed: give NeoGS the classic 50% cross-feed by default | The schematics show NeoGS has none | Board behavior by default, `StereoMode` option |
| Host-folder SD card as a NeoGS feature | The user wanted it once, centrally | Came with the media manager's `sd.ngs` |
| Merge at 12:10 on 2026-09-28 | Two `SdCardSpi` models | Waited for master's media manager, then migrated |
| Phase 5e `Fpga=D` (old boards) | Old boards, no known need | Skipped by decision; notes kept |
| Neo Player v0.44 "unexplained" (09-27) | Deferred by the user | Explained the next day (§4.7) |

## 7. Session log

All times are local (UTC−4), author dates from git. Sessions are Claude Code
transcripts; only their NeoGS parts are listed. Every id below was checked with
`git cat-file -t`.

### Session `43a90e1c` (2026-09-18 23:22 → 09-19 21:21)

NeoGS parts: 09-18 23:58 → 09-19 00:11 (design request) and 09-19 13:05 → 14:31
(materials, NeoGS research, review rounds 1-3).

| Commit | Date | Description |
|---|---|---|
| `5107e773` | 09-19 14:31 | GS and NeoGS design documents and reference materials (NeoGS sketch, 423 lines) |
| `a5c5bc16` | 09-19 20:10 | GS emulation foundation, incl. `GSType=NGS` config parsing and NeoGS materials (classic GS work, NeoGS only reserved) |

### Session `20607a0d` (2026-09-26 15:37 → 09-27 14:18)

NeoGS part: 09-27 12:02 → 14:18 (checking another agent's paging claims against
primary sources, three source searches, TDD rewrite).

| Commit | Date | Description |
|---|---|---|
| `4c5d13a4` | 09-27 12:27 | TTD fixtures re-recorded after the GS window fix (committed by the user) |
| `65de3029` | 09-27 14:17 | Classic GS: fixed window is the upper half of MPAG 1 (BUG-10) |
| `df11e887` | 09-27 14:17 | Classic GS: port `#0A` status bit 7 = NOT MPAG bit 0 (BUG-11) |
| `2918ad83` | 09-27 14:17 | NeoGS full TDD and corrected hardware notes |
| `ecae2074` | 09-27 14:18 | GS debugger docs: physical-address page model, NeoGS facts |

### Session `a6408adb` (2026-09-27 16:24 → 09-29)

NeoGS parts: 09-27 16:24 → 09-28 21:07, and 09-29 01:27 → 01:58. After 09-28
21:08 the session moved on to the debugger model and TTD work, which is not
counted here.

**2026-09-27**

| Commit | Time | Description |
|---|---|---|
| `7167d0e8` | 17:26 | Design review round 4; design ready for phase 0 (on master) |
| — | 20:08 | Branch `neogs` created from master `47c40db2` in `scratch/wt-neogs` |
| `50b3ad35` | 22:43 | NeoGS phases 0-4 and 6 (TTD v1) |
| `d34f2c56` | 22:49 | Merge master into `neogs` (1) |
| `38bbe5ce` | 23:23 | Phase 5a: host bus overlay slot and one memory-interface selector |
| `2226270a` | 23:24 | Revert another session's CMOS files picked up by 5a |
| `c05f3c4e` | 23:35 | Golden host test restored without ATM3 |
| `ff53480a` | 23:51 | Merge master into `neogs` (2) |
| `0574e5b2` | 23:51 | ATM3 back in the golden host test (CMOS fix from master) |

**2026-09-28**

| Commit | Time | Description |
|---|---|---|
| `537badd7` | 00:44 | Phase 5b: ZX-DMA model |
| `78b06100` | 01:24 | Phase 5c: ZX-DMA in TTD and automation; `Fpga=D` skipped |
| `a34395f7` | 01:33 | Merge master into `neogs` (3) |
| `5786aa4d` | 01:33 | ATM3 golden row re-recorded after master's BaseConf E0 |
| `ca04ea8f` | 01:59 | Neo Player Light v0.44 hang explained and covered |
| `fc4a8a3b` | 02:15 | Player tests on every card layout, transport keys, own DMA player |
| `1e85a981` | 02:24 | Merge master into `neogs` (4) |
| `bc8881df` | 02:24 | ATM3 golden row re-recorded after master's BaseConf E1 |
| `6610654e` | 03:01 | SD requests on the machine thread; config fixed while TTD records; `[NGS]` in configs; Audio Settings and HUD |
| `1dfcab32` | 03:20 | All 14 shipped models fit NeoGS; GUI follows a card switch |
| `4671ce6c` | 03:44 | AC-coupled line output (no standing DC offset) |
| `a3004b3b` | 03:48 | Docs: GS and NeoGS output stages from the schematics; ways to insert the SD card |
| `e4e4cc7c` | 04:05 | Clock test suite; SPI/DMA follow a clock change |
| `efb2acc5` | 10:12 | *The Link* sources and analysis; regression test on Pentagon 1024 + NeoGS |
| `0ebfc667` | 11:12 | HUD indicators for NeoGS DMA and ZX transfers; automation stats design; HUD labels |
| `5119d36e` | 11:21 | `StereoMode` (separated / GS cross-feed / mono); HUD test fixed |
| `0d484d4d` | 11:40 | WD1793 use-after-free fix (cherry-picked to master as `94219b58`) |
| `3afc231a` | 20:22 | Merge master into `neogs` (5): SD card on the media manager (`sd.ngs`) |
| `d0359af3` | 20:33 | Merge master into `neogs` (6); master fast-forwarded to it and pushed |

**2026-09-29**

| Commit | Time | Description |
|---|---|---|
| — | 01:27-01:56 | Card RAM and flash removed from TTD v1 checkpoints (uncommitted, `scratch/wt-ttd-o1`) |

### Related master commits from other sessions

These were needed by, or triggered by, the NeoGS work. They are not counted in
§8.

| Commit | Date | Description |
|---|---|---|
| `4ecf980f` | 09-27 11:49 | Classic GS coprocessor on unreal-z80 (the runner in phase 0 builds on it) |
| `47c40db2` | 09-27 18:00 | ATM frame timing fix; the branch point of `neogs` |
| `5ded62c9` | 09-27 23:42 | ATM3 CMOS zero-init (ATM3 boot determinism) |
| `8dacbc82` | 09-28 01:31 | GS automation through TTD live input |
| `ed703577` | 09-28 08:59 | Z-Controller SD card on one reusable SD model (generalized from NeoGS's) |
| `2d1e706d` | 09-28 12:41 | Unified media manager design |
| `c6f9b8aa` | 09-28 20:53 | One GS report (incl. NeoGS) for every interface |
| `f8eedf52` | 09-29 14:50 | Write-only bus overlays and the overlay chain; NeoGS moved to it |

## 8. Time accounting

### 8.1 Method

1. **Find the sessions.** `grep -l -i neogs` over the Claude Code transcripts
   (`~/.claude/projects/-Volumes-TB4-4Tb-Projects-Test-unreal-ng/*.jsonl`).
   Three sessions did the NeoGS work: `43a90e1c`, `20607a0d`, `a6408adb`.
   Others mention NeoGS because they worked beside it (media manager, ATM3,
   TTD rules); they are excluded.
2. **Cut each session into windows** by its user messages and commits, for
   example "09-28 02:00-02:17: player tests, commit `fc4a8a3b`". Only windows
   about NeoGS count.
3. **Active time in a window.** Sort every transcript record's timestamp
   (user messages, assistant messages, tool calls and results). Add up the
   gaps between consecutive records, but drop any gap longer than a
   threshold. The main threshold is **30 minutes**; 15 and 60 minutes are
   shown for sensitivity. A build or a 6-minute test run counts as active,
   because the agent was working. A user away for an hour does not.
4. **Assign windows to categories:**
   - reviewers checking claims against sources, and source searches: analysis;
   - writing or fixing design documents: design;
   - merges, merge conflicts, golden re-records caused by master, the trial
     merge and the migration: porting;
   - running real software, investigating hangs, manual GUI checks, schematic
     checks: verification;
   - windows that produced one commit of code and tests: split by the
     commit's lines added under `core/tests/` versus source files (third-party
     code excluded). Shares used: phases 0-4 and 6: 38% tests; 5a: 62%; 5b+5c:
     55%; the 09-29 TTD change: 50% (guess, uncommitted).
5. **The joint GS + NeoGS start (09-18/19).** That session designed both cards.
   26% of its assistant records in the relevant windows mention NeoGS (`neogs`,
   `ngs`, `nedopc`, `vs1001`). So 26% of its active time is counted.
6. **Checks.** The category rows add up to the total. The per-sitting table
   (§8.3) totals 0.2 h more, because each cut between windows drops the gap
   that crosses it.

### 8.2 Result

| Category | Hours (30-min gaps) | Share | 15-min gaps | 60-min gaps | How derived |
|---|---|---|---|---|---|
| (1) Analysis of the primary sources | 0.84 | 6% | 0.84 | 0.84 | 26% of the 09-19 materials and research window (0.18 h); the 09-27 source check before the rewrite (0.42 h); the four review-round-4 reviewers (0.23 h) |
| (2) Requirements and technical design | 1.66 | 12% | 1.66 | 2.73 | 26% of the 09-18/19 design and review windows (0.25 h); the TDD rewrite (0.58 h); round-4 fixes and readiness (0.32 h); the ZX-DMA design (0.51 h) |
| (3) Proof of concept | 0.00 | 0% | 0.00 | 0.00 | None was built (§4.4) |
| (4) Coding | 3.79 | 28% | 3.79 | 4.43 | Code share of phases 0-4/6, 5a, 5b/5c and the 09-29 TTD change; SD/config/GUI (0.91 h); HUD/stereo (0.48 h) |
| (5) Verification | 1.54 | 11% | 1.54 | 2.33 | Neo Player v0.44 (0.36 h); stereo, DC offset and schematics (0.45 h); *The Link* (0.40 h); MP3 card for the user and the crash (0.33 h) |
| (6) Porting | 2.70 | 20% | 1.86 | 2.70 | Six merges, the ATM3/golden detour, the trial merge, the media manager migration and the final merge (2.19 h); worktree cleanup (0.51 h, mostly waiting for the user, so it drops at 15 min) |
| (7) Tests | 3.09 | 23% | 3.09 | 3.09 | Test assets (0.17 h); test share of the combined commits; player tests (0.28 h); clock suite (0.45 h) |
| **Total** | **13.62** | 100% | **12.77** | **16.13** | |

**Summary of the time:**
- Active time on NeoGS: **about 13.6 hours** (between 12.8 and 16.1 h,
  depending on the idle threshold).
- Calendar time: 9 days 21 hours from the first request to the merge. But
  8 of those days went to the classic GS, and NeoGS work sat idle. The real
  push ran from 2026-09-27 12:01 to 2026-09-28 21:08: 33 hours of calendar
  time, about 12.7 of them active.
- **Coding and tests together: 51%.** Porting to a fast-moving master took
  20%, almost as much as the whole design effort (18% with analysis).
- The biggest single step, phases 0-4 plus 6 (`50b3ad35`, about 14,000
  lines), took about **2 hours** of active time (20:08-22:13).

### 8.3 By sitting

| Sitting | Active hours (30-min gaps) | Main content |
|---|---|---|
| 09-18 23:57 → 09-19 14:32 | 0.43 (26% of 1.69) | Task, materials, sketch |
| 09-27 12:01 → 14:19 | 1.01 | Source check, GS bugs, TDD rewrite |
| 09-27 16:24 → 18:10 | 0.60 | Review round 4 |
| 09-27 19:50 → 09-28 00:00 | 3.97 | Test assets, phases 0-4, 6, 5a, merges |
| 09-28 00:00 → 04:30 | 4.16 | 5b, 5c, players, SD/config/GUI, stereo, clocks, merges |
| 09-28 08:55 → 12:30 | 1.76 | *The Link*, HUD, stereo mode, crash, trial merge |
| 09-28 19:35 → 21:08 | 1.42 | Media manager migration, final merge |
| 09-29 01:27 → 01:58 | 0.49 | Card memory out of TTD v1 checkpoints |
| **Total** | **13.84** | 0.22 h above §8.2 because of the window cuts |

### 8.4 Caveats

- **Agent time, not human time.** Timestamps show when the session was busy.
  The user ran several sessions at once (media manager, ZX-Evo, contention,
  TTD), so their own attention was shared and is lower than these numbers.
- **Parallel sub-agents are counted once.** The four reviewers of round 4 ran
  side by side for 7-12 minutes each (about 35 agent-minutes); they count as
  their 0.23 h of wall-clock time.
- **Category splits are estimates.** Coding vs tests inside one window uses
  line counts, which say nothing about which lines were harder. The 26%
  NeoGS share of the joint GS session is a proxy, too.
- **Overlaps.** One window often did two things. For example, 10:14-11:22 on
  2026-09-28 wrote the automation statistics design (design) and the HUD and
  stereo code (coding). It was counted as coding.
- **Excluded work that NeoGS depended on:** the classic GS itself; the move of
  its coprocessor to unreal-z80; the generalized SD card model (`ed703577`)
  and the media manager, both by other sessions, which saved NeoGS work but
  also caused the migration; this document.
- **Transcript gaps.** Records exist only while the session is active. A
  build longer than 30 minutes with no output would be dropped as idle. In
  the NeoGS windows every dropped gap (32 to 111 minutes) ended with a user
  message, so they were waits for the user, not silent work. The longest
  gaps kept (24 and 27 minutes) were waits for the user too, which is why the
  15-minute column is lower.

## 9. Driving the emulator through MCP / WebAPI

unreal-ng can be controlled while it runs. The surfaces are MCP tools
(`mcp__unreal-ng__*`), the WebAPI (HTTP on port 8090), the CLI (port 8765),
and Lua/Python execution endpoints. This section measures how much the NeoGS
bring-up used them. Counts come from the transcripts (main sessions and
their sub-agents) in the same NeoGS windows as §8. The time estimates in
§9.4 are estimates and are kept apart from the measured numbers.

### 9.1 How often (measured)

A "live call" is one tool call that touched a running emulator: an MCP tool,
an HTTP request to the WebAPI, or a start or stop of `unreal-qt`.

| Session | WebAPI (curl / script) | MCP | GUI start / stop only | CLI 8765, `/lua/exec`, `/python/exec` | Total |
|---|---|---|---|---|---|
| `43a90e1c` | 0 | 0 | 0 | 0 | 0 |
| `20607a0d` | 1 | 0 | 1 | 0 | 2 |
| `a6408adb` | 10 | 3 (2 `emulator_manage`: `create`, `status`; 1 script talking to the MCP bridge directly) | 7 | 0 | 20 |
| Sub-agents (all three sessions) | 0 | 0 | 0 | 0 | 0 |
| **Total** | **11** | **3** | **8** | **0** | **22** |

By purpose:

| Purpose | Live calls |
|---|---|
| Make test data: capture 30 s of audio from a running Pentagon for the test MP3s | 8 (6 HTTP, 2 to stop the instance) |
| Re-record the TTD fixture corpus through the WebAPI script | 2 |
| Triage a report from the user (*The Link* "hangs") | 2 |
| Prepare a machine or a build for the user's manual tests | 9 |
| Cleanup | 1 |
| Boot of the firmware, SD, MP3, DMA, TTD replay checks, screenshots | **0**: all done in the test harness (below) |

**The main tool was the in-process test harness, not the live surfaces.**
`core-tests` runs whole emulated machines (Pentagon + NeoGS) with no GUI. In
the same windows:
- 235 tool calls ran `core-tests` with a test filter, and 47 ran the full
  suite;
- 11 throwaway harnesses were written and deleted: 7 probe files
  (`core/tests/zz_*probe_test.cpp`: stereo, config, five for *The Link*) and
  4 temporary tests inside existing files (`DIAG_TEMP_Npl044`,
  `DIAG_TEMP_PlayerMatrix`, `DIAG_TEMP_PlayerControls`, `DIAG_Order`);
- about 47 of the filtered runs ran those probes or diagnostic tests.

A temporary `SDTRACE` log line in `sdcardspi.cpp` was part of the Neo Player
investigation.

**Failures and retries.** 4 of the 22 live calls (18%) failed or had to be
done again:

| Call | What went wrong | Cost |
|---|---|---|
| `GET /openapi.json` | Wrong path; the spec is at `/api/v1/openapi.json`. Found by probing four paths | about 1 min |
| `POST /resume` | "not paused": `start` already runs the machine. Harmless | none |
| `pkill -f <path>` | Pattern did not match the running process; killed by PID next | under 1 min |
| `open unreal-qt.app; curl ...` (09-28 09:10) | Hung for the 120 s tool timeout while the GUI started; a follow-up `pgrep` also timed out (30 s) | about 3 min |

Outside the live calls: on 09-28 10:14 loading the MCP tools failed ("No
matching deferred tools found"), because the MCP server had not connected.
The agent checked the bridge with a small stdio script. The user restarted MCP,
and the two MCP calls then worked at once.

### 9.2 What it was used for: episodes

| # | Episode | Calls | Outcome |
|---|---|---|---|
| 1 | **Test MP3s from a real run** (09-27 19:57-20:00) | Start Pentagon, load `eyeache1.sna`, `audio/capture` 30 s, poll the status, stop: 8 calls | "The 30 s capture is done (44.1 kHz stereo WAV, audio levels look normal)." Encoded into the three test MP3s used by the decoder, player and DMA tests (`50b3ad35`) |
| 2 | **TTD fixtures after the GS window fix** (09-27 12:19-12:20) | `TTD_Corpus_Test` failed after BUG-10. A second GUI on port 8191 (the user's own instance held 8090), then `record_fixtures.py --base-url http://localhost:8191` | Five fixtures re-recorded (301 checkpoints each) in about 7 s; committed by the user as `4c5d13a4` |
| 3 | **"The Link hangs"** (09-28 09:10-09:13) | `GET /emulator`, then `GET .../state/audio/gs` on the user's running instance, plus a look at its binary and config | The instance was a master build (`ed703577`) with the classic card (`GSType=Z80`, 4 channels). Verdict: "Nothing in the emulator had to be fixed"; the demo needs NeoGS. The analysis itself then ran in probes (`efb2acc5`) |
| 4 | **A machine for the user's test** (09-28 10:14-10:15) | Open the `neogs` build; MCP `emulator_manage create` (Pentagon, 1024 KB) and `status`; `GET .../state/audio/gs` | Confirmed "ngs \| NeoGS (Z80 @ 20 MHz, 8 x 8-bit DAC, 2048 KB RAM)". The user's session led to the HUD requests (`0ebfc667`) |
| 5 | **Builds handed to the user** (03:21, 11:20, 11:41, 20:19) | Start / restart `unreal-qt` from the worktree | The user's own GUI use found four issues: the "GS" label (`1dfcab32`), "stereo falls apart" (`4671ce6c`, `5119d36e`), the doubled HUD label (`0ebfc667`) and the WD1793 crash (`0d484d4d`). Human testing found these; the automation only started the program |

For contrast, the investigations that found bugs used the harness, not the
live surfaces:
- **Neo Player v0.44 hang (`ca04ea8f`):** a temporary test, `DIAG_TEMP_Npl044`,
  plus the `SDTRACE` log showed the sector the player asked for.
- **DC offset:** `zz_stereo_probe_test` played one MOD note per channel on both
  cards and measured L/R.
- ***The Link*:** five probes checked 128/512/1024 KB, long runs, per-frame
  change counts, screenshots of every part and reaching the credits. The first
  probe started at 04:07 on 09-28, inside the window §8 counts as tests.
- **Clock bug (`e4e4cc7c`):** a new test suite.

### 9.3 What automation could not do, or where it cost time

From the transcripts only:
- **No headless mode, no command-line options.** "Опций командной строки нет"
  ("there are no command-line options"): a model and RAM size cannot be
  passed on start-up. Every live session needed the GUI app first, then an
  HTTP or MCP call to create the machine.
- **One GUI per port.** A second instance needed `UNREAL_WEBAPI_PORT=8191`
  to avoid the user's instance on 8090 (episode 2).
- **MCP availability depends on the client.** The server was not connected
  until the user restarted it (09-28 10:14).
- **Starting the GUI from a tool call can block the shell** (the 120 s hang).
- **Nothing for the card's internals at the time:** a trace of SD commands,
  the card's RAM at a moment, and per-frame screen change counts were not
  reachable through the WebAPI. The agent wrote C++ probes for them. The GS
  state endpoint was enough only for the yes/no question in episode 3.
- **Not used at all:** the CLI on port 8765 and the Lua/Python exec
  endpoints. Nothing in the transcripts says why; the harness was simply the
  first choice.

### 9.4 Counterfactual: time without it (estimate, not measured)

Assumptions, kept conservative:
- A manual GUI iteration takes 5-15 minutes: start the app, pick the model,
  load a snapshot or disk, type `RUN`, wait (*The Link* assembles itself for
  about 28 s before it starts), read the debugger, take a screenshot.
- Recording one TTD fixture by hand takes 5-10 minutes: load, settle, record
  300 frames, save.
- Each probe run of §9.1 replaces one GUI iteration. That is generous to the
  GUI, because several probes measured things a person cannot see, such as
  per-frame change counts.

| Episode type | Measured | Without automation (low-high) | Saved (low-high) |
|---|---|---|---|
| Audio capture for the test MP3s (1) | about 3 min, 8 calls | 10-20 min (a manual GUI recording) | 7-17 min |
| TTD fixture re-record (2) | about 7 s run + about 1 min setup | 5 fixtures × 5-10 min = 25-50 min | 24-49 min |
| Triage of the user's instance (3) | about 3 min (incl. the hang) | 5-15 min (asking the user, checking their settings) | 2-12 min |
| Machine set-up for the user (4, 5) | seconds each | 1-3 min each for the user, 5 times | 5-15 min |
| **Live surfaces, total** | | | **about 0.6-1.6 h** |
| Probe and diagnostic runs in the headless harness | about 47 runs | 47 × 5-15 min = 3.9-11.8 h of GUI iterations | 3.9-11.8 h, minus the time to write 11 harnesses (not measured) |

**In short:**
- The live surfaces saved an estimated 0.6-1.6 hours. They were used 22 times.
  One call in five hit friction, costing about 5 minutes in total.
- The larger effect, an estimated 4-12 hours, came from running whole
  machines inside `core-tests`. That approach needs a C++ harness per
  question. It won here because the questions were about card internals that
  no endpoint exposed.
- A per-question harness has its own cost: writing and deleting 11 of them.
  An endpoint for the card's internals (SD trace, card RAM, frame change
  counts) would have made that work repeatable from MCP. Whether that would
  have been faster overall cannot be measured from these transcripts.

## 10. Lessons learned

- **Read the RTL before writing the design.** The 2026-09-19 sketch borrowed
  from another emulator and would have built the wrong DMA, flash, clock and
  class structure. The rewrite from the Verilog, then a review against the
  sources, found nine errors before any code was written.
- **Parallel source reviews pay off.** Four reviewers, about 12 minutes of
  wall-clock time, and the SPI restart and CMD59 alone would each have hung
  the SD boot.
- **Pin before you refactor.** Golden fingerprints of the classic card made
  phase 0 safe and caught a 24% slowdown at once.
- **Real software finds what unit tests do not.** The DC offset, the player
  hangs, the clock-switch bug and ZX-DMA use in *The Link* all came from
  running real programs or listening, not from the unit tests.
- **Not every hang is ours.** Two of the hardest problems (Neo Player v0.44,
  `npl044_dma`) were bugs in the software itself, proven from its sources.
- **A short branch still pays porting.** In 24 hours master changed under
  the branch six times; one fifth of the effort went into keeping up. Merging
  early and often kept each merge small; the one postponed merge became the
  largest.
- **Shared worktrees need careful staging.** One commit picked up another
  session's files. Stage by name.
- **Decide the TTD memory policy early.** The card's memory in checkpoints
  changed three times in two days. The final answer, "large memories wait for
  v2 regions", was the user's rule for v1 from the start.
