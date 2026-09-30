# RZX and TTD: two ways to record a Spectrum session, and how to bridge them

- **Date:** 2026-09-29
- **Status:** research article with a proposal. No RZX code exists in
  unreal-ng yet.
- **Related:** [ttd-offline-analysis.md](ttd-offline-analysis.md) (§6b and
  tracking O-29 … O-32), [use-cases-extended.md](use-cases-extended.md) (TA-20,
  X-3, X-20), the emulator debugger survey
  ([2026-09-28-emulator-debugger-survey](../2026-09-28-emulator-debugger-survey/README.md)),
  ZX-meta-db ([concept](../2026-09-28-zx-meta-db/concept.md)), PLAN #27 (RZX
  record / playback) and #40 (TTD v2).
- **Sources:** every fact below cites the RZX specification, a reference
  implementation, an emulator source file (paths relative to the local
  emulator source collection, e.g. `ZXMAK2/src/…`), or a web page; the full
  list is in [§16](#16-references). *(unconfirmed)* marks facts known only from
  search summaries.

> **In one line.** RZX records *what the CPU read from the outside world*
> (every `IN` value, framed by instruction-fetch counts) and is portable across
> emulators; TTD records *the whole machine* with random access and is
> specific to unreal-ng. Each can be produced from the other, and TTD should
> adopt RZX's central idea — an `IN`-value journal — for itself.

## Contents

- [1. Summary](#1-summary)
- [2. RZX in brief: history and idea](#2-rzx-in-brief-history-and-idea)
- [3. The RZX file format (v0.13)](#3-the-rzx-file-format-v013)
- [4. Semantics and edge cases](#4-semantics-and-edge-cases)
- [5. Security and trust](#5-security-and-trust)
- [6. Implementations](#6-implementations)
- [7. The ecosystem](#7-the-ecosystem)
- [8. SZX, the start state that matters](#8-szx-the-start-state-that-matters)
- [9. TTD in brief](#9-ttd-in-brief)
- [10. Side by side](#10-side-by-side)
- [11. TTD to RZX: export](#11-ttd-to-rzx-export)
- [12. RZX to TTD: import](#12-rzx-to-ttd-import)
- [13. What TTD should borrow from RZX](#13-what-ttd-should-borrow-from-rzx)
- [14. Related formats and tools outside the Spectrum world](#14-related-formats-and-tools-outside-the-spectrum-world)
- [15. Proposal, tracking and open questions](#15-proposal-tracking-and-open-questions)
- [16. References](#16-references)

---

## 1. Summary

| Question | Short answer |
|---|---|
| What is RZX? | The community's input-recording format since 2002 (Ramsoft): a start snapshot plus, per interrupt frame, the number of instruction fetches and every value returned by `IN`. |
| Why is it portable? | Timing and contention do not matter: only the CPU path must be identical, and every external input reaches the CPU as an `IN` value (keyboard, joystick, tape, disk, floating bus). |
| What is TTD? | unreal-ng's time-travel recording: full-machine checkpoints, host inputs, journals and indexes, with random access; exact for the whole machine but tied to unreal-ng. |
| TTD → RZX? | Yes, by replaying the TTD range with an analyzer that counts fetches and records `IN` values; exact for machines whose inputs all go through `IN`. |
| RZX → TTD? | Yes, with an RZX player mode (recorded `IN` values, interrupts at fetch counts) while TTD records; the RZX Archive then becomes an analyzable corpus. |
| What should TTD take from RZX? | An `IN`-value journal and per-frame fetch counts: replay without media or host clock, a cheap divergence check, and a softer version lock. |
| What is the catch? | RZX cannot express DMA, memory-mapped input or a second CPU; it has an ambiguity around `EI` and blocked interrupts; its security is weak by its own admission. |

## 2. RZX in brief: history and idea

- **Origin.** The specification is "Created by Ramsoft ZX Spectrum demogroup et
  al." and maintained by Ramsoft; the current revision is **0.13 of 2 March
  2005** [SPEC header, §6]. Ramsoft's page calls RZX "an open-source input
  recording system for ZX Spectrum emulators which aims to become the accepted
  standard", inspired by RealSpectrum's earlier AIR system, and offered an
  "RZX SDK 0.12 … with full source code" [RAMSOFT]. The RZX Archive credits an
  "original implementation by the FUSE and SpecEmu authors", evolved "with
  input from the SPIN Team, Spectaculator and Ramsoft authors" [ARCHIVE].
- **Why not timing-based logs.** AIR files were not portable because "the
  recording logic was very sensitive to emulation timings" [SPEC §1]; logging
  key presses by T-state was rejected because contention differs between
  emulators [SPEC §2].
- **The idea.** "The RZX engine is based on recording the input information at
  the end of the frame together with the number of CPU instruction fetches
  that have been performed in that frame"; on playback the emulator reads "the
  number of fetches to perform … and then will force an interrupt when the
  limit is reached" [SPEC §2]. Since revision 0.10 the input is the list of
  **I/O port read values** instead of a keyboard / joystick array, which also
  covers the floating bus, tape and disk loading and devices the replaying
  emulator does not emulate: "Any emulator can correctly replay an RZX file
  where the Kempston mouse and the lightgun have been used - no matter if it
  doesn't emulate these devices at all!" [SPEC §2, §7].
- **Design goals:** portability, timing-independent logic, easy
  implementation, small files (zlib), and "robust security" for tournaments
  [SPEC §1].
- **Revision history** [SPEC §7]:

| Revision | Date | Change |
|---|---|---|
| 0.01 | 2 Feb 2002 | first public draft |
| 0.02 | 14 Feb 2002 | idle frames (bit 15 of the instruction counter); electronic signature; snapshot descriptor |
| 0.10 | 6 Apr 2002 | frames log I/O port read values |
| 0.11 | 14 Apr 2002 | zlib compression of recordings and embedded snapshots |
| 0.12 | 30 Jul 2002 | idle frame marker moved to IN count = 65535; the interrupt-acknowledge R increment must not be counted |
| 0.13 | 2 Mar 2005 | security blocks, "as already implemented in several emulators" |

- **State of the spec.** The security chapter is headed "[obsolete info, needs
  updating to DSA]" and the block list ends "To be completed (Session
  Information Block, Comment Block)"; neither was defined [SPEC §3, §5]. A
  2021-22 proposal for a revision 0.14 (bit 15 of the fetch counter meaning
  "the frame ends but the interrupt is not actually generated") was discussed
  on Spectrum Computing but not adopted [PROP-014] *(unconfirmed)*.

## 3. The RZX file format (v0.13)

All values little-endian; reserved bits zero [SPEC §5].

**Header**

| Offset | Type | Field |
|---|---|---|
| 0x00 | ASCII[4] | `"RZX!"` |
| 0x04 | BYTE | major revision (0) |
| 0x05 | BYTE | minor revision (0x0D = 13) |
| 0x06 | DWORD | flags; bit 0: data up to the Security Signature Block is signed |
| 0x0A | — | blocks |

Every block: `BYTE id`, `DWORD length` (including these 5 bytes), payload.

| ID | Block | Payload |
|---|---|---|
| 0x10 | Creator information (required) | ASCIIZ[20] creator, WORD major, WORD minor, custom data |
| 0x20 | Security information | DWORD key id (low 32 bits of the DSA public value y), DWORD week code (for tournaments) |
| 0x21 | Security signature | DSA signature r and s as OpenPGP multi-precision integers |
| 0x30 | Snapshot | DWORD flags (bit 0 external = a descriptor instead of the image; bit 1 compressed), ASCIIZ[4] extension (`SNA`, `Z80`, `SZX` …), DWORD uncompressed length, data or descriptor (DWORD checksum, ASCIIZ file name) |
| 0x80 | Input recording (required) | DWORD number of frames, BYTE reserved, DWORD T-state counter at the beginning, DWORD flags (bit 0 protected = frames encrypted; bit 1 compressed), frames |

**Frame**: WORD fetch counter (R increments, interrupt acknowledge excluded);
WORD IN counter (65535 = "repeated frame: the port reads were exactly the same
as the last frame"); the IN values [SPEC §5].

**Blocks are commands**: several recording blocks separated by snapshot
blocks handle multiload games or change security parameters [SPEC §5].
Compression is zlib [SPEC §1, §5].

## 4. Semantics and edge cases

**The fetch counter.** "The fetch counter stepping is the same as the R
register, i.e. it is incremented by 1 for single-opcode instructions and by 2
for double-opcode ones. The interrupt acknowledge cycle is NOT considered a
regular instruction fetch" [SPEC §2]. Consequences, as implemented by the
faithful players: every prefix counts, each repeat of a block instruction
counts, each `HALT` cycle counts; `LD R,A` changes R without a fetch, so a
counter derived from R must be corrected — Fuse adjusts its offset on
`LD R,A` [FUSE-Z80] and SkoolKit special-cases it
(`skoolkit/skoolkit/rzxplay.py:291-300`).

**Which reads.** All port reads performed by the CPU in a frame, so that
"each IN instruction gets exactly the right value as expected" [SPEC §2];
implementations route every `IN` form (`IN A,(n)`, `IN r,(C)`, `IN (C)`,
`INI` / `IND` / `INIR` / `INDR`) through the override (§6).

**Frames and interrupts.** `rzx_update` is "called when an interrupt occurs
(or it would occur, if maskable interrupts are disabled)" [SPEC §4]: a frame is
written at every frame-interrupt point, even under `DI`. A retriggered
interrupt adds a frame; emulators recognize such frames by a small counter
("we suggest to check for a counter value less or equal to 4") [SPEC §2]. Fuse
writes a frame on every frame event and on each retriggered accepted
interrupt [FUSE-SPECTRUM], [FUSE-Z80].

**The `EI` ambiguity.** A frame cannot say "the frame ended but the interrupt
was blocked". The de-facto convention, documented by SkoolKit: "accepts an
interrupt at the start of every frame except the first, regardless of whether
the instruction just executed would normally block it. However, some RZX files
contain a short frame immediately after an 'EI' to indicate that the interrupt
should in fact be blocked" (`skoolkit/skoolkit/rzxplay.py:392-399`). Players
differ: ZX-M8XXX gives the interrupt only if `iff1 && !eiPending`
(`ZX-M8XXX/core/spectrum.js:2538-2545`); zxsp tolerates an overshoot of up to
2 fetches "if the encoder finishes a frame right after an EI opcode"
(`zxsp/Source/Uni/Machine/Machine.cpp:992-1002`); Spectaculator 6.0 fixed a
game that "used to fail randomly due to a quirk in the RZX spec" [SPECT-NEW].

**Other known quirks.** "Some RZX files fail" when the NMOS `LD A,I` /
`LD A,R` parity-flag behavior is emulated
(`skoolkit/skoolkit/rzxplay.py:388-390`); SkoolKit also has an option to
ignore later snapshots "for some RZX files created by the Fuse emulator"
(`:388-405`).

**The critical rule.** Replay requires exact Z80 behavior, documented and
undocumented: the spec's example is the rhino in Sabre Wulf, which depends on
the sign flag of `BIT 7,(IX+6)` [SPEC §2]. Timing and contention do not
matter.

## 5. Security and trust

- **The designed scheme** (now "obsolete"): per-file symmetric key for the
  frames, encrypted with the competition's public key; the emulator signs a
  hash with its own key; speed-violation and autofire detection [SPEC §3].
- **What shipped:** DSA signing only (blocks 0x20 / 0x21). libspectrum writes
  the key id and the week code, signs with libgcrypt, and writes unsigned
  files as 0.12 [LS]. Fuse embeds its DSA key, private part included, in its
  source [FUSE].
- **Admitted weakness.** The spec: "no safe vault exists for the emulator to
  keep its secret key in … once the malintentioned user obtains the secret key
  of the emulator, nothing can prevent him to produce arbitrary RZX files"
  [SPEC §3]. Fuse's manual: the signature "is not secure (and cannot be made
  so) … This feature is included in Fuse solely as it was one of the
  requirements for Fuse to be used in an on-line tournament" [FUSE-MAN].
- **Competition mode** in Fuse forces normal speed, stops recording if speed
  drifts more than 5 %, forbids snapshot loads and pauses, and signs the file;
  a "competition code" proves a recording was made after a code was released
  [FUSE], [FUSE-MAN]. Spectaculator disables its debugger during tournament
  recording ([spectaculator survey](../2026-09-28-emulator-debugger-survey/spectaculator-debugger.md)
  §2, §11; [SPECT-NEW] 7.0).
- **Verification in practice** combined a jury-supplied start snapshot, an
  emulator whitelist, competition-mode restrictions, the competition code and
  the (weak) signature [HARP], [FUSE-MAN]. Encrypted frames appear unused:
  Xpeccy and ZXMAK2 reject them (`Xpeccy/src/libxpeccy/filetypes/rzx.c:236-237`,
  `ZXMAK2/src/ZXMAK2.Engine/Serializers/SnapshotSerializers/RzxSerializer.cs:312`),
  others ignore the flag *(inference)*.
- **Lesson for unreal-ng:** a signature made by software on the player's
  machine proves little. Our live-segment integrity design
  ([ttd-offline-analysis §6a](ttd-offline-analysis.md#6a-live-segment-streaming-from-offline-to-near-live),
  OR-28) should rely on organizer-controlled machines, hash chains and
  evidence clips, and treat signatures as tamper evidence, not proof.

## 6. Implementations

**Reference: libspectrum and Fuse.**

- libspectrum: a block list (input, snapshot, signature); repeat frames on
  write and read; SNA / Z80 / SZX snapshots, Z80 by default with a fallback to
  SZX when Z80 "has lost a significant amount of information"; external
  snapshot links refused ("more trouble than they're worth"); rollback to the
  last or the n-th snapshot; **hard desync errors**: "wrong number of INs in
  frame" and "more INs during frame … than stored in RZX file" [LS].
- Fuse: the fetch counter is `R + offset`, with the offset corrected at the
  interrupt (INTA excluded) and on `LD R,A` [FUSE], [FUSE-Z80]; the frame ends
  when the count is reached [FUSE-OPS]; normal interrupts are suppressed
  during playback [FUSE-SPECTRUM]; a T-state "sentinel" warns about runaway
  frames; `IN` values come from the recording during playback and the merged
  real value is stored during recording [FUSE-PERIPH]; autosaves every 5
  seconds, pruned (all to 15 s, then one per 15 s to 1 min, one per minute to
  5 min, then one per 5 min); "Insert snapshot" and "Rollback" menu items
  [FUSE], [FUSE-MAN].
- Tools (fuse-utils): `rzxcheck` (verifies the competition signature),
  `rzxdump`, `rzxtool` (remove blocks, extract / insert snapshots,
  uncompress) [FUSE-UTILS], [RZXTOOL].
- Ramsoft's rzxlib (SDK 0.12, portable C) lives on in copies: Spectral
  (`Spectral/src/emu_rzx.h`) and UnrealSpeccyP (`UnrealSpeccyP/snapshot/rzx.cpp`).
  Its `rzx_get_input` returns a byte, so an error code cannot be told from data
  (`Spectral/src/emu_rzx.h:838-842`).

**Local emulator sources.**

| Emulator | Record | Play | Fetch counter | Frame end | Desync detection | Snapshots |
|---|---|---|---|---|---|---|
| ZXMAK2 / Kozynax | no | yes | R-exact, INTA excluded (`ZXMAK2/src/ZXMAK2.Engine.Cpu/Processor/Z80Cpu.cs:31`) | counter-driven, excess carried (`ZXMAK2/src/ZXMAK2.Engine/RzxHandler.cs:81-130`) | too many / too few INs, stops | any, external too |
| Zero | yes, with bookmarks and rollback | yes | per fetch (`Zero-Emulator/Ziggy/Speccy/Z80.cs:584-594`) | count reached, INT if IFF1 | reported only (monitor, trace) | SNA / Z80 / SZX; writes SZX |
| Xpeccy / xpeccy-plus | no | yes | M1 reads (`Xpeccy/src/libxpeccy/spectrum.c:71-73`) | count → forced INT | "overIO" error | SNA / Z80, external; plus model check |
| UnrealSpeccyP | no | yes | per fetch; `HALT` burns the rest | countdown | IN count must match (`UnrealSpeccyP/snapshot/rzx.cpp:63`) | SNA / Z80 / SZX, external |
| zxsp | yes | yes | with every R increment (`zxsp/Source/Uni/Items/Z80/Z80.h:113`) | count reached; overshoot ≤ 2 tolerated | too many INs, overshoot, wrong model | Z80 / SZX |
| SkoolKit (headless) | re-export | yes | R delta with `LD R,A` case | count ≤ 0, INT if IFF | leftover / exhausted INs, error | SNA / SZX / Z80 |
| ZX-M8XXX | yes | yes | M1 count ≈ R | INT if `iff1 && !eiPending` | warnings | Z80 / SNA / SZX; writes SZX |
| 8BitAnalysers | no | yes | opcode heuristic | own timing | log only | Z80 / SNA |
| jnext | yes | yes | one per instruction (non-compliant) | own frame, counts ignored | none | SNA / SZX |
| Spectral | unreachable | partial | not enforced | ULA vblank | none | — |
| Unreal Speccy, TS-Labs Unreal, BizHawk, MAME, DeZog, zxtune, zxpoly | — | — | — | — | — | no RZX code |

**Closed source.** Spectaculator: RZX since 4.0, signed tournament files since
5.2 (Speccy Tour 2003), rollback with multiple bookmarks and the debugger
disabled in tournament mode since 7.0 [SPECT-NEW], [SPECT-53]; ZXSpin: embedded
snapshots, IN-count mismatch reports, recordings left open to resume later
([zxspin survey](../2026-09-28-emulator-debugger-survey/zxspin-debugger.md)
§13). Zero shows expected and actual fetch and IN counts at an RZX frame-end
breakpoint ([zero survey](../2026-09-28-emulator-debugger-survey/zero-emulator-debugger.md)
§16).

**Observation.** The faithful fetch counters are Fuse, ZXMAK2 / Kozynax, zxsp
and SkoolKit; hard desync errors exist in libspectrum / Fuse, ZXMAK2,
UnrealSpeccyP, zxsp and SkoolKit. SkoolKit's headless `rzxplay.py` is the most
convenient **oracle** for testing a new player.

## 7. The ecosystem

- **The RZX Archive** ("watch recordings of speccy games") holds completions
  per game, walkthrough videos and a game-endings collection; it recommends
  SpecEmu, Spectaculator, Spin, Fuse, RealSpectrum, Es.pectrum, Xpeccy, ZXDS
  and Unreal Speccy Portable; it states "this site is no longer being
  updated", latest listed update November 2021; distribution follows
  publishers' permissions [ARCHIVE]. Sizes of 1900+ files and 2400+ videos are
  from social-media snippets [ARCHIVE-SOCIAL] *(unconfirmed)*.
- **Tournaments.** Speccy Tour 2002 drove the completion of RZX [RAMSOFT];
  Speccy Tour 2003 used signed files from Spectaculator and Fuse [SPECT-53].
  The HARP ZX Spectrum Zone ran leaderboards with regulation games, supplied
  start snapshots, rules on disallowed tricks, and an emulator whitelist
  [HARP].
- **Speedrun and TAS.** No evidence that speedrun.com requires RZX
  [SPEEDRUN] *(unconfirmed)*; TASVideos lists the ZX Spectrum, whose movies use
  BizHawk's ZXHawk core and its own movie format [TASVIDEOS] (BizHawk has no
  RZX importer, `BizHawk/src/BizHawk.Client.Common/movie/import/`).

## 8. SZX, the start state that matters

RZX defines only the input log; determinism depends on the start snapshot
being complete. **SZX (ZX-State)**, by Jonathan Needle for Spectaculator
(current v1.5), has a machine id (16K, 48K, 128K, +2, +2A, +3, +3e, Pentagon
128, TC2048, TC2068, Scorpion, SE, TS2068, Pentagon 512, Pentagon 1024, NTSC
48K, 128Ke) and about 35 device blocks (AY, Beta 128 and its disks, +3, +D,
Opus, IDE, GS and its RAM pages, Covox, Multiface, joystick, mouse, tape and
others) [SZX-HEADER], [SZX-INTRO]; unofficial extensions add DivIDE, DivMMC,
ULAplus, Spectranet and more, with "the official specification being somewhat
stalled" [SZX-WIKI]. libspectrum / Fuse, Zero, ZXDS, SpecEmu and Spin write it
[SZX-WIKI]; Zero and ZX-M8XXX embed SZX in their RZX files
(`Zero-Emulator/Ziggy/Peripherals/RZXFile.cs:1279`,
`ZX-M8XXX/core/spectrum.js:8687-8702`). SNA and Z80 lack many device states.

**For unreal-ng:** we read and write SNA and Z80 (v3); SZX is listed as a
snapshot extension but loading it is rejected, and there is no writer (see the
[unreal-ng self-survey](../2026-09-28-emulator-debugger-survey/unreal-ng-debugger.md)).
An **SZX reader and writer** is the precondition for good RZX interop on
anything beyond 48K / 128K, and useful on its own.

## 9. TTD in brief

From the code on 2026-09-29 (details: [ttd-offline-analysis §2](ttd-offline-analysis.md#2-what-exists-today)):

- A recording holds checkpoints (CPU and chipset state, references to 4 KB
  memory pieces stored in full, as XOR against the previous version or as
  zero, CRC per piece), device state blobs, a write journal (a 64 MB ring with
  a "complete" flag), a coverage index and bookmarks; time is counted in
  T-states at the model's top CPU clock.
- Live sessions add input events (keyboard, mouse, GS stimuli) and external
  events (tape / disk barriers, debugger edits), **which are not saved in the
  file today**.
- It offers seek, reverse step and continue, find-last, coverage queries —
  for the whole machine, exactly, but only in unreal-ng of a compatible
  version.

## 10. Side by side

| | RZX | TTD |
|---|---|---|
| Start state | an embedded snapshot (SNA / Z80 / SZX), several per file | a full checkpoint |
| Recorded input | per frame: fetch count and every `IN` value | host inputs with exact T-state times (live only today), external events, write journal |
| Time unit | interrupt frame, measured in R increments | T-states at the top CPU clock |
| Random access | none; seek = replay from an embedded snapshot (xpeccy-plus and jnext disable rewind during playback: `xpeccy-plus/src/libxpeccy/xstate.c:152`, `jnext/src/debugger/debugger_window.cpp:620-625`) | checkpoints |
| Portability | across emulators with exact Z80 cores | unreal-ng only, version-bound |
| Device independence | high: tape, disk, RTC, floating bus and unknown devices replay through `IN` values | low: needs the same device models and media |
| Guarantees | the same CPU path | the whole machine: CPU, video, sound, devices |
| Cannot express | DMA and memory-mapped input, a second CPU's internals, host events outside `IN` (NMI buttons, mid-frame snapshot loads), exact picture and sound timing | — |
| Divergence detection | IN-count mismatch; fetch overshoot | (proposed) per-frame digests |
| Trust | DSA signature, weak by design | (proposed) hash chains, controlled machines |
| Size | small (zlib, repeat frames) | large (full states; memory-region work in TTD v2) |
| Ecosystem | the RZX Archive, tournaments, many emulators | ours |

## 11. TTD to RZX: export

A replay of the TTD range with an analyzer, verified by playing the result.

1. **Start snapshot** at the in-point: SZX when available (needs our writer),
   else Z80 v3 or SNA, with a warning when the format loses state.
2. **Replay with the RZX analyzer:**
   - the fetch counter follows R exactly: +1 per opcode fetch including each
     prefix, each block-instruction repeat and each `HALT` cycle; the
     interrupt acknowledge is excluded; `LD R,A` does not disturb it (count
     fetches directly, not from R);
   - a frame is closed at **every frame-interrupt point**, whether or not the
     interrupt is accepted, plus a frame for each retriggered accepted
     interrupt [SPEC §2, §4];
   - every `IN` result is appended to the frame, all `IN` forms included;
   - after an `EI` that blocks the interrupt, write the short frame the
     community convention expects (§4), and document the choice.
3. **Write** header 0.12 (unsigned) or 0.13, creator block, snapshot block,
   input block with repeat frames and zlib compression; several input blocks
   with snapshot blocks between them when the range spans a snapshot point.
4. **Verify** by playing the RZX in our player (§12) and, in CI, in SkoolKit's
   `rzxplay.py` as an independent oracle.

**Refuse or warn** when the range contains DMA into memory (TSConf), a card
CPU whose behavior the main CPU observes through more than `IN`, a
memory-mapped device, or host events outside `IN` (NMI, debugger edits).

## 12. RZX to TTD: import

1. **An RZX player mode** in the core: load the snapshot; while playing, every
   `IN` returns the next recorded value; the interrupt is forced when the
   frame's fetch count is reached; normal frame interrupts are suppressed;
   frames continue across snapshot blocks (multiload).
2. **Conventions as options** (following SkoolKit): interrupt at every frame
   start vs honoring a short frame after `EI`; NMOS `LD A,I` / `LD A,R` parity
   behavior; ignoring later snapshots in known Fuse files.
3. **Divergence detection**, hard by default like libspectrum: too few or too
   many `IN`s in a frame, or a fetch overshoot beyond a small tolerance, stops
   playback and reports the frame; a diagnostic mode continues and logs, like
   Zero.
4. **Record TTD while playing:** the result is a full TTD recording with
   checkpoints, video and sound, marked "imported from RZX" with the RZX's
   hash and creator.
5. **Machine selection** from the snapshot (SZX machine id; Z80 / SNA
   heuristics), with a model check like xpeccy-plus
   (`xpeccy-plus/src/libxpeccy/filetypes/filetypes.c:84-97`).

**Why it matters.** Every archived RZX becomes a TTD recording, and every
offline analyzer applies: chapters, asset harvest, automatic unpacked images,
memory layouts per game phase, graphics and music extraction. The RZX Archive's
completions reach levels and endings that no automatic analysis would reach —
a corpus for [ZX-meta-db](../2026-09-28-zx-meta-db/concept.md) — and
fetch-count mismatches are precise **CPU regression tests** for our Z80 core.

## 13. What TTD should borrow from RZX

| Idea | Benefit for TTD |
|---|---|
| **An `IN`-value journal** next to host inputs | the CPU path replays without the tape, the disk, the RTC or the host clock; a newer build with changed device models can still replay an old recording's CPU path and report where devices would answer differently (softens the version lock); cost about one byte per `IN` |
| **Fetch counts per frame** | a cheap per-frame digest of the CPU path (offline-analysis OR-7) that pinpoints the diverging frame |
| **Frames at every interrupt point**, retriggers included | a natural, emulator-independent frame index for the timeline |
| **Rollback with pruned autosaves** (Fuse) | a memory-bounded history policy for long sessions (1 per 5 s → per 15 s → per minute → per 5 min) |
| **Competition mode** (Fuse, Spectaculator) | the model for a locked TTD recording mode: normal speed, no state loads or pauses, debugger off or journaled |
| **Live RZX output** from the same input taps | tournaments and archives that expect RZX |

## 14. Related formats and tools outside the Spectrum world

- **rr and Pernosco.** rr records only non-deterministic inputs of a Linux
  process tree and replays it under gdb with reverse execution; Pernosco takes
  rr recordings and analyzes them in the cloud as an omniscient debugger
  [RR], [PERNOSCO]. The same split as TTD's "record cheap, analyze without
  limits"; rr also positions events by a hardware counter rather than time,
  like RZX's fetch count *(from rr's publications, not re-checked here)*.
- **TAS movie formats.** FCEUX `.fm2` (a text header with a ROM checksum and
  a per-frame controller log, optionally starting from a savestate) [FM2] and
  BizHawk `.bk2` (a zip with header, per-frame input log, comments,
  subtitles and an optional core state) [BK2] log **controller state per
  frame** and depend on a bit-exact core with the same timing; RZX logs
  **every port read per interrupt frame**, which makes it portable across
  cores with different contention.

## 15. Proposal, tracking and open questions

Tracking lives in [ttd-offline-analysis §7](ttd-offline-analysis.md#7-tracking-and-priorities):

| ID | Item | Priority |
|---|---|---|
| O-29 | `IN`-value journal and per-frame fetch counts in TTD recordings | P1 |
| O-30 | RZX player mode with divergence detection; RZX → TTD import | P2 |
| O-31 | TTD → RZX export via replay, verified; SZX reader and writer | P2 |
| O-32 | RZX Archive import pipeline for ZX-meta-db and CPU regression | P3 |

Proposed for PLAN: move #27 (RZX record / playback) from T4 to T2 as part of
the offline-analysis program; add "SZX reader and writer" as its own small row.

**Open questions**

1. Which `EI` convention do we write, and which do we accept by default?
2. Do we sign RZX exports (0.13) at all, given the known weakness, or write
   0.12 only?
3. Do we keep an offline copy of the RZX Archive for the ZX-meta-db pipeline,
   respecting its distribution notes?
4. Should the `IN`-value journal be always on during TTD recording, or only in
   an "archival" recording mode?

## 16. References

**Specification and reference implementation**

| Key | Reference |
|---|---|
| [SPEC] | RZX format specification v0.13: https://worldofspectrum.net/RZXformat.html (read via https://web.archive.org/web/20231002013321/https://worldofspectrum.net/RZXformat.html) |
| [RAMSOFT] | Ramsoft RZX homepage (news 2002, SDK 0.12): https://web.archive.org/web/20060102102657/http://www.ramsoft.bbk.org/rzx.html |
| [PROP-014] | "Proposal for RZX format update" (0.14), Spectrum Computing forum: https://spectrumcomputing.co.uk/forums/viewtopic.php?t=6058 *(content from search summary)* |
| [LS] | libspectrum `rzx.c`: https://github.com/speccytools/libspectrum/blob/master/rzx.c |
| [FUSE] | Fuse `rzx.c`: https://github.com/speccytools/fuse/blob/master/rzx.c |
| [FUSE-OPS] | Fuse opcode loop: https://github.com/speccytools/fuse/blob/master/z80/z80_ops.c |
| [FUSE-Z80] | Fuse interrupt handling: https://github.com/speccytools/fuse/blob/master/z80/z80.c |
| [FUSE-SPECTRUM] | Fuse frame event: https://github.com/speccytools/fuse/blob/master/spectrum.c |
| [FUSE-PERIPH] | Fuse port reads: https://github.com/speccytools/fuse/blob/master/periph.c |
| [FUSE-UP] | Fuse / libspectrum / fuse-utils upstream: https://sourceforge.net/projects/fuse-emulator/ |
| [FUSE-MAN] | fuse(1) manual: https://web.archive.org/web/20240427231528/https://manpages.ubuntu.com/manpages/trusty/man1/fuse.1.html |
| [FUSE-UTILS] | fuse-utils(1): https://manpages.ubuntu.com/manpages/bionic/man1/fuse-utils.1.html; sources: https://github.com/fuse-emulator/fuse-utils |
| [RZXTOOL] | rzxtool(1): https://manpages.debian.org/unstable/fuse-emulator-utils/rzxtool.1.en.html |
| [ZENTOOLS] | Go RZX package: https://pkg.go.dev/github.com/ha1tch/zentools/pkg/rzx *(search summary)* |
| [XZX] | XZX-Pro ChangeLog (Ramsoft SDK): https://fossies.org/linux/xzx-pro/doc/ChangeLog *(search summary)* |
| [FORMATS-FAQ] | Emulator file formats FAQ: https://fizyka.umk.pl/~jacek/zx/faq/reference/formats.htm *(search summary)* |

**Emulators and ecosystem**

| Key | Reference |
|---|---|
| [SPECT-NEW] | Spectaculator revision history: https://web.archive.org/web/20251229103619/https://www.spectaculator.com/docs/spectaculator/9.0/getting_started/new.html |
| [SPECT-53] | Spectaculator 5.3 release (Speccy Tour 2003): https://web.archive.org/web/20230220173643/https://www.spectaculator.com/2003/10/spectaculator-5-3-released/ |
| [ARCHIVE] | The RZX Archive: https://www.rzxarchive.co.uk/ (read via https://web.archive.org/web/20250103205734/https://www.rzxarchive.co.uk/); YouTube: https://www.youtube.com/user/rzxarchive |
| [ARCHIVE-SOCIAL] | https://www.facebook.com/rzxarchive, https://x.com/rzxarchive, https://spectrumcomputing.co.uk/forums/viewtopic.php?f=29&t=12498, https://rickdangerous.co.uk/zx/rzx.html *(search summaries)* |
| [HARP] | HARP ZX Spectrum Zone: https://web.archive.org/web/20081218200927/http://www.zxspectrum.homeactionreplay.org/ ; Speccy Tour page: http://www.zxspectrum.homeactionreplay.org/tour/index.php |
| [SPEEDRUN] | speedrun.com emulators thread: https://www.speedrun.com/forums/speedrunning/oyc05 |
| [TASVIDEOS] | TASVideos systems: https://web.archive.org/web/2024/https://tasvideos.org/Systems |

**SZX**

| Key | Reference |
|---|---|
| [SZX-INTRO] | ZX-State v1.5: https://www.spectaculator.com/docs/zx-state/intro.html ; v1.4 intro: https://web.archive.org/web/20240803171710/https://www.spectaculator.com/docs/zx-state/intro.shtml |
| [SZX-HEADER] | ZX-State header: https://www.spectaculator.com/docs/zx-state/header.html ; https://web.archive.org/web/20230220175722/https://www.spectaculator.com/docs/zx-state/header.shtml |
| [SZX-WIKI] | Sinclair Wiki, ZX-State format: https://web.archive.org/web/20230111221423/https://sinclair.wiki.zxnet.co.uk/wiki/ZX-State_format |

**Outside the Spectrum world**

| Key | Reference |
|---|---|
| [RR] | rr: https://rr-project.org/ , https://github.com/rr-debugger/rr , https://en.wikipedia.org/wiki/Rr_(debugging) |
| [PERNOSCO] | Pernosco: https://pernos.co/ ; demo: https://robert.ocallahan.org/2021/04/demoing-pernosco-omniscient-debugger.html |
| [FM2] | FCEUX `.fm2`: https://web.archive.org/web/20250126124217/https://fceux.com/web/help/fm2.html |
| [BK2] | BizHawk `.bk2`: https://web.archive.org/web/20250108105800/https://tasvideos.org/Bizhawk/BK2Format |

**Local sources** (paths relative to the emulator source collection): Fuse's
Z80 core copy in `jnext/third_party/fuse-z80/`; ZXMAK2 and Kozynax
(`ZXMAK2/src/ZXMAK2.Engine/RzxHandler.cs`, `…/Serializers/SnapshotSerializers/RzxSerializer.cs`);
Zero (`Zero-Emulator/Ziggy/Peripherals/RZXFile.cs`); Xpeccy and xpeccy-plus
(`Xpeccy/src/libxpeccy/filetypes/rzx.c`); UnrealSpeccyP
(`UnrealSpeccyP/snapshot/rzx.cpp`); zxsp (`zxsp/Source/Uni/Files/RzxFile.cpp`,
`RzxBlock.cpp`, `Machine/Machine.cpp`); SkoolKit (`skoolkit/skoolkit/rzxplay.py`,
`rzxinfo.py`); ZX-M8XXX (`ZX-M8XXX/core/loaders/rzx.js`, `core/spectrum.js`);
8BitAnalysers (`8BitAnalysers/Source/ZXSpectrum/SnapshotLoaders/RZXLoader.cpp`);
jnext (`jnext/src/core/rzx.h`, `rzx_player.cpp`, `rzx_recorder.cpp`); Spectral
(`Spectral/src/emu_rzx.h`, `zx_rzx.h`). The research notes behind this article
cite exact lines for each.
