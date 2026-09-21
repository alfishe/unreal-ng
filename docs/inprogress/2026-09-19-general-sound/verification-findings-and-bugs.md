# General Sound: Playback Verification Findings and Bugs

- **Date**: 2026-09-20
- **Scope**: Verification of the full General Sound (GS) playback chain in unreal-ng against
  the actual GS firmware sources; root-cause analysis of the reported symptom
  *"software sees GS and pushes data, but no playback"*.
- **Method**: Every claim below is grounded in the firmware source at
  `/Volumes/TB4-4Tb/Projects/emulators/github/GeneralSound/firmware/src/v105b/src/`
  (in-tree copy: [`materials/gs/gs-firmware/`](materials/gs/gs-firmware/)) — no behavior inferred
  from documentation alone. Chip-level claims are backed by a passing GTest harness;
  application-level claims by live WebAPI-driven runs of real ZX programs through the real
  port decoder.

## 1. Verdict Summary

| Suspected link (original bug report) | Verdict | Evidence |
|:-------------------------------------|:--------|:---------|
| 37.5 kHz interrupts | **Working** | 8237 interrupts over 10 frames = 749.7/frame = exactly 37.5 kHz; IM2 vectoring lands at `#4040` (see §3, test 4) |
| Push to DAC | **Working** | DAC latch fires on memory reads in `#6000-#7FFF`; 8244 fetches in the interrupt-path test; live channel-sample animation during module playback (see §4) |
| Sound rendering and mixing | **Working** | blip_buf delta synthesis reaches the mixed frame buffer (peak 11693 covox / 4878 interrupt path); SoundManager mixes `getBuffer()` unmuted at 1.0 |

**The emulator implementation is functionally correct and complete.** The only real defect found
is a WebAPI/MCP automation race that derails the GS coprocessor when control actions are used
while the emulator runs — which reproduces the exact reported symptom on the automation path
(see §6, BUG-1).

## 2. Firmware Output Architecture (source-decoded)

The module player output path, decoded from the v105b firmware sources. This architecture is
what the emulator must (and does) reproduce.

### 2.1 The chain: GEN → QUANTUM → QTPLAY → interrupt handler

1. **GEN** (`GEN_L.a80`) — the per-channel resampler. It **writes** mixed sample quanta into the
   DAC window (RAM page 3, `#6000-#7FFF`) with plain `LD (DE),A / INC E` stores
   (`DE = high DAC0 + QTFREE + CHRDN : SGENOFF`). Silence tail is padded with `#80`.
   **Writes are plain RAM stores — they are NOT DAC events.**
2. **QUANTUM** (`QUANTUM.a80`) — builds a 4-byte slot descriptor per quantum in `QTMAP`
   (8 slots × 4 bytes): `[0]=SGENOFF`, `[1]=slot+#60+INTOFF` (buffer offset), `[2..3]=INTTB`
   (tick count). Also copies 4 volume-request bytes into `VOLTAB`. Decrements the tempo clock
   `TCKLEFT` by `-SGENOFF` per quantum; at zero runs `EFXINT` (sequencer tick: `GETROWS`,
   effects, pattern/song advance).
3. **QTPLAY** (`INTTST.a80`) — reads the current descriptor (`QTBUSY`), copies the
   timing-padded `INTn` handler (18 bytes, `LDIR`) to **`#4040`**, sets `HX` = buffer offset
   and `A'` = tick counter, and latches the 4 per-quantum volumes to **VOL0-VOL3**
   (GS ports `#06-#09`).
4. **Interrupt handlers** (`INT0..INT7`, `INTTST.a80`) — fired at 37.5 kHz. Each invocation
   builds `DE = HX : A'` and executes **`LD A,(DE)` — a READ of one sample byte** from the
   buffer. `INC E` walks the quantum; on `E` wrap the handler jumps to `QTDONE`, which
   advances `QTBUSY` to the next quantum and re-programs the ISR + timing.

### 2.2 Consequence: the read-only DAC latch model is CORRECT

Each ISR read of `#6000-#7FFF` feeds the read byte to the DAC channel selected by address bits
9-8. Writes never reach the DAC. `SoundChip_GeneralSound::readMem()` calling `dacFetch()` while
`writeMem()` does not (`soundchip_gs.cpp`) is therefore **faithful to the hardware/firmware**,
not a missing feature.

### 2.3 Tempo clock

`TCKLEFT` is decremented once per quantum by `-SGENOFF` (QUANTUM). Default `TICKLEN = 750` →
750 / 37500 = 20 ms per tick = 50 ticks/s — standard MOD tempo. The 37.5 kHz interrupt is the
sample clock; the quantum descriptors are the schedule.

### 2.4 Command protocol essentials (COM_L.a80 / COM_H.a80)

- **Data-first rule**: `COM16` (put byte), `COM13` (jump), `COM30` (load module),
  `COM31` (start module) all consume their parameter with `IN A,(DATRG)` at dispatch time.
  Host order is: `OUT (#B3), data` **then** `OUT (#BB), command`.
- **WTDTL** (exact semantics):
  ```asm
  WTDTL   IN A,(FLAGS)
          AND #81
          JR Z,WTDTL        ; wait until ANY flag (command bit0 or data bit7)
          IN A,(DATRG)
          JP P,COMINT1      ; bit7 clear = command arrived, no data → ABORT handler
          JP (IY)           ; bit7 set = data pending → consume, continue handler
  ```
  `IN A,(n)` does not set flags; `JP P` tests the sign result of `AND #81`.
  Arbitrary data bytes never stall it: the loop only spins while both flags are clear.
- **COM30** loads + parses only (PLAYMD). **COM31** is required to start playback:
  consumes modnum, acks, sets `BUSY=#FF`, `MODULE/CURMOD`, `MTSTAT=%00000011`, then
  `INITPAT`, `EFXGTNT`, **`PROCESS=#FF`**.
- **Dispatcher main loop** (`COMINT_`): polls FLAGS, dispatches on bit0, and executes
  `CALL ENGINE` whenever `PROCESS != 0 && BUSY == 0` — this is what runs the player.
- **ENGINE** (`ENGINE_L.a80`): `MTSTAT` bit6 set → idle-return; bit7 → stop
  (`PROCESS=0`). Otherwise calls `GEN` for active SFX channels and `QUANTUM` for the
  4 music channels.
- **COM0E** (covox): acks, then a tight loop `IN A,(DATRG)` → `LD (HL),A / LD (BC),A`
  into DAC0/DAC2 → re-reads both (the re-reads are the DAC latches) → exits when a new
  command arrives. No flag wait, no interrupt dependency.

## 3. Chip-Level Verification (bootdiag harness)

Harness: [`core/tests/emulator/sound/chips/soundchip_gs_bootdiag_test.cpp`](../../../core/tests/emulator/sound/chips/soundchip_gs_bootdiag_test.cpp)
(4 tests, all passing, firmware-protocol-accurate).

| # | Test | Asserts | Result |
|:--|:-----|:--------|:-------|
| 1 | POST completes | `I=0x17`, `PC < #2000` (ROM dispatcher), CPU not halted | PASS |
| 2 | High-command round-trip | COM23 `NUMPG` = pairs-1: 3/7/15 for 128/256/512 KB | PASS |
| 3 | Covox stream produces audio | COM0E + 2000 frames of alternating `#80/#D0/#30`; `dacFetches > 1M`; in-loop buffer peak 11693 | PASS |
| 4 | Interrupt path reaches DAC | putByte-installed handler at `#4040` (`LD A,(#6000); EI; RET`); 10 frames: `interrupts=8237` (37.5 kHz), `dacFetches=8244`, buffer peak 4878, `PC=#500B` (JR self) | PASS |

Notes:
- RAM detection semantics: `NUMPG` = verified RAM **pairs − 1** (INIT_L.a80 INC E per
  verified pair; empirically {3,7,15} for {128,256,512} KB).
- IM2 vectoring: firmware sets `I=#17` at POST; the all-`#40` vector table plus the
  `#FF` bus vector resolves the ISR address to `#4040` — both for the ROM default handler
  and after QTPLAY installs its ISR at the same address.
- TTD state blob layout: fixed state first, then raw RAM — patch RAM at offset
  `TTDStateSize() - ramKB*1024`.

## 4. Application-Level Verification (real software path)

All drives go through the **real ZX CPU → real PortDecoder → firmware** on the emulation
thread. The WebAPI GS *control* endpoints were deliberately NOT used while running
(they are racy — see BUG-1). Method: `POST /memory/write` the ZX program + data,
`PUT /registers/pc`, `POST /resume`, then sample state.

### 4.1 Covox streaming (real ZX program)

A 14-byte ZX streamer at `#8000` sends `SC #0E` then toggles `#D0/#30` via `OUT (#B3)`.
Result: GS entered the covox handler (`PC=#04A5`), DAC channels 0/2 latched the streamed
square wave — the full host → decoder → firmware → DAC → mix chain works under the real
emulation thread.

### 4.2 Real module playback (COM30 load → COM31 start)

Real 4-channel M.K. module `_test_spd_aft_loop.mod` (4348 bytes) streamed from `#9000`
by a 58-byte ZX program (`scratch/gs_prog.json`, idle `JR` at `#8038`):

1. `SD #01; SC #30` → GS LOAD consumed all 4348 bytes, `#D2` terminator processed, PLAYMD
   parsed the module (header/signature/sample table).
2. `SD #00; SC #31` → acked (`cmd=49`), `PROCESS=#FF` set, ZX reached its idle loop.
3. Live (running, unpaused) sampling showed playback **running**:

| Evidence | Observation |
|:---------|:------------|
| QTPLAY ISR installed | GS PC sampled at `#4042-#4045` (inside the 18-byte ISR copied to `#4040`) |
| ENGINE/GEN executing | GS PC sampled at `#CDED` (switched RAM page code) plus dispatcher `#270-#28A` |
| DAC reads animating | channel 0 sample: 225 → 69 → 128 → 0 → 138 across 0.5 s samples |
| Per-quantum volumes | `volume=[63,63,63,63]` latched via VOL0-3 |
| Card state | `available=true`, 512 KB RAM, ROM loaded, CPU not halted |

Channels 1-3 stay at `#80` because the test module only voices channel 0 — not an emulator
issue. The first load-only attempt (without COM31) correctly stayed silent, which is exactly
what the firmware does (COM30/PLAYMD parses but never starts the engine).

## 5. Configuration and Wiring Audit (no issues found)

| Area | Finding |
|:-----|:--------|
| Models with GS | `pentagon128k`, `pentagon512k`, `scorpion`, `profscorp`, `atm3`, `atm710` — all `GSType=Z80`, `GSVol=8000`, `gs105a.rom` (ATM pair enabled 2026-09-20, see §5.1) |
| Port decode | GS rows present in `portdecoder_pentagon128.cpp` (`#00B3/#00BB/#0033`, low-byte mask), `portdecoder_scorpion256.cpp` (`(#xx & 0x00F7)==0x00B3` chain) and `portdecoder_atm710.cpp` (same F7-mask family + `#33` full low byte; ATM3 inherits the ATM710 chain) |
| Mixer registry | GS device registered `mute=false, volume=1.0` (`SoundManager` `AudioDeviceInfo`) — not muted by default |
| features.ini | `[sound] state=on` |

### 5.1 GS enabled on the ATM models (2026-09-20)

Follow-up to this audit: both ATM configs (`data/configs/atm3`, `data/configs/atm710`) carried
the full GS wiring (`GSVol`, `gs105a.rom` ROM entry) but were shipped `GSType=NONE`, and the
ATM710 decoder (shared by ATM3 through inheritance) had no GS arms — the card would have been
created by a config flip alone but unreachable from the main CPU. Changes:

- `portdecoder_atm710.cpp`: `DecodePortIn` arm for the `(#xx & 0x00F7)==0x00B3` family (read
  side, normalized to the canonical keys) and `DecodePortOut` arms for the same family plus
  `#33` (write-only reset/NMI). Verified against every existing ATM arm for collisions — A0=1
  excludes `#FE`/AY, A1=1 excludes `#7FFD`, low bytes differ from `#77`/`#xF7` (manager group)
  and the `0x9F` palette mask; placed ahead of the Beta128 arm like the Pentagon table rows.
- Configs: `GSType=NONE` → `GSType=Z80` in both ATM `unreal.ini` files.
- Regression tests: `PortDecoder_ATM710_Test.GSHostPortsNormalizeMirrorsToCanonicalKeys`,
  `.GSHostPortsReachableDuringTrdosSession`, `PortDecoder_ATM3_Test.GSHostPortsReachBaseDecodeThroughOverrides`
  (mock `PortDevice` registered via `RegisterPortHandler` under the canonical keys, exactly the
  production `SoundManager::attachToPorts` mechanism).

## 6. Bugs Found

### BUG-1 (Critical): WebAPI/MCP GS control races the emulation thread

- **Where**: `postControlAudioGS` actions `send_command` / `send_data`
  (`core/automation/webapi/src/api/state_audio_api.cpp`) → `gs->sendCommand()/sendData()` →
  `flush()` → `z80ex_step()` on the **same z80ex core the emulation thread is executing**.
- **Root cause**: drogon serves requests on worker threads (`setThreadNum(8)`); the GS control
  endpoint takes no lock and does not use the (advisory, unused) emulator run-control claim.
  Cross-thread `z80ex_step` on a non-thread-safe core corrupts GS CPU state.
- **Reproduction**: while a real GS program runs, `POST /control/audio/gs` `send_data`
  derails the coprocessor — observed `PC=#0230` (inside a data table), `MPAG=11`,
  `SP=#8000`. Symptom: "data was pushed but nothing plays" — the exact reported bug,
  on the automation path.
- **Safe parts**: `reset`/`reset_card`/`nmi` are flag sets (no stepping) and the read-only
  `GET /state/audio/gs` only risks benign torn reads.
- **Proposed fix**: queue GS control mutations and apply them on the emulation thread during
  the frame lifecycle (or serialize the endpoint with the same mutex/run-control the core
  uses). Read-only state endpoints can stay lock-free.

### BUG-2 (Minor): register PUT silently zeroes hex-string values

`PUT /emulator/{id}/registers/pc` with `{"value": "0x8000"}` → jsoncpp `asUInt()` = 0 →
PC silently set to 0 (`debug_api.cpp setRegister`). Reproduced: program never ran, PC=0.
**Fix**: parse hex-prefixed strings or reject non-numeric values with HTTP 400.

### BUG-3 (Minor, unrelated to GS): default build target broken by stale PoC

`ninja -C cmake-build-release` (default target) fails in `tools/poc/011-ttd-v2-capture-analysis`
(stale TTD API usage — pre-existing, not touched by this work). Workaround: build the
`unreal-qt` target only. The PoC needs rebasing onto the current TTD API or exclusion from
the default target.

### BUG-4 (Major, FIXED): DRC anti-windup defect floated GS playback pitch

**Symptom**: GS module playback pitch floats continuously, even on simple, low-load mods.
The first hypothesis — unstable ZX↔GS t-state synchronization — is **disproven**. The
GS coupling audit (2026-09-20) shows the contract "GS frequency = coefficient against the
main t-state counter" already holds exactly: `currentZxTacts()` reads the live main-CPU
`AudioTstate`; the coefficient `GS_CLOCK_HZ/zxBaseHz/hostMultiplier` is the only
frequency-dependent term; the recalculation converts the *absolute* ZX offset per call
(no accumulating rounding); GS-domain counters are int64; all standard frame budgets are
exact integers (48K 239616, Pentagon 245760 GS cycles = frame × 24/7, since frame lengths
are multiples of 7). Worst case is ±1 GS cycle of phase quantization — frequency error
zero by construction.

**Root cause**: the DRC PI controller (`SoundManager::updateDrcControl`) held ring
occupancy at the 40 ms setpoint by trimming the resampler ratio within ±0.5% — and the
trim *is* a pitch trim (±0.5% = ±8.6 cents) applied to the whole mix. Its integral clamp
(±50) allowed `KI·I` to reach **8× the actuator rail**, so any sustained disturbance
(cold start, emergency refill, pause/resume, device reroute) pinned the integral at the
clamp and produced seconds of railed, **wrong-signed** trim afterwards — audible as the
mix (GS loudest: sustained pure tones) gliding ±8.6 cents. In addition `Kp = 0.08`
converted ±0.4 ms occupancy ripple into a continuous ±0.2-cent trim meander.

**Fix** (`soundmanager.h/.cpp`, 2026-09-20):
1. Integral clamp = the actuator's usable range: `±DRC_MAX_TRIM/DRC_KI` (= ±6.25).
2. Back-calculated integral while the output is railed — `I` always stays consistent
   with the saturated output and leaves the rail the first frame the error allows.
3. Soft error deadband `DRC_ERR_DEADBAND = 0.02` (±0.8 ms) — sub-band occupancy ripple
   no longer modulates pitch; tracking beyond the band is unchanged.

**Verification**: exact discrete simulation (`scratch/gs_drc_sim.py`) — wrong-signed
trim after a 300 ms overfill release drops **8.63 → 0.34 cents**, occupancy no longer
undershoots (min 40.8 ms vs 26.1 ms). Live probe (`scratch/gs_drc_probe.sh`,
`UNREAL_AUDIO_DIAG=1`): steady-state trim ±0.0003 (≤ ±0.5 cents), integral bounded
(±0.27 vs ±50), occupancy 38.6–39.8 ms, GS module playing throughout. Regression tests
in `sound_adaptivity_test.cpp`: `DRC_WindupRecoveryAfterSustainedOverfill`,
`DRC_DeadbandRippleKeepsUnityBypass`, `DRC_CompensatesSustainedRateOffset` — sound
suite 94/94 green. The temp `[drc-diag]` stderr telemetry follows the same
remove-with-the-diag-test rule as the mainloop one.

### BUG-5 (Critical, FIXED): Interrupt pulse-loss modulated the 37.5 kHz GS sample clock

**Symptom**: steady tones in GS modules still floated in pitch after the BUG-4 fix —
measured on the pre-DRC core-rate dump (WAV tap, before any resampler/DRC), so the drift
was generated inside the GS chip domain itself.

**Diagnosis** (per-frame `[gs-diag]` instrumentation + dual raw dump,
`scratch/gs_tone_probe.sh` / `gs_tone_analyze.py`, 2026-09-20): interrupts accepted per
frame scattered **740–767 around a mean of ~756** instead of the exact 768 boundaries a
Pentagon frame contains (245760/320); the pre-DRC tone wobbled p-p 6.98 cents (std 4.43,
5 s bucket means drifting −1.4…−4.2). The chronic ~1.6% under-production also drained the
device ring and railed the DRC trim (occupancy hit 0) — the downstream DRC wobble was a
symptom, not a cause. (The earlier “trim steady ≤±0.5 cents” observation was an aliasing
artifact of sampling every 250th frame.)

**Root cause**: `runTo()` treated the periodic INT as a one-shot pulse — at each 320-cycle
quantum boundary it attempted `z80ex_int()` exactly once; whenever the CPU could not
accept (IFF1=0 — inside the firmware ISR before its `EI`, or the QTDONE quantum-wrap path
whose 18-byte `LDIR` re-programming runs masked), the boundary was consumed and the
request **lost**. Each lost request is one missing sample step of the firmware’s
interrupt-driven mixer, i.e. instantaneous replay-rate modulation. The design (§2.4) had
always specified level-hold semantics (Xpeccy `intrq |= Z80_INT`); the implementation did
not match it.

**Fix** (`soundchip_gs.h/.cpp`, 2026-09-20): `_intPending` flag — the boundary asserts the
request, the top of the `runTo()` loop retries `z80ex_int()` every iteration until the CPU
accepts, and only acceptance clears it. Persisted as bit 1 of TTD fixed-state byte 23
(bit 0 = nmiPending; pre-fix captures load as no-pending — they could never hold one).

**Verification**: regression `5_InterruptLevelHoldNoLoss` — a 48%-duty DI loop (masked
window < 320 cycles) delivers exactly `25·239616/320 = 18720/18721` interrupts where the
pulse model delivers ~52%; the EI-parked stub is exact. Live re-probe: interrupts/frame
**{768: 1253, 769: 17}** (the 769s are frame-edge carry), pre-DRC tone **p-p 0.00 cents,
std 0.00** across all 52 windows, settled trim ±0.001, occupancy 1574–1905 frames, never
empty. Remaining known artifact: a startup-only DRC rail-bleed (integral filled from an
empty ring during ~6 s of silence can color the first ~0.4 s of audio ≤ +7 cents before
unwinding; from 5 s onward the device feed is also 0.00 cents) — follow-up candidate, not
steady-state float.

### BUG-6 (Critical, FIXED): NeoGS RAM default bled into the classic card and lost the boot race on scorpion-family models

**Symptom**: ZONE128.SCL (Betadisk GS trainer) played GS music on every model except
the scorpion family (SCORPION, PROFSCORP) — the trainer fell back to its no-GS path and
parked in its menu loop at #8062 with the GS firmware idle in the dispatcher.

**Diagnosis** (ZX-side port trace + 30 ms GS-state timeline, `scratch/gs_scorp_zxtrace.sh` /
`gs_timeline_probe.sh`, 2026-09-20): port decode and stimulus were identical and correct
on SCORPION vs PENTAGON (stray `OUT (#F3BB),0xF3` boot-loader write, trainer `OUT (#BB),0`,
`IN (#BB)`); the divergence was the value read — **0xFF on SCORPION vs 0x7E on PENTAGON**.
The trainer waits ~167 ms (10 frames) after the probe and does `CP #7E` on the status byte:
only the exact idle signature (bits 1–6 read as 1, bit0 and bit7 clear) takes the GS-present
path. 0xFF (bit0 command pending + bit7 boot latch byte) meant the probe landed **during
the firmware POST**: scorpion-family machines fastdisk-boot to the probe in ~0.7 s of
emulated time while the 512 KB POST RAM walk takes ~0.78 s, and the POST-end `OUT (#05)`
discards any command latched meanwhile. Pentagon's slower boot (~3 s) always probed after
POST.

**Root cause**: SoundManager passed `[NGS] RamSize` into the classic card. Shipped configs
default that key to 2048 KB (a NeoGS value — `config.cpp` itself notes RamSize "only
matters for NeoGS"), and the card clamp maps it to 512 KB — quadrupling the POST versus
the stock 128 KB card that every GS title must support. Emulator fastdisk boot is far
faster than a real floppy load, so the oversized POST lost a race real hardware never
sees.

**Fix** (`soundmanager.cpp` + `soundchip_gs.h`, 2026-09-20): classic `GSType=Z80` cards are
created with the stock geometry constant `RAM_SIZE_STANDARD_KB` (128 KB); `[NGS] RamSize`
remains a NeoGS-only key. The chip constructor still accepts and clamps 128–512 KB for
expansion-card emulation.

**Verification**: regression `RamSize_NeoGSConfigDoesNotLeakIntoClassicCard`; live boot of
ZONE128.SCL on SCORPION / PROFSCORP / PENTAGON — POST walks 4 pairs and ends by ~0.27 s,
the stray #F3 is consumed instantly, the trainer passes `CP #7E` (`IN (#BB)` = 0x7E at
#801D on all three models), uploads its 19,649-byte module (complete post-fix trace; the
pre-fix Pentagon capture stopped at the 200K-event cap mid-upload and under-counted) —
fits the stock card with a 6.6× margin — and leaves the GS firmware executing the
interrupt-driven player continuously (68 % of timeline samples in player code on SCORPION).

## 7. Non-Bugs (verified correct — do not "fix")

- **`data_pending=true` after COM31**: `OUT (OUTRG)` (`gsOut` case `0x03`) sets
  `_status |= 0x80` and `_dataToHost` — the GS legitimately holds an unread reply for the
  host. Correct firmware behavior; hosts read it via `IN (#B3)` or ignore it.
- **Read-only DAC latch** (§2.2) — matches the firmware's drain-by-read ISR design.
- **`gsIn` case `0x02` clearing status bit7** — matches WTDTL's flag semantics
  (data flag clears when the firmware consumes DATRG).
- **Static channel samples while paused** — a sampling artifact of the observing script
  (pause freezes the emulator), not a playback defect.

## 8. Verification Environment Notes

- Model name must be `"PENTAGON"` (runtime-authoritative list:
  `GET /api/v1/emulator/models` with `creatable` flags).
- Register values in WebAPI bodies must be **decimal** (see BUG-2).
- Port 8090 may be held by an `UnrealNGCube.app` simulator process — `pkill -9 -f UnrealNGC`
  if `pkill unreal-qt` is not enough, or launch with `UNREAL_WEBAPI_PORT=<port>` to redirect
  the WebAPI listener (added 2026-09-20 during the BUG-6 investigation).
- To exercise GS without the race: drive the ZX side
  (`/memory/write` + `/registers/pc` + `/resume`), never the GS control actions, while running.

## 9. Artifacts and References

| Artifact | Path |
|:---------|:-----|
| Chip-level test harness | `core/tests/emulator/sound/chips/soundchip_gs_bootdiag_test.cpp` |
| DRC windup probe / simulation | `scratch/gs_drc_probe.sh`, `scratch/gs_drc_sim.py` |
| Pitch-drift probe / analyzer | `scratch/gs_tone_build.py`, `scratch/gs_tone_probe.sh`, `scratch/gs_tone_analyze.py` |
| DRC regression tests | `core/tests/emulator/sound/sound_adaptivity_test.cpp` (BUG-4) |
| Module playback script | `scratch/gs_mod_verify.sh`, live monitor `scratch/gs_mod_live.sh` |
| ZX loader program (58 B) | `scratch/gs_prog.json`; module bytes `scratch/gs_mod.json` |
| Covox / early verification | `scratch/gs_zx_verify.sh`, `scratch/gs_zx_verify2.sh` |
| Scorpion boot-race probes | `scratch/gs_scorp_zxtrace.sh`, `scratch/gs_timeline_probe.sh`, `scratch/gs_disasm.py` |
| Emulator implementation | `core/src/emulator/sound/chips/soundchip_gs.cpp` / `.h` |
| Firmware sources | `/Volumes/TB4-4Tb/Projects/emulators/github/GeneralSound/firmware/src/v105b/src/` (`COM_L`, `COM_H`, `INIT_L`, `LOAD_L`, `ENGINE_L`, `QUANTUM`, `PLAY`, `GEN_L`, `INTTST` `.a80`) |
| In-tree firmware copy | [`materials/gs/gs-firmware/`](materials/gs/gs-firmware/) |
| Test module | `mods/_test/_test_spd_aft_loop.mod` in the GeneralSound repo |

### Remaining open items

- [ ] Implement the BUG-1 fix (serialize GS control with the emulation thread).
- [ ] Fix BUG-2 (hex-string register values).
- [ ] Repaired/rebased PoC `011-ttd-v2-capture-analysis` or exclude from default build (BUG-3).
- [ ] Startup DRC rail-bleed: faster integral unwind (or fill-phase freeze) so the first
      ~0.4 s of audio after a cold start cannot inherit a railed trim (BUG-5 verification
      note) — cosmetic, steady-state is exact.
- [x] The first "specific GS software is silent" report materialized as ZONE128.SCL on the
      scorpion family and was root-caused as BUG-6 (boot race, fixed 2026-09-20). Keep the
      standing rule: every mechanism GS software uses (COM0E/COM16/COM18/COM30/COM31,
      interrupts, DAC, mixing) is verified working — a new silent title means a new
      investigation, not a reopened old one.
