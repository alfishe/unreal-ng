#include "ym2203pair.h"

#include <algorithm>
#include <cassert>
#include <cstring>

/// region <Construction, reset, time>

Ym2203Pair::Ym2203Pair(EmulatorContext* context, const Ym2203PairConfig& config)
    : _config(config), _unity(config.masterClockHz == config.hostTickRate)
{
    assert(config.masterClockHz > 0 && config.hostTickRate > 0);
    _chips[0] = std::make_unique<Ym2203Chip>(context);
    _chips[1] = std::make_unique<Ym2203Chip>(context);
    // The output state's coupling is built for the TSFM's corner; another board's corner is configured here
    if (_config.fmCouplingHz != Ym2203PairConfig{}.fmCouplingHz)
        for (auto& c : _chips)
            c->out.coupling.configure(kYm2203FmInputRate, _config.fmCouplingHz);
}

void Ym2203Pair::reset()
{
    // Clock: adopt the host's position on the next sync; nobody has touched the chips since the reset, so there
    // is nothing to advance to
    _adoptCpuClock = true;
    _ratioPhase = 0;
    _chips[0]->words.clear();
    _chips[1]->words.clear();

    // Per-chip sequence (construction runs the same code, TSFM §5.4)
    _chips[0]->resetChip();
    _chips[1]->resetChip();

    // Render cursor (the rule, anchorRender): on a frame-relative axis the reset restarts the axis' time - the
    // cursor goes to the frame origin; on a continuous axis time does not jump at a reset and the cursor keeps
    // its place behind the chips
    if (frameRelativeChipAxis())
        anchorRender(0, 0, true);

    // Output stage: the FM hold / boxcar state and the decimators (state only; the rate-designed coefficients
    // and the slave wiring are preserved)
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
    if (_channels)
    {
        for (int i = 0; i < 2; i++)
        {
            _channels->fm[i].reset();
            for (auto& d : _channels->ssg[i])
                d.reset();
        }
    }
}

void Ym2203Pair::syncTo(uint64_t t)
{
    if (_adoptCpuClock)
    {
        // After reset: adopt the host's position without advancing - nothing has been simulated yet. On the
        // 1 : 1 path the master-clock axis is the host axis; with a ratio it is continuous and stays where it is
        _syncedT = t;
        if (_unity)
            _chipT = int64_t(t);
        _adoptCpuClock = false;
        return;
    }
    if (t <= _syncedT)
        return;

    int32_t delta;
    if (_unity)
        delta = int32_t(t - _syncedT);
    else
    {
        // Integer ratio accumulator: host ticks x masterClockHz, one master clock per hostTickRate
        const uint64_t acc = _ratioPhase + (t - _syncedT) * uint64_t(_config.masterClockHz);
        delta = int32_t(acc / _config.hostTickRate);
        _ratioPhase = acc % _config.hostTickRate;
    }

    if (delta > 0)
    {
        for (auto& c : _chips)
            advanceChip(*c, delta, _chipT);
        _chipT += delta;
    }
    _syncedT = t;
}

void Ym2203Pair::rebaseFrame(uint64_t now)
{
    // Frame rollover (TSFM §5.2): the host already subtracted the frame length from its clock; shift the synced
    // position by the same delta. On the 1 : 1 path the master-clock axis is the host axis: every queued word,
    // pending SSG write and the cursor move with it. No CPU instruction runs between the host's adjust and this
    // call, so no time is lost or double-counted.
    if (_adoptCpuClock)
        return;
    assert(!_config.continuousHostAxis && "a continuous host axis is never rebased");
    const int32_t delta = int32_t(_syncedT - now);
    if (delta == 0)
        return;
    _syncedT = uint64_t(int64_t(_syncedT) - int64_t(delta));
    if (!_unity)
        return;
    _chipT -= delta;
    _chips[0]->words.rebase(delta);
    _chips[1]->words.rebase(delta);
    _chips[0]->ssgWrites.rebase(delta);
    _chips[1]->ssgWrites.rebase(delta);
    _renderT -= delta;
}

void Ym2203Pair::advanceChip(Ym2203Chip& c, int32_t delta, int64_t t0)
{
    // Walk FM sample boundaries and timer expiries in time order, so a CSM key-on from timer A lands on the right
    // FM sample (TSFM §5.2 ordering rule: at the same clock, expiry is processed before the sample).
    if (_coreSynthesisSkipped)
    {
        // Sound off and no TTD: the FM operators (phases, envelopes) feed only the sound output, so their
        // clocking is skipped. Everything the CPU can observe stays exact - timers expire on their clock (CSM
        // key-on included), busy counts down, and the sample-clock phase keeps its alignment for the moment
        // synthesis resumes
        while (delta > 0)
        {
            const int32_t period = 12 * int32_t(c.fm.fmClockPrescale());
            if (c.fmClockPhase >= period)
                c.fmClockPhase = period - 1;

            const int32_t n = std::min(delta, c.intf.clocksToNextExpiry());  // INT32_MAX when both stopped
            c.intf.advance(n);

            // FM sample clocks that would have completed in these n clocks; the engine still counts them (its low
            // clock-counter bits are CPU-observable through the timer B first load)
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
        // A prescaler write (address 0x2D-0x2F) takes effect on the next iteration; clamp the phase so a
        // shrinking period cannot produce toClock <= 0 (negative step) or skip a sample.
        if (c.fmClockPhase >= period)
            c.fmClockPhase = period - 1;
        const int32_t toClock = period - c.fmClockPhase;
        const int32_t toTimer = c.intf.clocksToNextExpiry();  // INT32_MAX when both stopped
        const int32_t n = std::min({delta, toClock, toTimer});

        c.intf.advance(n);  // counts down busy; fires expired timers exactly on their clock
        c.fmClockPhase += n;
        delta -= n;
        t0 += n;

        if (c.fmClockPhase == period)
        {
            c.fmClockPhase = 0;
            const int16_t word = c.fm.clockFmOnce();
            c.words.push(uint64_t(t0), word);  // the owner drops these when nothing renders
        }
    }
}

/// endregion </Construction, reset, time>

/// region <Bus interface>

void Ym2203Pair::writeData(int index, uint8_t value)
{
    Ym2203Chip& c = *_chips[index];
    if (c.address < 0x10)
    {
        // SSG register: the CPU (and the I/O port pins) see it now, the generators on the tick of this clock
        // (SsgWriteQueue). Busy is set by SSG data writes too - ymfm's write_data does it for both halves.
        const uint8_t reg = c.ssg.getCurrentRegisterIndex();
        c.ssg.latchRegister(reg, value, _syncedT);
        queueSsgWrite(c, reg, value);
        c.intf.ymfm_set_busy_end(c.fm.busyClocks());
    }
    else
    {
        // FM register; sets busy itself; allowed while a board mutes FM
        c.fm.write_data(value);
        // Key-on mirror for the state report: 0x28 = ch (bits 0-1, 3 = none) | slot mask (bits 4-7)
        if (c.address == 0x28 && (value & 3) < 3)
            c.fmKeyOn[value & 3] = uint8_t(value >> 4);
    }
}

void Ym2203Pair::queueSsgWrite(Ym2203Chip& c, uint8_t reg, uint8_t value)
{
    if (_synthesisSuppressed)
    {
        // Nothing renders, so no tick would ever take it: the generators simply follow the register file
        c.ssg.applyRegister(reg, value);
        return;
    }
    if (c.ssgWrites.full())
    {
        c.ssg.applyRegister(c.ssgWrites.front().reg, c.ssgWrites.front().value);
        c.ssgWrites.pop();
    }
    c.ssgWrites.push(SsgWrite{_chipT, reg, value});
}

/// endregion </Bus interface>

/// region <Stereo output stage>

void Ym2203Pair::configureDecimators(size_t rate, FilterDecimator::Quality quality)
{
    _chips[0]->ssg.decimatorLeft().configure((double)rate, quality);
    _chips[0]->ssg.decimatorRight().configure((double)rate, quality);
    _chips[1]->ssg.decimatorLeft().configure((double)rate, quality);
    _chips[1]->ssg.decimatorRight().configure((double)rate, quality);
    _chips[0]->out.decimator.configure((double)rate, quality, false, kYm2203FmInputRate);
    _chips[1]->out.decimator.configure((double)rate, quality, false, kYm2203FmInputRate);

    // FM decimators run in slave mode: chip-0 SSG left gates the output cadence of every stream (TSFM §6.3)
    _chips[0]->out.decimator.attachMaster(&_chips[0]->ssg.decimatorLeft());
    _chips[1]->out.decimator.attachMaster(&_chips[0]->ssg.decimatorLeft());
}

double Ym2203Pair::fmLqSample(int index)
{
    Ym2203OutputState& o = _chips[index]->out;
    if (o.lqCount == 0)
        return o.lastFed;  // no half-tick landed on this output sample
    const double sample = o.lqSum / o.lqCount;
    o.lqSum = 0.0;
    o.lqCount = 0;
    return sample;
}

void Ym2203Pair::applyAllSsgWrites()
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

void Ym2203Pair::flushOutputStage()
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
    if (_channels)
    {
        for (int i = 0; i < 2; i++)
        {
            _channels->fm[i].clearHistory();
            for (auto& d : _channels->ssg[i])
                d.clearHistory();
        }
    }
}

/// endregion </Stereo output stage>

/// region <Per-channel outputs>

void Ym2203Pair::configureChannelOutputs(size_t rate, FilterDecimator::Quality quality)
{
    // The generators tick at master / 16 (the SSG clock at prescaler /6 and the half-tick grid); FM words are
    // held on a half-tick grid of master / 8
    const double ssgRate = double(_config.masterClockHz) / 16.0;
    const double fmRate = double(_config.masterClockHz) / 8.0;
    _channelRate = std::max<size_t>(rate, 1);
    if (!_channels)
        _channels = std::make_unique<ChannelOutputs>();
    FilterDecimator* master = &_channels->ssg[0][0];
    for (int i = 0; i < 2; i++)
    {
        for (int ch = 0; ch < 3; ch++)
        {
            FilterDecimator& d = _channels->ssg[i][ch];
            d.configure(double(rate), quality, false, ssgRate);
            d.attachMaster(&d == master ? nullptr : master);
        }
        _channels->fm[i].configure(double(rate), quality, false, fmRate);
        _channels->fm[i].attachMaster(master);
        _channels->fmZeroRun[i] = 0;
    }
}

size_t Ym2203Pair::renderChannels(size_t frames, const Ym2203ChannelBlock& block, bool fmEnabled)
{
    if (!_channels)
        return 0;

    // The cursor was placed by the owner's beginChannelRender (the rule, anchorRender): the synced period's
    // blocks end kRenderLag behind the chips. Rendering past the chips would consume every queued word at the first
    // half-tick and hold the last one - a block-rate staircase instead of the chips' output (MS-7 owner report
    // 2026-10-05, when the check looked at the block's start)

    FilterDecimator& master = _channels->ssg[0][0];
    FilterDecimator* const fmDec = _channels->fm;
    size_t* const fmZeroRun = _channels->fmZeroRun;
    // A muted or silent FM part feeds zeros, a silent SSG channel level 0.0: a decimator's output is exactly +0.0
    // once its whole window is zero (FilterDecimator::window), so the FIR is skipped - the same output for a
    // fraction of the cost (frame-cost profile 2026-10-06: the two FM FIRs were 12 % of an idle card frame; the
    // SSG skip is measured in ym-decimator-prototype.md)
    auto feedFm = [&](int i)
    {
        const double v = fmEnabled ? _chips[i]->out.hold : 0.0;
        fmDec[i].feedSample(v);
        fmZeroRun[i] = v == 0.0 ? fmZeroRun[i] + 1 : 0;
    };
    for (size_t n = 0; n < frames; n++)
    {
        while (!master.hasOutput())
        {
            for (int i = 0; i < 2; i++)
            {
                consumeWords(*_chips[i], _renderT);
                feedFm(i);
            }
            applySsgWrites(_renderT);
            updateState(true);
            for (int i = 0; i < 2; i++)
            {
                const double* levels = _chips[i]->ssg.left();  // per channel, before panning and DC removal
                for (int ch = 0; ch < 3; ch++)
                {
                    _channels->ssg[i][ch].feedSample(levels[ch]);
                    size_t& run = _channels->ssgZeroRun[i][ch];
                    run = levels[ch] == 0.0 ? run + 1 : 0;
                }
            }
            for (int i = 0; i < 2; i++)
            {
                consumeWords(*_chips[i], _renderT + 8);
                feedFm(i);
            }
            _renderT += 16;
        }

        // All eight at one output instant; the order does not matter (a slave takes the instant from the master's
        // phase, before or after the master consumed it alike). A silent channel - SSG or FM - outputs +0.0 once its
        // whole window is zero (FilterDecimator::window); the others share one pass per design
        // (FilterDecimator::getOutputs: the SSG decimators their coefficient rows, the FM ones theirs)
        FilterDecimator* decimators[8];
        float* targets[8];
        size_t count = 0;
        auto evaluate = [&](FilterDecimator& d, float* row)
        {
            decimators[count] = &d;
            targets[count++] = row ? &row[n] : nullptr;
        };
        for (int i = 0; i < 2; i++)
        {
            for (int ch = 0; ch < 3; ch++)
            {
                FilterDecimator& d = _channels->ssg[i][ch];
                float* row = block.ssg[i][ch];
                if (_channels->ssgZeroRun[i][ch] < d.window())
                {
                    evaluate(d, row);
                    continue;
                }
                d.skipOutput();   // the master's phase moves as getOutput moves it
                if (row)
                    row[n] = 0.0f;
            }
        }
        for (int i = 0; i < 2; i++)
        {
            if (fmZeroRun[i] < fmDec[i].window())
            {
                evaluate(fmDec[i], block.fm[i]);
                continue;
            }
            if (block.fm[i])
                block.fm[i][n] = 0.0f;
        }
        double values[8];
        FilterDecimator::getOutputs(decimators, count, values);
        for (size_t k = 0; k < count; k++)
        {
            if (targets[k])
                *targets[k] = static_cast<float>(values[k]);
        }
    }
    return frames;
}

/// endregion </Per-channel outputs>

/// region <TTD>

namespace
{
inline void PutU8(uint8_t*& cur, uint8_t v)   { *cur++ = v; }
inline void PutU16(uint8_t*& cur, uint16_t v) { std::memcpy(cur, &v, 2); cur += 2; }
inline void PutI32(uint8_t*& cur, int32_t v)  { std::memcpy(cur, &v, 4); cur += 4; }
inline void PutU64(uint8_t*& cur, uint64_t v) { std::memcpy(cur, &v, 8); cur += 8; }
inline void PutF64(uint8_t*& cur, double v)   { std::memcpy(cur, &v, 8); cur += 8; }

inline uint8_t  GetU8(const uint8_t*& cur)  { return *cur++; }
inline uint16_t GetU16(const uint8_t*& cur) { uint16_t v; std::memcpy(&v, cur, 2); cur += 2; return v; }
inline int32_t  GetI32(const uint8_t*& cur) { int32_t v; std::memcpy(&v, cur, 4); cur += 4; return v; }
inline uint64_t GetU64(const uint8_t*& cur) { uint64_t v; std::memcpy(&v, cur, 8); cur += 8; return v; }
inline double   GetF64(const uint8_t*& cur) { double v; std::memcpy(&v, cur, 8); cur += 8; return v; }
}  // namespace

static_assert(Ym2203Pair::kChipStateSize == 586, "YM2203 per-chip state size drift (TSFM blob §8.2)");

void Ym2203Pair::saveChipState(int index, uint8_t*& cur) const
{
    Ym2203Chip& c = *_chips[index];  // non-const through the unique_ptr: ymfm's save path is non-const

    PutU8(cur, c.address);
    PutI32(cur, c.fmClockPhase);
    PutI32(cur, c.intf._timer[0]);
    PutI32(cur, c.intf._timer[1]);
    PutI32(cur, c.intf._busy);

    // ymfm serializes via push_back into a caller-owned vector; the scratch buffer is pre-reserved
    // (Ym2203Chip::ttdScratch) so this does not allocate on the steady-state save path.
    c.ttdScratch.clear();
    ymfm::ymfm_saved_state state(c.ttdScratch, /*saving=*/true);
    c.fm.save_restore(state);
    assert(c.ttdScratch.size() == kYmfmStateSize &&
           "ymfm engine payload size drifted from the TSFM §8.2-measured 494 bytes");

    PutU16(cur, static_cast<uint16_t>(c.ttdScratch.size()));
    std::memcpy(cur, c.ttdScratch.data(), c.ttdScratch.size());
    cur += c.ttdScratch.size();

    c.ssg.TTDSaveState(cur);
    cur += c.ssg.TTDStateSize();
}

void Ym2203Pair::loadChipState(int index, const uint8_t*& cur)
{
    Ym2203Chip& c = *_chips[index];

    c.address = GetU8(cur);
    c.fmClockPhase = GetI32(cur);
    c.intf._timer[0] = GetI32(cur);
    c.intf._timer[1] = GetI32(cur);
    c.intf._busy = GetI32(cur);

    const uint16_t ymfmSize = GetU16(cur);
    assert(ymfmSize == kYmfmStateSize && "TTD blob's ymfm payload size does not match this build's engine layout");
    c.ttdScratch.assign(cur, cur + ymfmSize);
    cur += ymfmSize;
    ymfm::ymfm_saved_state state(c.ttdScratch, /*saving=*/false);
    c.fm.save_restore(state);

    c.ssg.TTDLoadState(cur);
    cur += c.ssg.TTDStateSize();
}

void Ym2203Pair::saveTimeline(uint8_t*& cur) const
{
    // Relative to the synced master clock: the cursor decides on which tick every SSG write lands, a pending
    // write is chip input that has not reached the generators yet
    const int64_t base = _chipT;
    PutU64(cur, uint64_t(_renderT - base));
    for (int i = 0; i < 2; ++i)
    {
        const SsgWriteQueue& q = _chips[i]->ssgWrites;
        PutU8(cur, uint8_t(q.size()));
        for (size_t k = 0; k < SsgWriteQueue::kCapacity; ++k)
        {
            const SsgWrite w = (k < q.size()) ? q.at(k) : SsgWrite{};
            PutI32(cur, (k < q.size()) ? int32_t(w.t - base) : 0);
            PutU8(cur, w.reg);
            PutU8(cur, w.value);
        }
    }
}

void Ym2203Pair::loadTimeline(const uint8_t*& cur, uint64_t base)
{
    // The pair continues from the restored host position without adopting: the next syncTo clocks the chips over
    // exactly the ticks the original run did. The master-clock axis restarts at the same value (1 : 1: it is the
    // host axis; with a ratio its origin is free - everything on it is stored relative)
    _syncedT = base;
    _chipT = int64_t(base);
    _adoptCpuClock = false;

    const int64_t cursorOffset = int64_t(GetU64(cur));
    for (int i = 0; i < 2; ++i)
    {
        SsgWriteQueue& q = _chips[i]->ssgWrites;
        q.clear();
        const size_t count = GetU8(cur);
        for (size_t k = 0; k < SsgWriteQueue::kCapacity; ++k)
        {
            const int32_t offset = GetI32(cur);
            const uint8_t reg = GetU8(cur);
            const uint8_t value = GetU8(cur);
            if (k < count)
                q.push(SsgWrite{_chipT + offset, reg, value});
        }
    }
    _renderT = _chipT + cursorOffset;
}

void Ym2203Pair::TTDSaveState(uint8_t* dst) const
{
    uint8_t* cur = dst;
    PutU8(cur, kStateVersion);
    PutU64(cur, _ratioPhase);
    PutF64(cur, _channels ? _channels->ssg[0][0].phase() : 0.0);
    saveChipState(0, cur);
    saveChipState(1, cur);
    saveTimeline(cur);
    assert(static_cast<size_t>(cur - dst) == kStateSize);
}

void Ym2203Pair::TTDLoadState(const uint8_t* src, uint64_t base)
{
    const uint8_t* cur = src;
    const uint8_t version = GetU8(cur);
    assert(version == kStateVersion && "Ym2203Pair TTD blob version mismatch");
    (void)version;
    _ratioPhase = GetU64(cur);
    const double channelPhase = GetF64(cur);
    loadChipState(0, cur);
    loadChipState(1, cur);
    loadTimeline(cur, base);

    // Output content is render cache: flushed to silence, the tick-gating phase restored
    clearWords();
    flushOutputStage();
    if (_channels)
        _channels->ssg[0][0].setPhase(channelPhase);
}

/// endregion </TTD>
