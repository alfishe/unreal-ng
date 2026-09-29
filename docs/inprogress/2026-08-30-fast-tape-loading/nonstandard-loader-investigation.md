# Non-standard loader tapes: investigation, prior art and fix proposal

**Date:** 2026-09-27
**Tracks:** PLAN #5 ([TODO.md](TODO.md))
**Status:** P1 and P4 implemented in `ac200bb8` (2026-09-27); P2, P3 (with the B4 restart fix) and the
sweep done 2026-09-28 (see [TODO.md](TODO.md)); O1-O3 open, O4 closed (ALEX_S is a 128K-only release)
(see [TODO.md](TODO.md))
**Code under discussion:** `core/src/emulator/io/tape/tape.cpp` at `47c40db2`

## 1. Summary

The 2026-09-16 report said that 6 of 18 custom-loader tapes fail, and blamed one mechanism: the
ERR_NR watchdog. A headless re-run with a proper outcome check (§4) shows a more varied
picture:

- **The ERR_NR watchdog does fire on custom loaders.** It fired in 6 of the 8 tapes tested. It
  broke one tape outright (DIZZY_X_EMELYANOV with fast loading off, on both 48K and 128K). In the
  other runs it happened to fire after the needed data had loaded, so the damage was hidden.
- **Stopping consumes the block that is playing.** That turns a false stop into data loss.
  Removing *either* the ERR_NR stop *or* the consume fixes EMELYANOV.
- **The deck does not wait for a busy loader.** With fast loading off, SAN-SAN plays every block
  while its loader is busy elsewhere and misses one. Freezing the tape sooner (after 1 s without
  reads instead of 3 s) fixes it on both models.
- **Restarting the ROM loader after a freeze replays the frozen block** from its start, even when
  that block was fully read. This is visible in the code and consistent with an experiment (§6, B4).
- **Several "failures" in the 2026-09-16 sweep were not failures.** The games were waiting for a
  key press at a loader prompt. The sweep never pressed a key. SAN-SAN's "dead key-wait loop at
  `PC=0x8ABD`" is the game's title screen waiting for a key; it reacts normally once a key is
  pressed.
- **Three failures are not explained by anything above** and stay open (§6, O1–O3).

**No other emulator surveyed (16 of them) uses ERR_NR, or any system variable, to control the
tape.** The robust ones never skip the block that is playing when they stop. They either keep the
exact position or rewind to the start of that block.

Proposal (§8): remove the ERR_NR stop, make every non-final stop keep its position, hold the tape
in the gap between blocks until the loader listens again, and fix the ROM restart after a freeze.

## 2. Terms used

| Term | Meaning |
|:--|:--|
| ERR_NR | System variable at `$5C3A`. The 48K ROM stores "error code − 1" there; `$FF` means no error. It is ordinary RAM, so any program can write to it. |
| Loader | The code that reads the tape. The *ROM loader* is `LD-BYTES` at `$0556` in the 48K ROM. A *custom loader* is a program's own routine in RAM. |
| Block | One record on the tape: a pilot tone (a long run of identical pulses the loader locks onto), a sync pulse, the data, then a silence. |
| In-flight block | The block the tape head is currently inside. |
| Consume | Our term for "move the tape cursor past a block so it is never played again". |
| Freeze / pause | Our read-gap watchdog: after 150 frames (3 s) with no reads of the tape port, the tape stops **in place**. Nothing is consumed. |
| Stop | `Tape::stopPlayback()`: a stop that consumes the in-flight block. Called at end of tape and by the ERR_NR watchdog. |
| Fast loading (fasttape) | Our ROM trap: when the ROM loader is called for a standard block, the block is copied into memory instantly with no signal. |
| Warp (turbotape) | Running the emulator flat-out while the tape signal plays. It changes wall-clock speed only, never emulated timing. |

## 3. How the tape engine starts and stops today

All references are to `core/src/emulator/io/tape/tape.cpp`.

| Mechanism | Code | What it does |
|:--|:--|:--|
| ROM anchor start | `handlePortIn`, `:586-600` | A read at PC `$0564` with the ROM byte there = `$1F` calls `StartPlaybackAtCursor()`, which plays the cursor block **from its first pulse**. |
| Sustained-poll resume | `handlePortIn`, `:541-562` | 256 port reads in one frame (joystick rows excluded) calls `ResumePlaybackAfterPoll()`, which continues a frozen position in place, otherwise starts at the cursor. |
| ERR_NR stop | `handleFrameEnd`, `:709-716` | If ERR_NR differs from the value captured when playback started, call `stopPlayback()`. |
| Read-gap freeze | `handleFrameEnd`, `:731-734` | More than 150 frames without a read calls `pausePlayback()`, which keeps the exact position. |
| Consume on stop | `stopPlayback`, `:97-100` | Moves the cursor past the in-flight block. |
| End of tape | `getTapeStreamBit`, `:844-848` | Calls `stopPlayback()` after the last pulse. |

The consume rule is justified in [design.md](design.md) §9.4: "a real tape keeps rolling during a
failed load — on retry the ROM resynchronizes on the *next* pilot tone". That is correct for a ROM
load the user aborted. It is wrong for a stop the emulator made up while a custom loader was still
reading.

The ERR_NR stop was also used as a "loading is over" signal for warp
([turbo-tape design](../2026-09-04-turbo-tape-loading/design.md) §E3). Its note at line 269
assumes that "a successful `0 OK` leaves ERR_NR at $FF". That is true for the ROM, but custom
loaders write the byte directly (§5).

## 4. Method

The 2026-09-16 sweep drove the Qt app over the WebAPI, typed `LOAD ""`, and judged a tape by
where the machine ended up. It never pressed a key after that. Many of these cracks stop at a
prompt ("INFINITE LIVES (Y/N)?", "PRESS YES", "1-cheat 2-normal"), and a game's title screen also
waits for a key. The sweep could not tell such a wait from a crash.

This investigation used a throwaway gtest probe that runs the real machine headlessly:

1. Boot 48K (dismiss the copyright screen) or Pentagon 128, then type `LOAD ""`.
2. Run up to 30,000 frames (10 minutes of emulated time). Tap **Y** every 500 frames: it answers
   the Y/N prompts and is not the BREAK key.
3. Log every change of tape state, cursor block and ERR_NR, with the PC at that frame.
4. **Liveness check:** hash the screen, press 0, 1, ENTER, SPACE and N in turn, and record whether
   the screen changed and where the PC went.
5. Dump the final screen and render all runs into one contact sheet per model.

The same run is repeated with switches compiled into a scratch copy of the tree: ERR_NR stop
off, consume off, fast loading off, and a different read-gap freeze threshold.

- 8 tapes: the 6 reported failures plus 2 known-good ones (CHEFRANOV, ALEX_S).
- 2 models: 48K and Pentagon 128.
- Up to 5 switch combinations.

The logs, the probe source, the switch patch and the contact sheets are in `scratch/tape-errnr/`
(git-ignored; see §10).

## 5. Results

Legend:
- **OK**: the game or demo runs and reacts to keys.
- **hang**: the ROM loader is still waiting for a signal after the tape ended.
- **BASIC**: the program fell back to the BASIC editor.
- **crash**: the screen is solid red and the machine is unresponsive.

### 5.1 48K

| Tape | fast on | fast on, no ERR_NR stop | fast off | fast off, no ERR_NR stop, no consume |
|:--|:--|:--|:--|:--|
| ALEX_S (control) | OK | OK | OK¹ | OK¹ |
| CHEFRANOV (control) | OK | OK | OK | OK |
| EMELYANOV | OK | OK | **hang** (ERR_NR stop) | **OK** |
| HACKER_SHURIK | OK | OK | OK | OK |
| KID__DR | OK² | OK | hang³ | hang³ |
| SAN-SAN | OK² | OK | **hang** | **hang** |
| TIMOFEY | BASIC | BASIC | BASIC | BASIC |
| echology | OK | OK | OK | OK |

¹ The liveness keys drop this program back to BASIC. Before the keys, the 48K runs show its
"1-cheat 2-normal" menu, so the load completes; the Pentagon runs were not checked before the
keys.
² The ERR_NR stop fires here but after the needed data. KID__DR stops at block 4 of 6 and SAN-SAN
at block 6 of 7. The tail blocks are the 128K extras of a "48/128k" release, which a 48K machine
never reads.
³ Ends on a "TO BE CONTINUED" screen and does not react to keys.

### 5.2 Pentagon 128

| Tape | fast on | fast on, no ERR_NR stop | fast off | fast off, no ERR_NR stop, no consume |
|:--|:--|:--|:--|:--|
| ALEX_S | OK¹ | OK¹ | OK¹ | OK¹ |
| CHEFRANOV | OK | OK | OK | OK |
| EMELYANOV | OK | OK | **hang** (ERR_NR stop) | **OK** |
| HACKER_SHURIK | OK | OK | hang | hang |
| KID__DR | crash | crash | crash | crash |
| SAN-SAN | OK | OK | **hang** | **hang** |
| TIMOFEY | OK | OK | OK | OK |
| echology | OK | OK | OK | OK |

### 5.3 EMELYANOV in detail (fast loading off)

The timeline is identical on 48K and 128K apart from a few frames:

```
f=    0  play  block 0        ERR_NR=FF   ROM loads the BASIC header
f=  305  play  block 1        ERR_NR=FF   ROM loads the 526-byte BASIC program
f=  557  ERR_NR FF->00                    written by the machine code in BASIC line 1
f=  557  stop  cursor 1->2                watchdog stop consumes the rest of block 1
f=  607  play  block 2 from its pilot     ROM anchor restart
...      blocks 2-5 play to the end
end      ROM loader still waiting at $05ED/$05F3 -> hang
```

Each change on its own turns the hang into a working game:
- **ERR_NR stop off:** the tape never stops.
- **Consume off:** the stop happens, block 1 replays from its start, the loader skips it because
  of its flag byte, then catches block 2 on time.

The consume is what changes the timing: block 2 starts 50 frames after the stop instead of at its
natural place after block 1's silence.

BASIC line 1 is `RANDOMIZE USR` into machine code stored in the same line ("HELLO HACKERS OF
S.-PETERSBURG!"). That code writes `$00` to ERR_NR just before it calls `LD-BYTES` with its own
flag bytes (`$07`, `$10`, `$11`, `$17`). No ROM error report is involved.

### 5.4 SAN-SAN with fast loading off: the deck does not wait

Nothing stops or pauses the tape. All 7 blocks play back to back, the tape ends, and the ROM
loader is still waiting at `$05F5`. The loader was busy (unpacking) while the next block's start
went past. Our TAP blocks have a fixed 1000 ms silence (`tape.cpp:887`), and a data block's
pilot tone lasts about 2 s. So a loader that is busy for more than about 3 s loses the next
block. The read-gap freeze only acts after 150 frames, which is also 3 s.

Lowering the freeze threshold in the probe gives these results:

| Freeze after | SAN-SAN 48K | SAN-SAN 128K | HACKER 128K | EMELYANOV |
|:--|:--|:--|:--|:--|
| 150 frames (current) | hang | hang | hang | hang (ERR_NR) |
| 50 frames | **OK** | **OK** | hang | hang (ERR_NR) |
| 10 frames | hang | hang | hang | hang |

With fast loading on, SAN-SAN works. That is because the trap serves the first blocks instantly
and later blocks start only when the loader polls for them. In other words, the tape waits for
the loader.

The 10-frame row breaks everything, including the tapes that pass at 150. §6 B4 is the likely
reason: a freeze during a block's trailing silence, followed by a ROM restart, replays the block
the loader has already read.

## 6. Findings

### Confirmed defects

**B1: the ERR_NR watchdog stops the tape on writes by custom loaders**
(`tape.cpp:709-716`).
- It fired in EMELYANOV, SAN-SAN, KID__DR, HACKER_SHURIK, CHEFRANOV and TIMOFEY. Written values
  seen: `$00`, `$07`, `$0B`, `$15`, `$19`, `$60`, `$DF`, from RAM code or from BASIC running a
  loader. The exception is TIMOFEY's `$02`, written at ROM `$11DE` just as it fell back to
  BASIC, which may be a genuine ROM report.
- It is harmful when it lands mid-load (EMELYANOV). It is silently harmless when it lands after
  the needed data.
- The baseline is re-read at every start and resume. So a loader that writes ERR_NR *before*
  playback starts gets away with it: CHEFRANOV writes `$DF` two frames before its first custom
  block.

**B2: a false stop consumes the in-flight block** (`stopPlayback`, `tape.cpp:97-100`).
- This amplifies B1: a stop that should cost nothing costs a block, or shifts every later block in
  time.
- Only Xpeccy does the same, and there the trigger is a ROM exit address, so it can't misfire
  (§7).

**B3: the deck does not wait for a busy loader.**
- The read-gap freeze (150 frames = 3 s) is as long as the silence plus pilot window of a TAP data
  block (1 s + about 2 s).
- A loader that spends 3 s or more between blocks loses the next block's start (SAN-SAN).
- A 50-frame freeze fixes SAN-SAN on both models.

### Defects found by reading the code

**B4: a ROM restart after a freeze replays the frozen block from its start**
(`handlePortIn`, `tape.cpp:586-600` → `StartPlaybackAtCursor`).
- `StartPlaybackAtCursor()` sets `_currentTapeBlock = nullptr` and plays the cursor block from
  pulse 0.
- After a freeze, the cursor block is the frozen in-flight block. So when the freeze happened in
  that block's trailing silence (all data already read), the ROM loader is fed the same block
  again.
- The sustained-poll resume handles this correctly (it continues in place), but the ROM anchor
  runs first.
- Evidence: the code, plus the 10-frame experiment in §5.4, where frequent freezes break tapes
  that load fine otherwise. Not yet isolated by a dedicated test.

**B5: TZX "stop the tape" blocks are inert.** `0x20` with a 0 ms pause and `0x2A` (stop if 48K)
are parsed but do nothing (`loader_tzx.cpp:1303-1305`, `:1387`). Every emulator that handles them
treats them as a stop that the next loader poll can resume, and only on 48K for `0x2A` (§7). Low
impact today; recorded for completeness.

### Corrections to the 2026-09-16 record

- **SAN-SAN:** "dies at block 6, dead key-wait loop at `PC=0x8ABD`". With fast loading on, the
  load completes and `0x8ABD` is the game waiting for a key; it reacts once a key is pressed. The
  real SAN-SAN defect is B3, with fast loading off.
- **KID__DR and HACKER_SHURIK:** stop at a loader prompt (PRESS YES / INFINITE LIVES Y/N) and
  continue once Y is pressed.
- **"Failures reproduce identically with fasttape and turbotape off":** not reproduced. With
  fast loading on and a key pressed, 6 of the 8 tapes load on 48K. The failures are specific to
  the signal path (fast loading off) or to one model.

### Open, not explained by B1–B4

- **O1: KID__DR on Pentagon 128** ends on a solid red screen in every configuration, including
  fast loading on. It works on 48K. The block-2 loader never polls the tape: the cursor stays at
  2 while the machine derails. Suspect 128K paging or memory contents that the crack does not
  expect.
- **O2: TIMOFEY on 48K** returns to BASIC in every configuration. It works on Pentagon. Possibly
  a 128K-only release; check the loader before calling it a defect.
- **O3: HACKER_SHURIK on Pentagon, fast loading off** hangs even with a 50-frame freeze. It works
  with fast loading on.

## 7. Prior art

These emulators were surveyed; source trees are in `emulators/github/`, and Fuse's 1.10.0 master
and 1.6.0 were fetched from SourceForge:
- Fuse
- SkoolKit
- Spectral
- zxsp
- BizHawk (ZXHawk)
- pico-spec
- Zero
- Xpeccy
- xpeccy-plus
- Unreal Speccy (0.39 line, zx-evo-unreal, UnrealSpeccyP)
- ZXMAK2
- ZX-M8XXX
- MAME
- ZXSpeculator
- 8BitAnalysers

**No emulator reads ERR_NR or any other system variable to control the tape.** Grep hits are
limited to snapshot setup (SkoolKit `tap2sna.py:148`), a debugger label table (Zero
`Monitor.cs:47`) and a commented-out line (Spectral `zx.h:2133`).

| Emulator | Starts the tape when | Stops the tape when | Position after a stop |
|:--|:--|:--|:--|
| Fuse 1.10 | 10 loader-like reads: ≤500 T apart, B ±1, or an opcode signature (`loader.c:199-239`) | 10 **non-loader** reads in a row; never on silence (`LOADER_STOP_NON_EAR_READS`) | exact pulse kept (`tape.c:407`, `:369`) |
| xpeccy-plus | ROM trap, or 10 reads ≤500 T apart from the same PC in RAM (`tape.c:526-590`) | 1 frame of non-loader reads; **no no-read timeout**: "a loader … unpacking what it has read … is waited for" (`tape.c:543-546`) | exact pulse kept (`tape.c:430-446`, `:473-479`) |
| SkoolKit `--sim-load` | the next port read from loader code; the tape clock **jumps to the next block's first edge** (`loadtracer.py:402-408`) | end of tape only; pauses between blocks by default (`:478`) | never stops mid-block |
| BizHawk | 16 steps: same PC, <96 T apart, one register ±1 (`DatacorderDevice.cs:680-734`) | 50-frame timeout, never inside pause blocks (`:762-825`) | block restarts from its start (`:128-141`) |
| ZXMAK2 | 8 such steps (`TapeDevice.cs:507-541`) | 50 frames without loader reads (`:544-552`) | block restarts from its start (`:452-467`) |
| Zero | 8 such steps (`zxSpectrum.cs:6589-6660`) | 100-frame timeout (`:277`) | block restarts from its start (`:8561-8570`) |
| Spectral | more than 200 whitelisted reads per second (`zx.h:1534-1575`) | 9 or fewer per second, decided once per second: "stopping a tape is a risky action" (`:917-925`) | position kept; silence is not played while nobody reads (`zx_tap.h:275-278`) |
| pico-spec | 200 same-PC reads + an opcode check (`Tape.cpp:2639-2657`) | only on entering a pilot block (`:2581-2593`) | block restarts from its start (`:1057-1070`) |
| Unreal Speccy family | ROM PC `$0564` + ROM byte check (`io.cpp:999`) | never automatically (end, stop marker, F7) | block restarts from its start (`tape.cpp:116-125`, `:186`) |
| zxsp | ROM entry trap | ROM return trap (acts as pause) | position kept |
| Xpeccy | ROM PC `$56C`/`$5E7` | ROM PC `$5DF`/`$53A`, then **next block** (`ethread.cpp:152-156`) | skips the block, as we do, but ROM-only |

Lessons, strongest first:

1. **Never infer tape state from RAM contents.** Where a "ROM gave up" signal exists, it is
   keyed to a ROM address with the ROM paged in (Xpeccy, zxsp).
2. **A stop keeps the position** (Fuse, xpeccy-plus, Spectral, zxsp), **or rewinds to the start
   of the block** (BizHawk, ZXMAK2, Zero, pico-spec, Unreal). Skipping the block is acceptable
   only on a trigger that is certain the ROM gave up.
3. **Silence on the port does not mean "done".** Fuse and xpeccy-plus have no no-read timeout at
   all. The others that do use it only to *pause*, and several refuse to stop inside a gap or
   pause block.
4. **The deck waits for the loader between blocks.** SkoolKit jumps the tape to the next block's
   first edge when the loader polls again. Spectral does not play silence while nobody reads.
   pico-spec only stops at a pilot boundary. This is the general fix for B3.
5. **Detect a loader by what it does, not by counting reads.** The standard test is: same PC,
   reads a few hundred T apart, one counter register moving by ±1 (Fuse, ZXMAK2, BizHawk, Zero).
   Some add a check of the opcodes after the IN (`RRA`, `AND #20/#40`, `BIT 6,A`), or exclude
   ROM reads.
6. **zxsp's author disabled auto start/stop** on file open, because it "stops at the first custom
   block" (`MachineController.cpp:640`). Spectral turns autostop off for tapes with several stop
   blocks. Conservative defaults are the norm.

### 7.1 xpeccy-plus re-check (2026-09-27, commits 2026-09-18..27)

The table above was read from a tree that already had `5e6c283e`. Its history since 2026-09-18
adds these points:

| Commit | Change | What it means for us |
|:--|:--|:--|
| `5e6c283e` | Start and stop are decided per port read. Stop needs a whole frame (69888 T) of reads that are neither a loader's nor a keyboard scan. Silence never stops the tape. A stop keeps the exact pulse; play continues there only if nothing moved the tape since, otherwise the block starts over with a 0.5 s lead-in | Confirms P2/P3; the "nothing moved" test goes into P3. An interrupt that scans the keys in the middle of a load (Joe Blade 2) must not stop the tape |
| `zx_in_use()` in `hardware/common.c` | Classifies an `IN` from `#FE` by the code after it: `AND #40`, `BIT 6,A`, `RRA; AND #20`, `RRA; XOR C; AND #20` = EAR test (loader); `AND` with bits 0-4 only, `BIT 0..4,A`, `OR #E0` = keyboard scan. The EAR test wins, because a loader may check BREAK from the same `IN` | The classifier P4 needs |
| `77bbeee2` | After fast loading hands over a block, a following custom block is *armed*: play is pressed on the first read of the port from RAM. Pressing it at once let SpeedLock's block run past while the loader was still setting up | Same rule as P4 ("the tape moves when the loader asks"), there only in fast-loading mode |
| `08093597` | Reads from ROM never start the tape: TR-DOS calls the 48K ROM's BREAK-KEY over and over while it works the disk | Our poll resume counts ROM reads; the 256 threshold avoids this by luck, not by design |
| `529c8201` | The level change that ends the last pulse of a tape was swallowed by the stop (Deflektor) | Check our end of tape with a test while P2 touches `stopPlayback()` |

xpeccy-plus does **not** hold the tape in the gap between blocks in signal mode: a busy loader that
reads nothing is "waited for" only in the sense that nothing stops the tape, which keeps rolling.
Its TAP pause is also 1 s (`filetypes/tap.c:57`), so it most likely shares B3 with fast loading
off. Not verified by running it.

## 8. Proposal

**P1: remove the ERR_NR stop** (fixes B1).
- Delete the check in `handleFrameEnd` and the `_initialErrNr` field; see the TTD note in §9 step 1.
- Warp, the only consumer of this stop, stands down on the read-gap freeze. **Correction
  (2026-09-27, found while implementing P1):** after a BREAK the ROM editor's interrupt keeps
  scanning the keyboard, and every such read fed the watchdog (B6; fixed by P4 in `ac200bb8`). Until P4, a
  BREAK leaves the tape rolling to its end, with warp on if turbo tape is enabled. P4 makes key
  reads "not listening" and closes this.
- If a faster "ROM gave up" signal is wanted later, key it to the ROM error entry with the 48K ROM
  paged and verified by its bytes, never to RAM.

**P2: a stop never consumes a partly played block** (fixes B2).
- After P1, the only remaining caller of the consuming stop is end-of-tape, which has nothing to
  consume.
- Remove the consume from `stopPlayback()` and make any future non-final stop a freeze, as
  `pausePlayback()` already does.
- Update [design.md](design.md) §9.4: the "keeps rolling" argument only applies to a user abort,
  and the ROM re-syncs on the next pilot anyway.

**P3: fix the ROM restart after a freeze** (fixes B4). When the ROM anchor fires and a frozen
position exists:
- **Frozen after the block's last data edge** (in its trailing silence): start at the *next*
  block's pilot. The loader has already read this block.
- **Frozen mid-data:** rewind to the start of the frozen block, which is the Unreal/BizHawk/ZXMAK2
  behaviour. A ROM loader restarting mid-block would otherwise sync on garbage.
- **Frozen mid-pilot:** restart the pilot from its start. A partial pilot can be too short for
  the ROM or for a loader that times it ([loader-follow-design.md](loader-follow-design.md) §5.4).

The sustained-poll path already continues in place. It keeps doing so, except in the
trailing-silence case, which it should treat the same way (skip the silence and go to the next
pilot). Continuing in place is only valid if nothing moved the tape since the freeze (a rewind,
a block pick, a new image); otherwise the cursor block starts over (xpeccy-plus `5e6c283e`).

**Requirement R (user, 2026-09-27).** A load that stops listening to the tape, to wait for a key
("press any key", "INFINITE LIVES Y/N?") or to play beeper or AY music, must work however long
the pause lasts. Playback resumes by itself as soon as the program polls the EAR bit again.

Before `ac200bb8` this was broken (**B6**, from the code; fixed by P4):
- While the tape plays, *every* port read resets the read-gap counter (`tape.cpp:531`),
  including a key-wait loop and the ROM's keyboard scan in the 50 Hz interrupt. With interrupts
  on, the freeze never fires, and the tape keeps rolling through the wait.
- A frozen tape resumes on 256 reads in one frame (`TAPE_EAR_POLL_RESUME_THRESHOLD`). A tight
  key-wait loop makes about 2000, so it can resume the tape while the program is still waiting.

**P4: the tape moves only while a loader listens** (fixes B3 and B6, meets R; merges the former
P4 gap hold and P5 loader test). Full algorithm with diagrams:
[loader-follow-design.md](loader-follow-design.md). Summary:
- **Classify each read** of the tape port:
  - *EAR read*: the code after the `IN` tests the EAR bit (the `zx_in_use()` patterns in §7.1),
    or the ROM edge loop, or reads that follow the counter pattern (same PC, a few hundred T
    apart, one register ±1; §7 lesson 5) for loaders the pattern table misses;
  - *keyboard read*: the code after the `IN` masks key bits only;
  - *other*: anything else. Reads from ROM other than the loader entry never count as EAR
    reads (TR-DOS, `08093597`).
- **Pause:** N consecutive frames with no EAR read, whether the port is silent or only keyboard
  and other reads happen. N is at least 1 frame (so one interrupt keyboard scan inside a load
  cannot pause it) and is set by the tests: the smallest N that pauses reliably with no false
  pause across the fixture sweep. The gap and mid-block may end up with different N.
- **Where it pauses:**
  - in a block's trailing silence: park at the *start of the next block's pilot*;
  - mid-pilot or mid-data: keep the exact position.
- **Resume:** on the first EAR reads, at the parked or frozen position. Keyboard and other reads
  never resume the tape. This replaces the 256-reads threshold.
- This makes the signal path behave like the fast-loading path, where blocks already start when
  the loader asks. The 50-frame experiment in §5.4 is a crude version that already fixes SAN-SAN.

**P5 (later): honour TZX `0x20`(0)/`0x2A` as resumable stops** (B5). Do it when a tape needs it.

## 9. Plan

Each step: a test that fails first, then the fix, then the full suite. Every test is synthetic
(a small TAP and a few bytes of loader code poked into RAM, as the turbo-tape integration suite
already does), so each stays near the 50 ms budget.

1. **P1: ERR_NR stop removed.**
   - Test: a RAM loader writes `$5C3A` between two `LD-BYTES` calls with custom flags; both
     blocks must load.
   - Remove the check and `_initialErrNr`.
   - The TTD serialization carries this byte at offset 44 (`tape.cpp:982`, `:1026`, `:1043`):
     keep the slot as reserved, or bump the tape state version. Decide according to the TTD v2
     `PeripheralId` rules (PLAN #40).
2. **P2: no consume on stop.**
   - Test: freeze mid-block, restart through the poll path; the block's bytes arrive intact.
   - Remove the consume.
   - Update [design.md](design.md) §9.4 and §12.1-8, and the note in the turbo-tape design (lines
     269-270).
3. **P3: ROM restart after a freeze.** Three tests:
   - freeze in the trailing silence, then `LD-BYTES` loads the *next* block;
   - freeze mid-data, then `LD-BYTES` loads the *same* block;
   - freeze mid-pilot, then the load succeeds.
4. **P4: the tape moves only while a loader listens.** Tests, all with fast loading off; in each,
   a RAM loader reads block A, does something else for 5 s (longer than silence plus pilot),
   then reads block B, which must load:
   - busy with no port reads (unpacking), interrupts off;
   - a key-wait loop (`IN A,(#FE); AND #1F; CP #1F; JR Z`), then a key; also: the tape is
     parked at block B's pilot during the wait, and the loop does not resume it;
   - AY music from an IM2 interrupt with the ROM keyboard scan running;
   - a beeper tune (`OUT (#FE)` only);
   - negative: an interrupt keyboard scan inside a block does not pause it;
   - negative: a loader that checks BREAK from the same `IN` counts as listening;
   - negative: ROM reads outside the loader (TR-DOS BREAK-KEY) never start the tape.
   - Choose N from the sweep in step 5.
5. **Regression sweep with the real fixtures.** Re-run the probe matrix (§4) on 48K and Pentagon.
   Required outcomes:
   - every tape with a prompt loads whether the key comes after 1 s, 10 s or 60 s (R);
   - EMELYANOV and SAN-SAN are OK with fast loading off, on both models;
   - nothing that is OK today regresses;
   - O1–O3 are recorded as they stand.

   Because a full run takes minutes, it belongs in `tools/verification/` as a script, not in
   `core-tests`.
6. **Close-out.** Update [TODO.md](TODO.md) and PLAN #5. Open O1–O3 as a follow-up item if they
   survive the fixes, then restore `DONE.md`.

Effort: steps 1–3 are small (under a day with tests); step 4 is medium; step 5 reuses the
existing probe.

## 10. Evidence

In `scratch/tape-errnr/` (git-ignored):

- `tapeerrnrprobe_test.cpp`: the probe (drop into `core/tests/emulator/io/tape/` and re-run CMake).
- `probe-switches.patch`: the `PROBE_NO_ERRNR`, `PROBE_NO_CONSUME` and `PROBE_GAP_FRAMES`
  switches for `tape.cpp`. Scratch-only, never for master.
- `probe2-*.txt` (48K) and `probe3-p128-*.txt` (Pentagon): logs of the §5 matrix.
  `probe4-gap-experiment.txt`: the §5.4 threshold runs.
- `screens-48k/sheet.png`, `screens-p128/sheet.png`: final screens after the liveness check.
  Rows are the tapes in alphabetical order; columns are fast on, fast on + no ERR_NR stop, fast
  off, fast off + no ERR_NR stop + no consume.

Environment: probe built from `47c40db2` in a separate worktree, because master's working tree
did not compile `core-tests` at the time (uncommitted video changes removed
`ScreenZX::DrawAlcoMode`, which `atm_video_modes_suite_test.cpp` still calls).

Side note: `BasicEncoder::runCommand` cannot type into a real 48K model. It sees ROM page 0 and
assumes the 128K menu. The probe works around it with `injectTo48K` + `injectEnter`.
