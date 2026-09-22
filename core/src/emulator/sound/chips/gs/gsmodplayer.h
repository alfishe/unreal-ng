#pragma once

/// @file gsmodplayer.h
/// @brief In-tree ProTracker (MOD) player core for the lightweight GS card
/// (design: docs/inprogress/2026-09-19-general-sound/gs-card-personalities-tdd.md §5).
///
/// UI-free, chip-free, directly unit-testable: parses a standard Amiga
/// ProTracker module (M.K./M!K! 31-sample header, 64-row patterns, 4
/// channels) and advances playback in the card's 37.5 kHz quantum domain.
///
/// Timing model mirrors the firmware sequencer (verification doc §2.4):
/// - The 320-cycle quantum (37.5 kHz) is the sample clock: one sample-and-hold
///   DAC latch update per quantum, exactly like the firmware's one
///   LD A,(DE) fetch per interrupt.
/// - The tempo tick is TICKLEN = 37500/(0.4*BPM) quanta (750 at the default
///   125 BPM = 20 ms = 50 ticks/s); each Fxx speed ticks advance one row.
/// - Position lives in the row/tick/quantum domain, never the audio sample
///   domain, so a runtime setSampleRate (blip rebuild) cannot disturb it.
///
/// Effect coverage is the standard ProTracker music set; E0x (filter) and
/// EFx (invert loop) parse-and-ignore, matching the firmware (guide §6).

#include <cstddef>
#include <cstdint>
#include <vector>

class GSModPlayer
{
public:
    static constexpr int kChannels = 4;
    static constexpr int kRowsPerPattern = 64;
    static constexpr uint32_t kQuantumRate = 37500;     // card quanta per second
    static constexpr uint32_t kDefaultTickQuanta = 750; // 125 BPM -> 50 Hz
    static constexpr uint8_t kDefaultSpeed = 6;         // ticks per row
    static constexpr uint8_t kDefaultBpm = 125;
    static constexpr uint16_t kMinPeriod = 113;         // PT note range guards
    static constexpr uint16_t kMaxPeriod = 856;

    /// One 31-entry sample header (ProTracker layout, offsets in the module)
    struct SampleInfo
    {
        char name[23] = {};     // 22 chars + NUL
        uint16_t lengthWords = 0;   // sample length / 2 as stored in the module
        uint8_t finetune = 0;       // low nibble 0-15 (interpreted as -8..+7)
        uint8_t volume = 0;         // default 0-63
        uint16_t loopStartWords = 0;
        uint16_t loopLengthWords = 0;
        uint32_t dataOffset = 0;    // byte offset of the sample data
        uint32_t dataLength = 0;    // bytes
        uint32_t loopStart = 0;     // bytes
        uint32_t loopLength = 0;    // bytes (0 or < 4 = one-shot)
    };

    /// Per-voice runtime state (also the TTD-serialized runtime)
    struct ChannelState
    {
        // 16.16 fixed-point byte offset/step, in a 64-bit container: a
        // 32-bit container's 16-bit integer part tops out at 65535 bytes,
        // but ProTracker samples run up to 131070 bytes (16-bit word length
        // field) - a 32-bit position/loopEnd silently wraps well before the
        // real end of any sample over 64 KB (live-verified distortion on
        // real GS demo content with large samples, 2026-09-21)
        uint64_t position = 0;        // 16.16 fixed-point byte offset
        int64_t increment = 0;        // 16.16 fixed-point bytes per quantum
        uint8_t volume = 0;           // current 0-63 (row volume + effects)
        uint8_t sample = 0;           // 1-31, 0 = none
        uint16_t period = 0;          // current Amiga period
        uint16_t portamentoTarget = 0; // 3xx target
        uint8_t portamentoParam = 0;
        uint8_t vibratoParam = 0;
        uint8_t tremoloParam = 0;
        uint8_t volumeSlideParam = 0; // shared 5xx/6xx/Axx/EBx memory
        uint16_t vibratoPos = 0;
        uint16_t tremoloPos = 0;
        // Per-tick tremolo volume delta (sine*depth/128, rendered in
        // processTick): held for the whole tick so fetchChannelOutput only
        // adds it. Positive or negative, clamped onto ch.volume there
        int8_t tremoloDelta = 0;
        uint16_t arpeggioPeriods[3] = {0, 0, 0};
        uint8_t offsetParam = 0;
        bool muted = false;           // ECx
    };

    GSModPlayer() = default;

    /// Parse a full ProTracker module image. Returns false + a reason on
    /// validation failure (bad tag, truncated data, pattern table overflow).
    bool parse(const uint8_t* data, size_t size, const char** reason = nullptr);
    bool isParsed() const { return _parsed; }

    /// Parsed-module introspection (queries 63/64, tests)
    size_t sampleCount() const { return _sampleCount; }
    size_t patternCount() const { return _patternCount; }
    size_t songLength() const { return _songLength; }
    const SampleInfo& sampleInfo(size_t index) const { return _samples[index]; }
    const std::vector<uint8_t>& moduleBytes() const { return _module; }

    // Playback control (firmware COM31/COM32/COM33/COM65 semantics)
    void start(uint8_t songPosition);
    void stop();         // COM32: freezes position (COM33 resumes)
    void continuePlay(); // COM33
    bool isPlaying() const { return _playing; }
    bool isStopped() const { return !_playing; }

    /// Advance exactly one 320-cycle quantum. Fills out[4] with 0x80-centered
    /// DAC bytes (the signed ProTracker sample XOR 0x80, as the firmware
    /// uploads it) and vols[4] with 0-64 per-channel volumes (ProTracker
    /// range, 0x40 = full; row volume + tremolo, before the card's
    /// MODVOL/MTVOL scaling maps it onto the 6-bit latch).
    void advanceQuantum(uint8_t out[kChannels], uint8_t vols[kChannels]);

    // Position queries (firmware COM60/61/62 variables)
    uint8_t songPosition() const { return _songPosition; }
    uint8_t patternPosition() const { return _row; }
    uint8_t currentPattern() const;
    uint8_t speed() const { return _speed; }
    uint8_t bpm() const { return _bpm; }
    uint32_t quantaIntoTick() const { return _quantaIntoTick; }
    uint8_t tick() const { return _tick; }

    /// Per-channel current sample (query 63 domain)
    uint8_t channelSample(int channel) const { return _channels[channel].sample; }

    /// Per-channel current row volume (query 64 domain, 0-63)
    uint8_t channelVolume(int channel) const { return _channels[channel].volume; }

    /// Per-channel current Amiga period and playback step (test/diagnostic
    /// introspection: pins down effect correctness - e.g. that vibrato's
    /// increment offset doesn't outlive the row that carries it)
    uint16_t channelPeriod(int channel) const { return _channels[channel].period; }
    int64_t channelIncrement(int channel) const { return _channels[channel].increment; }

    /// COM66: external tempo change (firmware FXF - rescales the tick
    /// length without touching the row/tick speed)
    void setBpm(uint8_t bpm);

    /// TTD: quantum-domain runtime only - the parsed module is rebuilt from
    /// the card's upload store, never carried here
    size_t serializeStateSize() const;
    void serializeState(uint8_t* dst) const;
    void loadState(const uint8_t* src, size_t size);

    void reset(); // interpreter-level reset (COMF3/F4/00 family callers)

private:
    // Row/effect engine
    void processRow();
    void processTick();
    void applyEffect(ChannelState& ch, uint8_t effect, uint8_t param, bool rowStart);
    void fetchChannelOutput(int ch, uint8_t& out, uint8_t& vol);
    static uint16_t periodForNote(int note, uint8_t finetune);
    static uint32_t incrementForPeriod(uint16_t period);

    bool _parsed = false;
    std::vector<uint8_t> _module; // raw module image (sample data lives here)
    SampleInfo _samples[31];
    size_t _sampleCount = 0;
    size_t _patternCount = 0;
    uint8_t _patternTable[128] = {};
    size_t _songLength = 0;

    // Sequencer position (quantum domain - survives audio-rate changes)
    bool _playing = false;
    uint8_t _songPosition = 0;
    uint8_t _row = 0;
    uint8_t _tick = 0;
    uint8_t _speed = kDefaultSpeed;
    uint8_t _bpm = kDefaultBpm;
    uint32_t _quantaIntoTick = 0;
    uint32_t _tickQuanta = kDefaultTickQuanta;
    uint8_t _rowDelay = 0; // EEx pattern delay in rows

    // Deferred row navigation (Bxx/Dxx act at row end, PT semantics)
    bool _jumpFlag = false;
    uint8_t _jumpPosition = 0;
    bool _breakFlag = false;
    uint8_t _breakRow = 0;

    ChannelState _channels[kChannels];
};
