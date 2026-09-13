#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include <ymfm_opn.h>

/// ymfm TTD patch regression tests (TSFM implementation plan, P1).
///
/// gtest port of verification/stress.cpp (see
/// docs/inprogress/2026-09-10-turbosound-fm/verification/) at CI size:
/// randomized YM2203 register traffic driven on a T-state axis, with three
/// chips running in lockstep — A never saves, B saves at every checkpoint,
/// C is a fresh chip restored from B's save at each checkpoint. Asserts the
/// three properties the local TTD patch (3rdparty/ymfm/PATCHES.md) provides:
///   * saving has no side effect (A and B hash identically),
///   * a restored chip continues bit-exactly (C matches A at checkpoints,
///     and restore -> save reproduces the checkpoint bytes),
///   * the serialized state is a stable 494 bytes.
/// With the patch reverted, the first two fail on upstream 81aec25c because
/// save_restore forces prepare() and update_prescale() — that negative
/// control is recorded in PATCHES.md.
///
/// These are stress tests by design and exceed the 50 ms per-test budget
/// (plan-mandated sizes: 400k steps = 3.2M clocks, and a 50k-step run that
/// checkpoints every single step); unit-size runs cannot cover the
/// save/restore path this densely.

namespace
{

/// Interface counting the two timers and the busy flag: busy decrements per
/// clock, timers fire engine_timer_expired() on expiry with overshoot carry
class TtdStressInterface : public ymfm::ymfm_interface
{
public:
    void ymfm_set_timer(uint32_t timer, int32_t duration) override { _timers[timer] = duration; }
    void ymfm_set_busy_end(uint32_t clocks) override { _busy = static_cast<int32_t>(clocks); }
    bool ymfm_is_busy() override { return _busy > 0; }

    void Advance(int32_t clocks)
    {
        for (int timer = 0; timer < 2; timer++)
        {
            if (_timers[timer] > 0 && (_timers[timer] -= clocks) <= 0)
            {
                int32_t overshoot = -_timers[timer];
                _timers[timer] = 0;
                m_engine->engine_timer_expired(timer);
                if (_timers[timer] > 0)
                    _timers[timer] -= overshoot;
            }
        }
        if (_busy > 0)
            _busy -= clocks;
    }

    int32_t _timers[2] = {-1, -1};
    int32_t _busy = 0;
};

class TtdStressEngine : public ymfm::ym2203
{
public:
    using ymfm::ym2203::ym2203;

    int16_t ClockOnceAndGetSample()
    {
        clock_fm();
        return static_cast<int16_t>(m_last_fm.data[0]);
    }

    uint32_t Prescale() const { return m_fm.clock_prescale(); }
};

struct TtdStressChip
{
    TtdStressInterface interface;
    TtdStressEngine engine{interface};
    int32_t phase = 0;

    TtdStressChip() { engine.reset(); }
};

void SaveChipState(TtdStressChip& chip, std::vector<uint8_t>& buffer)
{
    ymfm::ymfm_saved_state state(buffer, true);
    chip.engine.save_restore(state);
}

/// Restores the chip core from the checkpoint and mirrors the interface
/// state (timers, busy, FM clock phase) of the source chip — that part lives
/// outside the serialized state
void RestoreChipState(TtdStressChip& chip, std::vector<uint8_t>& buffer, const TtdStressChip& source)
{
    ymfm::ymfm_saved_state state(buffer, false);
    chip.engine.save_restore(state);
    chip.interface._timers[0] = source.interface._timers[0];
    chip.interface._timers[1] = source.interface._timers[1];
    chip.interface._busy = source.interface._busy;
    chip.phase = source.phase;
}

/// One T-state step of 8 clocks (a half AY tick); clocks FM every
/// 12*prescale clocks and folds the sample plus the status read into the hash
void StepChip(TtdStressChip& chip, uint64_t& hash)
{
    chip.interface.Advance(8);
    chip.phase += 8;
    while (chip.phase >= static_cast<int32_t>(12 * chip.engine.Prescale()))
    {
        chip.phase -= static_cast<int32_t>(12 * chip.engine.Prescale());
        hash = hash * 1000003u ^ static_cast<uint16_t>(chip.engine.ClockOnceAndGetSample());
    }
    hash = hash * 31u ^ chip.engine.read_status();
}

struct RegisterEvent
{
    uint64_t at = 0;
    uint8_t address = 0;
    uint8_t value = 0;
};

/// Randomized YM2203 register traffic shaped like a TFM player: key on/off,
/// operator levels and multipliers, frequencies, algorithm/feedback, timer
/// prescaler/mode changes, and CSM/accommodation writes
std::vector<RegisterEvent> GenerateEvents(uint32_t seed, uint64_t steps)
{
    std::mt19937 rng(seed);
    std::vector<RegisterEvent> events;
    uint64_t at = 0;
    auto write = [&events, &at](uint8_t address, uint8_t value) { events.push_back({at, address, value}); };

    while (at < steps)
    {
        at += rng() % 600;
        int kind = rng() % 100;
        if (kind < 30)
        {
            uint32_t slot = rng() % 16;
            uint32_t operatorIndex = rng() % 3;
            write(0x28, static_cast<uint8_t>((slot << 4) | operatorIndex));
        }
        else if (kind < 55)
        {
            static constexpr uint8_t bases[] = {0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90};
            uint32_t base = rng() % 7;
            uint32_t offset = rng() % 16;
            write(static_cast<uint8_t>(bases[base] + offset), static_cast<uint8_t>(rng()));
        }
        else if (kind < 70)
        {
            write(static_cast<uint8_t>(0xA0 + (rng() % 3)), static_cast<uint8_t>(rng()));
            write(static_cast<uint8_t>(0xA4 + (rng() % 3)), static_cast<uint8_t>(rng() & 0x1F));
        }
        else if (kind < 78)
        {
            write(static_cast<uint8_t>(0xA8 + (rng() % 3)), static_cast<uint8_t>(rng()));
            write(static_cast<uint8_t>(0xAC + (rng() % 3)), static_cast<uint8_t>(rng() & 0x1F));
        }
        else if (kind < 85)
        {
            write(static_cast<uint8_t>(0xB0 + (rng() % 3)), static_cast<uint8_t>(rng()));
        }
        else if (kind < 93)
        {
            write(0x24, static_cast<uint8_t>(rng()));
            write(0x25, static_cast<uint8_t>(rng()));
            write(0x26, static_cast<uint8_t>(rng()));
        }
        else if (kind < 98)
        {
            static constexpr uint8_t modes[] = {0x00, 0x15, 0x3F, 0x85, 0x8F, 0x45, 0x2A, 0xBF};
            write(0x27, modes[rng() % 8]);
        }
        else
        {
            write(rng() % 2 ? 0x2F : 0x2D, 0);
            if (rng() % 4 == 0)
                write(0x2E, 0);
            write(0x2D, 0);
        }
    }
    return events;
}

/// 0x2D..0x2F are addressed without data in the player (status/ACCOM mode)
void ApplyEvent(TtdStressChip& chip, const RegisterEvent& event)
{
    chip.engine.write_address(event.address);
    if (event.address < 0x2D || event.address > 0x2F)
        chip.engine.write_data(event.value);
}

struct StressRunResult
{
    bool sideEffectFree = true;
    uint64_t restoreMismatches = 0;
    size_t stateSize = 0;
    bool stateSizeStable = true;
};

StressRunResult RunStress(uint32_t seed, uint64_t steps, uint64_t everyCheckpoint)
{
    const std::vector<RegisterEvent> events = GenerateEvents(seed, steps);

    TtdStressChip chipA;  // never saved: the reference
    TtdStressChip chipB;  // saved at every checkpoint
    std::unique_ptr<TtdStressChip> chipC;  // restored fresh at each checkpoint
    uint64_t hashA = 0;
    uint64_t hashB = 0;
    uint64_t hashC = 0;
    size_t eventIndex = 0;
    std::vector<uint8_t> buffer;

    StressRunResult result;
    for (uint64_t step = 0; step < steps; step++)
    {
        while (eventIndex < events.size() && events[eventIndex].at == step)
        {
            ApplyEvent(chipA, events[eventIndex]);
            ApplyEvent(chipB, events[eventIndex]);
            if (chipC)
                ApplyEvent(*chipC, events[eventIndex]);
            eventIndex++;
        }

        StepChip(chipA, hashA);
        StepChip(chipB, hashB);
        if (chipC)
            StepChip(*chipC, hashC);

        if (step % everyCheckpoint == 0)
        {
            SaveChipState(chipB, buffer);
            if (result.stateSize == 0)
                result.stateSize = buffer.size();
            else if (buffer.size() != result.stateSize)
                result.stateSizeStable = false;

            // C has run from the previous checkpoint to here: its output and
            // status stream must match the never-saved reference
            if (chipC && hashC != hashA)
                result.restoreMismatches++;

            chipC = std::make_unique<TtdStressChip>();
            RestoreChipState(*chipC, buffer, chipB);
            hashC = hashA;

            // byte round-trip on a throwaway chip: restore -> save must
            // reproduce the checkpoint bytes exactly
            TtdStressChip throwaway;
            RestoreChipState(throwaway, buffer, chipB);
            std::vector<uint8_t> roundTrip;
            SaveChipState(throwaway, roundTrip);
            if (roundTrip != buffer)
                result.restoreMismatches++;
        }
    }

    result.sideEffectFree = (hashA == hashB);
    return result;
}

}  // namespace

TEST(YmfmTtdPatch, LongRunWithPeriodicCheckpointsIsExact)
{
    // Plan-sized long run: 1 seed x 400k steps (3.2M clocks), checkpoints
    // every 5000 steps
    StressRunResult result = RunStress(1, 400'000, 5'000);

    EXPECT_TRUE(result.sideEffectFree) << "saving must not alter chip behaviour";
    EXPECT_EQ(result.restoreMismatches, 0u) << "restored chip must continue bit-exactly";
    EXPECT_EQ(result.stateSize, 494u) << "patched state layout size";
    EXPECT_TRUE(result.stateSizeStable);
}

TEST(YmfmTtdPatch, CheckpointingEveryStepIsExact)
{
    // 'Every step' torture run: save + restore + byte round-trip at each of
    // 50k steps — the densest save/restore coverage the TTD path can see
    StressRunResult result = RunStress(2, 50'000, 1);

    EXPECT_TRUE(result.sideEffectFree) << "saving must not alter chip behaviour";
    EXPECT_EQ(result.restoreMismatches, 0u) << "restored chip must continue bit-exactly";
    EXPECT_EQ(result.stateSize, 494u) << "patched state layout size";
    EXPECT_TRUE(result.stateSizeStable);
}
