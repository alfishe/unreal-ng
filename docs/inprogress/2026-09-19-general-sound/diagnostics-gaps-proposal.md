# GS Diagnostics: Gaps Found in Live Triage, and a Proposal

Status: PROPOSAL for §1-§6 (not implemented), §7.2 (not implemented) and
§8 (P2, not implemented);
§7.1 IMPLEMENTED AND TESTED (2026-09-22, same day as the rest of this doc -
see below). Written 2026-09-22 after a real debugging session (BUG-10..16,
the render-quality pass, and the automation-parity pass) that repeatedly hit
the limits of what WebAPI/MCP/CLI/Lua/Python can currently see into the GS
card. Every gap below is something this session's actual triage work needed
and didn't have - not a speculative wishlist. Each entry says what broke,
what I had to do instead, and what a fix would look like. §6 covers how to
wire §1-§5 into the existing automation surfaces; §7 covers what the same
question means for TTD.

Companion audit: while writing this I found the automation layer's own
comments describing a 16-deep FIFO mailbox that no longer exists (removed in
the 2026-09-21 single-latch rewrite - see `gs-card-personalities-tdd.md`
§7.5-ish history). Those comments are fixed alongside this doc (see §5); the
missing counter that rewrite left behind is proposal item §1.5.

## 1. LLE (Z80 coprocessor): no way to see what the firmware is doing

### 1.1 No disassembly of the GS coprocessor

`inspect_state`'s `disasm` aspect (WebAPI `GET /{id}/disasm`) disassembles
the **main** ZX Spectrum Z80. There is no equivalent for the GS card's own
Z80 (z80ex, `SoundChip_GeneralSound::_cpu`). When I needed to understand why
the coprocessor was stuck at a specific PC (an earlier investigation this
session: PC parked at `0x0C32` inside a `IN A,(FLAGS) / AND B / JR Z,loop`
polling loop, `interrupts_accepted` frozen at 0), the only path available was:

1. Read `cpu.pc` from `/state/audio/gs` (the one register that IS exposed).
2. Manually `open()` the `gs105a.rom` file in a Python script, slice out
   bytes around that address, and hand-decode the opcodes against my own
   knowledge of Z80 mnemonics.
3. Cross-reference the decoded bytes against the firmware source tree
   (`materials/gs/gs-firmware/firmware/src/*.a80`) to figure out which
   routine it was.

That is a completely manual process that exists ONLY because there is no
`GET /{id}/state/audio/gs/disasm?address=..&count=..` (or an `inspect_state`
aspect like `audio_gs_disasm`) reading through `gs->getCPUReg(regPC)` /
GS memory instead of the main Z80's. The disassembler itself
(`debugger/disassembler/z80disasm.h`) is chip-agnostic - it takes a memory
reader callback, and `GeneralSoundCard` already has the read primitives
(`TTDSaveState` gives full RAM+banking; a bank-aware read is buildable from
`applyBanking()`'s window table). This is a wiring gap, not a missing
subsystem.

**Proposal:** add `GeneralSoundCard::readMemory(uint16_t addr) const` (LLE:
through the current bank windows; LW: N/A, no address space) to the
interface, then an `inspect_state` aspect `audio_gs_disasm` (and the matching
WebAPI `GET /{id}/state/audio/gs/disasm?address=&count=`) that feeds it to
the existing `Z80Disassembler`. LW responds `{"available": false}` like the
other device aspects already do for a not-fitted card.

### 1.2 GS coprocessor register exposure is PC/SP/AF/halted only

`z80ex_get_reg` (via `GeneralSoundCard::getCPUReg(Z80_REG_T)`) can already
return the full register file - `regBC/regDE/regHL` and their shadows,
`regIX/regIY`, `regI/regR/regR7`, `regIM`, `regIFF1/regIFF2` - the enum is
right there in `z80ex.h`. `getStateAudioGS` (state_audio_api.cpp) only reads
four of them:

```cpp
cpu["pc"] = gs->getCPUReg(regPC);
cpu["sp"] = gs->getCPUReg(regSP);
cpu["af"] = gs->getCPUReg(regAF);
cpu["halted"] = gs->isCPUHalted();
```

During this session I repeatedly wanted `IFF1` (is the firmware's DI window
still open - directly relevant to the interrupt-coalescing bugs BUG-5 was
about) and `BC/DE/HL` (register-passed arguments the firmware dispatcher
uses) and had no way to read them short of a full `TTDSaveState` dump and
manually indexing into the serialized blob's Z80 register block
(`soundchip_gs.h`'s documented TTD layout, offset 24).

**Proposal:** extend the `cpu` object with the rest of the register file
(`bc`, `de`, `hl`, `af_`/`bc_`/`de_`/`hl_`, `ix`, `iy`, `i`, `r`, `im`,
`iff1`, `iff2`) whenever `hasCoprocessor()` is true. Same shape as the main
Z80's `registers` block in `inspect_state`, so nothing new to learn.

### 1.3 Banked RAM access is a single fixed window

`?ram=1` on `/state/audio/gs` dumps exactly the fixed window (RAM page 3,
`0x4000-0x7FFF`) where firmware variables live - genuinely useful, and I used
it. But an uploaded module's actual sample data and pattern data live in the
**banked** windows (2 and 3, selected by MPAG) once the firmware has parsed
and stored them, and there is no way to read those without knowing the
current `page` value and computing the bank offset by hand against the TTD
blob's RAM layout - which I never actually did this session (I always went
around it via `dump_module`, capturing the raw upload BEFORE the firmware
touched it, rather than verifying what the firmware actually stored).

**Proposal:** either (a) generalize `?ram=1` to `?ram=1&window=0|1|2|3`
using `applyBanking()`'s current window table, or (b) add a raw
`?ram=1&addr=0x8000&len=0x4000` mode that resolves through the live bank
mapping the same way the coprocessor's own memory bus would. (b) is more
useful once §1.1's disassembler needs the same resolution logic anyway -
worth sharing the implementation.

## 2. LW (lightweight card): the sequencer is a black box at runtime

This is the gap that cost the most real time this session. Diagnosing the
vibrato-bleed bug (BUG-13), the 5xx-portamento bug (BUG-14), and the song-wrap
tempo bug (BUG-16) all needed to know, **while a module was actually
playing**, things like: which row/tick is it on, what's channel 0's current
period and step size, is vibrato/tremolo currently modulating it. None of
that is visible through any automation surface. `GSModPlayer` already has
every getter needed - I added most of them this session specifically so the
*unit tests* could ask these questions directly against the C++ object:

```cpp
uint8_t songPosition() const;      // COM60 domain
uint8_t patternPosition() const;   // COM61 domain (row)
uint8_t currentPattern() const;
uint8_t speed() const;
uint8_t bpm() const;
uint32_t quantaIntoTick() const;
uint8_t tick() const;
uint8_t channelSample(int) const;
uint8_t channelVolume(int) const;
uint16_t channelPeriod(int) const;    // test/diagnostic-only right now
int64_t channelIncrement(int) const;  // test/diagnostic-only right now
```

None of this is reachable from `SoundChip_GSLightweight` (which owns the
`GSModPlayer` instance privately), so none of it is reachable from
`GeneralSoundCard`, so none of it is reachable from any automation surface.
What I actually did instead, every time I needed to inspect live playback
state, was build a **separate offline tool**
(`scratch/modtest/render_gsmodplayer.cpp`, `bpm_trace.cpp`) that links
`gsmodplayer.cpp` directly and replays a *captured* module (via
`dump_module`) outside the running emulator, then prints position/tempo
traces or renders a WAV to inspect externally. That workflow works, but it's
a static replay of a captured snapshot - it cannot show what a *live,
currently playing* card is doing, and it needed a from-scratch C++ tool each
time rather than a query.

**Proposal:**
1. Add the LW-only surface to `GeneralSoundCard`'s interface as optional
   (default-false/empty, like `captureModuleUpload`) rather than forcing the
   LLE to implement dummies: `getPlayerState()` returning a small struct
   (song position, row, tick, speed, bpm, per-channel sample/volume/period).
   `channelSample`/`channelVolume` already exist on the base interface (LLE
   reports the DAC latch, LW would report the same numbers from the same
   struct) - only the sequencer fields (position/row/tick/speed/bpm/period)
   are new.
2. Surface it in `/state/audio/gs` as a `player` object, present only when
   `implementation == "lightweight"` (mirrors how `cpu` is already
   conditional on `hasCoprocessor()`).
3. Mirror into `inspect_state`'s `audio_gs` aspect (automatic, since it just
   forwards the WebAPI body) and into the Lua/Python `gs_state()` /
   `gs_counters()`-style calls (`gs_player_state()`).
4. Contract (agreed in review, 2026-09-22): the accessors return **values**,
   never references into the card - `switchGeneralSoundCard` can delete the
   card concurrently, and a reference-returning accessor is the same
   use-after-free class as BUG-17 (`sampleInfo()`'s `const&` is fine inside
   the core; the `GeneralSoundCard`-level and JSON layers copy). Sequencer
   fields are read from the automation thread while the emulation thread
   advances the player: a torn position/tick read is cosmetic and accepted,
   the same tolerance the existing `cpu`/latch reads already have; the
   module table (§3) must be copied as a plain struct/vector so no torn
   pointer can outlive the call.

## 3. LW: no way to inspect the parsed module's structure

Related to §2 but distinct: I frequently wanted to sanity-check what
`GSModPlayer::parse()` actually extracted from a real upload - sample count,
per-sample length/volume/loop points, pattern count, song length - without
re-deriving it by hand-parsing the dumped `.mod` bytes in a Python script
(which is what I did, every single time, this whole session). `GSModPlayer`
already exposes this:

```cpp
size_t sampleCount() const;
size_t patternCount() const;
size_t songLength() const;
const SampleInfo& sampleInfo(size_t index) const;  // name, length, finetune,
                                                     // volume, loop start/length
```

**Proposal:** fold this into the same `player` object from §2 as a `module`
sub-object (sample table + pattern/song counts), or a separate
`?module=1` query flag on `/state/audio/gs` (mirrors the existing `?ram=1`
pattern) so it's opt-in and doesn't bloat every state read. Whatever the
shape, the §2 item-4 value-copy contract applies to the sample table too.

## 4. `dump_module` diagnoses the upload, not the parse

`dump_module` (added this session) writes the raw bytes the host streamed
through COM30..D2 - it answers "what did the demo send". It does not answer
"what does the card's own interpreter think that data means" - that's
exactly what §3 would answer, and the two are complementary: §3 for a quick
live check, `dump_module` + an offline reference player (as I did with
`openmpt123` this session, see `verification-findings-and-bugs.md`) for a
byte-exact cross-validation. No action needed here beyond linking the two in
docs - noted because it clarifies why §3 doesn't replace `dump_module`.

## 5. Dead and mislabeled activity counters (fixed alongside this doc)

While auditing the counters for this document I found `hostCommandsDropped`
is **dead on both personalities** - it was the "16-deep command FIFO was
full" counter from before the 2026-09-21 single-latch mailbox rewrite
(BUG-10..16 session), and nothing increments it anymore (the bump functions
it depended on, `bumpHostCommandsDropped`/`bumpHostDataDropped` in the old
`gsmailbox.cpp`, don't exist post-rewrite). `hostDataDropped` still
increments, but only on the LW card's internal param-reordering buffer
(`PARAM_QUEUE_CAPACITY=16` in `soundchip_gslw.cpp`) overflowing - a
different, LW-specific condition than what the counter's old comment
described (a shared mailbox FIFO). The LLE never increments it at all.

Comments fixed in this pass (`gsporttrace.h`, `soundchip_gs.h`,
`generalsoundcard.h`) so the counters' actual (non-)meaning is documented
where they're declared, rather than describing removed FIFO behavior.

### 5.1 The real gap underneath: no clobber counter for the new protocol

The single-latch mailbox has a genuine failure mode the old FIFO counters
used to (sort of) cover and the new ones don't: a same-direction host write
before the card has consumed the pending one **silently overwrites it** -
correct emulation of the original hardware, but silent, with **zero
counter** tracking how often it happens. A host driver with a timing bug
(polling bit7/bit0 too loosely) would lose bytes with no observable signal
anywhere in the counters or port trace today.

**Proposal:** add `hostCommandOverwritten`/`hostDataOverwritten` to
`GSActivityCounters`, incremented in `onHostCommandWrite`/`onHostDataWrite`
(soundchip_gs.cpp) and the LW equivalents when the relevant status bit was
already set at write time. Cheap (one branch on an already-computed value),
and it's the single most direct "is the host racing the card" signal the
counters could offer - directly useful for exactly the kind of protocol-
timing bug this session's mailbox rewrite was about.

## 6. How to wire §1-§5 into automation/diagnostics

Everything above described a gap and a shape for the fix in isolation. This
section is the actual integration plan: where each piece attaches to the
existing surfaces, in what order, and what it shares so five surfaces don't
mean five implementations.

### 6.0 One rule that makes all five gaps cheap: put the logic on `GeneralSoundCard`, not in `state_audio_api.cpp`

Every gap in §1-§3 is reachable today from C++ (`GSModPlayer`'s getters,
`getCPUReg`, `applyBanking()`) but only from inside the emulator core - none
of it crosses into `core/automation/`. The automation-parity work already
established the pattern that makes this a one-time cost instead of a
five-times cost: **WebAPI is the only surface that talks to the emulator
core directly; MCP/CLI/Lua/Python all forward to WebAPI** (`gs_switch_personality`
and `gs_dump_module`, added this session, are the reference examples -
`mcp-tools.cpp`'s dispatch is a literal `action.substr(3)` forward to
`/control/audio/gs`, and Lua/Python's `gs_*` functions do the same). So:

1. Add the new read primitive to `GeneralSoundCard` (`core/src/emulator/sound/chips/gs/generalsoundcard.h`)
   as an interface method with a safe default (LLE-only methods default to
   `{available: false}` on the base/LW class, mirroring `captureModuleUpload`'s
   existing optional-override pattern - §1's methods are LLE-only, §2/§3's are
   LW-only, in each case for a real reason, not a placeholder).
2. Implement it once in the concrete class that has the data
   (`soundchip_gs.cpp` for §1, `soundchip_gslw.cpp` for §2/§3 - forwarding to
   the `GSModPlayer` getters that already exist).
3. Wire it into `state_audio_api.cpp`'s `getStateAudioGS` (or a new endpoint
   for §1.1's disasm, which is a query not a state field) - this single call
   site is what MCP's `inspect_state` aspect, CLI's `state audio gs`, and the
   Lua/Python state getters all already read through, so nothing else needs
   touching for the other four surfaces to pick it up automatically.
4. Only for the two *action* additions (§1.1's disasm query takes parameters,
   so it may be worth a dedicated endpoint rather than a `?flag=1` on the
   existing GET) does each of MCP/CLI/Lua/Python need its own few lines - and
   only because each has to declare the new parameter in its own schema
   (JSON schema / CLI usage string / Lua signature / Python signature), not
   because the underlying logic differs. `emulator_manage`'s existing
   `gs_switch_personality`/`gs_dump_module` entries are the template to copy.

This is why §5.1 (the clobber counters) needs *zero* new wiring: counters
already flow through the existing `activity_counters` object on all five
surfaces, so two new fields on `GSActivityCounters` are visible everywhere
the moment they're incremented - no endpoint work at all. It is also why
§1.2 (full LLE register file) is nearly as cheap: it extends an object
(`cpu`) that's already wired everywhere, it just doesn't populate every
field of it yet.

### 6.1 Concrete field/endpoint additions

| Gap | New surface | Shape |
|:----|:-----------|:------|
| §5.1 | `activity_counters.hostCommandOverwritten`/`hostDataOverwritten` | existing `GSActivityCounters` fields, no endpoint change |
| §1.2 | `cpu.{bc,de,hl,af_,bc_,de_,hl_,ix,iy,i,r,im,iff1,iff2}` | extends the existing `cpu` object in `getStateAudioGS`, present only when `hasCoprocessor()` |
| §2 | `player.{songPosition,row,tick,speed,bpm,channels[].{sample,volume,period,increment}}` | new object in `getStateAudioGS`, present only when `implementation == "lightweight"` |
| §3 | `player.module.{sampleCount,patternCount,songLength,samples[].{name,length,finetune,volume,loopStart,loopLength}}` | sub-object of §2's `player`, or its own `?module=1` flag if `player` should stay cheap by default |
| §1.3 | `GET /{id}/state/audio/gs?ram=1&window=0..3` (extend) | reuses `applyBanking()`'s window table, same response shape as today's `?ram=1` |
| §1.1 | `GET /{id}/state/audio/gs/disasm?address=&count=` (new) | feeds `GeneralSoundCard::readMemory()` (new, needs §1.3's bank resolution) to the existing `Z80Disassembler` |

Rollout order (each step keeps the suite green on its own, per the standing
per-commit-green rule): §5.1 -> §1.2 -> §2 -> §3 -> §1.3 -> §1.1, matching
the priority list below.

## 7. TTD implications

The user's question that prompted this section: does any of §1-§5 matter for
Time Travel Debug specifically, beyond the read-only automation surfaces?
Two answers, one already acted on this session, one still open.

### 7.1 A real bug found while checking, already fixed (BUG-17)

`TTDPeripheralRegistry` registers `GeneralSoundCard*` by raw pointer exactly
once, at `StartRecording`/session-load (`RegisterModelPeripherals`, private).
`SoundManager::switchGeneralSoundCard` deletes the outgoing card and installs
a new one (`delete _gs; _gs = card;`) with no TTD awareness. Before this
session's automation-parity pass, reaching a personality switch required
hand-driving the WebAPI; after it, all 5 surfaces can trigger one via
`gs_switch_personality`/`switch_personality`/`gs switch` - so "TTD recording
+ a switch call" went from a theoretical combination to a one-liner away on
any automation surface. If a switch happens while recording is active, the
next checkpoint's `TTDSaveState()` call runs on freed memory - reproduced as
a real `SIGSEGV` (exit 139) while building the regression test below, not
just reasoned about.

**Fixed:** `TimeTravelManager::UpdatePeripheral(PeripheralId, TTDSerializable*)`
(`timetravelmanager.h`), called from `switchGeneralSoundCard` immediately
after the pointer swap. Full writeup: `verification-findings-and-bugs.md`
BUG-17. Regression tests: `core/tests/debugger/ttd/ttdgeneralsoundswitch_test.cpp`
(one of the three reproduces the SIGSEGV directly when reverted).

### 7.2 A related, deeper gap this fix does not close

Even with BUG-17 fixed, TTD's checkpoint format has no concept of "the
registered device's shape changed since this checkpoint was recorded."
`GeneralSoundCard::TTDStateSize()` is fixed for LLE (RAM size is a boot-time
config) but **varies at runtime for LW** - it includes the uploaded module's
byte count (`TTD_FIXED_STATE_SIZE + 4 + _store.size() + 4 + _player.serializeStateSize()`).
So seeking a timeline backward past either of:

- a personality switch (LLE's and LW's `TTDStateSize()` differ by
  construction - different fields entirely), or
- for LW alone, a *different-sized module upload* at an earlier point in the
  same recording (no personality change needed)

lands on a checkpoint whose GS blob size no longer matches the currently
registered device. `TTDPeripheralRegistry::RestoreAll` already guards this
correctly at the memory-safety level - `state.size() != device->TTDStateSize()`
skips the load rather than reading/writing out of bounds, counted in
`TTDRestoreReport::sizeMismatches` (see `ttdperipheralregistry_test.cpp`'s
`BlobFiledUnderTheWrongDeviceIsRejected` for the existing coverage of this
guard, under a different trigger). What it does *not* do is tell anyone: the
GS card's state after such a seek is silently whatever it was before the
seek, `sizeMismatches` increments, and **no automation surface exposes that
counter today** (confirmed - `grep -rn sizeMismatches core/automation/`
finds nothing).

**Proposal (not yet implemented):**
1. Surface `TTDRestoreReport` (or a running total of it, since a single seek
   can touch many checkpoints in the reverse-executor path) through the TTD
   status endpoint (`GET /{id}/state/ttd` or equivalent - wherever
   `TTDSessionState`'s "idle"/"recording"/"detached" string already lives,
   §10.4 of the parent TTD contract) as e.g.
   `restore.{restored,missingBlobs,sizeMismatches,unclaimedBlobs}` for the
   most recent seek, so a stale-after-seek GS card (or any other device hit
   by the same guard) is at least visible, not just silently wrong.
2. Longer term, GS specifically could avoid the size-mismatch path entirely
   for the LW-module-size case (not the personality-switch case, which is a
   genuine shape change) by fixing `TTDStateSize()` to a config-time ceiling
   (e.g. `gsRamKB`-derived) and padding/truncating the stored module inside
   that fixed envelope, the same way LLE's RAM size is already fixed at
   config time rather than tracked per-upload. That is a real behavior
   change to `soundchip_gslw.cpp`'s TTD serialization, not just a diagnostics
   addition - out of scope for this proposal, noted for the next TTD-adjacent
   GS pass.

## 8. Future feature (P2): per-source audio capture

`POST /audio/capture` captures the master mix only - `AudioCaptureAnalyzer`
has no source selector, even though `AudioSourceType` already enumerates the
per-device buffers (`SoundManager::getBuffer()` hands out GeneralSound, AY,
beeper, covox views of the same frame). A `source` field on the capture
start body would give a GS-only WAV tap with everything else unmuted - the
cheapest live A/B tool for the next render-quality pass (LLE vs LW, same
game moment). This session never blocked on it (the offline
`scratch/modtest/` re-render tools covered the need), so it is a future
feature, not a session gap: P2.

## Priority, if this gets picked up

1. §7.1 (TTD use-after-free) - already fixed and tested this session; listed
   here only for completeness, nothing left to do.
2. §5.1 (clobber counters) - smallest remaining change, closes a real blind
   spot in the rewrite this session shipped.
3. §2/§3 (LW player + module state) - highest value for the next round of
   LW content bugs; likely the single biggest time-saver based on this
   session's actual bottlenecks.
4. §1.2 (full LLE register file) - small, mechanical, same shape as the main
   Z80's exposure already has.
5. §1.1/§1.3 (GS disasm + banked RAM) - most work (shared bank-resolution
   logic), highest value specifically for LLE firmware-level triage.
6. §7.2 (TTD restore-report visibility) - independent of the GS work above;
   worth doing alongside whichever GS item lands first since it shares the
   TTD status endpoint most of those touch anyway.
7. §8 (per-source audio capture) - P2 future feature; picked up whenever
   the next render-quality pass wants live A/B taps instead of offline
   re-renders.
