# ZX-MultiSound: open questions for the owner

| | |
|---|---|
| **Date** | 2026-10-03 |
| **For** | the ZX-MultiSound card design; prerequisite: [ZX-bus slots](../2026-10-03-zx-bus-slots/open-questions.md) |
| **Order** | most important first |

## Q1. Card incompatibility in slots

Decided at the slot level: [ZX-bus slots Q1](../2026-10-03-zx-bus-slots/open-questions.md#q1-what-happens-when-a-card-is-incompatible-with-cards-already-plugged-in).
The ZX-MultiSound declares the functions TSFM (`ay-socket`), `gs`, `saa`, `soundrive` and `midi`, each subject to
its DIP switch. Plugging it in can push out several installed cards in one step.

## Q2. Where do the missing synthesizers come from?

**Owner decision (2026-10-03):**
- **Simple chips:** we write our own and bring them to full accuracy by co-simulation against reference
  implementations.
- **SAA1099** is simple. We write our own module. The references are run side by side and compared, never copied:
  SAASound (Dave Hooper), MAME `saa1099.cpp`, and the RTL implementations (MiSTer `saa1099.sv`, rejunity
  `tt06-psg-saa1099`). The reference consensus rule applies.
- **SAM2695 (Dream GM synthesizer):** we write our own library and vendor it ourselves, the same way as libopl4
  (`core/src/3rdparty/opl4/`). Inside the library we aim for the most elegant, highest-quality solution.
- **The MIDI path into the chip** (the serial line driven from the YM / AY I/O port, 31 250 baud, MIDI byte parsing)
  is emulated exactly and recorded by TTD.

## Q3. Where does the SAM2695 library get its samples?

**Owner decision (2026-10-03): A.** The Dream CleanWave mask-ROM sample set is not available. Our engine (voices,
envelopes, filter, reverb, chorus, EQ, the SAM2695 MIDI implementation incl. NRPN / SysEx per the datasheet) reads
SF2 banks (DLS later). The default bank is **GeneralUser GS** (S. Christian Collins). A user bank is selected with a
config key (`[MIDI] Bank=`), for example a community bank that approximates CleanWave.

For development, every GM bank we can reach is downloaded to `testdata/midi/` (one folder per bank, index in
`testdata/midi/README.md`). The banks are not tracked in the repository: `.gitignore` keeps only the README.

## Q4. How does the default GeneralUser GS bank reach the user?

**Owner decision (2026-10-03): A.** One file is tracked in the repository, `data/midi/generaluser-gs.sf2`, with the
author's license next to it (like the ROMs in `data/`). Installation copies it next to the application. Builds and
CI work offline, and the library tests pin the bank by SHA-256. The one-time cost is about 30 MB of git history.

## Q5. Which variants of the card are modeled?

**Owner decision (2026-10-03): A.** The default is the current master CPLD firmware. Card options:
- `gsRam = 1M | 2M`: the two official firmware builds (`rev_A1.pof`, `rev_A1_2mb.pof`);
- the DIP switches (YM, SAA, GS, SD);
- `ctrlMask = pro | classic`. `classic` is the community patch from issue #11: with the SAA DIP off, the control byte
  mask is five bits, as on the classic TSFM, which fixes the Ball Quest clicks. The slot report marks it as
  unofficial firmware.

Board revisions (A, A1, A2) and older firmware builds (before the 2023-12 IORQGE exclusions, before the 2024-01 YM
swap) are described in the hardware reference only, not modeled.

**Owner addition (2026-10-03): model the least buggy variant.** The modeled board is rev.A2 with the current
firmware: the errata of rev.A / A1 (missing MREQ wire, swapped L/R on the 3.5 mm jack, the unbuffered 3.3 V Z80 clock
that makes the GS unstable) and the board-level reset glitch from issue #9 (the YM2203 prescaler left wrong after a
too-short RESET) are not reproduced. The YM2203 is reset properly, the stereo channels are the right way round, and
the GS runs stable. These faults are listed in the hardware reference as real-world notes only.
