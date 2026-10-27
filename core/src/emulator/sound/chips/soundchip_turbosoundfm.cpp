#include "soundchip_turbosoundfm.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/breakpoints/chipregister.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/soundmanager.h"

namespace
{
// A mixed level [-1, 1] as a 16-bit sample. Two chips (and the FM parts) summed go past full scale: clamped,
// since a float out of the int16_t range converts to an undefined value. Scaled in the level's own type: a sample
// in range is what the plain conversion gave
template <typename Level>
int16_t ToSample(Level level)
{
    return static_cast<int16_t>(std::clamp(level * INT16_MAX, Level(INT16_MIN), Level(INT16_MAX)));
}
}  // namespace

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

// The core loop itself (syncTo / advanceChip, FM half-ticks, timed SSG
// writes, the output-stage flush) is the shared Ym2203Pair's (tsfm/ym2203pair.cpp)

/// endregion </Core loop>

/// region <Methods>

void SoundChip_TurboSoundFM::reset()
{
    settleSteps();
    // §5.4: CPLD reset state = OUT #FFFD,#FE - first chip (D1), register
    // read, FM muted. Both YM2203 /RES pins are wired to host reset.
    _board = TsfmBoard{};

    // Both chips (/RES on host reset, §5.4), the clock (adopt the CPU's
    // T-state on the next sync), the render cursor and the output stage -
    // FM hold/boxcar state and the decimators (state only; the rate-designed
    // coefficients and the slave wiring from setCoreRate are preserved)
    _pair.reset();

    // Render loop (§6): same reset set as the legacy device - PLL, buffer
    // cursor, LQ phase
    _lastTStates = 0;
    _samplePhase = 0;
    _ayBufferIndex = 0;
    _decimationPhase = 0.0;
    _renderReanchor = false;
    _outputFlushPending = false;
    _syncDeferred = false;
}

/// endregion </Methods>

/// region <Emulation events>

void SoundChip_TurboSoundFM::handleFrameStart()
{
    settleSteps();
    // Frame rollover (§5.2): Core::AdjustFrameCounters already subtracted
    // the frame length from z80->t; shift the core's position and every
    // queued word timestamp by the same delta so they land on the new
    // frame's axis. No CPU instruction runs between the adjust and this
    // hook, so no time is lost or double-counted.
    _pair.rebaseFrame(nowT());

    // The FM cursor carries its position across the boundary: the render loop
    // does not run exactly frame/16 ticks per frame (the decimators' fractional
    // phase carries over), so restarting it at the frame start slipped the FM
    // content by up to ~80 T (about one FM word) at most boundaries. At 1x it
    // saws within about one output sample around its anchor with no drift. A
    // rate or quality switch moves the render loop against the CPU clock
    // (re-anchor once, together with the filter redesign, instead of letting
    // the offsets pile up); far outside the window it has lost the timeline
    // (host speed multiplier > 1, synthesis resumed after suppression).
    // The pair's one cursor rule (Ym2203Pair::anchorRender): this owner renders as the CPU runs, so at the frame
    // start the reference is the frame origin of its frame-relative axis (0) with nothing rendered yet (span 0)
    _pair.anchorRender(0, 0, _renderReanchor);
    _renderReanchor = false;

    // Audio from before an LQ -> HQ switch or a suppression gap must not
    // replay: the HQ decimators were not fed and the hold / coupling still
    // carry the old level (ISSUES #7). Applied here, on the emulation thread
    if (_outputFlushPending)
    {
        _pair.flushOutputStage();
        _outputFlushPending = false;
    }

    // When the output stage is off, nobody drains the word queues - clear
    // them here so they cannot wrap (§6.1; handleFrameStart runs in turbo
    // and with the sound feature off too).
    if (_pair.synthesisSuppressed())
    {
        _pair.clearWords();
        _pair.applyAllSsgWrites();  // queued just before suppression began
    }

    // §9.4: the SSG clock change at /3 or /2 is not modelled - warn once per
    // device instance, at frame granularity, so a player's 0x2F -> 0x2D init
    // (which ends at /6 within a frame) never warns.
    if (!_prescalerWarned &&
        (_pair.chip(0)->fm.fmClockPrescale() != 6 || _pair.chip(1)->fm.fmClockPrescale() != 6))
    {
        _prescalerWarned = true;
        MLOGWARNING("SoundChip_TurboSoundFM: prescaler != /6 - the SSG clock ratio is not modelled (design §9.4)");
    }

    // Render-loop frame base (§6.2), same set as the legacy device (§11):
    // _samplePhase and _decimationPhase carry across frames - only reset()
    // and setCoreRate() clear them, and the mixer's frame-start phase was
    // pushed just before this call (followSamplePhase: equal at 1x, taken
    // back after a host speed multiplier or a sound-off gap); the FM cursor was rebased above with the
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
/// half-ticks (boundaries renderT and renderT+8 of the pair's cursor) per tick, each consuming
/// the FM words that landed by its boundary into a sample-and-hold register
/// and feeding the muted-or-held value to the 437.5 kHz decimator (HQ) or
/// the boxcar accumulator (LQ). The FM decimators are slaves of chip-0 SSG
/// left (§6.3): they output exactly when the SSG master does.
void SoundChip_TurboSoundFM::handleStep()
{
    // The step gate: before the deadline a step only notes where it ended (settleSteps books it)
    if (_stepDeadline != 0)
    {
        if (noteGatedStep(nowT(), _context->emulatorState.HostSpeedMultiplier()))
            return;
        settleSteps();
    }
    stepAt(nowT());
}

void SoundChip_TurboSoundFM::stepAt(uint64_t now)
{
    // §6.1: the core advances in every mode (turbo, sound feature off) -
    // SoundManager reaches this call unconditionally. Rendering is gated
    // on synthesis suppression; the core never is. With its synthesis
    // skipped the core only notes the position (deferred sync, header)
    if (_pair.coreSynthesisSkipped() && _pair.synthesisSuppressed() && _deferAllowed && !_pair.adoptsCpuClock())
    {
        _deferredT = now;
        _syncDeferred = true;
        // Every step up to the next entry takes this branch with the same outcome (the flags change only through
        // entries that settle): the gate holds them, settleSteps takes the last one here
        if (_stepGate)
        {
            _stepDeadline = std::numeric_limits<uint64_t>::max();
            _quietDeferral = true;
        }
        return;
    }
    if (_pair.synthesisSuppressed())
    {
        syncTo(now);
        return;
    }

    // Hardware turbo descaled: the YM2203 has its own clock, so under the
    // Scorpion 7 MHz flip-flop the chip must see the real-time position
    // (t/2), not the doubled CPU count - otherwise it emitted 2x samples
    // per frame (copied from the legacy device)
    size_t currentTStates = size_t(now);  // nowT(): AudioTstate of the CPU's clock

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

    // A step that completes no output sample renders nothing: the chips need not stand at the CPU yet. The step only moves
    // the sample phase and notes the position; the chips catch up when a sample is due, at a port access, at the frame end and
    // wherever else the deferred sync does (header). Ym2203Pair::syncTo gives the same state and the same words for one long
    // span as for the steps it is made of
    if (_lazySync && _deferAllowed && !_pair.adoptsCpuClock() &&
        (diff <= 0 || _samplePhase + uint64_t(diff) * _coreRate < CPU_CLOCK_RATE ||
         _ayBufferIndex >= MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS))
    {
        if (diff > 0)
            _samplePhase += uint64_t(diff) * _coreRate;
        _lastTStates = scaledCurrentTStates;
        _deferredT = now;
        _syncDeferred = true;

        // Until the step whose T completes a sample, every step takes this branch with the same outcome: the gate's
        // deadline (x1 speed, a sample to come, room in the buffer). samplePhase + d x rate < CPU_CLOCK_RATE holds
        // for d < ceil((CPU_CLOCK_RATE - samplePhase) / rate)
        if (_stepGate && speedMultiplier == 1 && _samplePhase < CPU_CLOCK_RATE &&
            _ayBufferIndex < MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS)
            _stepDeadline = uint64_t(_lastTStates) + (CPU_CLOCK_RATE - _samplePhase + _coreRate - 1) / _coreRate;
        if (sleepsThroughSilence())
        {
            _stepDeadline = std::numeric_limits<uint64_t>::max();
            _quietDeferral = true;
        }
        return;
    }
    syncTo(now);

    if (diff > 0)
    {
        // Native-rate recording tap: active only during DSD capture.
        // Checked once per handleStep batch; per-tick cost is a plain bool.
        const bool tapActive = _nativeTap->isActive();
        auto feedSsg = [](FilterDecimator& d, double v, size_t& zeroRun) {
            d.feedSample(v);
            zeroRun = v == 0.0 ? zeroRun + 1 : 0;
        };
        const bool hqEnabled = _pair.hqEnabled();
        TsfmChip& chip0 = *_pair.chip(0);
        TsfmChip& chip1 = *_pair.chip(1);

        _samplePhase += uint64_t(diff) * _coreRate;

        while (_samplePhase >= CPU_CLOCK_RATE && _ayBufferIndex < MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS)
        {
            // The outputs due in one quiet run while nothing sounds (renderQuietOutputs)
            if (hqEnabled && !tapActive && renderQuietOutputs() > 0)
                continue;

            _samplePhase -= CPU_CLOCK_RATE;

            int16_t leftSample;
            int16_t rightSample;

            if (hqEnabled)
            {
                // ========== HIGH QUALITY MODE ==========
                // Native clock rendering + FIR decimation (legacy loop)
                // with the FM half-ticks of §6.2 interleaved per tick

                // Feed generator samples to decimators until we have an output.
                // One SSG tick of the pair: FM half-tick 1/2 (§6.2), the
                // register writes timed up to this tick applied, generators
                // ticked (bypass internal prescaler), the lambda, FM half-tick
                // 2/2; the cursor advances one SSG tick
                // The pair quiet at rest up to the next output (Ym2203Pair::renderQuiet) and the four SSG streams all
                // zeros: the ticks feed zeros, which moves only their positions
                const size_t quiet = chip0.ssg.decimatorLeft().feedsToNextOutput();
                if (!tapActive && _ssgZeroRun[0] >= FilterDecimator::history() &&
                    _ssgZeroRun[1] >= FilterDecimator::history() && _ssgZeroRun[2] >= FilterDecimator::history() &&
                    _ssgZeroRun[3] >= FilterDecimator::history() &&
                    _pair.renderQuiet(static_cast<uint32_t>(quiet), _board.fmEnabled))
                {
                    chip0.ssg.decimatorLeft().feedZerosIntoSilence(quiet);
                    chip0.ssg.decimatorRight().feedZerosIntoSilence(quiet);
                    chip1.ssg.decimatorLeft().feedZerosIntoSilence(quiet);
                    chip1.ssg.decimatorRight().feedZerosIntoSilence(quiet);
                    for (size_t& zeroRun : _ssgZeroRun)
                        zeroRun += quiet;
                }
                while (!chip0.ssg.decimatorLeft().hasOutput())
                {
                    _pair.renderTick(_board.fmEnabled, [&](TsfmChip& c0, TsfmChip& c1)
                    {
                        // Native-rate tap for DSD capture (pre-decimation, both SSGs summed)
                        if (tapActive)
                        {
                            _nativeTap->push(static_cast<float>(c0.ssg.mixedLeft() + c1.ssg.mixedLeft()),
                                             static_cast<float>(c0.ssg.mixedRight() + c1.ssg.mixedRight()));
                        }

                        // Feed mixed output to decimators
                        feedSsg(c0.ssg.decimatorLeft(), c0.ssg.mixedLeft(), _ssgZeroRun[0]);
                        feedSsg(c0.ssg.decimatorRight(), c0.ssg.mixedRight(), _ssgZeroRun[1]);
                        feedSsg(c1.ssg.decimatorLeft(), c1.ssg.mixedLeft(), _ssgZeroRun[2]);
                        feedSsg(c1.ssg.decimatorRight(), c1.ssg.mixedRight(), _ssgZeroRun[3]);
                    });
                }

                // Get decimated output per chip (SSG verbatim; FM slaves
                // follow the master's cadence, gain-scaled, §7.1)
                // One pass per design (FilterDecimator::getOutputs): the four SSG decimators are fed in lockstep
                // and share their rows, the two FM slaves theirs; the same order as one getOutput() each
                // A silent stream skips its FIR (FilterDecimator::getOutputsSkippingSilent): the same +0.0
                FilterDecimator* decimators[6] = {&chip0.ssg.decimatorLeft(), &chip0.ssg.decimatorRight(),
                                                  &chip1.ssg.decimatorLeft(), &chip1.ssg.decimatorRight(),
                                                  &chip0.out.decimator,       &chip1.out.decimator};
                const size_t zeroRuns[6] = {_ssgZeroRun[0], _ssgZeroRun[1], _ssgZeroRun[2], _ssgZeroRun[3],
                                            chip0.out.zeroRun, chip1.out.zeroRun};
                double values[6];
                FilterDecimator::getOutputsSkippingSilent(decimators, zeroRuns, 6, values);
                float c0L = static_cast<float>(values[0]);
                float c0R = static_cast<float>(values[1]);
                float c1L = static_cast<float>(values[2]);
                float c1R = static_cast<float>(values[3]);
                float f0 = static_cast<float>(values[4] * _fmGain);
                float f1 = static_cast<float>(values[5] * _fmGain);

                // Store per-chip buffers (SSG for registry-driven capture,
                // FM centre-panned, §6.4/§7.2)
                _chip0Buffer[_ayBufferIndex]     = ToSample(c0L);
                _chip0Buffer[_ayBufferIndex + 1] = ToSample(c0R);
                _chip1Buffer[_ayBufferIndex]     = ToSample(c1L);
                _chip1Buffer[_ayBufferIndex + 1] = ToSample(c1R);
                _fm0Buffer[_ayBufferIndex]       = ToSample(f0);
                _fm0Buffer[_ayBufferIndex + 1]   = ToSample(f0);
                _fm1Buffer[_ayBufferIndex]       = ToSample(f1);
                _fm1Buffer[_ayBufferIndex + 1]   = ToSample(f1);

                // Combined output; with FM silent f0/f1 are exactly 0.0f and
                // the float sums stay byte-identical to the legacy device
                leftSample = ToSample(c0L + c1L + f0 + f1);
                rightSample = ToSample(c0R + c1R + f0 + f1);
            }
            else
            {
                // ========== LOW QUALITY MODE ==========
                // Native clock rendering + simple averaging (legacy loop)
                // with the FM half-ticks of §6.2 interleaved per tick

                double leftSum = 0.0;
                double rightSum = 0.0;
                double chipSums[4] = {};  // chip 0 left / right, chip 1 left / right: the per-chip buffers
                int sampleCount = 0;

                // Run generator ticks for this output sample period
                _decimationPhase += _lqTicksPerSample;

                while (_decimationPhase >= 1.0)
                {
                    _decimationPhase -= 1.0;

                    // One SSG tick of the pair (FM half-ticks interleaved,
                    // timed writes applied, generators ticked directly)
                    _pair.renderTick(_board.fmEnabled, [&](TsfmChip& c0, TsfmChip& c1)
                    {
                        double l = c0.ssg.mixedLeft() + c1.ssg.mixedLeft();
                        double r = c0.ssg.mixedRight() + c1.ssg.mixedRight();

                        // Native-rate tap for DSD capture (pre-decimation)
                        if (tapActive)
                        {
                            _nativeTap->push(static_cast<float>(l), static_cast<float>(r));
                        }

                        // Accumulate samples
                        leftSum += l;
                        rightSum += r;
                        chipSums[0] += c0.ssg.mixedLeft();
                        chipSums[1] += c0.ssg.mixedRight();
                        chipSums[2] += c1.ssg.mixedLeft();
                        chipSums[3] += c1.ssg.mixedRight();
                        sampleCount++;
                    });
                }

                // FM boxcar sample (gated values; the hold when no half-tick
                // landed on this output sample), gain-scaled (§7.1)
                const double f0 = _pair.fmLqSample(0) * _fmGain;
                const double f1 = _pair.fmLqSample(1) * _fmGain;

                // Average (simple boxcar decimation)
                float c0L, c0R, c1L, c1R;
                if (sampleCount > 0)
                {
                    // Per-chip buffers average each chip on its own (the mixer plays them), as the legacy device
                    // does - its chip 0 is TSFM's chip 1 (§11 chip swap), each buffer still one chip's own sum.
                    // Splitting the sum by the chips' current levels blew up: past the DC filter the levels are
                    // signed, and two chips cancelling each other made the share hundreds of times full scale
                    c0L = static_cast<float>(chipSums[0] / sampleCount);
                    c0R = static_cast<float>(chipSums[1] / sampleCount);
                    c1L = static_cast<float>(chipSums[2] / sampleCount);
                    c1R = static_cast<float>(chipSums[3] / sampleCount);

                    leftSample = ToSample((leftSum / sampleCount) + f0 + f1);
                    rightSample = ToSample((rightSum / sampleCount) + f0 + f1);
                }
                else
                {
                    // No ticks this sample - use previous value
                    c0L = static_cast<float>(chip0.ssg.mixedLeft());
                    c0R = static_cast<float>(chip0.ssg.mixedRight());
                    c1L = static_cast<float>(chip1.ssg.mixedLeft());
                    c1R = static_cast<float>(chip1.ssg.mixedRight());
                    leftSample = ToSample(c0L + c1L + static_cast<float>(f0) + static_cast<float>(f1));
                    rightSample = ToSample(c0R + c1R + static_cast<float>(f0) + static_cast<float>(f1));
                }

                // Store per-chip buffers (SSG + centre-panned FM, §6.4/§7.2)
                _chip0Buffer[_ayBufferIndex]     = ToSample(c0L);
                _chip0Buffer[_ayBufferIndex + 1] = ToSample(c0R);
                _chip1Buffer[_ayBufferIndex]     = ToSample(c1L);
                _chip1Buffer[_ayBufferIndex + 1] = ToSample(c1R);
                _fm0Buffer[_ayBufferIndex]       = ToSample(f0);
                _fm0Buffer[_ayBufferIndex + 1]   = ToSample(f0);
                _fm1Buffer[_ayBufferIndex]       = ToSample(f1);
                _fm1Buffer[_ayBufferIndex + 1]   = ToSample(f1);
            }

            // Store samples in combined output buffer
            _ayBuffer[_ayBufferIndex++] = leftSample;
            _ayBuffer[_ayBufferIndex++] = rightSample;
        }
    }

    _lastTStates = scaledCurrentTStates;

    // Quiet at rest with nothing queued: every output stays +0.0 until something enters the device, so the gate holds
    // until then and the step at the last noted T renders them in one run (settleSteps)
    if (sleepsThroughSilence())
    {
        _stepDeadline = std::numeric_limits<uint64_t>::max();
        _quietDeferral = true;
    }
}

bool SoundChip_TurboSoundFM::sleepsThroughSilence() const
{
    if (!_stepGate || !_lazySync || !_deferAllowed || !_pair.quietTicksEnabled() || !_pair.hqEnabled() ||
        _pair.synthesisSuppressed() || _pair.adoptsCpuClock() || _nativeTap->isActive() ||
        _context->emulatorState.HostSpeedMultiplier() != 1)
        return false;
    for (size_t zeroRun : _ssgZeroRun)
    {
        if (zeroRun < FilterDecimator::history())
            return false;
    }
    for (int i = 0; i < 2; i++)
    {
        const TsfmChip& c = *_pair.chip(i);
        const TsfmOutputState& o = c.out;
        if (!c.ssgWrites.empty() || !c.ssg.silentAtRest() || !c.fmIdleKnown || o.hold != 0.0 ||
            o.nativeTap->isActive() || o.zeroRun < FilterDecimator::history() || !o.coupling.restsAt(0.0))
            return false;
    }
    return true;
}

size_t SoundChip_TurboSoundFM::renderQuietOutputs()
{
    if (!_pair.quietTicksEnabled())
        return 0;
    for (size_t zeroRun : _ssgZeroRun)
    {
        if (zeroRun < FilterDecimator::history())
            return 0;
    }
    // The quiet now, and the first events that end it: an SSG write due, an FM word off the hold
    int64_t nextWrite = std::numeric_limits<int64_t>::max();
    int64_t nextWord = std::numeric_limits<int64_t>::max();
    for (int i = 0; i < 2; i++)
    {
        const TsfmChip& c = *_pair.chip(i);
        const TsfmOutputState& o = c.out;
        if (!c.ssg.silentAtRest() || o.nativeTap->isActive() || o.zeroRun < FilterDecimator::history() ||
            !o.coupling.restsAt(_board.fmEnabled ? o.hold : 0.0))
            return 0;
        if (!c.ssgWrites.empty())
            nextWrite = std::min<int64_t>(nextWrite, c.ssgWrites.front().t);
        for (size_t w = 0; w < c.words.size(); w++)
        {
            if (static_cast<double>(c.words.at(w).word) / 32768.0 != o.hold)
            {
                nextWord = std::min<int64_t>(nextWord, int64_t(c.words.at(w).t));
                break;
            }
        }
    }

    // The outputs the run can take: each one's ticks with the master's own phase arithmetic (feedSample adds 1.0 a
    // tick, skipOutput takes an output's worth), up to the one with a tick at or past a write (renderTick applies the
    // writes due by the tick's cursor) or a half-tick at or past a word off the hold
    TsfmChip& chip0 = *_pair.chip(0);
    TsfmChip& chip1 = *_pair.chip(1);
    FilterDecimator& master = chip0.ssg.decimatorLeft();
    const double perOutput = master.samplesPerOutput();
    const double startPhase = master.phase();
    double phase = startPhase;
    const int64_t cursor = _pair.renderT();
    uint64_t ticks = 0;
    size_t outputs = 0;
    uint64_t samplePhase = _samplePhase;
    size_t index = _ayBufferIndex;
    _quietOutputTicks.clear();
    while (samplePhase >= CPU_CLOCK_RATE && index < MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS)
    {
        double p = phase;
        uint64_t t = 0;
        bool reached = false;
        while (!(p >= perOutput))
        {
            const int64_t tick = cursor + 16 * int64_t(ticks + t);
            if (tick >= nextWrite || tick + 8 >= nextWord)
            {
                reached = true;
                break;
            }
            p += 1.0;
            t++;
        }
        if (reached)
            break;
        p -= perOutput;
        phase = p;
        ticks += t;
        outputs++;
        _quietOutputTicks.push_back(static_cast<uint32_t>(t));
        samplePhase -= CPU_CLOCK_RATE;
        index += AUDIO_CHANNELS;
    }
    if (outputs == 0)
        return 0;

    // The run: the pair's side (generators, words, FM decimators, the cursor), then the four SSG decimators
    if (ticks > 0 && !_pair.renderQuiet(static_cast<uint32_t>(ticks), _board.fmEnabled))
        return 0;
    // Each decimator that keeps its own phase (all four here: none is a slave) takes the run's additions and output steps
    // in their order, as feedSample and skipOutput would; from the master's starting phase that is the master's own
    // result
    for (FilterDecimator* d : {&chip0.ssg.decimatorLeft(), &chip0.ssg.decimatorRight(), &chip1.ssg.decimatorLeft(),
                               &chip1.ssg.decimatorRight()})
    {
        double own = d->phase();
        d->moveZerosIntoSilence(size_t(ticks));  // the phase set below
        if (d->isSlave())
            continue;
        if (own == startPhase && d->samplesPerOutput() == perOutput)
            own = phase;
        else
        {
            for (const uint32_t t : _quietOutputTicks)
            {
                for (uint32_t i = 0; i < t; i++)
                    own += 1.0;
                own -= d->samplesPerOutput();
            }
        }
        d->setPhase(own);
    }
    for (size_t& zeroRun : _ssgZeroRun)
        zeroRun += size_t(ticks);
    for (size_t i = 0; i < outputs; i++)
    {
        _chip0Buffer[_ayBufferIndex] = 0;
        _chip0Buffer[_ayBufferIndex + 1] = 0;
        _chip1Buffer[_ayBufferIndex] = 0;
        _chip1Buffer[_ayBufferIndex + 1] = 0;
        _fm0Buffer[_ayBufferIndex] = 0;
        _fm0Buffer[_ayBufferIndex + 1] = 0;
        _fm1Buffer[_ayBufferIndex] = 0;
        _fm1Buffer[_ayBufferIndex + 1] = 0;
        _ayBuffer[_ayBufferIndex++] = 0;
        _ayBuffer[_ayBufferIndex++] = 0;
    }
    _samplePhase = samplePhase;
    _longestQuietRun = std::max(_longestQuietRun, outputs);
    return outputs;
}

void SoundChip_TurboSoundFM::handleFrameEnd()
{
    // The frame's deferred clocks, to the last instruction's end: z80->t is already rebased here, so the noted
    // position is the frame end the per-step sync reached, and the next frame start rebases from it
    catchUp();

    // HUD activity (AY/TS and FM): SoundManager (AudioActivityIndicators),
    // from the audio-settings LEDs computed on the chip / FM buffers

    // No FM word drain here: words the render cursor has not reached stay
    // queued and are rebased into the next frame (§6.2). Draining them into
    // the hold skipped their samples and slipped the FM timeline
}

/// endregion </Emulation events>

/// region <PortDevice interface methods>

uint8_t SoundChip_TurboSoundFM::portDeviceInMethod(uint16_t port)
{
    syncTo(nowT());

    // Status mode applies to #FFFD only; IN #BFFD keeps the register path
    // for regression parity with the legacy device (§5.3)
    if (_board.statusRead && port == PORT_FFFD)
        return _pair.readStatus(_board.chip);  // busy | timer B | timer A

    // SSG register on the bus (input ports read their pins), #FF while an FM
    // address is latched (hardware-reference H2)
    return _pair.readData(_board.chip);
}

void SoundChip_TurboSoundFM::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    syncTo(nowT());

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
                // (hardware §3.4): ymfm address + prescaler side effect
                // (0x2D-0x2F), SSG select (<0x10 selects; >=0x10 keeps the
                // previous register)
                _pair.writeAddress(_board.chip, value);
            }
            break;

        case PORT_BFFD:
            // SSG register (the CPU sees it now, the generators on the tick
            // of this T-state) or FM register (allowed while muted); both
            // set busy
            _pair.writeData(_board.chip, value);
            if (_context && _context->chipRegisterWatch) [[unlikely]]
            {
                const uint8_t reg = _pair.chip(_board.chip)->address;
                ReportChipRegisterWrite(_context, reg < 0x10 ? SoundChipKind::Ay : SoundChipKind::Fm,
                                        static_cast<uint8_t>(_board.chip), reg, value);
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
        record.reg = (port == PORT_BFFD) ? _pair.chip(_board.chip)->address : value;
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
    settleSteps();
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
inline void put_u32(uint8_t*& cur, uint32_t v)  { std::memcpy(cur, &v, 4); cur += 4; }
inline void put_u64(uint8_t*& cur, uint64_t v)  { std::memcpy(cur, &v, 8); cur += 8; }
inline void put_f64(uint8_t*& cur, double v)    { std::memcpy(cur, &v, 8); cur += 8; }

inline uint8_t  get_u8 (const uint8_t*& cur)  { return *cur++; }
inline uint32_t get_u32(const uint8_t*& cur)  { uint32_t v; std::memcpy(&v, cur, 4); cur += 4; return v; }
inline uint64_t get_u64(const uint8_t*& cur)  { uint64_t v; std::memcpy(&v, cur, 8); cur += 8; return v; }
inline double   get_f64(const uint8_t*& cur)  { double v; std::memcpy(&v, cur, 8); cur += 8; return v; }

// Per-chip payload (the shared Ym2203Pair writes it): address(1) +
// fmClockPhase(4) + timer[2](8) + busy(4) + ymfmSize(2) + ymfm payload(494:
// ymfm's save_restore() of ym2203 - FM engine, its dormant internal SSG copy,
// the SSG resampler - measured and pinned by ymfm_ttd_patch_test.cpp) + SSG
// payload(73, SoundChip_AY8910)
constexpr size_t kTsfmChipStateSize = Ym2203Pair::kChipStateSize;
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
// (Ym2203Pair::saveTimeline / loadTimeline)
constexpr size_t kTimelineStateSize = Ym2203Pair::kTimelineStateSize;
static_assert(kTimelineStateSize == 8 /* render cursor offset */ + 2 * (1 + SsgWriteQueue::kCapacity * (4 + 1 + 1)),
              "TSFM timeline tail size drift");

// v5 tail: the current frame's render progress - the frame position the
// render loop has reached (_lastTStates, scaled T-states) and the samples it
// has produced (_ayBufferIndex). A checkpoint is taken inside a started frame:
// at its first instruction (both 0) for per-frame checkpoints, anywhere for a
// recording's baseline. Restarting them at 0 on a mid-frame restore rendered
// [0, t) a second time - extra generator ticks and a shifted _samplePhase
constexpr size_t kFrameProgressStateSize = 4 /* lastTStates */ + 4 /* ayBufferIndex */;

// version(1) + board(1) + render-loop phase(48) + 2 x per-chip payload + v4 tail + v5 tail
constexpr size_t kTsfmStateSize =
    1 + 1 + kRenderPhaseStateSize + 2 * kTsfmChipStateSize + kTimelineStateSize + kFrameProgressStateSize;
static_assert(kTsfmStateSize == 2008,
              "TSFM state size must match design §8.2 + render-phase fixes + v4 timeline + v5 frame progress (2008 bytes)");

static_assert(1 + 1 + kRenderPhaseStateSize == SoundChip_TurboSoundFM::kTsfmStateHeaderSize,
              "the engine descriptor's time fields follow the blob header (TTDDescribe)");

constexpr uint8_t kTsfmStateVersion = 5;

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
    // A capture reads the chips where the CPU is: a sync still deferred runs first (it changes nothing a later sync would not)
    const_cast<SoundChip_TurboSoundFM*>(this)->catchUp();

    uint8_t* cur = dst;

    put_u8(cur, kTsfmStateVersion);
    // Bit 3: the render cursor's pending re-anchor (consumed at the next frame
    // start; a capture inside a frame - a recording's first checkpoint - can hold it)
    put_u8(cur, static_cast<uint8_t>(EncodeBoardByte(_board) | (_renderReanchor ? 0x08 : 0)));
    put_u64(cur, _samplePhase);
    put_f64(cur, _decimationPhase);
    put_f64(cur, _pair.chip(0)->ssg.decimatorLeft().phase());
    put_f64(cur, _pair.chip(0)->ssg.decimatorRight().phase());
    put_f64(cur, _pair.chip(1)->ssg.decimatorLeft().phase());
    put_f64(cur, _pair.chip(1)->ssg.decimatorRight().phase());

    // Per chip: latch, clock phase, timers, busy, ymfm engine, SSG half
    _pair.saveChipState(0, cur);
    _pair.saveChipState(1, cur);

    // v4 tail: render cursor + pending SSG writes, relative to the synced
    // position
    _pair.saveTimeline(cur);

    // v5 tail: the current frame's render progress
    put_u32(cur, _lastTStates);
    put_u32(cur, static_cast<uint32_t>(_ayBufferIndex));

    assert(static_cast<size_t>(cur - dst) == kTsfmStateSize);
}

void SoundChip_TurboSoundFM::TTDLoadState(const uint8_t* src)
{
    _stepPending = false;   // the restored state replaces what the gate noted
    _stepDeadline = 0;
    _quietDeferral = false;
    _syncDeferred = false;  // the restored chips stand at the restored position
    const uint8_t* cur = src;
    ++_renderEpoch;   // the host's character chains restart (ITurboSoundDevice::renderEpoch)

    const uint8_t version = get_u8(cur);
    assert(version == kTsfmStateVersion &&
           "TTD blob predates the v5 frame progress (soundchip_turbosoundfm.cpp) "
           "and cannot be loaded by this build");
    (void)version;
    TsfmBoard board;
    const uint8_t boardByte = get_u8(cur);
    DecodeBoardByte(boardByte, board);
    _board = board;
    const bool renderReanchor = (boardByte & 0x08) != 0;
    _samplePhase = get_u64(cur);
    _decimationPhase = get_f64(cur);
    const double chip0LeftPhase = get_f64(cur);
    const double chip0RightPhase = get_f64(cur);
    const double chip1LeftPhase = get_f64(cur);
    const double chip1RightPhase = get_f64(cur);

    _pair.loadChipState(0, cur);
    _pair.loadChipState(1, cur);

    // v4 tail: render cursor + pending SSG writes, rebuilt around the
    // restored CPU position (the machine resumes at the start of a frame).
    // The cursor is historical: it decides on which tick the pending and
    // future SSG writes land. The pair continues from this position without
    // adopting it: the restore sets z80.t before the devices load, so the
    // next syncTo() clocks the FM chips over exactly the T-states the
    // original run did (adopting the next position instead skipped the first
    // instruction's T-states and slipped every FM sample clock)
    _pair.loadTimeline(cur, nowT());

    // v5 tail: the current frame's render progress
    const uint32_t lastTStates = get_u32(cur);
    const uint32_t ayBufferIndex = get_u32(cur);

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
    _pair.clearWords();
    _pair.flushOutputStage();
    _outputFlushPending = false;

    // The flush keeps each SSG decimator's resampling PHASE
    // (FilterDecimator::_phase) - but that is the live pre-seek one, and
    // phase is a tick-gating accumulator with the same determinism
    // requirement as _samplePhase/_decimationPhase above (v3 fix; see
    // kRenderPhaseStateSize comment). Restore it explicitly on top of the
    // flush: buffer silent, phase historically correct.
    _pair.chip(0)->ssg.decimatorLeft().setPhase(chip0LeftPhase);
    _pair.chip(0)->ssg.decimatorRight().setPhase(chip0RightPhase);
    _pair.chip(1)->ssg.decimatorLeft().setPhase(chip1LeftPhase);
    _pair.chip(1)->ssg.decimatorRight().setPhase(chip1RightPhase);

    // The render cursor is historical (v4 tail above), and so is a re-anchor
    // still pending for the next frame start (board byte bit 3)
    _renderReanchor = renderReanchor;

    // The per-frame render progress is historical (v5 tail): the frame was
    // rendered up to the checkpoint's position - left at its live pre-seek
    // values it miscounted the first frame (up to a few samples too many)
    // and shifted _samplePhase for good; restarted at 0 on a mid-frame
    // checkpoint it rendered [0, t) twice. The samples already produced this
    // frame are output content from before the seek: silence (Tier C).
    _lastTStates = lastTStates;
    _ayBufferIndex = ayBufferIndex;
    memset(_ayBuffer, 0x00, _ayAudioDescriptor.memoryBufferSizeInBytes);
    memset(_chip0Buffer, 0x00, _chip0AudioDescriptor.memoryBufferSizeInBytes);
    memset(_chip1Buffer, 0x00, _chip1AudioDescriptor.memoryBufferSizeInBytes);
    memset(_fm0Buffer, 0x00, _fm0AudioDescriptor.memoryBufferSizeInBytes);
    memset(_fm1Buffer, 0x00, _fm1AudioDescriptor.memoryBufferSizeInBytes);

    // _samplePhase itself is historical now, the mixer's accumulator is
    // still the live pre-seek one: it takes the device's phase AT THE FRAME
    // START (the mixer advances once per frame end) - the progress above
    // un-applied. Either left out, the two frame sample counts disagreed
    // every few frames after any seek (issue #2 again: one zero sample per
    // 904-sample frame)
    if (_context->pSoundManager)
    {
        const uint64_t frameStartPhase = _samplePhase - uint64_t(_lastTStates) * _coreRate +
                                         uint64_t(_ayBufferIndex / AUDIO_CHANNELS) * CPU_CLOCK_RATE;
        _context->pSoundManager->adoptSamplePhase(frameStartPhase);
    }
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
