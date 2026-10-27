# CL-2: real-program bus traces of the ZX-MultiSound

| | |
|---|---|
| **Date** | 2026-10-08 |
| **Branch** | `ms-cl2-traces` |
| **Design** | [tdd-card-logic.md](tdd-card-logic.md) §4, §8; [TODO.md](TODO.md) item "CL-2" |
| **Data** | `testdata/sound/multisound/traces/` (traces, capture scripts, RTL hashes) |
| **Tests** | `core/tests/emulator/slots/cards/multisound/multisoundtrace_test.cpp` (`MultiSoundTrace_Test`), capture driver `multisoundtracecapture_test.cpp` |

Five real programs ran on emulated machines with the ZX-MultiSound in a ZX-bus slot. Every bus cycle the card's CPLD
saw, from the Spectrum's bus and from the General Sound's own CPU, was written to a trace together with what the
program read back and when. Each trace is then played into our card logic and into the card's own CPLD source in
Verilator, and replayed into a fresh emulated card. Result: **every cycle and every read agrees** (208 176 cycle
lines, 46 469 of them reads), every polling loop of the programs ends the same way on the
replayed card, and the audio of each replay is pinned by golden digests plus content checks. No model bug was found;
the traces did show three things about the programs themselves (section 3).

## 1. Method

### 1.1 The card on the bus

The card is fitted through the slots, as a user fits it: a shipped config (`data/configs/<machine>/unreal.ini`) with
its `[SLOTS]` section replaced by `zxbus.1 = multisound` (all DIP functions on, `ctrlMask = pro`, 1 MB GS RAM). The
shipped configs keep the card off; the test helper `multisoundtest::StagedMachine` writes the staged config to the
scratch folder. Machines: Pentagon 128 (`pentagon128k`), ZX-Evo BaseConf (`atm3`) and TS-Conf (`ts-conf`, its CPU at
14 MHz under Wild Commander). On the ZX-Evo and TS-Conf the card takes the board's YM2149 out of its socket (slots
Q7).

### 1.2 The software

Third-party programs, not in the repository: the untracked folder `testdata/sound/multisound/software/` (its
README lists every image with its origin in the owner's collection and the keys that run it). The capture loads them
as a user would: a TR-DOS image into drive A and `RUN "name"` through TR-DOS (`TRDOSTestHelper::startCommand`: a reset
into TR-DOS with the command typed for it), the ZX-Evo's boot menu by keys, or Wild Commander from an SD card image
(`wc/wc-multisound.img`, TS-Conf) driven with PC keys.

### 1.3 Capture

- **Hook** (`core/src/emulator/slots/cards/multisound/multisoundbustrace.h`): `MultiSoundCard::SetBusTrace(sink)`
  gives the sink one `MultiSoundBusEvent` per host IN / OUT the card receives (port, value, whether the card drove
  the bus, card time, the M1 address of the IN / OUT instruction), per bus reset, per frame start / end, and - through
  `IGSBusObserver` on `SoundChip_GeneralSound` - per GS CPU port cycle (with the value the GS CPU got) and DAC fetch
  (GS read at `#6000-#7FFF`). Off by default: one pointer test per host port cycle, per GS port cycle and per GS DAC
  fetch, nothing else.
- **Order**: causal, as the emulator computed it. The card runs its GS up to a host cycle's time before that cycle
  touches shared state (mailbox, DACs), so the GS cycles that came first are in the trace first.
- **Writer** (`MultiSoundTraceWriter`, `core/tests/_helpers/multisoundscenario.h`): writes the card logic scenario
  format (`.msc`, the same format as the hand-written CL-0 scenarios, extended; reference in
  `tools/verification/multisound/README.md`, "Real-program traces"):

  ```
  m1 64D7                  the IN / OUT instruction's address (the M1 context; the ROM-fetch lock follows from it)
  in FFFD =80 *6 +61       a read: what the program got (=XX, or =-- when nobody drove), 6 identical reads, 61 T later
  out BFFD 00 +26          a write, 26 T after the previous host cycle
  gin 0004 =7E *223        the GS CPU read its port 4 223 times and got #7E
  gmr 6000 80              a GS DAC fetch
  frame-end / frame-start  the card's frame calls
  ```

  Times are card-axis ticks (3.5 MHz T-states on every machine, header `rate`) from the card's power-on. To stay small,
  identical reads without side effects collapse (`*N`), DAC fetches that do not change their channel are dropped, and
  only the first 4096 changing DAC fetches are kept.
- **Driver**: `MultiSoundTraceCapture_Test.DISABLED_Script` runs a capture script
  (`testdata/sound/multisound/traces/<name>.script`: machine, media, keys, frames) and writes `scratch/<name>.msc` and
  the stored form `scratch/<name>.msc.zst` (zstd level 19). Every trace starts at the card's power-on, so a fresh card
  can replay it.

### 1.4 Storage decision

The programs cannot be committed (third-party, not freely distributable) and the CI has no copy, so the owner's
preferred form - fixtures generated in the test from the software - is not possible here. The traces are stored
instead: zstd-compressed scenario text, 67 KB for all five (3.2 MB uncompressed); the scripts that produce them are
stored next to them. The traces carry what the programs sent the card, which includes register streams of the tunes
and, for the MOD player, the 5120-byte module file uploaded to the GS; this is the same kind of derived data as the
existing `tfm-player-trace.msc`.

### 1.5 Checks

For each trace (`MultiSoundTrace_Test`):

1. **Logic vs RTL vs the emulator's reads.** `mscosim trace` (Verilator, `tools/verification/multisound/`) plays every
   line into the card's CPLD (`top.v` at `d7f3ac2`) and into `MultiSoundLogic`, compares the full records (IORQGE, YM /
   SAA / SounDrive strobes, driven read value, GS memory map, every latch, flags, DAC registers) and checks every read
   the program made against the RTL: driven or not, and the value - GS status, GS reply, GS port reads (`#FF` for
   undecoded ports) - except where the YM2203 answers (the testbench's stand-in drives a marker; the chip is not part
   of the CPLD). It freezes the RTL records as an FNV-1a hash chain, checkpoint every 8192 cycles (`<name>.rtl`). The
   core test replays the trace into `MultiSoundLogic` and checks the same reads and the hash chain.
2. **The card reproduces the program's run.** The host lines are replayed into a fresh `MultiSoundCard` at their
   times with the frame calls where they were. Every read must return what the program got - so every polling loop
   ends where it ended in the live run - and the card's own trace of the replay (its GS running the real GS 1.05b
   firmware from power-on) must equal the stored trace line for line, GS side included.
3. **Audio.** The rows of the replay: golden digests of the rows the program uses, all other rows digital silence,
   and per-program content checks (levels, L / R split, the MIDI bytes against the file).

### 1.6 Reproduce

```bash
tools/build/build.sh core-tests
# capture (needs the untracked software folder); writes scratch/<name>.msc and .msc.zst
MS_CAPTURE_SCRIPT=testdata/sound/multisound/traces/gs-modplayer.script \
  cmake-build-agent-release/bin/core-tests --gtest_also_run_disabled_tests \
  --gtest_filter='MultiSoundTraceCapture_Test.DISABLED_Script'
cp scratch/gs-modplayer.msc.zst testdata/sound/multisound/traces/
tools/verification/multisound/regenerate.sh --scenarios-only     # every scenario and trace through the RTL
tools/verification/multisound/trace-stats.py testdata/sound/multisound/traces/gs-modplayer.msc.zst
tools/build/test.sh --gtest_filter='MultiSoundTrace_Test.*'
```

The capture is deterministic: the same script gives the same trace. `MultiSoundTrace_Test.DISABLED_PrintReplay`
(`MS_TRACE=<name>`) prints a replay's digests and levels.

## 2. Programs

### 2.1 TFM Music Compiler player: "uzhos" (YM2203 status polling)

- **Software**: `tsfm/uzhos.scl` (collection `software/music/multisound/tsfm/uzhos.zip`), a TFM Music Compiler tune
  with its player. Pentagon 128; `RUN "uzhos"`. Script `tfmc-uzhos.script`.
- **Trace**: `tfmc-uzhos.msc.zst`, 260 frames (5.3 s; the tune starts 2.4 s in, after loading), 8685 cycle lines,
  2.6 KB.
- **What it does**: every frame it selects chip 0 with `#F8` (FM on, status mode), writes its registers, then `#F9`
  for chip 1. Before every address write and every data write it reads `#FFFD` until bit 7 (busy) is clear:

  ```
  t=8465799 PC=64F8 out FFFD F8        control byte: U4, status mode, FM on, SAA clock off
  t=8465874 PC=64D7 in  FFFD =00       free
  t=8465896 PC=64DC out FFFD 0D        register 13
  t=8465912 PC=64DF in  FFFD =00       free
  t=8465938 PC=64E5 out BFFD 00        data
  t=8465999 PC=64D7 in  FFFD =80 *6    busy, six polls 22 T apart
  t=8466131 PC=64D7 in  FFFD =00       free: the next register
  ```

- **Statistics**: 7149 status reads, 1167 `#FFFD` and 887 `#BFFD` writes; 760 busy loops at 14 instructions, 5-8 reads
  each, every one ended by the flag clearing (`#80` -> `#00`); busy after a data write lasts 171-193 T, the 192 T
  (32 x 6 master clocks) of the TSFM hardware reference §2; register pairs 276 T apart. The GS side: the firmware's POST
  (page register sweep `#3F`..`#00`), then its idle loop polling port 4 (456 405 reads in 210 lines).
- **RTL**: 0 differences; 2657 read lines, all driven by the selected chip (the 73 GS port reads also by value).
- **Replay / audio**: every status read reproduced (busy where the program saw busy); FM 1 RMS 1124, FM 2 RMS 725;
  digests pinned; SSG (this tune uses none), SAA, DAC, MIDI silent.
- **Tests**: `MultiSoundTrace_Test.TfmCompilerPlayerPollsTheBusyFlag`, `.TfmCompilerPlayerReplaysOnTheCard`.

### 2.2 Mod Player v2.5 (General Sound MOD player, the mailbox protocol)

- **Software**: `gs/mplv2_5.trd` (collection `software/music/multisound/general-sound/mplv2_5 .trd`). Pentagon 128;
  `RUN "M_PLv2.5"`, `a` (the list), `a` x6 down to "gameover", Space. Script `gs-modplayer.script`.
- **Trace**: `gs-modplayer.msc.zst`, 640 frames (13.1 s, playback from 10.2 s), 73 824 cycle lines, 40 KB.
- **What it does** (GS command numbers per the GS command table): `#F4` (cold reset of the GS), `#23`, `#20`
  (memory size: "TOTAL RAM #03F0Kb" on screen), `#F3`, `#30` (load module), `#D1` (open stream), the module file through `#B3`
  (5120 bytes in 0.1 s), `#D2` (close stream), `#31` (play), then every frame `#60`, `#61`, `#63`, `#64` (position, pattern, notes,
  volumes) and 8 reply bytes for the display. Each command: OUT `#BB` at `#6C1B`, poll `#BB` at `#6C1D` until the
  command flag is clear and the data flag set (`#7F` -> `#FE`), IN `#B3`:

  ```
  t=40710676 PC=6C1B out 61BB 61
  t=40710687 PC=6C1D in  61BB =7F      command flag set: the GS has not taken it yet
                     gin 0001 =61      the GS CPU reads the command
                     gout 0003 05      ... answers (data flag set)
                     gout 0005 05      ... and clears the command flag
  t=40710762 PC=6C1D in  BFBB =FE      answer ready
  t=40710797 PC=6C46 in  7FB3 =05
  ```

  The upload polls `#BB` once per byte at `#6A2D` and always finds the data flag already clear (`#7E`): the GS
  firmware at 16 MHz takes a byte in less than the player's 60 T per byte.
- **Statistics**: 471 376 `#BB` reads (835 runs of identical reads, 702 of them waits ended by the GS's answer; the
  others are repeated status reads at `#7564` that the program leaves), 1315 `#B3` reads, 5122 `#B3` and 664 `#BB`
  writes. The longest wait: 404 419 polls over 2.9 s after `#23`, while the GS firmware still runs the POST of the cold
  reset `#F4` - the player has no time-out. GS side: 856 232 port 4 reads, 5128 port 2 reads (the uploaded bytes),
  1453 port 3 replies, 666 port 5 acknowledges, 443 x 4 volume writes, 5694 page writes, 208 354 DAC fetches (4096
  stored).
- **RTL**: 0 differences; all 40 441 read lines compared by value - every status byte, reply byte and GS port value
  the emulator's General Sound produced is what the CPLD drives.
- **Replay / audio**: the replayed GS (firmware from power-on, 640 frames) answers every poll and reply as in the live
  run and performs the same port cycles and DAC fetches; PCM row L RMS 1264 / R 2220, L / R correlation below 0.5
  (two different channel pairs: hard left / right), digest pinned, no late DAC events.
- **Tests**: `MultiSoundTrace_Test.GsModPlayerMailboxAgreesWithTheRtl`, `.GsModPlayerReplaysOnTheCard`.

### 2.3 Ball Quest (`#F0`-`#F7` written as register numbers, issue #11)

- **Software**: `tsfm/BQ.TRD` (collection `software/games/multisound/ballques.zip`). ZX-Evo BaseConf (`atm3`); Evo
  Reset Service: `y` (virtual drive B), Enter (TR-DOS boot), Enter (`BQ   ATM`); the in-game demo runs by itself.
  Script `ballquest.script`.
- **Trace**: `ballquest.msc.zst`, 1595 frames (31.9 s), 115 230 cycle lines, 19 KB.
- **What it does**: a TurboSound player: every frame `#FE` / `#FF` (chip select, register read mode) and AY registers
  of both chips (30 795 `#FFFD` and 26 230 `#BFFD` writes, 14 T apart: no polling), 260 register read-backs at `#8E58`
  and `#8EDF`. Four times it writes a register number of `#F0` or above:

  ```
  t=100083956 PC=8ECB out FFFD F2      (28.6 s)
  t=106233978 PC=8E9E out FFFD F0      (30.4 s; again at 30.6 s twice)
  ```

  With the card's firmware (`ctrlMask = pro`) each is a control byte: chip 0, FM unmuted, until the next `#FE` /
  `#FF` - 187 T later once, a whole frame (67 500 T, 19 ms) later three times: the clicks of issue #11 on a real card.
- **RTL**: 0 differences with `pro` (1666 read lines; 1403 values compared: the ones the GS drives); the same trace on
  the issue #11 firmware (`classic`, SAA off, `ballquest-classic.rtl`) also agrees, and there the four writes leave FM
  muted.
- **Replay / audio**: SSG 1 / SSG 2 RMS 532-778, digests pinned; FM rows digital silence (no FM voice is programmed,
  the click itself is not modeled: owner decision 2026-10-05, tdd-integration §6.1).
- **Tests**: `MultiSoundTrace_Test.BallQuestControlBytesProAndClassic` (4 unmutes with `pro`, 0 with `classic`, both
  against their RTL), `.BallQuestReplaysOnTheCard`.

### 2.4 VGMPLAY.WMF (Wild Commander plugin, YM2203 and SAA1099)

- **Software**: `wc/wc-multisound.img` (Wild Commander 1.11 RC7 + `VGMPLAY.WMF` v0.9.03-beta, from
  `testdata/machines/tsconf/wildcommander/`). TS-Conf at 14 MHz; WC: Enter (`music`), Down x5 + Enter (`vgm`), Down x3 +
  Enter (`ym2203`), Down + Enter (tune 01), 3 s, Esc; Home, Enter, Up + Enter (`saa`), Down x3 + Enter
  (`saa-tones.vgm`), 3 s. Script `wc-vgmplay.script`.
- **Trace**: `wc-vgmplay.msc.zst`, 1152 frames (23.6 s; the first tune starts 13 s in, after Wild Commander's start from the SD card), 7411 cycle lines, 3 KB.
- **What it does**: no reads at all - it paces its writes instead of polling (YM2203 writes 110-120 T apart, SAA
  122-123 T). Every YM register write is a triplet: control byte `#FA` (chip 0, FM on), register, data (1017 x).
  For the SAA it writes `#F6` (SAA clock on), then `#F3` at its register-write instruction (`#AEA7`: the control byte
  path again), then 48 address / data pairs at `#01FF` / `#00FF`.
- **RTL**: 0 differences (965 GS read lines compared by value); the SAA clock starts.
- **Replay / audio**: FM 1 RMS 385, SSG 1 RMS 208, SAA RMS 632 / 614; chip 1 (U10) silent; digests pinned.
- **Tests**: `MultiSoundTrace_Test.VgmPlayAgreesWithTheRtl`, `.VgmPlayReplaysOnTheCard`.

### 2.5 Wild Commander MIDI player (GSPLAYER.WMF, MIDI on U4 port A)

- **Software**: the same SD image; `GSPLAYER.WMF` in MIDI mode, `wc.ini` `-midi_chip=2` (the `#FE` chip = U4, the
  card's MIDI chip; the player shows "Select output: Second AY"). Keys: Enter (`music`), Down x2 + Enter (`mid`), Down
  x18 + Enter (`scale.mid`, generated: C major up and down on channel 1). Script `wc-midi.script`.
- **Trace**: `wc-midi.msc.zst`, 927 frames (19.0 s; the file plays from 14.7 s), 3026 cycle lines, 1.4 KB.
- **What it does**: `#FE`, R7 (port A output), then R14 once per bit level change at `#8D51` / `#8D6D`: 971 writes,
  107-114 T apart (median 114 T = 30 700 baud, 1.8 % slower than 31 250 baud, 112 T; within the receiver's tolerance).
- **RTL**: 0 differences (740 GS read lines by value); 971 data writes reach U4.
- **MIDI content**: the line decoded from the logic's routing of the trace (R7 + R14 bit 2, 8N1) carries 96 bytes:
  `F0`, `C0 00`, `B0 07 64`, then note on / off for C4 D4 E4 F4 G4 A4 B4 C5 B4 ... C4 - the file, except that the
  plugin sends only the `F0` of the file's GM System On SysEx (`F0 7E 7F 09 01 F7`) and skips its body. One framing
  error: the break at the start, R7 makes port A an output while R14 bit 2 is still 0 (the same on a real card,
  tdd-integration §6.1).
- **Replay / audio**: the synthesizer receives the same 96 bytes with the same single framing error and plays them
  (MIDI row RMS 542, digest pinned with the test bank).
- **Tests**: `MultiSoundTrace_Test.WcMidiPlayerAgreesWithTheRtl`, `.WcMidiPlayerReplaysOnTheCard`.

### 2.6 Not traced

- Other TSFM players with status polling (TFM Music Compiler 1.12 from `TSFM-EL.TAP`, Wild Player's TFM mode): the
  same player family as 2.1; the TSFM-EL player's writes are already the CL-0 scenario `tfm-player-trace.msc`.
- SounDrive players and the shared GS / SounDrive DACs under a real program: not a CL-2 target (no reads); the shared
  DACs are covered by `MultiSoundCard_Test` and the MS-7 runs.
- Programs the earlier MS-7 passes found are not card programs (S98 Player, ZXM-SoundCard SAA programs) or could not
  be driven (Nedodemo): nothing to trace.

## 3. Analysis

- **Status polling.** The TFM Compiler player relies on the YM2203 busy flag before every write: 760 loops in 5 s,
  each ended by the flag clearing 171-193 T after the data write. A model whose busy flag never cleared would hang it;
  one that never set it would change its timing. The card replay shows the busy flag sets and clears at the same
  T-state as in the live run. VGMPLAY and Ball Quest do not poll: they pace (110-123 T) or write back to back (14 T),
  so they depend on the YM2203 accepting writes that fast (the model does; the real chip's busy time is not checked by
  them).
- **GS command protocol.** The MOD player uses the classic handshake on both flags: command flag (bit 0) for "taken",
  data flag (bit 7) for "answer ready", and reads a reply only after `#FE`. It never times out: after the cold reset
  `#F4` it waited 2.9 s (404 419 reads) for the firmware's POST. The upload polls only once per byte because the GS at
  16 MHz is faster than the player. The traces exercise every GS flag rule the RTL defines on real traffic: port 1 / 2
  reads, port 3 replies, port 5 acknowledges, the host's `#B3` read clearing the data flag, the `#FF` the CPLD drives
  on undecoded GS ports.
- **MIDI bit-bang timing.** At 14 MHz on TS-Conf the plugin's bits come 107-114 T (3.5 MHz axis) apart: inside the
  UART's tolerance, decoded without error after the start-up break both by the trace decoder and by the synthesizer.
- **What the model now covers that it did not.** Before: one write-only trace. Now: reads (YM status, YM register
  read-back, GS status, GS replies, GS port reads), M1 context (every host cycle carries its instruction's address;
  the ROM-fetch lock follows from it), the GS side of the board interleaved causally with the host side, frame and
  reset placement, three machines (Pentagon, ZX-Evo, TS-Conf at 14 MHz), both control masks on Ball Quest. And the
  whole card, not only its logic, reproduces each program's run read for read.
- **Risk areas left.** The YM2203's own status timing (busy 192 T, timers) is the YM2203 module's, not the CPLD's: the
  RTL check covers only who drives a YM read; the busy time matches the TSFM hardware reference but no real-chip
  measurement exists. The DAC fetch stream is stored only for its first 4096 changes per trace (the RTL comparison of
  DAC registers covers those). The issue #11 click is not modeled (owner decision). The traces are captured from the
  emulator; a capture from a real card (a logic analyzer on the ZX bus) would check the emulated programs' timing
  itself.

## 4. Summary

| Program | Machine | Traced | Read lines checked (by value) | RTL match | Card replay | Audio | Findings |
|---|---|---|---|---|---|---|---|
| TFM Music Compiler player ("uzhos") | Pentagon 128 | 5.3 s, 8685 lines | 2657 (73) | yes | all reads, all GS cycles | FM 1 / FM 2 digests, rest silent | busy loops end 171-193 T after a data write, as designed |
| Mod Player v2.5 (GS) | Pentagon 128 | 13.1 s, 73 824 lines | 40 441 (40 441) | yes | all reads, all GS cycles | PCM digest, hard L / R | no time-out on GS waits (2.9 s after `#F4`); upload never waits |
| Ball Quest | ZX-Evo BaseConf | 31.9 s, 115 230 lines | 1666 (1403) | yes, `pro` and `classic` | all reads, all GS cycles | SSG digests, FM silent | 4 control bytes with `pro`, 0 with `classic` (issue #11) |
| VGMPLAY.WMF | TS-Conf 14 MHz | 23.6 s, 7411 lines | 965 (965) | yes | all reads, all GS cycles | FM 1, SSG 1, SAA digests | no polling: paced writes; `#F3` through the register path |
| WC GSPLAYER MIDI | TS-Conf 14 MHz | 19.0 s, 3026 lines | 740 (740) | yes | all reads, all GS cycles | 96 MIDI bytes = the file, MIDI digest | plugin drops the SysEx body; one start-up break |

No emulator or model bug was found: the logic, the RTL, the emulator's General Sound and the replayed card agree on
every cycle of the five programs.
