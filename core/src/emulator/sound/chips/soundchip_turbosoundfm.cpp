#include "soundchip_turbosoundfm.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/cpu/core.h"
#include "emulator/notifications.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/soundmanager.h"

/// region <Core loop (§5.2)>

uint64_t SoundChip_TurboSoundFM::nowT() const
{
    // The YM2203 is clocked from the AY socket, not the CPU: AudioTstate
    // already undoes the Scorpion hardware turbo (§4). The HOST speed
    // multiplier is deliberately not applied (verification report C10).
    // No core: a standalone device (serializer tests) sits at T 0
    if (!_context->pCore)
        return 0;
    return _context->emulatorState.AudioTstate(_context->pCore->GetZ80()->t);
}

void SoundChip_TurboSoundFM::fmHalfTick(int chipIndex, int64_t h)
{
    TsfmChip& c = *_chips[chipIndex];
    TsfmOutputState& o = c.out;

    // Consume every word that has landed by half-tick boundary h into the
    // hold register (§6.2). The tap sees the raw, pre-mute DAC stream;
    // words are 72 T apart at /6, half-ticks 8 T, so each word is held for
    // exactly 9 half-ticks - across frame boundaries too, since the cursor
    // and the words share one rebased timeline. Signed compare: words left
    // over from the previous frame sit at negative T after the rebase
    bool consumed = false;
    while (!c.words.empty() && int64_t(c.words.front().t) <= h)
    {
        o.hold = static_cast<double>(c.words.front().word) / 32768.0;
        if (o.nativeTap->isActive())
            o.nativeTap->push(static_cast<float>(o.hold), static_cast<float>(o.hold));
        c.words.pop();
        consumed = true;
    }

    // Mute-at-hold-input (§6.2): the board mute grounds the DAC data line,
    // so the filter (and its state) sees silence while FM is disabled -
    // no click on unmute, the decimator stays warmed up. The output coupling
    // capacitor sits after the DAC buffer, so it sees the gated value too
    const double gated = _board.fmEnabled ? o.hold : 0.0;

    // First live word after a flush (TTD seek, resume after a gap): the chip
    // may be mid-sound; pick the coupling up at the level it is fed instead
    // of passing a step from the flushed 0 to it
    if (consumed && o.couplingSettlePending)
    {
        o.coupling.settle(gated);
        o.couplingSettlePending = false;
    }
    const double sample = o.coupling.filter(gated);
    o.lastFed = sample;
    if (_hqEnabled)
        o.decimator.feedSample(sample);
    else
    {
        o.lqSum += sample;
        o.lqCount++;
    }
}

double SoundChip_TurboSoundFM::fmLqSample(int chipIndex)
{
    TsfmOutputState& o = _chips[chipIndex]->out;
    if (o.lqCount == 0)
        return o.lastFed;  // no half-tick landed on this output sample
    const double sample = o.lqSum / o.lqCount;
    o.lqSum = 0.0;
    o.lqCount = 0;
    return sample;
}

void SoundChip_TurboSoundFM::flushOutputStage()
{
    for (auto& c : _chips)
    {
        c->out.hold = 0.0;
        c->out.coupling.reset();
        c->out.couplingSettlePending = true;
        c->out.lastFed = 0.0;
        c->out.lqSum = 0.0;
        c->out.lqCount = 0;
        c->ssg.decimatorLeft().clearHistory();
        c->ssg.decimatorRight().clearHistory();
        c->out.decimator.clearHistory();
    }
}

void SoundChip_TurboSoundFM::queueSsgWrite(TsfmChip& c, uint8_t reg, uint8_t value)
{
    if (_synthesisSuppressed)
    {
        // Nothing renders, so no tick would ever take it: the generators
        // simply follow the register file
        c.ssg.applyRegister(reg, value);
        return;
    }
    if (c.ssgWrites.full())
    {
        c.ssg.applyRegister(c.ssgWrites.front().reg, c.ssgWrites.front().value);
        c.ssgWrites.pop();
    }
    c.ssgWrites.push(SsgWrite{int64_t(_syncedT), reg, value});
}

void SoundChip_TurboSoundFM::applySsgWrites(int64_t t)
{
    for (auto& c : _chips)
    {
        while (!c->ssgWrites.empty() && c->ssgWrites.front().t <= t)
        {
            c->ssg.applyRegister(c->ssgWrites.front().reg, c->ssgWrites.front().value);
            c->ssgWrites.pop();
        }
    }
}

void SoundChip_TurboSoundFM::applyAllSsgWrites()
{
    for (auto& c : _chips)
    {
        while (!c->ssgWrites.empty())
        {
            c->ssg.applyRegister(c->ssgWrites.front().reg, c->ssgWrites.front().value);
            c->ssgWrites.pop();
        }
    }
}

void SoundChip_TurboSoundFM::syncTo(uint64_t t)
{
    if (_adoptCpuClock)
    {
        // After reset/restore: adopt the CPU's position without advancing -
        // nothing has been simulated yet, so there is nothing to catch up.
        _syncedT = t;
        _adoptCpuClock = false;
        return;
    }
    if (t <= _syncedT)
        return;
    int32_t delta = int32_t(t - _syncedT);
    for (auto& c : _chips)
        advanceChip(*c, delta, _syncedT);
    _syncedT = t;
}

void SoundChip_TurboSoundFM::advanceChip(TsfmChip& c, int32_t delta, uint64_t t0)
{
    // Walk FM sample boundaries and timer expiries in time order, so a CSM
    // key-on from timer A lands on the right FM sample (§5.2 ordering rule:
    // at the same T-state, expiry is processed before the sample).
    if (_coreSynthesisSkipped)
    {
        // Sound off and no TTD: the FM operators (phases, envelopes) feed only the sound output, so
        // their clocking is skipped. Everything the CPU can observe stays exact - timers expire on
        // their T-state (CSM key-on included), busy counts down, and the sample-clock phase keeps
        // its alignment for the moment synthesis resumes
        while (delta > 0)
        {
            const int32_t period = 12 * int32_t(c.fm.fmClockPrescale());
            if (c.fmClockPhase >= period)
                c.fmClockPhase = period - 1;

            const int32_t n = std::min(delta, c.intf.clocksToNextExpiry());  // INT32_MAX when both stopped
            c.intf.advance(n);

            // FM sample clocks that would have completed in these n T-states; the engine still counts
            // them (its low clock-counter bits are CPU-observable through the timer B first load)
            const int32_t total = c.fmClockPhase + n;
            c.fm.skipFmClocks(uint32_t(total / period));
            c.fmClockPhase = total % period;
            delta -= n;
        }
        return;
    }

    while (delta > 0)
    {
        const int32_t period = 12 * int32_t(c.fm.fmClockPrescale());
        // A prescaler write (address 0x2D-0x2F) takes effect on the next
        // iteration; clamp the phase so a shrinking period cannot produce
        // toClock <= 0 (negative step) or skip a sample.
        if (c.fmClockPhase >= period)
            c.fmClockPhase = period - 1;
        const int32_t toClock = period - c.fmClockPhase;
        const int32_t toTimer = c.intf.clocksToNextExpiry();  // INT32_MAX when both stopped
        const int32_t n = std::min({delta, toClock, toTimer});

        c.intf.advance(n);  // counts down busy; fires expired timers exactly on their T-state
        c.fmClockPhase += n;
        delta -= n;
        t0 += n;

        if (c.fmClockPhase == period)
        {
            c.fmClockPhase = 0;
            const int16_t word = c.fm.clockFmOnce();
            c.words.push(t0, word);  // the output stage drops these when suppressed (§6.1)
        }
    }
}

/// endregion </Core loop>

/// region <Methods>

void SoundChip_TurboSoundFM::reset()
{
    // §5.4: CPLD reset state = OUT #FFFD,#FE - first chip (D1), register
    // read, FM muted. Both YM2203 /RES pins are wired to host reset.
    _board = TsfmBoard{};

    // Clock: adopt the CPU's T-state on the next sync; nobody has touched
    // the chips since the reset, so there is nothing to advance to.
    _adoptCpuClock = true;
    _chips[0]->words.clear();
    _chips[1]->words.clear();

    // Per-chip sequence (construction runs the same code, §5.4)
    _chips[0]->resetChip();
    _chips[1]->resetChip();

    // Render loop (§6): same reset set as the legacy device - PLL, buffer
    // cursor, LQ phase - plus the FM hold/boxcar state and the decimators
    // (state only; the rate-designed coefficients and the slave wiring
    // from setCoreRate are preserved)
    _lastTStates = 0;
    _samplePhase = 0;
    _ayBufferIndex = 0;
    _decimationPhase = 0.0;
    _renderT = -kFmRenderLagT;
    _renderReanchor = false;
    _outputFlushPending = false;
    for (auto& c : _chips)
    {
        c->out.hold = 0.0;
        c->out.coupling.reset();
        c->out.lastFed = 0.0;
        c->out.lqSum = 0.0;
        c->out.lqCount = 0;
        c->ssg.decimatorLeft().reset();
        c->ssg.decimatorRight().reset();
        c->out.decimator.reset();
    }
}

/// endregion </Methods>

/// region <Emulation events>

void SoundChip_TurboSoundFM::handleFrameStart()
{
    // Reset activity tracking for the new frame
    _frameHadActivity = false;
    _chip0ActiveThisFrame = false;
    _chip1ActiveThisFrame = false;
    _fmActiveThisFrame = false;

    // Frame rollover (§5.2): Core::AdjustFrameCounters already subtracted
    // the frame length from z80->t; shift the core's position and every
    // queued word timestamp by the same delta so they land on the new
    // frame's axis. No CPU instruction runs between the adjust and this
    // hook, so no time is lost or double-counted.
    if (!_adoptCpuClock)
    {
        const int32_t delta = int32_t(_syncedT - nowT());
        if (delta != 0)
        {
            _syncedT = uint64_t(int64_t(_syncedT) - int64_t(delta));
            _chips[0]->words.rebase(delta);
            _chips[1]->words.rebase(delta);
            _chips[0]->ssgWrites.rebase(delta);
            _chips[1]->ssgWrites.rebase(delta);
            _renderT -= delta;
        }
    }

    // The FM cursor carries its position across the boundary: the render loop
    // does not run exactly frame/16 ticks per frame (the decimators' fractional
    // phase carries over), so restarting it at the frame start slipped the FM
    // content by up to ~80 T (about one FM word) at most boundaries. At 1x it
    // saws within about one output sample around its anchor with no drift. A
    // rate or quality switch moves the render loop against the CPU clock
    // (re-anchor once, together with the filter redesign, instead of letting
    // the offsets pile up); far outside the window it has lost the timeline
    // (host speed multiplier > 1, synthesis resumed after suppression)
    if (_renderReanchor || _renderT < -4 * kFmRenderLagT || _renderT > 0)
    {
        _renderT = -kFmRenderLagT;
        _renderReanchor = false;
    }

    // Audio from before an LQ -> HQ switch or a suppression gap must not
    // replay: the HQ decimators were not fed and the hold / coupling still
    // carry the old level (ISSUES #7). Applied here, on the emulation thread
    if (_outputFlushPending)
    {
        flushOutputStage();
        _outputFlushPending = false;
    }

    // When the output stage is off, nobody drains the word queues - clear
    // them here so they cannot wrap (§6.1; handleFrameStart runs in turbo
    // and with the sound feature off too).
    if (_synthesisSuppressed)
    {
        _chips[0]->words.clear();
        _chips[1]->words.clear();
        applyAllSsgWrites();  // queued just before suppression began
    }

    // §9.4: the SSG clock change at /3 or /2 is not modelled - warn once per
    // device instance, at frame granularity, so a player's 0x2F -> 0x2D init
    // (which ends at /6 within a frame) never warns.
    if (!_prescalerWarned &&
        (_chips[0]->fm.fmClockPrescale() != 6 || _chips[1]->fm.fmClockPrescale() != 6))
    {
        _prescalerWarned = true;
        MLOGWARNING("SoundChip_TurboSoundFM: prescaler != /6 - the SSG clock ratio is not modelled (design §9.4)");
    }

    // Render-loop frame base (§6.2), same set as the legacy device (§11):
    // _samplePhase and _decimationPhase carry across frames - only reset()
    // and setCoreRate() clear them; the FM cursor was rebased above with the
    // word timestamps. The buffer clears run even when synthesis is
    // suppressed: the sound-feature-off mixing path relies on zeroed buffers.
    _lastTStates = 0;
    _ayBufferIndex = 0;
    memset(_ayBuffer, 0x00, _ayAudioDescriptor.memoryBufferSizeInBytes);
    memset(_chip0Buffer, 0x00, _chip0AudioDescriptor.memoryBufferSizeInBytes);
    memset(_chip1Buffer, 0x00, _chip1AudioDescriptor.memoryBufferSizeInBytes);
    memset(_fm0Buffer, 0x00, _fm0AudioDescriptor.memoryBufferSizeInBytes);
    memset(_fm1Buffer, 0x00, _fm1AudioDescriptor.memoryBufferSizeInBytes);
}

/// @brief Generate audio samples synchronized to CPU t-states (§6.2)
///
/// The SSG half is the legacy SoundChip_TurboSound render loop copied
/// VERBATIM - free-running sample PLL, per-output decimator refill, LQ
/// boxcar - so a TSFM configured with a silent FM half is bit-identical to
/// the legacy device (§11): the FM term contributes exactly +0.0 to every
/// float sum while no word has been produced, and +0.0 additions do not
/// change a float (the -0.0 + 0.0 -> +0.0 edge casts to the same int16).
///
/// The FM half interleaves around each SSG generator tick (§6.2): two FM
/// half-ticks (boundaries _renderT and _renderT+8) per tick, each consuming
/// the FM words that landed by its boundary into a sample-and-hold register
/// and feeding the muted-or-held value to the 437.5 kHz decimator (HQ) or
/// the boxcar accumulator (LQ). The FM decimators are slaves of chip-0 SSG
/// left (§6.3): they output exactly when the SSG master does.
void SoundChip_TurboSoundFM::handleStep()
{
    // §6.1: the core advances in every mode (turbo, sound feature off) -
    // SoundManager reaches this call unconditionally. Rendering is gated
    // on _synthesisSuppressed; the core never is.
    syncTo(nowT());

    if (_synthesisSuppressed)
        return;

    // Hardware turbo descaled: the YM2203 has its own clock, so under the
    // Scorpion 7 MHz flip-flop the chip must see the real-time position
    // (t/2), not the doubled CPU count - otherwise it emitted 2x samples
    // per frame (copied from the legacy device)
    size_t currentTStates = _context->emulatorState.AudioTstate(_context->pCore->GetZ80()->t);

    // Scale t-states by the HOST speed multiplier for correct audio pitch
    uint8_t speedMultiplier = _context->emulatorState.HostSpeedMultiplier();
    size_t scaledCurrentTStates = currentTStates * speedMultiplier;

    // Clip at the frame boundary so the accumulator sees exactly one frame
    // of T-states per frame - the mixer's count (legacy loop, see
    // SoundChip_TurboSound::handleStep for the reasoning)
    const size_t frameTStates = size_t(_context->config.frame) * speedMultiplier;
    if (frameTStates > 0 && scaledCurrentTStates > frameTStates)
        scaledCurrentTStates = frameTStates;

    int32_t diff = int32_t(scaledCurrentTStates) - int32_t(_lastTStates);

    if (diff > 0)
    {
        // Native-rate recording tap: active only during DSD capture.
        // Checked once per handleStep batch; per-tick cost is a plain bool.
        const bool tapActive = _nativeTap->isActive();

        _samplePhase += uint64_t(diff) * _coreRate;

        while (_samplePhase >= CPU_CLOCK_RATE && _ayBufferIndex < MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS)
        {
            _samplePhase -= CPU_CLOCK_RATE;

            int16_t leftSample;
            int16_t rightSample;

            if (_hqEnabled)
            {
                // ========== HIGH QUALITY MODE ==========
                // Native clock rendering + FIR decimation (legacy loop)
                // with the FM half-ticks of §6.2 interleaved per tick

                // Feed generator samples to decimators until we have an output
                while (!_chips[0]->ssg.decimatorLeft().hasOutput())
                {
                    // FM half-tick 1/2 of this SSG tick (§6.2)
                    fmHalfTick(0, _renderT);
                    fmHalfTick(1, _renderT);

                    // Tick generators (bypass internal prescaler), with the
                    // register writes timed up to this tick applied first
                    applySsgWrites(_renderT);
                    updateState(true);

                    // Native-rate tap for DSD capture (pre-decimation, both SSGs summed)
                    if (tapActive)
                    {
                        _nativeTap->push(static_cast<float>(_chips[0]->ssg.mixedLeft() + _chips[1]->ssg.mixedLeft()),
                                         static_cast<float>(_chips[0]->ssg.mixedRight() + _chips[1]->ssg.mixedRight()));
                    }

                    // Feed mixed output to decimators
                    _chips[0]->ssg.decimatorLeft().feedSample(_chips[0]->ssg.mixedLeft());
                    _chips[0]->ssg.decimatorRight().feedSample(_chips[0]->ssg.mixedRight());
                    _chips[1]->ssg.decimatorLeft().feedSample(_chips[1]->ssg.mixedLeft());
                    _chips[1]->ssg.decimatorRight().feedSample(_chips[1]->ssg.mixedRight());

                    // FM half-tick 2/2 (§6.2); the cursor advances one SSG tick
                    fmHalfTick(0, _renderT + 8);
                    fmHalfTick(1, _renderT + 8);
                    _renderT += 16;
                }

                // Get decimated output per chip (SSG verbatim; FM slaves
                // follow the master's cadence, gain-scaled, §7.1)
                float c0L = _chips[0]->ssg.decimatorLeft().getOutput();
                float c0R = _chips[0]->ssg.decimatorRight().getOutput();
                float c1L = _chips[1]->ssg.decimatorLeft().getOutput();
                float c1R = _chips[1]->ssg.decimatorRight().getOutput();
                float f0 = static_cast<float>(_chips[0]->out.decimator.getOutput() * _fmGain);
                float f1 = static_cast<float>(_chips[1]->out.decimator.getOutput() * _fmGain);

                // Store per-chip buffers (SSG for registry-driven capture,
                // FM centre-panned, §6.4/§7.2)
                _chip0Buffer[_ayBufferIndex]     = static_cast<int16_t>(c0L * INT16_MAX);
                _chip0Buffer[_ayBufferIndex + 1] = static_cast<int16_t>(c0R * INT16_MAX);
                _chip1Buffer[_ayBufferIndex]     = static_cast<int16_t>(c1L * INT16_MAX);
                _chip1Buffer[_ayBufferIndex + 1] = static_cast<int16_t>(c1R * INT16_MAX);
                _fm0Buffer[_ayBufferIndex]       = static_cast<int16_t>(f0 * INT16_MAX);
                _fm0Buffer[_ayBufferIndex + 1]   = static_cast<int16_t>(f0 * INT16_MAX);
                _fm1Buffer[_ayBufferIndex]       = static_cast<int16_t>(f1 * INT16_MAX);
                _fm1Buffer[_ayBufferIndex + 1]   = static_cast<int16_t>(f1 * INT16_MAX);

                // Combined output; with FM silent f0/f1 are exactly 0.0f and
                // the float sums stay byte-identical to the legacy device
                leftSample = static_cast<int16_t>((c0L + c1L + f0 + f1) * INT16_MAX);
                rightSample = static_cast<int16_t>((c0R + c1R + f0 + f1) * INT16_MAX);
            }
            else
            {
                // ========== LOW QUALITY MODE ==========
                // Native clock rendering + simple averaging (legacy loop)
                // with the FM half-ticks of §6.2 interleaved per tick

                double leftSum = 0.0;
                double rightSum = 0.0;
                int sampleCount = 0;

                // Run generator ticks for this output sample period
                _decimationPhase += _lqTicksPerSample;

                while (_decimationPhase >= 1.0)
                {
                    _decimationPhase -= 1.0;

                    // FM half-tick 1/2 of this SSG tick (§6.2)
                    fmHalfTick(0, _renderT);
                    fmHalfTick(1, _renderT);

                    // Tick generators directly (bypass internal prescaler),
                    // with the register writes timed up to this tick applied
                    applySsgWrites(_renderT);
                    updateState(true);

                    double l = _chips[0]->ssg.mixedLeft() + _chips[1]->ssg.mixedLeft();
                    double r = _chips[0]->ssg.mixedRight() + _chips[1]->ssg.mixedRight();

                    // Native-rate tap for DSD capture (pre-decimation)
                    if (tapActive)
                    {
                        _nativeTap->push(static_cast<float>(l), static_cast<float>(r));
                    }

                    // Accumulate samples
                    leftSum += l;
                    rightSum += r;
                    sampleCount++;

                    // FM half-tick 2/2 (§6.2); the cursor advances one SSG tick
                    fmHalfTick(0, _renderT + 8);
                    fmHalfTick(1, _renderT + 8);
                    _renderT += 16;
                }

                // FM boxcar sample (gated values; the hold when no half-tick
                // landed on this output sample), gain-scaled (§7.1)
                const double f0 = fmLqSample(0) * _fmGain;
                const double f1 = fmLqSample(1) * _fmGain;

                // Average (simple boxcar decimation)
                float c0L, c0R, c1L, c1R;
                if (sampleCount > 0)
                {
                    // Split the accumulated sums per chip for per-chip buffers
                    // In LQ mode we don't have separate accumulators, so approximate
                    // by using current chip output ratios. The ratio keys on the
                    // legacy device's chip 0, which is TSFM's chip 1 (§11 chip
                    // swap): mirroring the legacy expression (that chip over the
                    // sum, same association order) keeps the per-chip LQ buffers
                    // bit-identical under the swap - chip 1 takes the r0 share,
                    // chip 0 the 1-r0 share
                    double total = leftSum + rightSum;
                    double r0 = (total > 0) ? (_chips[1]->ssg.mixedLeft() + _chips[1]->ssg.mixedRight()) /
                                              (_chips[0]->ssg.mixedLeft() + _chips[0]->ssg.mixedRight() +
                                               _chips[1]->ssg.mixedLeft() + _chips[1]->ssg.mixedRight() + 1e-9) : 0.5;

                    c0L = static_cast<float>((leftSum * (1.0 - r0)) / sampleCount);
                    c0R = static_cast<float>((rightSum * (1.0 - r0)) / sampleCount);
                    c1L = static_cast<float>((leftSum * r0) / sampleCount);
                    c1R = static_cast<float>((rightSum * r0) / sampleCount);

                    leftSample = static_cast<int16_t>(((leftSum / sampleCount) + f0 + f1) * INT16_MAX);
                    rightSample = static_cast<int16_t>(((rightSum / sampleCount) + f0 + f1) * INT16_MAX);
                }
                else
                {
                    // No ticks this sample - use previous value
                    c0L = static_cast<float>(_chips[0]->ssg.mixedLeft());
                    c0R = static_cast<float>(_chips[0]->ssg.mixedRight());
                    c1L = static_cast<float>(_chips[1]->ssg.mixedLeft());
                    c1R = static_cast<float>(_chips[1]->ssg.mixedRight());
                    leftSample = static_cast<int16_t>((c0L + c1L + static_cast<float>(f0) + static_cast<float>(f1)) * INT16_MAX);
                    rightSample = static_cast<int16_t>((c0R + c1R + static_cast<float>(f0) + static_cast<float>(f1)) * INT16_MAX);
                }

                // Store per-chip buffers (SSG + centre-panned FM, §6.4/§7.2)
                _chip0Buffer[_ayBufferIndex]     = static_cast<int16_t>(c0L * INT16_MAX);
                _chip0Buffer[_ayBufferIndex + 1] = static_cast<int16_t>(c0R * INT16_MAX);
                _chip1Buffer[_ayBufferIndex]     = static_cast<int16_t>(c1L * INT16_MAX);
                _chip1Buffer[_ayBufferIndex + 1] = static_cast<int16_t>(c1R * INT16_MAX);
                _fm0Buffer[_ayBufferIndex]       = static_cast<int16_t>(f0 * INT16_MAX);
                _fm0Buffer[_ayBufferIndex + 1]   = static_cast<int16_t>(f0 * INT16_MAX);
                _fm1Buffer[_ayBufferIndex]       = static_cast<int16_t>(f1 * INT16_MAX);
                _fm1Buffer[_ayBufferIndex + 1]   = static_cast<int16_t>(f1 * INT16_MAX);
            }

            // Store samples in combined output buffer
            _ayBuffer[_ayBufferIndex++] = leftSample;
            _ayBuffer[_ayBufferIndex++] = rightSample;
        }
    }

    _lastTStates = scaledCurrentTStates;
}

void SoundChip_TurboSoundFM::handleFrameEnd()
{
    // Determine audio sources for HUD notification
    // Post separate notifications for AY/TS and FM so they can be displayed independently
    bool isTurboSound = _chip1ActiveThisFrame;
    bool isFM = _fmActiveThisFrame;

    // Post AY/TurboSound notification on activity or mode change
    bool ayStateChanged = (_frameHadActivity != _wasActive) || (isTurboSound != _wasTurboSound);
    if (_frameHadActivity || ayStateChanged)
    {
        _wasActive = _frameHadActivity;
        _wasTurboSound = isTurboSound;

        AudioSource aySource = isTurboSound ? AudioSource::TurboSound : AudioSource::AY;
        MessageCenter::DefaultMessageCenter().Post(
            NC_AUDIO_ACTIVITY, new AudioActivityPayload(_context->emulatorId, aySource, _wasActive));
    }

    // Post separate FM notification if FM state changed
    bool fmStateChanged = (isFM != _wasFM);
    if (isFM || fmStateChanged)
    {
        _wasFM = isFM;
        MessageCenter::DefaultMessageCenter().Post(
            NC_AUDIO_ACTIVITY, new AudioActivityPayload(_context->emulatorId, AudioSource::FM, _wasFM));
    }

    // No FM word drain here: words the render cursor has not reached stay
    // queued and are rebased into the next frame (§6.2). Draining them into
    // the hold skipped their samples and slipped the FM timeline
}

/// endregion </Emulation events>

/// region <PortDevice interface methods>

uint8_t SoundChip_TurboSoundFM::portDeviceInMethod(uint16_t port)
{
    syncTo(nowT());
    TsfmChip& c = *_chips[_board.chip];

    // Status mode applies to #FFFD only; IN #BFFD keeps the register path
    // for regression parity with the legacy device (§5.3)
    if (_board.statusRead && port == PORT_FFFD)
        return c.fm.read_status();  // busy | timer B | timer A

    if (c.address < 0x10)
        return c.ssg.readCurrentRegister();

    return 0xFF;  // FM address latched (hardware-reference H2)
}

void SoundChip_TurboSoundFM::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    syncTo(nowT());
    _frameHadActivity = true;  // Track activity for HUD notification

    // Track per-chip activity for HUD
    // chip 0 = primary (AY), chip 1 = secondary (TurboSound mode when accessed)
    if (_board.chip == 0)
        _chip0ActiveThisFrame = true;
    else
        _chip1ActiveThisFrame = true;

    TsfmChip& c = *_chips[_board.chip];

    switch (port)
    {
        case PORT_FFFD:
            if ((value & 0xF8) == 0xF8)
            {
                // Control word: board latches only. _WR is held inactive,
                // so neither chip's address latch changes (hardware §3.2).
                _board.chip = (value & 0x01) ? 1 : 0;
                _board.statusRead = !(value & 0x02);
                _board.fmEnabled = !(value & 0x04);
            }
            else
            {
                // Address: latched in BOTH modes, regardless of FM mute
                // (hardware §3.4)
                c.address = value;
                c.fm.write_address(value);  // ymfm address + prescaler side effect (0x2D-0x2F)
                c.ssg.setRegister(value);   // SSG: <0x10 selects; >=0x10 keeps the previous register
            }
            break;

        case PORT_BFFD:
            if (c.address < 0x10)
            {
                // SSG register: the CPU sees it now, the generators on the
                // tick of this T-state (SsgWriteQueue). Busy is set by SSG
                // data writes too - ymfm's write_data does it for both halves.
                const uint8_t reg = c.ssg.getCurrentRegisterIndex();
                c.ssg.latchRegister(reg, value);
                queueSsgWrite(c, reg, value);
                c.intf.ymfm_set_busy_end(c.fm.busyClocks());
            }
            else
            {
                // FM register; sets busy itself; allowed while muted
                _fmActiveThisFrame = true;  // Track FM access for HUD
                c.fm.write_data(value);
                // Key-on mirror for the state report: 0x28 = ch (bits 0-1,
                // 3 = none) | slot mask (bits 4-7)
                if (c.address == 0x28 && (value & 3) < 3)
                    c.fmKeyOn[value & 3] = uint8_t(value >> 4);
            }
            break;

        default:
            return;  // Not a board port - no tap
    }

    // Automation tap (MCP M7j): fires after the write so `chip` identifies
    // the chip that received it. Control-word distinction (flags byte) is
    // the observability upgrade of P7 - P4 records plain records.
    if (_logSink) [[unlikely]]
    {
        AYLogRecord record;
        record.port = port;
        record.value = value;
        record.chip = _board.chip;
        record.reg = (port == PORT_BFFD) ? c.address : value;
        if (_context && _context->pCore)
        {
            const Z80* z80 = _context->pCore->GetZ80();
            record.pc = z80->m1_pc;  // PC of the OUT instruction, not the next one
            record.tacts = z80->t;
            record.frame = _context->emulatorState.frame_counter;
        }
        _logSink(_logSinkContext, record);
    }
}

/// endregion </PortDevice interface methods>

/// region <Ports interaction>

bool SoundChip_TurboSoundFM::attachToPorts(PortDecoder* decoder)
{
    bool result = false;

    if (decoder)
    {
        _portDecoder = decoder;

        [[maybe_unused]] PortDevice* device = this;
        result = decoder->RegisterPortHandler(0xBFFD, this);
        result &= decoder->RegisterPortHandler(0xFFFD, this);

        if (result)
        {
            _chipAttachedToPortDecoder = true;
        }
    }

    return result;
}

void SoundChip_TurboSoundFM::detachFromPorts()
{
    if (_portDecoder && _chipAttachedToPortDecoder)
    {
        _portDecoder->UnregisterPortHandler(0xBFFD);
        _portDecoder->UnregisterPortHandler(0xFFFD);

        _chipAttachedToPortDecoder = false;
    }
}

/// endregion </Ports interaction>

/// region <TTDSerializable interface (§8.2)>

namespace
{
/// Cursor-based little-endian writers/readers, matching soundchip_ay8910.cpp.
inline void put_u8 (uint8_t*& cur, uint8_t v)   { *cur++ = v; }
inline void put_u16(uint8_t*& cur, uint16_t v)  { std::memcpy(cur, &v, 2); cur += 2; }
inline void put_i32(uint8_t*& cur, int32_t v)   { std::memcpy(cur, &v, 4); cur += 4; }
inline void put_u64(uint8_t*& cur, uint64_t v)  { std::memcpy(cur, &v, 8); cur += 8; }
inline void put_f64(uint8_t*& cur, double v)    { std::memcpy(cur, &v, 8); cur += 8; }

inline uint8_t  get_u8 (const uint8_t*& cur)  { return *cur++; }
inline uint16_t get_u16(const uint8_t*& cur)  { uint16_t v; std::memcpy(&v, cur, 2); cur += 2; return v; }
inline int32_t  get_i32(const uint8_t*& cur)  { int32_t v; std::memcpy(&v, cur, 4); cur += 4; return v; }
inline uint64_t get_u64(const uint8_t*& cur)  { uint64_t v; std::memcpy(&v, cur, 8); cur += 8; return v; }
inline double   get_f64(const uint8_t*& cur)  { double v; std::memcpy(&v, cur, 8); cur += 8; return v; }

// ymfm's save_restore() serializes ym2203 (FM engine, its dormant internal
// SSG copy, and the SSG resampler) into a caller-owned byte vector. Measured
// and pinned by ymfm_ttd_patch_test.cpp (YmfmTtdPatch.*): fixed at 494 bytes
// for the vendored/patched engine as long as the register file and channel
// topology don't change.
constexpr size_t kYmfmStateSize = 494;

// address(1) + fmClockPhase(4) + timer[2](8) + busy(4) + ymfmSize(2) +
// ymfm payload(kYmfmStateSize) + SSG payload(73, SoundChip_AY8910)
constexpr size_t kTsfmChipStateSize = 1 + 4 + 4 + 4 + 4 + 2 + kYmfmStateSize + 73;
static_assert(kTsfmChipStateSize == 586, "TSFM per-chip state size drift");

// Render-loop free-running accumulators (§6.2): _samplePhase (the mixer-exact
// sample PLL) and _decimationPhase (LQ boxcar phase) are explicitly NOT reset
// per-frame (handleFrameStart's own comment: "only reset() and setCoreRate()
// clear them") - they carry fractional position across every frame of a
// session. They gate how many times updateState() ticks the tone/noise/
// envelope generators between two T-states, so excluding them from TTD state
// does not just affect audio buffering (like the rest of the output stage) -
// it lets the actual generator PHASE COUNTERS drift after a restore, a few
// ticks per seek, because the restored device starts accumulating from
// whatever these fields happened to hold live rather than their true
// historical value. Found via SeekTo_NoiseGeneratorStateDeterministicFromDeltaFrame
// (ttdtsfm_test.cpp): a channel B tone counter differed by a few ticks after
// resuming playback from a restored checkpoint, even though every other byte
// of the payload (registers, LFSR, ymfm engine, timers) matched exactly.
// v3 addition: each SSG decimator's own fractional resampling phase
// (FilterDecimator::_phase) is the SAME class of persistent, tick-gating
// accumulator as _samplePhase/_decimationPhase above - it decides how many
// generator ticks the inner `while (!hasOutput())` loop in handleStep() runs
// before the next output sample, so it must be restored to its true
// historical value too. It was missed in v2 because it lives inside
// FilterDecimator, not SoundChip_TurboSoundFM, and only surfaced once the
// v2 fix's own regression test (SeekTo_NoiseGeneratorStateDeterministicFrom
// DeltaFrame) was re-run against the v3-motivating output-stage-flush change
// below: flushing the decimators via reset() (needed to clear stale FIR
// history and avoid an audible click) also zeroed this phase, which
// reintroduced the exact same drift class in a different accumulator. Only
// the 4 SSG decimators need this (chip0/chip1 x left/right); each chip's FM
// decimator runs permanently in slave mode (attachMaster() in setCoreRate())
// so its own _phase is never consulted while a master is attached.
constexpr size_t kRenderPhaseStateSize =
    8 /* samplePhase */ + 8 /* decimationPhase */ + 4 * 8 /* 4 SSG decimator phases */;

// v4 tail: the render cursor and the pending timed SSG writes. The cursor
// decides on which tick every SSG write lands, so it is tick-gating state
// like the phases above; a write pending at a checkpoint is chip input that
// has not reached the generators yet. Both are stored relative to the
// device's synced position (the frame end at a checkpoint) and rebuilt
// around the restored CPU position - the same rebase a frame start does.
// Per chip: count(1) + kCapacity x {t offset i32, reg u8, value u8}
constexpr size_t kSsgQueueStateSize = 1 + SsgWriteQueue::kCapacity * (4 + 1 + 1);
constexpr size_t kTimelineStateSize = 8 /* render cursor offset */ + 2 * kSsgQueueStateSize;

// version(1) + board(1) + render-loop phase(48) + 2 x per-chip payload + v4 tail
constexpr size_t kTsfmStateSize = 1 + 1 + kRenderPhaseStateSize + 2 * kTsfmChipStateSize + kTimelineStateSize;
static_assert(kTsfmStateSize == 2000, "TSFM state size must match design §8.2 + render-phase fixes + v4 timeline (2000 bytes)");

constexpr uint8_t kTsfmStateVersion = 4;

uint8_t EncodeBoardByte(const TsfmBoard& b)
{
    uint8_t v = b.chip & 0x01;
    v |= b.statusRead ? 0x02 : 0;
    v |= b.fmEnabled  ? 0x04 : 0;
    return v;
}

void DecodeBoardByte(uint8_t v, TsfmBoard& b)
{
    b.chip = v & 0x01;
    b.statusRead = (v & 0x02) != 0;
    b.fmEnabled  = (v & 0x04) != 0;
}
} // anonymous namespace

size_t SoundChip_TurboSoundFM::TTDStateSize() const
{
    return kTsfmStateSize;
}

void SoundChip_TurboSoundFM::TTDSaveState(uint8_t* dst) const
{
    uint8_t* cur = dst;

    put_u8(cur, kTsfmStateVersion);
    put_u8(cur, EncodeBoardByte(_board));
    put_u64(cur, _samplePhase);
    put_f64(cur, _decimationPhase);
    put_f64(cur, _chips[0]->ssg.decimatorLeft().phase());
    put_f64(cur, _chips[0]->ssg.decimatorRight().phase());
    put_f64(cur, _chips[1]->ssg.decimatorLeft().phase());
    put_f64(cur, _chips[1]->ssg.decimatorRight().phase());

    for (int i = 0; i < 2; ++i)
    {
        TsfmChip& c = *_chips[i];  // non-const through the unique_ptr, see header comment

        put_u8 (cur, c.address);
        put_i32(cur, c.fmClockPhase);
        put_i32(cur, c.intf._timer[0]);
        put_i32(cur, c.intf._timer[1]);
        put_i32(cur, c.intf._busy);

        // ymfm serializes via push_back into a caller-owned vector; the
        // scratch buffer is pre-reserved (TsfmChip::ttdScratch) so this does
        // not allocate on the steady-state save path.
        c.ttdScratch.clear();
        ymfm::ymfm_saved_state state(c.ttdScratch, /*saving=*/true);
        c.fm.save_restore(state);
        assert(c.ttdScratch.size() == kYmfmStateSize &&
               "ymfm engine payload size drifted from the §8.2-measured 494 bytes");

        put_u16(cur, static_cast<uint16_t>(c.ttdScratch.size()));
        std::memcpy(cur, c.ttdScratch.data(), c.ttdScratch.size());
        cur += c.ttdScratch.size();

        c.ssg.TTDSaveState(cur);
        cur += c.ssg.TTDStateSize();
    }

    // v4 tail: render cursor + pending SSG writes, relative to _syncedT
    const int64_t base = int64_t(_syncedT);
    put_u64(cur, uint64_t(_renderT - base));
    for (int i = 0; i < 2; ++i)
    {
        const SsgWriteQueue& q = _chips[i]->ssgWrites;
        put_u8(cur, uint8_t(q.size()));
        for (size_t k = 0; k < SsgWriteQueue::kCapacity; ++k)
        {
            const SsgWrite w = (k < q.size()) ? q.at(k) : SsgWrite{};
            put_i32(cur, (k < q.size()) ? int32_t(w.t - base) : 0);
            put_u8(cur, w.reg);
            put_u8(cur, w.value);
        }
    }

    assert(static_cast<size_t>(cur - dst) == kTsfmStateSize);
}

void SoundChip_TurboSoundFM::TTDLoadState(const uint8_t* src)
{
    const uint8_t* cur = src;

    const uint8_t version = get_u8(cur);
    assert(version == kTsfmStateVersion &&
           "TTD blob predates the v4 timeline (soundchip_turbosoundfm.cpp) "
           "and cannot be loaded by this build");
    (void)version;
    TsfmBoard board;
    DecodeBoardByte(get_u8(cur), board);
    _board = board;
    _samplePhase = get_u64(cur);
    _decimationPhase = get_f64(cur);
    const double chip0LeftPhase = get_f64(cur);
    const double chip0RightPhase = get_f64(cur);
    const double chip1LeftPhase = get_f64(cur);
    const double chip1RightPhase = get_f64(cur);

    for (int i = 0; i < 2; ++i)
    {
        TsfmChip& c = *_chips[i];

        c.address = get_u8(cur);
        c.fmClockPhase = get_i32(cur);
        c.intf._timer[0] = get_i32(cur);
        c.intf._timer[1] = get_i32(cur);
        c.intf._busy = get_i32(cur);

        const uint16_t ymfmSize = get_u16(cur);
        assert(ymfmSize == kYmfmStateSize &&
               "TTD blob's ymfm payload size does not match this build's engine layout");
        c.ttdScratch.assign(cur, cur + ymfmSize);
        cur += ymfmSize;
        ymfm::ymfm_saved_state state(c.ttdScratch, /*saving=*/false);
        c.fm.save_restore(state);

        c.ssg.TTDLoadState(cur);
        cur += c.ssg.TTDStateSize();
    }

    // v4 tail: render cursor + pending SSG writes, rebuilt around the
    // restored CPU position (the machine resumes at the start of a frame)
    const int64_t base = int64_t(nowT());
    _syncedT = uint64_t(base);
    const int64_t cursorOffset = int64_t(get_u64(cur));
    for (int i = 0; i < 2; ++i)
    {
        SsgWriteQueue& q = _chips[i]->ssgWrites;
        q.clear();
        const size_t count = get_u8(cur);
        for (size_t k = 0; k < SsgWriteQueue::kCapacity; ++k)
        {
            const int32_t offset = get_i32(cur);
            const uint8_t reg = get_u8(cur);
            const uint8_t value = get_u8(cur);
            if (k < count)
                q.push(SsgWrite{base + offset, reg, value});
        }
    }

    // Output-stage FLUSH (not restore) of AUDIO CONTENT: the decimator FIR
    // history, hold register, LQ boxcar and word queues are not part of TTD
    // state (§8.2 policy - they're host-side rendering caches, not chip
    // state) and are therefore left holding whatever they had LIVE right
    // before this seek - i.e. audio content from a completely different
    // point in the tune. Left untouched, the next samples mix that stale,
    // uncorrelated history with the freshly-restored (different-timeline)
    // generator output through the decimator's FIR taps, producing an
    // audible click/discontinuity right at the seek point (reported against
    // scratch/tsfm-issues3.ttd). The fix is not to restore this content
    // (that would need yet more TTD payload for a purely cosmetic concern)
    // but to FLUSH it to silence here, exactly as reset() already does for
    // these same fields (minus resetChip(), which would erase the chip
    // state just restored above) - a clean, silent decimator settling in
    // over one filter length is inaudible; stale foreign history snapping
    // into new content is not.
    _chips[0]->words.clear();
    _chips[1]->words.clear();
    flushOutputStage();
    _outputFlushPending = false;

    // The flush keeps each SSG decimator's resampling PHASE
    // (FilterDecimator::_phase) - but that is the live pre-seek one, and
    // phase is a tick-gating accumulator with the same determinism
    // requirement as _samplePhase/_decimationPhase above (v3 fix; see
    // kRenderPhaseStateSize comment). Restore it explicitly on top of the
    // flush: buffer silent, phase historically correct.
    _chips[0]->ssg.decimatorLeft().setPhase(chip0LeftPhase);
    _chips[0]->ssg.decimatorRight().setPhase(chip0RightPhase);
    _chips[1]->ssg.decimatorLeft().setPhase(chip1LeftPhase);
    _chips[1]->ssg.decimatorRight().setPhase(chip1RightPhase);

    // The render cursor is historical (v4 tail above): it decides on which
    // tick the pending and future SSG writes land
    _renderT = base + cursorOffset;
    _renderReanchor = false;

    // Checkpoints sit on a frame boundary and the machine resumes at T 0 of a
    // fresh frame, so the per-frame render cursor restarts like
    // handleFrameStart - its live pre-seek values miscounted the first frame
    // (up to a few samples too many) and shifted _samplePhase for good.
    // _samplePhase itself is historical now, the mixer's accumulator is
    // still the live pre-seek one: it takes the device's position. Either
    // left out, the two frame sample counts disagreed every few frames after
    // any seek (issue #2 again: one zero sample per 904-sample frame)
    _lastTStates = 0;
    _ayBufferIndex = 0;
    if (_context->pSoundManager)
        _context->pSoundManager->adoptSamplePhase(_samplePhase);

    // The core continues from the restored CPU position (_syncedT = base
    // above): the restore sets z80.t before the devices load, so the next
    // syncTo() clocks the FM chips over exactly the T-states the original run
    // did. Adopting the next position instead skipped the first instruction's
    // T-states and slipped every FM sample clock after a restore
    _adoptCpuClock = false;
}

uint64_t SoundChip_TurboSoundFM::TTDHashState() const
{
    // FNV-1a over the full serialized payload - simplest way to guarantee
    // the hash tracks every field TTDSaveState captures, with no separate
    // field list to keep in sync.
    std::vector<uint8_t> buf(kTsfmStateSize);
    TTDSaveState(buf.data());

    uint64_t hash = 0xcbf29ce484222325ull;  // FNV-1a offset basis
    for (uint8_t b : buf)
    {
        hash ^= b;
        hash *= 0x100000001b3ull;  // FNV-1a prime
    }
    return hash;
}

/// endregion </TTDSerializable interface>
