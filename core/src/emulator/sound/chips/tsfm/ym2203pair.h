#pragma once

#include <stdafx.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "common/sound/filters/filter_decimator.h"
#include "common/sound/filters/filterdcblocker.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/ayioport.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/ssgwritequeue.h"
#include "emulator/sound/chips/tsfm/fm_word_queue.h"
#include "emulator/sound/chips/tsfm/ym2203_engine.h"
#include "emulator/sound/native_audio_tap.h"

class EmulatorContext;

/// @file ym2203pair.h
/// @brief Two YM2203 on one board - the chip engine shared by the TurboSound FM board
/// (SoundChip_TurboSoundFM) and the ZX-MultiSound card (ZX-MultiSound MS-1,
/// docs/inprogress/2026-10-03-zx-multisound/architecture.md §2, §4.1).
///
/// What a board adds on top (chip select, control-word parse, FM mute, its mixer) stays with the board; the pair
/// is the two chips and their time:
///  - per chip: the SSG half (SoundChip_AY8910 at YM2149, I/O ports included), the ymfm FM engine with timers and
///    busy in master clocks, the address latch, the FM word queue and the timed SSG write queue;
///  - the core loop (syncTo / advanceChip): the chips are advanced in master clocks at every port access, so the
///    CPU-visible state (status, timers, busy) is exact at the instruction boundary;
///  - the render primitives (FM half-ticks with sample-and-hold, SSG ticks with timed writes) and two outputs: the
///    stereo output stage the TSFM mixes (per chip SSG left / right and FM through the board's coupling) and the
///    per-chip, per-channel outputs a board mixer takes (FM per chip, SSG per chip and channel);
///  - the TTD pieces: the per-chip payload and the timeline tail (the TSFM's blob layout, byte for byte), and a
///    blob of its own for boards that carry the pair whole (adds the ratio phase).
///
/// Time. The owner's axis is its host tick (the emulator's audio T-states). The chips run on their own master
/// clock: an integer ratio accumulator turns host ticks into master clocks (masterClockHz : hostTickRate), its
/// remainder (the phase) is TTD state. With equal rates the ratio is 1 : 1 and the master-clock axis IS the
/// host axis, frame-relative and rebased with it - exactly the TSFM's original arithmetic. With a true ratio
/// (the MultiSound's own 3.5 MHz oscillator on a 3.5469 MHz 128K host) the master-clock axis is continuous and
/// never rebased; the host axis still is.

/// Pair parameters (board configuration, not state)
struct Ym2203PairConfig
{
    /// YM2203 master clock (the TSFM: the AY socket's 1.75 MHz doubled = the 3.5 MHz audio axis)
    uint32_t masterClockHz = static_cast<uint32_t>(CPU_CLOCK_RATE);
    /// Rate of the owner's time axis (ticks per second of the times passed to syncTo)
    uint32_t hostTickRate = static_cast<uint32_t>(CPU_CLOCK_RATE);
    /// Stereo output stage: FM output coupling high-pass corner (the TSFM's C14 / C15 into the DA5 mixer)
    double fmCouplingHz = 1.0 / (2.0 * 3.14159265358979323846 * 12000.0 * 10e-6);
};

/// FM input rate of the output stage (TSFM §6.3): the YM2203 sample clock at prescaler /6 is master / 8 =
/// 437.5 kHz at 3.5 MHz = exactly 2x the SSG generator rate (the half-tick grid)
constexpr double kYm2203FmInputRate = static_cast<double>(PSG_CLOCK_RATE) / 4.0;

/// Per-chip stereo output-stage state (TSFM §6): the sample-and-hold value of the newest consumed FM word, the
/// coupling high-pass after the mute gate, its mono decimator (slave of chip-0 SSG left, §6.3), the LQ boxcar
/// accumulator and the raw pre-mute DAC tap (§6.4). Not TTD state.
struct Ym2203OutputState
{
    double hold = 0.0;                    // newest FM word / 32768, held until the next word
    FilterDCBlocker coupling{kYm2203FmInputRate, Ym2203PairConfig{}.fmCouplingHz};  // board output coupling
    double lastFed = 0.0;                 // newest coupled half-tick value (LQ sample with no half-tick)
    bool couplingSettlePending = false;   // after a flush: settle the coupling on the first live word
    FilterDecimator decimator;            // 437.5 kHz -> core rate, HQ path
    double lqSum = 0.0;                   // LQ boxcar: sum of gated half-tick values
    uint32_t lqCount = 0;                 // LQ boxcar: half-ticks summed for this output sample
    std::shared_ptr<NativeAudioTap> nativeTap = std::make_shared<NativeAudioTap>();
};

/// One YM2203: the SSG half is the same SoundChip_AY8910 the AY devices use, the FM half is a vendored ymfm
/// engine. Not copyable or movable - ymfm holds references to the interface and the override adapter.
class Ym2203Chip
{
public:
    explicit Ym2203Chip(EmulatorContext* context)
        : ssg(context), fm(intf), ssgAdapter(ssg)
    {
        // TSFM §5.4: construction runs the machine-reset sequence. ymfm's constructor does NOT call reset() -
        // its register array is uninitialised until reset() runs.
        fm.ssg_override(ssgAdapter);
        resetChip();

        // TTD save-path scratch (§8.2): reserved once so a save never allocates on its steady-state path
        // (measured ymfm payload: 494 B).
        ttdScratch.reserve(1024);
    }

    Ym2203Chip(const Ym2203Chip&) = delete;
    Ym2203Chip& operator=(const Ym2203Chip&) = delete;

    /// Per-chip half of the reset sequence (TSFM §5.4)
    void resetChip()
    {
        ssg.reset();
        // ymfm resets FM registers, operators and status; the adapter's ssg_reset() is a no-op, so the AY is not
        // reset twice
        fm.reset();
        // ymfm's reset leaves the prescaler as it was; a real YM2203 reset returns to /6
        fm.write_address(0x2D);
        address = 0;
        fmClockPhase = 0;
        fmKeyOn[0] = fmKeyOn[1] = fmKeyOn[2] = 0;
        intf.reset();
        ssg.setChipModel(AYChipModel::YM2149);
        ssgWrites.clear();
    }

    SoundChip_AY8910 ssg;      // SSG half, model YM2149
    Ym2203Interface intf;      // timers + busy, in master clocks
    Ym2203Engine fm;           // ymfm::ym2203 subclass, patched
    SsgOverrideAdapter ssgAdapter;

    uint8_t address = 0;       // YM2203 address latch (8-bit)
    int32_t fmClockPhase = 0;  // master clocks since the last FM sample, 0 .. 12*p-1

    // Key-on mask per FM channel as last written to register 0x28 (bits 4-7 = slots S1,S2,S3,S4). Mirror for the
    // state report (DeviceState::FmChip); ymfm keeps the live key state privately.
    uint8_t fmKeyOn[3] = {0, 0, 0};

    // Output-side hand-off (not TTD state)
    FmWordQueue words;

    // SSG register writes timed to their master clock, applied by the render loop on the tick they fall in
    // (TTD state: pending writes are chip input)
    SsgWriteQueue ssgWrites;

    // Stereo output stage: hold register, decimator, LQ boxcar, raw DAC tap
    Ym2203OutputState out;

    // TTD save-path scratch: ymfm_saved_state serializes into a vector via push_back
    std::vector<uint8_t> ttdScratch;
};

/// Per-chip, per-channel output block for a board mixer (renderChannels): mono float streams at the output rate.
/// FM: the DAC word / 32768 after the board's mute gate, no coupling. SSG: the YM2149 table level 0..1 of each
/// channel (A, B, C) before any panning or DC removal. A null pointer skips that stream.
struct Ym2203ChannelBlock
{
    float* fm[2] = {};
    float* ssg[2][3] = {};
};

class Ym2203Pair
{
public:
    /// Constant lag of the render cursor behind the newest timed event (see kTurboSoundRenderLagT)
    static constexpr int64_t kRenderLag = kTurboSoundRenderLagT;

    /// TTD sizes. The per-chip payload and the timeline tail are the TSFM blob's (§8.2), byte for byte:
    /// per chip: address(1) + fmClockPhase(4) + timer[2](8) + busy(4) + ymfmSize(2) + ymfm(494) + SSG(73)
    static constexpr size_t kYmfmStateSize = 494;
    static constexpr size_t kChipStateSize = 1 + 4 + 4 + 4 + 4 + 2 + kYmfmStateSize + 73;
    /// render cursor offset(8) + per chip: count(1) + kCapacity x {t offset i32, reg u8, value u8}
    static constexpr size_t kTimelineStateSize = 8 + 2 * (1 + SsgWriteQueue::kCapacity * (4 + 1 + 1));
    /// The pair's own blob (boards that carry the pair whole): version(1) + ratio phase(8) + channel-render
    /// master decimator phase(8) + 2 chips + timeline
    static constexpr uint8_t kStateVersion = 1;
    static constexpr size_t kStateSize = 1 + 8 + 8 + 2 * kChipStateSize + kTimelineStateSize;

    Ym2203Pair(EmulatorContext* context, const Ym2203PairConfig& config = Ym2203PairConfig{});

    Ym2203Pair(const Ym2203Pair&) = delete;
    Ym2203Pair& operator=(const Ym2203Pair&) = delete;

    /// region <Configuration>
    const Ym2203PairConfig& config() const
    {
        return _config;
    }

    /// True when master clock and host tick run at the same rate (the TSFM): the 1 : 1 path
    bool unityRatio() const
    {
        return _unity;
    }

    Ym2203Chip* chip(int index) const
    {
        return (index == 0 || index == 1) ? _chips[index].get() : nullptr;
    }
    /// endregion </Configuration>

    /// region <Reset and time>
    /// Both chips' reset sequence (/RES on host reset), the timeline and the output stage (state only; the
    /// rate-designed decimator coefficients and the slave wiring are preserved). The next syncTo adopts its time.
    void reset();

    /// Advance both chips to host tick t (frame-relative). The first call after reset / construction adopts t
    void syncTo(uint64_t t);

    /// Host tick the pair has been advanced to
    uint64_t syncedT() const
    {
        return _syncedT;
    }

    /// Master clock the chips have been advanced to (equal to syncedT() on the 1 : 1 path)
    int64_t chipT() const
    {
        return _chipT;
    }

    /// Remainder of the ratio accumulator (host ticks x masterClockHz not yet worth one master clock); 0 at 1 : 1
    uint64_t ratioPhase() const
    {
        return _ratioPhase;
    }

    /// Frame rollover: the owner's host axis was rebased so that `now` is the new frame's position. Shifts the
    /// synced position; on the 1 : 1 path also every queued word, pending SSG write and the render cursor
    void rebaseFrame(uint64_t now);
    /// endregion </Reset and time>

    /// region <Bus interface (the chip side of a board's port decode)>
    /// Address write to chip `index`: the address latch, ymfm's address (prescaler side effect 0x2D-0x2F) and the
    /// SSG register select (< 0x10 selects, >= 0x10 keeps the previous register)
    void writeAddress(int index, uint8_t value)
    {
        Ym2203Chip& c = *_chips[index];
        c.address = value;
        c.fm.write_address(value);
        c.ssg.setRegister(value);
    }

    /// Data write to chip `index` at the synced position: SSG register (the CPU sees it now, the generators on the
    /// tick of this master clock; the I/O port pins change now) or FM register; both set busy
    void writeData(int index, uint8_t value);

    /// Data read of chip `index`: the selected SSG register as the bus sees it (an input port reads its pins), or
    /// #FF while an FM address is latched (TSFM hardware-reference H2)
    uint8_t readData(int index)
    {
        Ym2203Chip& c = *_chips[index];
        if (c.address < 0x10)
            return c.ssg.readCurrentRegister();
        return 0xFF;
    }

    /// Status read of chip `index`: busy | timer B | timer A
    uint8_t readStatus(int index)
    {
        return _chips[index]->fm.read_status();
    }

    /// I/O port pins of chip `index`'s SSG (register 14 / 15 with the register 7 direction bits). The listener
    /// hears pin changes with the host tick of the write (syncedT). Null detaches
    void setIoPortListener(int index, IAyIoPortListener* listener)
    {
        _chips[index]->ssg.setIoPortListener(listener);
    }
    /// endregion </Bus interface>

    /// region <Render switches>
    void setHQEnabled(bool enabled)
    {
        _hqEnabled = enabled;
    }
    bool hqEnabled() const
    {
        return _hqEnabled;
    }

    /// Output stage off (turbo, sound feature off): SSG writes go straight to the generators
    void setSynthesisSuppressed(bool suppressed)
    {
        _synthesisSuppressed = suppressed;
    }
    bool synthesisSuppressed() const
    {
        return _synthesisSuppressed;
    }

    /// FM operator clocking frozen (sound off, no TTD); everything CPU-observable stays exact
    void setCoreSynthesisSkipped(bool skipped)
    {
        _coreSynthesisSkipped = skipped;
    }
    bool coreSynthesisSkipped() const
    {
        return _coreSynthesisSkipped;
    }
    /// endregion </Render switches>

    /// region <Stereo output stage (the TSFM's render loop)>
    /// Designs the six decimators of the stereo output stage for the output rate (TSFM §6.3): four SSG ones at the
    /// generator rate, two FM ones at kYm2203FmInputRate in slave mode under chip-0 SSG left
    void configureDecimators(size_t rate, FilterDecimator::Quality quality);

    /// Render cursor (master-clock axis): the FM half-tick boundary of the next SSG tick
    int64_t renderT() const
    {
        return _renderT;
    }
    void setRenderT(int64_t t)
    {
        _renderT = t;
    }

    /// Consume every FM word of chip `index` that landed by half-tick boundary h into the hold register (the raw
    /// DAC tap sees each word). Returns whether a word was consumed
    bool consumeWords(Ym2203Chip& c, int64_t h)
    {
        Ym2203OutputState& o = c.out;
        bool consumed = false;
        while (!c.words.empty() && int64_t(c.words.front().t) <= h)
        {
            o.hold = static_cast<double>(c.words.front().word) / 32768.0;
            if (o.nativeTap->isActive())
                o.nativeTap->push(static_cast<float>(o.hold), static_cast<float>(o.hold));
            c.words.pop();
            consumed = true;
        }
        return consumed;
    }

    /// One FM half-tick of chip `index` ending at half-tick boundary h (TSFM §6.2): consume the words that landed
    /// by h into the hold register, then feed the gated hold - a board mute grounds the DAC data line - through
    /// the output coupling to the HQ decimator or the LQ boxcar accumulator
    void fmHalfTick(int index, int64_t h, bool fmEnabled)
    {
        Ym2203Chip& c = *_chips[index];
        Ym2203OutputState& o = c.out;

        // Words are 72 clocks apart at /6, half-ticks 8, so each word is held for exactly 9 half-ticks - across
        // frame boundaries too, since the cursor and the words share one timeline. Signed compare: words left
        // over from the previous frame sit at negative T after a rebase
        const bool consumed = consumeWords(c, h);

        // Mute-at-hold-input: the filter (and its state) sees silence while FM is disabled - no click on unmute,
        // the decimator stays warmed up. The output coupling capacitor sits after the DAC buffer, so it sees the
        // gated value too
        const double gated = fmEnabled ? o.hold : 0.0;

        // First live word after a flush (TTD seek, resume after a gap): the chip may be mid-sound; pick the
        // coupling up at the level it is fed instead of passing a step from the flushed 0 to it
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

    /// LQ boxcar output of one chip's FM hold stream: average of the summed half-ticks (or the newest fed value
    /// when no half-tick landed on this output sample), resetting the accumulator
    double fmLqSample(int index);

    /// Apply every pending SSG write timed at or before t (render cursor)
    void applySsgWrites(int64_t t)
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

    /// Apply every pending SSG write now (nothing will tick them in)
    void applyAllSsgWrites();

    /// Tick both SSG generators once
    void updateState(bool bypassPrescaler = false)
    {
        _chips[0]->ssg.updateState(bypassPrescaler);
        _chips[1]->ssg.updateState(bypassPrescaler);
    }

    /// One SSG generator tick of the render loop with its two FM half-ticks interleaved (TSFM §6.2): half-tick
    /// 1/2 at the cursor, the SSG writes timed up to it applied, both generators ticked, `onSsgTick(chip0, chip1)`
    /// (the owner takes the new SSG levels), half-tick 2/2, the cursor advances one SSG tick (16 master clocks)
    template <typename OnSsgTick>
    void renderTick(bool fmEnabled, OnSsgTick&& onSsgTick)
    {
        fmHalfTick(0, _renderT, fmEnabled);
        fmHalfTick(1, _renderT, fmEnabled);
        applySsgWrites(_renderT);
        updateState(true);
        onSsgTick(*_chips[0], *_chips[1]);
        fmHalfTick(0, _renderT + 8, fmEnabled);
        fmHalfTick(1, _renderT + 8, fmEnabled);
        _renderT += 16;
    }

    /// Silence the output-stage audio content - hold, coupling, LQ boxcar, decimator histories - keeping every
    /// tick-gating phase (determinism)
    void flushOutputStage();

    /// Drop the FM words nobody will render (output stage off)
    void clearWords()
    {
        _chips[0]->words.clear();
        _chips[1]->words.clear();
    }
    /// endregion </Stereo output stage>

    /// region <Per-channel outputs (board mixers)>
    /// Designs the eight per-channel decimators for the output rate (FM per chip at 2x the SSG generator rate,
    /// SSG per chip and channel; all slaves of chip-0 channel A). Called once by a board that mixes per channel;
    /// the stereo output stage is not used then
    void configureChannelOutputs(size_t rate, FilterDecimator::Quality quality = FilterDecimator::Quality::Reference);

    /// Render `frames` output samples of the per-channel streams into `block`. The cursor follows the master-clock
    /// axis (kRenderLag behind the synced position; re-anchored when the owner lost the timeline). Returns the
    /// samples written (0 before configureChannelOutputs)
    size_t renderChannels(size_t frames, const Ym2203ChannelBlock& block, bool fmEnabled);
    /// endregion </Per-channel outputs>

    /// region <TTD>
    /// The TSFM blob's per-chip payload (kChipStateSize bytes) of chip `index`
    void saveChipState(int index, uint8_t*& cur) const;
    void loadChipState(int index, const uint8_t*& cur);

    /// The timeline tail (kTimelineStateSize bytes): render cursor and pending SSG writes, relative to the synced
    /// master clock
    void saveTimeline(uint8_t*& cur) const;
    /// Rebuilds the timeline around host tick `base` (the restored CPU position): the pair is synced there (no
    /// adopt), the cursor and the pending writes land at their offsets
    void loadTimeline(const uint8_t*& cur, uint64_t base);

    /// The pair's own blob (kStateSize): chips, timeline, ratio phase and the per-channel render phase. Output
    /// content (word queues, filter histories) is flushed on load, as the TSFM does
    size_t TTDStateSize() const
    {
        return kStateSize;
    }
    void TTDSaveState(uint8_t* dst) const;
    void TTDLoadState(const uint8_t* src, uint64_t base);
    /// endregion </TTD>

private:
    void advanceChip(Ym2203Chip& c, int32_t delta, int64_t t0);
    void queueSsgWrite(Ym2203Chip& c, uint8_t reg, uint8_t value);

    Ym2203PairConfig _config;
    bool _unity = true;
    std::unique_ptr<Ym2203Chip> _chips[2];

    uint64_t _syncedT = 0;       // host tick the chips were advanced to (frame-relative); not TTD state
    int64_t _chipT = 0;          // master clock the chips were advanced to (== _syncedT at 1 : 1)
    uint64_t _ratioPhase = 0;    // ratio accumulator remainder, in host ticks x masterClockHz (TTD state)
    bool _adoptCpuClock = true;  // set by reset: the next sync adopts the host's position without advancing

    bool _hqEnabled = true;
    bool _synthesisSuppressed = false;
    bool _coreSynthesisSkipped = false;

    // Render cursor on the master-clock axis, kRenderLag behind the word and SSG-write timeline
    int64_t _renderT = -kRenderLag;

    // Per-channel outputs (configureChannelOutputs, allocated on first use): master = SSG chip 0 channel A
    struct ChannelOutputs
    {
        FilterDecimator fm[2];
        FilterDecimator ssg[2][3];
    };
    std::unique_ptr<ChannelOutputs> _channels;
};
