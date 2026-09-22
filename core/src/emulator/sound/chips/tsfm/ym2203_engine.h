#pragma once

#include <stdafx.h>

#include <algorithm>
#include <cstdint>

#include <ymfm.h>
#include <ymfm_opn.h>

#include "emulator/sound/chips/soundchip_ay8910.h"

/// @file ym2203_engine.h
/// @brief ymfm integration layer for the TSFM chip core (design §9).
///
/// Three small classes between the SoundChip_TurboSoundFM device and the
/// vendored ymfm engine:
/// - Ym2203Interface: ymfm_interface with the two timers and the busy flag
///   carried in T-states, so the core loop can stop exactly on an expiry
///   instead of stepping past it (§9.2);
/// - Ym2203Engine: ymfm::ym2203 subclass exposing the protected pieces the
///   core loop needs - one FM sample per call, the prescaler, the busy
///   duration (§9.1);
/// - SsgOverrideAdapter: routes ymfm's internal SSG accesses to the same
///   SoundChip_AY8910 that serves the legacy port path (§9.3). The device
///   never enters that path through the ports (§5.3), so the adapter is a
///   safety net that keeps the two halves of one YM2203 consistent.

/// Timers and busy flag in T-states (design §9.2).
///
/// ymfm reports timer durations in master clocks and busy as a future
/// timestamp. With 1 master clock = 1 T-state (design §4) the counters can be
/// carried verbatim: ymfm_set_timer stores the remaining T-states (negative
/// stops the timer, matching ymfm's convention), ymfm_set_busy_end stores the
/// busy duration. advance() is only ever called with clocks up to the next
/// expiry (the core loop guarantees that), so expiry handling needs no
/// overshoot arithmetic.
class Ym2203Interface final : public ymfm::ymfm_interface
{
public:
    /// Timers stopped, not busy (§5.4 reset state)
    void reset()
    {
        _timer[0] = _timer[1] = -1;
        _busy = 0;
    }

    /// T-states until the next timer expiry; INT32_MAX when both are stopped
    int32_t clocksToNextExpiry() const
    {
        int32_t n = INT32_MAX;
        for (int32_t r : _timer)
            if (r > 0)
                n = std::min(n, r);
        return n;
    }

    /// Count down busy and the timers; fire expiries exactly on their
    /// T-state. Precondition: clocks <= clocksToNextExpiry().
    void advance(int32_t clocks)
    {
        if (_busy > 0)
            _busy = std::max(0, _busy - clocks);
        for (uint32_t t = 0; t < 2; t++)
        {
            if (_timer[t] > 0 && (_timer[t] -= clocks) == 0)
            {
                _timer[t] = -1;
                // Sets the status flag, CSM key-on, reload via ymfm_set_timer
                m_engine->engine_timer_expired(t);
            }
        }
    }

    /// region <ymfm_interface callbacks>
    void ymfm_set_timer(uint32_t tnum, int32_t durationClocks) override
    {
        _timer[tnum] = durationClocks;  // < 0 = stop
    }

    void ymfm_set_busy_end(uint32_t clocks) override
    {
        _busy = int32_t(clocks);
    }

    bool ymfm_is_busy() override
    {
        return _busy > 0;
    }

    void ymfm_update_irq(bool) override
    {
        // IRQ pin not connected on the board (design §9.2)
    }
    /// endregion </ymfm_interface callbacks>

    int32_t _timer[2] = {-1, -1};  // remaining T-states; -1 = stopped (TTD state, §8.2)
    int32_t _busy = 0;             // remaining busy T-states (TTD state, §8.2)
};

/// ymfm::ym2203 subclass exposing the engine surface the core loop needs
/// (design §9.1). clockFmOnce() is exactly ymfm's clock_fm() - engine clock,
/// channel sum, YM3014 10.3 floating-point quantisation - with the left
/// channel word returned as the DAC sample. generate() and the SSG resampler
/// are never called: the device drives the SSG through SoundChip_AY8910 and
/// the FM DAC through the word queue.
class Ym2203Engine final : public ymfm::ym2203
{
public:
    explicit Ym2203Engine(ymfm::ymfm_interface& intf) : ymfm::ym2203(intf)
    {
    }

    Ym2203Engine(const Ym2203Engine&) = delete;
    Ym2203Engine& operator=(const Ym2203Engine&) = delete;

    /// One FM sample: advance the engine one master clock and return the
    /// quantised left-channel DAC word
    int16_t clockFmOnce()
    {
        clock_fm();
        return int16_t(m_last_fm.data[0]);
    }

    /// Count @p count FM sample clocks without synthesising them (muted core, see
    /// ITurboSoundDevice::setCoreSynthesisSkipped): keeps the CPU-observable clock counter exact
    void skipFmClocks(uint32_t count)
    {
        m_fm.skip_clocks(count);
    }

    uint32_t fmClockPrescale() const
    {
        return m_fm.clock_prescale();  // 6 / 3 / 2
    }

    /// Busy duration set by a data write: 32 master clocks scaled by the
    /// prescaler (design §4)
    uint32_t busyClocks() const
    {
        return 32u * m_fm.clock_prescale();
    }

    /// Test/verification accessor for the FM register file (non-const:
    /// ymfm exposes regs() mutable-only)
    uint8_t fmReg(uint32_t regnum)
    {
        return m_fm.regs().read(regnum);
    }

    /// Test/verification accessor for the FM engine itself (ymfm's
    /// debug_channel / debug_operator getters: envelope state, attenuation)
    fm_engine& fmEngine()
    {
        return m_fm;
    }
};

/// Routes ymfm's internal SSG accesses into the SoundChip_AY8910 instance
/// that also serves the port path (design §9.3). ssg_reset() is a no-op: the
/// device owns AY reset so a ymfm reset() does not reset it twice (§5.4).
/// ssg_prescale_changed() is a no-op: the SSG clock change is not modelled
/// and the off-/6 prescaler is warned about once per frame instead (§9.4).
class SsgOverrideAdapter final : public ymfm::ssg_override
{
public:
    // Design §9.3 sketch also holds a reference to the FM engine; nothing
    // reads it (the adapter is a pure pass-through), and the zero-warnings
    // policy rejects unused private fields, so it is not kept.
    explicit SsgOverrideAdapter(SoundChip_AY8910& ssg) : _ssg(ssg)
    {
    }

    void ssg_reset() override
    {
    }

    uint8_t ssg_read(uint32_t reg) override
    {
        return _ssg.readRegister(uint8_t(reg & 0x0F));
    }

    void ssg_write(uint32_t reg, uint8_t v) override
    {
        _ssg.writeRegister(uint8_t(reg & 0x0F), v);
    }

    void ssg_prescale_changed() override
    {
    }

private:
    SoundChip_AY8910& _ssg;
};
