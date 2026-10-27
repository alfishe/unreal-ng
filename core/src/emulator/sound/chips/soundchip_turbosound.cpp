#include "soundchip_turbosound.h"

#include <limits>

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/breakpoints/chipregister.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"
#include "emulator/sound/soundmanager.h"

/// region <Emulation events>
int64_t SoundChip_TurboSound::nowT() const
{
    // The AY is clocked from its own socket: AudioTstate undoes the Scorpion
    // hardware turbo; the host speed multiplier is not applied (as TSFM).
    // No core: a standalone device (serializer tests) sits at T 0
    if (!_context->pCore)
        return 0;
    return int64_t(_context->emulatorState.AudioTstate(_context->pCore->GetZ80()->t));
}

namespace
{
// A clock marker on the SSG write queue (SetPsgClock): register numbers stop
// at 15, so bit 7 of `reg` tells it apart; the clock rides in 15 bits of
// 100 Hz (reg bits 0-6 high, value low). Queue entries are TTD state, so a
// switch still pending at a checkpoint is restored with its T-state
constexpr uint8_t kSsgClockMarker = 0x80;

// A mixed level [-1, 1] as a 16-bit sample. Two chips (and the FM parts) summed go past full scale: clamped,
// since a float out of the int16_t range converts to an undefined value. Scaled in the level's own type: a sample
// in range is what the plain conversion gave
template <typename Level>
int16_t ToSample(Level level)
{
    return static_cast<int16_t>(std::clamp(level * INT16_MAX, Level(INT16_MIN), Level(INT16_MAX)));
}

SsgWrite ClockMarker(int64_t t, uint32_t hz)
{
    const uint32_t code = hz / SoundChip_TurboSound::kPsgClockStepHz;
    return SsgWrite{t, static_cast<uint8_t>(kSsgClockMarker | ((code >> 8) & 0x7F)), static_cast<uint8_t>(code & 0xFF)};
}

uint32_t ClockMarkerHz(const SsgWrite& write)
{
    return ((uint32_t(write.reg & 0x7F) << 8) | write.value) * SoundChip_TurboSound::kPsgClockStepHz;
}
}  // namespace

void SoundChip_TurboSound::applySsgWrite(int chipIndex, const SsgWrite& write)
{
    if (write.reg & kSsgClockMarker) [[unlikely]]
    {
        applyPsgClock(ClockMarkerHz(write));
        return;
    }
    (chipIndex == 0 ? _chip0 : _chip1)->applyRegister(write.reg, write.value);
}

void SoundChip_TurboSound::applyPsgClock(uint32_t hz)
{
    _psgClock = hz;

    // One tick = 8 AY clocks = 8 x CPU_CLOCK_RATE / hz base T, in 1/120 T
    const uint64_t sub = (uint64_t(CPU_CLOCK_RATE) * 8u * kRenderSubUnits + hz / 2) / hz;
    _tickT = int64_t(sub / kRenderSubUnits);
    _tickSubT = uint32_t(sub % kRenderSubUnits);
    if (_tickSubT == 0)
        _renderSub = 0;

    const double rate = generatorRate();
    _lqTicksPerSample = rate / (double)_coreRate;
    _decimationStep = rate / (double)(_coreRate * FilterInterpolate::DECIMATE_FACTOR);
    _chip0->decimatorLeft().setInputRate(rate);
    _chip0->decimatorRight().setInputRate(rate);
    _chip1->decimatorLeft().setInputRate(rate);
    _chip1->decimatorRight().setInputRate(rate);
    _chip0->setGeneratorRate(rate);
    _chip1->setGeneratorRate(rate);
}

bool SoundChip_TurboSound::SetPsgClock(uint32_t hz)
{
    settleSteps();
    hz = (hz + kPsgClockStepHz / 2) / kPsgClockStepHz * kPsgClockStepHz;
    if (hz < kMinPsgClock || hz > kMaxPsgClock)
        return false;
    if (hz == _psgClockRequested)
        return true;
    _psgClockRequested = hz;

    if (_synthesisSuppressed)
    {
        // Nothing renders: the queue drains in order, then the clock follows
        applyAllSsgWrites();
        applyPsgClock(hz);
        return true;
    }

    // At this T-state on the render timeline (the port write scheme)
    _lastSeenT = nowT();
    _seenT = true;
    SsgWriteQueue& q = _ssgWrites[0];
    if (q.full())
    {
        applySsgWrite(0, q.front());
        q.pop();
    }
    q.push(ClockMarker(_lastSeenT, hz));
    return true;
}

void SoundChip_TurboSound::queueSsgWrite(int chipIndex, uint8_t reg, uint8_t value)
{
    SoundChip_AY8910* chip = chipIndex == 0 ? _chip0 : _chip1;
    if (_synthesisSuppressed)
    {
        // Nothing renders, so no tick would ever take it: the generators
        // simply follow the register file
        chip->applyRegister(reg, value);
        return;
    }
    SsgWriteQueue& q = _ssgWrites[chipIndex];
    if (q.full())
    {
        applySsgWrite(chipIndex, q.front());
        q.pop();
    }
    q.push(SsgWrite{_lastSeenT, reg, value});
}

void SoundChip_TurboSound::applySsgWrites(int64_t t)
{
    for (int i = 0; i < 2; i++)
    {
        SsgWriteQueue& q = _ssgWrites[i];
        while (!q.empty() && q.front().t <= t)
        {
            applySsgWrite(i, q.front());
            q.pop();
        }
    }
}

void SoundChip_TurboSound::applyAllSsgWrites()
{
    for (int i = 0; i < 2; i++)
    {
        SsgWriteQueue& q = _ssgWrites[i];
        while (!q.empty())
        {
            applySsgWrite(i, q.front());
            q.pop();
        }
    }
}

void SoundChip_TurboSound::handleFrameStart()
{
    settleSteps();
    // Frame rollover: AdjustFrameCounters already rebased z80->t; shift the
    // render cursor and every pending write by the same delta so they stay on
    // one continuous timeline (the TSFM scheme, kept bit-identical)
    if (_seenT)
    {
        const int64_t now = nowT();
        const int64_t delta = _lastSeenT - now;
        if (delta != 0)
        {
            _ssgWrites[0].rebase(delta);
            _ssgWrites[1].rebase(delta);
            _renderT -= delta;
        }
        _lastSeenT = now;
    }
    // Off its lag window: a rate or quality switch moved the render loop
    // against the CPU clock, or the timeline was lost (host speed multiplier
    // > 1, rendering resumed after suppression) - re-anchor
    if (_renderReanchor || _renderT < -4 * kTurboSoundRenderLagT || _renderT > 0)
    {
        _renderT = -kTurboSoundRenderLagT;
        _renderSub = 0;
        _renderReanchor = false;
    }
    if (_synthesisSuppressed)
        applyAllSsgWrites();  // queued just before suppression began

    // NOTE: the frame buffer clears below run even when synthesis is
    // suppressed (§6.1): the sound-feature-off output path relies on zeroed
    // buffers (SoundManager::handleFrameEnd mixes whatever is here), and a
    // TSFM device clears its word queues here for the same reason.
    _lastTStates = 0;
    // NOTE: _samplePhase deliberately NOT reset here. The fractional sample
    // phase must carry across frames (audio-sync design, Fix 1): zeroing it
    // every frame truncated the fractional sample per frame, locking the AY
    // at 903 samples/frame (never 904) - a systematic -0.019% rate bias vs
    // the mixer's accumulator, plus a phase discontinuity at each frame
    // boundary. _samplePhase resets only in reset() and setCoreRate().
    _ayBufferIndex = 0;

    // Initialize render buffers (combined + per-chip)
    memset(_ayBuffer, 0x00, _ayAudioDescriptor.memoryBufferSizeInBytes);
    memset(_chip0Buffer, 0x00, _chip0AudioDescriptor.memoryBufferSizeInBytes);
    memset(_chip1Buffer, 0x00, _chip1AudioDescriptor.memoryBufferSizeInBytes);

    // Audio from before an LQ -> HQ switch or a suppression gap must not
    // replay through the HQ decimators (ISSUES #7). History only: the
    // resampling phases gate generator ticks and keep their position
    if (_outputFlushPending)
    {
        _chip0->decimatorLeft().clearHistory();
        _chip0->decimatorRight().clearHistory();
        _chip1->decimatorLeft().clearHistory();
        _chip1->decimatorRight().clearHistory();
        _outputFlushPending = false;
    }
}

/// @brief Generate audio samples synchronized to CPU t-states
///
/// ## Native Clock Architecture
///
/// AY-3-8910 runs at PSG_CLOCK_RATE (1.75 MHz for Pentagon, 1.7734 MHz for Spectrum 128).
/// Internally, the chip divides this by 16 for tone/noise generators (~109.375 kHz effective rate).
///
/// Previous implementation used 64x oversampling at 44100*64 = 2.8224 MHz, which is
/// asynchronous to the chip clock. This caused:
/// - Timing jitter (±0.18 µs per event)
/// - FM sidebands from beating between the two frequencies
/// - Subtle "impurity" on high notes
///
/// New implementation:
/// - Generators tick at true PSG_CLOCK_RATE (with internal /16 prescaler)
/// - Fractional decimation (PSG_CLOCK_RATE → 44100 Hz) using phase accumulator
/// - Same approach as amiga-paula PWM renderer
///
/// Benefits:
/// - Zero jitter on generator events
/// - ~37% less CPU (1.77 MHz < 2.82 MHz)
/// - Correct relationship between chip clock and generator periods
void SoundChip_TurboSound::handleStep()
{
    // The step gate: before the deadline a step only notes where it ended (settleSteps books it)
    if (_stepDeadline != 0)
    {
        if (noteGatedStep(uint64_t(nowT()), _context->emulatorState.HostSpeedMultiplier()))
            return;
        settleSteps();
    }
    stepAt(nowT());
}

void SoundChip_TurboSound::stepAt(int64_t now)
{
    // The timeline follows the core in every mode (the frame rebase and the
    // write timestamps need it), rendering does not
    _lastSeenT = now;
    _seenT = true;

    // Output-stage suppression (§6.1): turbo without audio or the sound
    // feature off. The manager keeps calling - a device with an emulated
    // core (TSFM) advances it here - but the legacy device has nothing to
    // do when its rendering is off, so this early return is the whole
    // suppressed cost.
    if (_synthesisSuppressed)
        return;

    // Hardware turbo descaled: the AY has its own clock, so under the Scorpion
    // 7 MHz flip-flop the chip must see the real-time position (t/2), not the
    // doubled CPU count - otherwise it emitted 2x samples per frame
    size_t currentTStates = size_t(now);  // nowT(): AudioTstate of the CPU's clock

    // Scale t-states by the HOST speed multiplier for correct AY audio pitch
    uint8_t speedMultiplier = _context->emulatorState.HostSpeedMultiplier();
    size_t scaledCurrentTStates = currentTStates * speedMultiplier;

    // Partition time exactly at the frame boundary: the last instruction of
    // a frame runs a few T-states past it, and those T-states are counted
    // again at the top of the next frame (AdjustFrameCounters rebases t,
    // handleFrameStart zeroes _lastTStates). Clip here so each frame feeds
    // the accumulator exactly config.frame x multiplier T-states - the same
    // quantity SoundManager::handleFrameEnd adds to its accumulator - and
    // the two never disagree on a frame's sample count. The generator time
    // past the boundary is not lost: the next frame starts counting from 0.
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
            // The outputs due in one quiet run while nothing sounds (renderQuietOutputs)
            if (_hqEnabled && !tapActive && renderQuietOutputs() > 0)
                continue;

            _samplePhase -= CPU_CLOCK_RATE;

            int16_t leftSample;
            int16_t rightSample;

            if (_hqEnabled)
            {
                // ========== HIGH QUALITY MODE ==========
                // Native clock rendering + FIR decimation
                //
                // Generators tick at _psgClock / 8 = 218.75 kHz by default
                // FIR decimates to 44.1 kHz (~4.96:1 ratio)

                auto feedSsg = [](FilterDecimator& d, double v, size_t& zeroRun) {
                    d.feedSample(v);
                    zeroRun = v == 0.0 ? zeroRun + 1 : 0;
                };
                // Feed generator samples to decimator until we have an output
                // The pair quiet at rest up to the next output (renderQuiet) and the four streams all zeros: the ticks
                // feed zeros, which moves only their positions
                const size_t quiet = _chip0->decimatorLeft().feedsToNextOutput();
                if (!tapActive && _ssgZeroRun[0] >= FilterDecimator::history() &&
                    _ssgZeroRun[1] >= FilterDecimator::history() && _ssgZeroRun[2] >= FilterDecimator::history() &&
                    _ssgZeroRun[3] >= FilterDecimator::history() && renderQuiet(static_cast<uint32_t>(quiet)))
                {
                    _chip0->decimatorLeft().feedZerosIntoSilence(quiet);
                    _chip0->decimatorRight().feedZerosIntoSilence(quiet);
                    _chip1->decimatorLeft().feedZerosIntoSilence(quiet);
                    _chip1->decimatorRight().feedZerosIntoSilence(quiet);
                    for (size_t& zeroRun : _ssgZeroRun)
                        zeroRun += quiet;
                }
                while (!_chip0->decimatorLeft().hasOutput())
                {
                    // Tick generators (bypass internal prescaler), with the
                    // register writes timed up to this tick applied first
                    applySsgWrites(_renderT);
                    updateState(true);
                    advanceRenderCursor();

                    // Native-rate tap for DSD capture (pre-decimation, both chips summed)
                    if (tapActive)
                    {
                        _nativeTap->push(static_cast<float>(_chip0->mixedLeft() + _chip1->mixedLeft()),
                                         static_cast<float>(_chip0->mixedRight() + _chip1->mixedRight()));
                    }

                    // Feed mixed output to decimators
                    feedSsg(_chip0->decimatorLeft(), _chip0->mixedLeft(), _ssgZeroRun[0]);
                    feedSsg(_chip0->decimatorRight(), _chip0->mixedRight(), _ssgZeroRun[1]);
                    feedSsg(_chip1->decimatorLeft(), _chip1->mixedLeft(), _ssgZeroRun[2]);
                    feedSsg(_chip1->decimatorRight(), _chip1->mixedRight(), _ssgZeroRun[3]);
                }

                // Get decimated output per chip
                // One pass over the taps for the four decimators fed in lockstep (FilterDecimator::getOutputs)
                FilterDecimator* decimators[4] = {&_chip0->decimatorLeft(), &_chip0->decimatorRight(),
                                                  &_chip1->decimatorLeft(), &_chip1->decimatorRight()};
                // A silent chip side skips its FIR (FilterDecimator::getOutputsSkippingSilent): the same +0.0
                double values[4];
                FilterDecimator::getOutputsSkippingSilent(decimators, _ssgZeroRun, 4, values);
                float c0L = static_cast<float>(values[0]);
                float c0R = static_cast<float>(values[1]);
                float c1L = static_cast<float>(values[2]);
                float c1R = static_cast<float>(values[3]);

                // Store per-chip buffers for registry-driven capture
                _chip0Buffer[_ayBufferIndex]     = ToSample(c0L);
                _chip0Buffer[_ayBufferIndex + 1] = ToSample(c0R);
                _chip1Buffer[_ayBufferIndex]     = ToSample(c1L);
                _chip1Buffer[_ayBufferIndex + 1] = ToSample(c1R);

                // Combined output
                leftSample = ToSample(c0L + c1L);
                rightSample = ToSample(c0R + c1R);
            }
            else
            {
                // ========== LOW QUALITY MODE ==========
                // Native clock rendering + simple averaging (no FIR)
                // Faster but may have aliasing on high frequencies

                double leftSum = 0.0;
                double rightSum = 0.0;
                double chipSums[4] = {};  // chip 0 left / right, chip 1 left / right: the per-chip buffers
                int sampleCount = 0;

                // Run generator ticks for this output sample period
                // Generator rate = _psgClock / 8 (~218.75 kHz by default)
                // Ticks per output sample = (PSG_CLOCK_RATE / 8) / core rate (~4.96 @44.1k)
                _decimationPhase += _lqTicksPerSample;

                while (_decimationPhase >= 1.0)
                {
                    _decimationPhase -= 1.0;

                    // Tick generators directly (bypass internal prescaler),
                    // with the register writes timed up to this tick applied
                    applySsgWrites(_renderT);
                    updateState(true);
                    advanceRenderCursor();

                    double l = _chip0->mixedLeft() + _chip1->mixedLeft();
                    double r = _chip0->mixedRight() + _chip1->mixedRight();

                    // Native-rate tap for DSD capture (pre-decimation)
                    if (tapActive)
                    {
                        _nativeTap->push(static_cast<float>(l), static_cast<float>(r));
                    }

                    // Accumulate samples
                    leftSum += l;
                    rightSum += r;
                    chipSums[0] += _chip0->mixedLeft();
                    chipSums[1] += _chip0->mixedRight();
                    chipSums[2] += _chip1->mixedLeft();
                    chipSums[3] += _chip1->mixedRight();
                    sampleCount++;
                }

                // Average (simple boxcar decimation)
                float c0L, c0R, c1L, c1R;
                if (sampleCount > 0)
                {
                    // Per-chip buffers average each chip on its own (the mixer plays them). Splitting the sum by
                    // the chips' current levels blew up: past the DC filter the levels are signed, and two chips
                    // cancelling each other made the share hundreds of times full scale
                    c0L = static_cast<float>(chipSums[0] / sampleCount);
                    c0R = static_cast<float>(chipSums[1] / sampleCount);
                    c1L = static_cast<float>(chipSums[2] / sampleCount);
                    c1R = static_cast<float>(chipSums[3] / sampleCount);

                    leftSample = ToSample(leftSum / sampleCount);
                    rightSample = ToSample(rightSum / sampleCount);
                }
                else
                {
                    // No ticks this sample - use previous value
                    c0L = static_cast<float>(_chip0->mixedLeft());
                    c0R = static_cast<float>(_chip0->mixedRight());
                    c1L = static_cast<float>(_chip1->mixedLeft());
                    c1R = static_cast<float>(_chip1->mixedRight());
                    leftSample = ToSample(c0L + c1L);
                    rightSample = ToSample(c0R + c1R);
                }

                // Store per-chip buffers
                _chip0Buffer[_ayBufferIndex]     = ToSample(c0L);
                _chip0Buffer[_ayBufferIndex + 1] = ToSample(c0R);
                _chip1Buffer[_ayBufferIndex]     = ToSample(c1L);
                _chip1Buffer[_ayBufferIndex + 1] = ToSample(c1R);
            }

            // Store samples in combined output buffer
            _ayBuffer[_ayBufferIndex++] = leftSample;
            _ayBuffer[_ayBufferIndex++] = rightSample;
        }
    }

    // The render loop's position in the units it counts in (scaled, clipped): storing the unscaled position made
    // every step of a host-speed-multiplied frame count the frame from about its own position again - the phase
    // piled up a backlog of millions of samples that rendered 8192 samples per frame (time-compressed audio, a jump
    // at every frame boundary) for minutes after the speed went back to 1x. At 1x the two are the same number
    _lastTStates = uint32_t(scaledCurrentTStates);

    // Until the step whose T completes a sample every step only moves the timeline and the phase: the gate's deadline
    // (x1 speed, room in the buffer). samplePhase + d x rate < CPU_CLOCK_RATE holds for d < ceil((CPU_CLOCK_RATE -
    // samplePhase) / rate)
    if (_stepGate && speedMultiplier == 1 && _samplePhase < CPU_CLOCK_RATE &&
        _ayBufferIndex < MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS)
        _stepDeadline = uint64_t(_lastTStates) + (CPU_CLOCK_RATE - _samplePhase + _coreRate - 1) / _coreRate;

    // Quiet at rest with nothing queued: every output stays +0.0 until something enters the device, so the gate holds
    // until then and the step at the last noted T renders them in one run (settleSteps)
    if (_stepGate && _quietTicksEnabled && speedMultiplier == 1 && _hqEnabled && !_nativeTap->isActive() &&
        _ssgWrites[0].empty() && _ssgWrites[1].empty() && _chip0->silentAtRest() && _chip1->silentAtRest() &&
        _ssgZeroRun[0] >= FilterDecimator::history() && _ssgZeroRun[1] >= FilterDecimator::history() &&
        _ssgZeroRun[2] >= FilterDecimator::history() && _ssgZeroRun[3] >= FilterDecimator::history())
    {
        _stepDeadline = std::numeric_limits<uint64_t>::max();
        _quietDeferral = true;
    }
}

size_t SoundChip_TurboSound::renderQuietOutputs()
{
    if (!_quietTicksEnabled || !_chip0->silentAtRest() || !_chip1->silentAtRest())
        return 0;
    for (size_t zeroRun : _ssgZeroRun)
    {
        if (zeroRun < FilterDecimator::history())
            return 0;
    }
    int64_t nextWrite = std::numeric_limits<int64_t>::max();
    for (const SsgWriteQueue& q : _ssgWrites)
    {
        if (!q.empty())
            nextWrite = std::min<int64_t>(nextWrite, q.front().t);
    }

    // The outputs the run can take: each one's ticks with the master's own phase arithmetic (feedSample adds 1.0 a
    // tick, skipOutput takes an output's worth) and the render cursor's, up to the one with a tick at or past a write
    FilterDecimator& master = _chip0->decimatorLeft();
    const double perOutput = master.samplesPerOutput();
    const double startPhase = master.phase();
    double phase = startPhase;
    const int64_t cursorT = _renderT;
    const uint32_t cursorSub = _renderSub;
    uint64_t ticks = 0;
    size_t outputs = 0;
    uint64_t samplePhase = _samplePhase;
    size_t index = _ayBufferIndex;
    _quietOutputTicks.clear();
    while (samplePhase >= CPU_CLOCK_RATE && index < MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS)
    {
        double p = phase;
        uint64_t t = 0;
        const int64_t outputT = _renderT;
        const uint32_t outputSub = _renderSub;
        bool reached = false;
        while (!(p >= perOutput))
        {
            if (_renderT >= nextWrite)  // the tick at the cursor applies writes due by it
            {
                reached = true;
                break;
            }
            advanceRenderCursor();
            p += 1.0;
            t++;
        }
        if (reached)
        {
            _renderT = outputT;
            _renderSub = outputSub;
            break;
        }
        p -= perOutput;
        phase = p;
        ticks += t;
        outputs++;
        _quietOutputTicks.push_back(static_cast<uint32_t>(t));
        samplePhase -= CPU_CLOCK_RATE;
        index += AUDIO_CHANNELS;
    }
    if (outputs == 0)
    {
        _renderT = cursorT;
        _renderSub = cursorSub;
        return 0;
    }

    // The run (the cursor stands at its end already)
    _chip0->advanceSilent(static_cast<uint32_t>(ticks));
    _chip1->advanceSilent(static_cast<uint32_t>(ticks));
    // Each decimator that keeps its own phase (all four here: none is a slave) takes the run's additions and output steps
    // in their order, as feedSample and skipOutput would; from the master's starting phase that is the master's own
    // result
    for (FilterDecimator* d : {&_chip0->decimatorLeft(), &_chip0->decimatorRight(), &_chip1->decimatorLeft(),
                               &_chip1->decimatorRight()})
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
    _quietTicks += ticks;
    for (size_t i = 0; i < outputs; i++)
    {
        _chip0Buffer[_ayBufferIndex] = 0;
        _chip0Buffer[_ayBufferIndex + 1] = 0;
        _chip1Buffer[_ayBufferIndex] = 0;
        _chip1Buffer[_ayBufferIndex + 1] = 0;
        _ayBuffer[_ayBufferIndex++] = 0;
        _ayBuffer[_ayBufferIndex++] = 0;
    }
    _samplePhase = samplePhase;
    return outputs;
}

void SoundChip_TurboSound::handleFrameEnd()
{
    settleSteps();
    // Nothing to drain. HUD activity: SoundManager (AudioActivityIndicators),
    // from the audio-settings LEDs computed on the chip buffers
}

/// endregion </Emulation events>

/// region <PortDevice interface methods>
uint8_t SoundChip_TurboSound::portDeviceInMethod(uint16_t port)
{
    settleSteps();
    uint8_t result = _currentChip->portDeviceInMethod(port);

    return result;
}

void SoundChip_TurboSound::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    settleSteps();
    switch (port)
    {
        case PORT_FFFD:
            if (value > 0x0F && !_singleChip)
            {
                /// region <Attempt to switch active chip>
                switch (value)
                {
                    case 0xFF:
                        _currentChip = _chip0;
                        break;
                    case 0xFE:
                        _currentChip = _chip1;
                        break;
                    default:
                        break;
                }
                /// endregion </Attempt to switch active chip>
            }
            _currentChip->setRegister(value);
            break;
        case PORT_BFFD:
        {
            // SSG register: the CPU sees it now, the generators on the tick
            // of this T-state (SsgWriteQueue)
            _lastSeenT = nowT();
            _seenT = true;
            const uint8_t reg = _currentChip->getCurrentRegisterIndex();
            _currentChip->latchRegister(reg, value, static_cast<uint64_t>(_lastSeenT));
            queueSsgWrite(_currentChip == _chip0 ? 0 : 1, reg, value);
            if (_context && _context->chipRegisterWatch) [[unlikely]]
                ReportChipRegisterWrite(_context, SoundChipKind::Ay, _currentChip == _chip0 ? 0 : 1, reg, value);
            break;
        }
        default:
            return;  // Not an AY port — no tap
    }

    // Automation tap (MCP M7j): fires after the write so `chip` identifies the
    // chip that received it (chip-switch commands report the newly active chip)
    if (_logSink) [[unlikely]]
    {
        AYLogRecord record;
        record.port = port;
        record.value = value;
        record.chip = (_currentChip == _chip1) ? 1 : 0;
        record.reg = (port == PORT_BFFD) ? _currentChip->getCurrentRegister() : value;
        if (_context && _context->pCore)
        {
            const Z80* z80 = _context->pCore->GetZ80();
            record.pc = z80->m1_pc;     // PC of the OUT instruction, not the next one
            record.tacts = z80->t;      // T-states within the current frame
            record.frame = _context->emulatorState.frame_counter;
        }
        _logSink(_logSinkContext, record);
    }
}
/// endregion </PortDevice interface methods>

/// region <Ports interaction>
bool SoundChip_TurboSound::attachToPorts(PortDecoder* decoder)
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

void SoundChip_TurboSound::detachFromPorts()
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

/// region <TTDSerializable (P1.5 — parent TDD §6.4)>
//
// Layout: 1 byte current-chip index + chip0 full state + chip1 full state
// (each child SoundChip_AY8910's own TTDSerializable payload) + timeline tail
// + render-phase tail + frame-progress tail.

namespace
{
// Timeline tail (the same scheme as TSFM's v4): render cursor offset + per
// chip pending SSG writes {count, kCapacity x {i32 t offset, u8 reg, u8 value}}
// (clock markers included, see kSsgClockMarker)
//
// The cursor is an i64 whose value always fits an i32 (it sits within a few
// render lags of the frame end), so its upper half is pure sign extension.
// The AY clock state rides there, XORed onto the sign extension: bits 0-6 the
// cursor's fraction (_renderSub), bits 7-21 the generators' clock in 100 Hz
// (0 = the default PSG_CLOCK_RATE). Both are 0 at the default clock, so every
// existing capture keeps its bytes and loads as the default clock
constexpr size_t kSsgQueueStateSize = 1 + SsgWriteQueue::kCapacity * (4 + 1 + 1);
constexpr size_t kTimelineStateSize = 8 + 2 * kSsgQueueStateSize;
constexpr uint32_t kCursorSubMask = 0x7F;
constexpr uint32_t kCursorClockShift = 7;
constexpr uint32_t kCursorClockMask = 0x7FFF;

uint64_t PackCursor(int64_t cursor, uint32_t renderSub, uint32_t clockCode)
{
    if (cursor > INT32_MAX)
        cursor = INT32_MAX;
    if (cursor < INT32_MIN)
        cursor = INT32_MIN;
    const uint32_t extra = (renderSub & kCursorSubMask) | ((clockCode & kCursorClockMask) << kCursorClockShift);
    return uint64_t(cursor) ^ (uint64_t(extra) << 32);
}

void UnpackCursor(uint64_t raw, int64_t& cursor, uint32_t& renderSub, uint32_t& clockCode)
{
    const int32_t low = static_cast<int32_t>(static_cast<uint32_t>(raw & 0xFFFFFFFFu));
    const uint32_t extension = low < 0 ? 0xFFFFFFFFu : 0u;
    const uint32_t extra = static_cast<uint32_t>(raw >> 32) ^ extension;
    cursor = low;
    renderSub = extra & kCursorSubMask;
    clockCode = (extra >> kCursorClockShift) & kCursorClockMask;
}

// Render-phase tail (the same accumulators as TSFM's v2/v3 fixes): the
// render loop's free-running sample phase, the LQ boxcar phase and the four
// HQ decimators' resampling phases decide how many generator ticks land
// before each output sample - tick-gating state, so a restore must bring
// back their historical values or the tone/noise/envelope counters drift
// from the recording after a seek
constexpr size_t kRenderPhaseStateSize = 8 /* samplePhase */ + 8 /* decimationPhase */ + 4 * 8 /* decimator phases */;

// Frame-progress tail (TSFM's v5): the frame position the render loop has
// reached and the samples it has produced - non-zero for a checkpoint taken
// mid-frame (a recording's baseline)
constexpr size_t kFrameProgressStateSize = 4 /* lastTStates */ + 4 /* ayBufferIndex */;
}  // namespace

size_t SoundChip_TurboSound::TTDStateSize() const
{
    // _chip0 and _chip1 are always created in the constructor; both contribute
    // their fixed-size payload. If a chip pointer were somehow null we still
    // report the same fixed size (the round-trip contract is size-stable).
    const size_t oneChip = _chip0 ? _chip0->TTDStateSize() : 0;
    return 1 /*current-chip index*/ + 2 * oneChip + kTimelineStateSize + kRenderPhaseStateSize +
           kFrameProgressStateSize;
}

void SoundChip_TurboSound::TTDSaveState(uint8_t* dst) const
{
    const_cast<SoundChip_TurboSound*>(this)->settleSteps();
    uint8_t* cur = dst;

    // Current chip selector (0 or 1).
    uint8_t currentIdx = (_currentChip == _chip1) ? 1u : 0u;
    *cur++ = currentIdx;

    // Both chips always serialize (TurboSound owns two AYs even when only one
    // is active — see constructor). A null chip writes nothing for its slot,
    // which matches the TTDStateSize accounting above.
    if (_chip0)
    {
        _chip0->TTDSaveState(cur);
        cur += _chip0->TTDStateSize();
    }
    if (_chip1)
    {
        _chip1->TTDSaveState(cur);
        cur += _chip1->TTDStateSize();
    }

    // Timeline tail, relative to the last T-state seen (the frame end at a
    // checkpoint): the cursor decides on which tick every SSG write lands,
    // and a write pending at the checkpoint has not reached the generators
    const int64_t base = _lastSeenT;
    const uint32_t clockCode = _psgClock == PSG_CLOCK_RATE ? 0u : _psgClock / kPsgClockStepHz;
    const uint64_t cursor = PackCursor(_renderT - base, _renderSub, clockCode);
    std::memcpy(cur, &cursor, 8);
    cur += 8;
    for (const SsgWriteQueue& q : _ssgWrites)
    {
        *cur++ = uint8_t(q.size());
        for (size_t k = 0; k < SsgWriteQueue::kCapacity; ++k)
        {
            const SsgWrite w = (k < q.size()) ? q.at(k) : SsgWrite{};
            const int32_t offset = (k < q.size()) ? int32_t(w.t - base) : 0;
            std::memcpy(cur, &offset, 4);
            cur += 4;
            *cur++ = w.reg;
            *cur++ = w.value;
        }
    }

    // Render-phase tail
    const double phases[5] = {_decimationPhase,
                              _chip0->decimatorLeft().phase(), _chip0->decimatorRight().phase(),
                              _chip1->decimatorLeft().phase(), _chip1->decimatorRight().phase()};
    std::memcpy(cur, &_samplePhase, 8);
    cur += 8;
    std::memcpy(cur, phases, sizeof(phases));
    cur += sizeof(phases);

    // Frame-progress tail
    const uint32_t progress[2] = {_lastTStates, static_cast<uint32_t>(_ayBufferIndex)};
    std::memcpy(cur, progress, sizeof(progress));
}

void SoundChip_TurboSound::TTDLoadState(const uint8_t* src)
{
    _stepPending = false;  // the restored state replaces what the gate noted
    _stepDeadline = 0;
    const uint8_t* cur = src;
    ++_renderEpoch;   // the host's character chains restart (ITurboSoundDevice::renderEpoch)

    uint8_t currentIdx = *cur++;

    if (_chip0)
    {
        _chip0->TTDLoadState(cur);
        cur += _chip0->TTDStateSize();
    }
    if (_chip1)
    {
        _chip1->TTDLoadState(cur);
        cur += _chip1->TTDStateSize();
    }

    // Restore the active-chip pointer.
    _currentChip = (currentIdx == 1 && _chip1) ? _chip1 : _chip0;

    // Timeline tail, rebuilt around the restored CPU position (the machine
    // resumes at the start of a frame - the rebase a frame start does)
    const int64_t base = nowT();
    _lastSeenT = base;
    _seenT = true;
    uint64_t rawCursor = 0;
    std::memcpy(&rawCursor, cur, 8);
    cur += 8;
    int64_t cursor = 0;
    uint32_t renderSub = 0;
    uint32_t clockCode = 0;
    UnpackCursor(rawCursor, cursor, renderSub, clockCode);
    // The generators' clock first (it redesigns the decimators, whose phases
    // are put back below), then the cursor's fraction on its tick grid
    const uint32_t clock = clockCode == 0 ? static_cast<uint32_t>(PSG_CLOCK_RATE) : clockCode * kPsgClockStepHz;
    if (clock != _psgClock)
        applyPsgClock(clock);
    _renderT = base + cursor;
    _renderSub = _tickSubT != 0 ? renderSub % kRenderSubUnits : 0;
    _renderReanchor = false;
    for (SsgWriteQueue& q : _ssgWrites)
    {
        q.clear();
        const size_t count = *cur++;
        for (size_t k = 0; k < SsgWriteQueue::kCapacity; ++k)
        {
            int32_t offset = 0;
            std::memcpy(&offset, cur, 4);
            cur += 4;
            const uint8_t reg = *cur++;
            const uint8_t value = *cur++;
            if (k < count)
                q.push(SsgWrite{base + offset, reg, value});
        }
    }
    // The latest request: a clock marker still pending, else the clock now.
    // The machine re-derives it after the restore (the Profi from #DFFD) and
    // finds it equal, so nothing more is queued
    _psgClockRequested = _psgClock;
    for (size_t k = 0; k < _ssgWrites[0].size(); ++k)
        if (_ssgWrites[0].at(k).reg & kSsgClockMarker)
            _psgClockRequested = ClockMarkerHz(_ssgWrites[0].at(k));

    // Render-phase tail: historical tick-gating accumulators
    std::memcpy(&_samplePhase, cur, 8);
    cur += 8;
    double phases[5];
    std::memcpy(phases, cur, sizeof(phases));
    cur += sizeof(phases);
    _decimationPhase = phases[0];

    // Output stage: FLUSH the audio content (decimator history is output from
    // before the seek, not chip state - TSFM's click-on-seek policy), then put
    // the historical resampling phases back on top of the flushed decimators
    _chip0->decimatorLeft().clearHistory();
    _chip0->decimatorRight().clearHistory();
    _chip1->decimatorLeft().clearHistory();
    _chip1->decimatorRight().clearHistory();
    _chip0->decimatorLeft().setPhase(phases[1]);
    _chip0->decimatorRight().setPhase(phases[2]);
    _chip1->decimatorLeft().setPhase(phases[3]);
    _chip1->decimatorRight().setPhase(phases[4]);
    _outputFlushPending = false;

    // Frame-progress tail: the frame was rendered up to the checkpoint's
    // position (0 for a per-frame checkpoint). Left at its pre-seek values
    // the cursor miscounted the first frame and shifted _samplePhase against
    // the mixer for good (the TSFM fix, 4c0b7153); restarted at 0 on a
    // mid-frame checkpoint it rendered [0, t) twice. The samples produced
    // before the seek are output content: silence (Tier C)
    uint32_t progress[2];
    std::memcpy(progress, cur, sizeof(progress));
    _lastTStates = progress[0];
    _ayBufferIndex = progress[1];
    memset(_ayBuffer, 0x00, _ayAudioDescriptor.memoryBufferSizeInBytes);
    memset(_chip0Buffer, 0x00, _chip0AudioDescriptor.memoryBufferSizeInBytes);
    memset(_chip1Buffer, 0x00, _chip1AudioDescriptor.memoryBufferSizeInBytes);

    // The mixer's accumulator is still the live pre-seek one: it takes the
    // device's phase at the frame start (the mixer advances once per frame
    // end) - otherwise the two frame sample counts disagree after any seek
    if (_context->pSoundManager)
    {
        const uint64_t frameStartPhase = _samplePhase - uint64_t(_lastTStates) * _coreRate +
                                         uint64_t(_ayBufferIndex / AUDIO_CHANNELS) * CPU_CLOCK_RATE;
        _context->pSoundManager->adoptSamplePhase(frameStartPhase);
    }
}

/// endregion </TTDSerializable>
