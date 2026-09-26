#include "soundchip_turbosound.h"

#include <cstring>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/cpu/core.h"
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
        chip->applyRegister(q.front().reg, q.front().value);
        q.pop();
    }
    q.push(SsgWrite{_lastSeenT, reg, value});
}

void SoundChip_TurboSound::applySsgWrites(int64_t t)
{
    for (int i = 0; i < 2; i++)
    {
        SoundChip_AY8910* chip = i == 0 ? _chip0 : _chip1;
        SsgWriteQueue& q = _ssgWrites[i];
        while (!q.empty() && q.front().t <= t)
        {
            chip->applyRegister(q.front().reg, q.front().value);
            q.pop();
        }
    }
}

void SoundChip_TurboSound::applyAllSsgWrites()
{
    for (int i = 0; i < 2; i++)
    {
        SoundChip_AY8910* chip = i == 0 ? _chip0 : _chip1;
        SsgWriteQueue& q = _ssgWrites[i];
        while (!q.empty())
        {
            chip->applyRegister(q.front().reg, q.front().value);
            q.pop();
        }
    }
}

void SoundChip_TurboSound::handleFrameStart()
{
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
    // The timeline follows the core in every mode (the frame rebase and the
    // write timestamps need it), rendering does not
    _lastSeenT = nowT();
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
    size_t currentTStates = _context->emulatorState.AudioTstate(_context->pCore->GetZ80()->t);

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
            _samplePhase -= CPU_CLOCK_RATE;

            int16_t leftSample;
            int16_t rightSample;

            if (_hqEnabled)
            {
                // ========== HIGH QUALITY MODE ==========
                // Native clock rendering + FIR decimation
                //
                // Generators tick at PSG_CLOCK_RATE/8 = 218.75 kHz
                // FIR decimates to 44.1 kHz (~4.96:1 ratio)

                // Feed generator samples to decimator until we have an output
                while (!_chip0->decimatorLeft().hasOutput())
                {
                    // Tick generators (bypass internal prescaler), with the
                    // register writes timed up to this tick applied first
                    applySsgWrites(_renderT);
                    updateState(true);
                    _renderT += 16;

                    // Native-rate tap for DSD capture (pre-decimation, both chips summed)
                    if (tapActive)
                    {
                        _nativeTap->push(static_cast<float>(_chip0->mixedLeft() + _chip1->mixedLeft()),
                                         static_cast<float>(_chip0->mixedRight() + _chip1->mixedRight()));
                    }

                    // Feed mixed output to decimators
                    _chip0->decimatorLeft().feedSample(_chip0->mixedLeft());
                    _chip0->decimatorRight().feedSample(_chip0->mixedRight());
                    _chip1->decimatorLeft().feedSample(_chip1->mixedLeft());
                    _chip1->decimatorRight().feedSample(_chip1->mixedRight());
                }

                // Get decimated output per chip
                float c0L = _chip0->decimatorLeft().getOutput();
                float c0R = _chip0->decimatorRight().getOutput();
                float c1L = _chip1->decimatorLeft().getOutput();
                float c1R = _chip1->decimatorRight().getOutput();

                // Store per-chip buffers for registry-driven capture
                _chip0Buffer[_ayBufferIndex]     = static_cast<int16_t>(c0L * INT16_MAX);
                _chip0Buffer[_ayBufferIndex + 1] = static_cast<int16_t>(c0R * INT16_MAX);
                _chip1Buffer[_ayBufferIndex]     = static_cast<int16_t>(c1L * INT16_MAX);
                _chip1Buffer[_ayBufferIndex + 1] = static_cast<int16_t>(c1R * INT16_MAX);

                // Combined output
                leftSample = static_cast<int16_t>((c0L + c1L) * INT16_MAX);
                rightSample = static_cast<int16_t>((c0R + c1R) * INT16_MAX);
            }
            else
            {
                // ========== LOW QUALITY MODE ==========
                // Native clock rendering + simple averaging (no FIR)
                // Faster but may have aliasing on high frequencies

                double leftSum = 0.0;
                double rightSum = 0.0;
                int sampleCount = 0;

                // Run generator ticks for this output sample period
                // Generator rate = PSG_CLOCK_RATE / 8 (~218.75 kHz)
                // Ticks per output sample = (PSG_CLOCK_RATE / 8) / core rate (~4.96 @44.1k)
                _decimationPhase += _lqTicksPerSample;

                while (_decimationPhase >= 1.0)
                {
                    _decimationPhase -= 1.0;

                    // Tick generators directly (bypass internal prescaler),
                    // with the register writes timed up to this tick applied
                    applySsgWrites(_renderT);
                    updateState(true);
                    _renderT += 16;

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
                    sampleCount++;
                }

                // Average (simple boxcar decimation)
                float c0L, c0R, c1L, c1R;
                if (sampleCount > 0)
                {
                    // Split the accumulated sums per chip for per-chip buffers
                    // In LQ mode we don't have separate accumulators, so approximate
                    // by using current chip output ratios
                    double total = leftSum + rightSum;
                    double r0 = (total > 0) ? (_chip0->mixedLeft() + _chip0->mixedRight()) /
                                              (_chip0->mixedLeft() + _chip0->mixedRight() +
                                               _chip1->mixedLeft() + _chip1->mixedRight() + 1e-9) : 0.5;

                    c0L = static_cast<float>((leftSum * r0) / sampleCount);
                    c0R = static_cast<float>((rightSum * r0) / sampleCount);
                    c1L = static_cast<float>((leftSum * (1.0 - r0)) / sampleCount);
                    c1R = static_cast<float>((rightSum * (1.0 - r0)) / sampleCount);

                    leftSample = static_cast<int16_t>((leftSum / sampleCount) * INT16_MAX);
                    rightSample = static_cast<int16_t>((rightSum / sampleCount) * INT16_MAX);
                }
                else
                {
                    // No ticks this sample - use previous value
                    c0L = static_cast<float>(_chip0->mixedLeft());
                    c0R = static_cast<float>(_chip0->mixedRight());
                    c1L = static_cast<float>(_chip1->mixedLeft());
                    c1R = static_cast<float>(_chip1->mixedRight());
                    leftSample = static_cast<int16_t>((c0L + c1L) * INT16_MAX);
                    rightSample = static_cast<int16_t>((c0R + c1R) * INT16_MAX);
                }

                // Store per-chip buffers
                _chip0Buffer[_ayBufferIndex]     = static_cast<int16_t>(c0L * INT16_MAX);
                _chip0Buffer[_ayBufferIndex + 1] = static_cast<int16_t>(c0R * INT16_MAX);
                _chip1Buffer[_ayBufferIndex]     = static_cast<int16_t>(c1L * INT16_MAX);
                _chip1Buffer[_ayBufferIndex + 1] = static_cast<int16_t>(c1R * INT16_MAX);
            }

            // Store samples in combined output buffer
            _ayBuffer[_ayBufferIndex++] = leftSample;
            _ayBuffer[_ayBufferIndex++] = rightSample;
        }
    }

    _lastTStates = currentTStates;
}

void SoundChip_TurboSound::handleFrameEnd()
{
    // Nothing to drain. HUD activity: SoundManager (AudioActivityIndicators),
    // from the audio-settings LEDs computed on the chip buffers
}

/// endregion </Emulation events>

/// region <PortDevice interface methods>
uint8_t SoundChip_TurboSound::portDeviceInMethod(uint16_t port)
{
    uint8_t result = _currentChip->portDeviceInMethod(port);

    return result;
}

void SoundChip_TurboSound::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    switch (port)
    {
        case PORT_FFFD:
            if (value > 0x0F)
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
            _currentChip->latchRegister(reg, value);
            queueSsgWrite(_currentChip == _chip0 ? 0 : 1, reg, value);
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
constexpr size_t kSsgQueueStateSize = 1 + SsgWriteQueue::kCapacity * (4 + 1 + 1);
constexpr size_t kTimelineStateSize = 8 + 2 * kSsgQueueStateSize;

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
    const int64_t cursor = _renderT - base;
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
    const uint8_t* cur = src;

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
    int64_t cursor = 0;
    std::memcpy(&cursor, cur, 8);
    cur += 8;
    _renderT = base + cursor;
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
