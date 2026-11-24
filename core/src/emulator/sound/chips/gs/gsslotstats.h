#pragma once

/// @file gsslotstats.h
/// @brief GS-slot statistics (neogs-automation-design.md, gs-debugger N7): what the fitted card did - both sides of
/// the mailbox, interrupts, the coprocessor, the audio; on NeoGS every DMA transaction, the SPI, SD card, MP3 decoder
/// and flash traffic and their errors. One block for every card, each fills the sections it has.
///
/// - **Counters** are plain `uint64_t` on the machine thread, where the event happens (the cards and their devices).
/// - **Publishing**: at a frame end the machine thread collects the block only when a reader asked in the last 2 s
///   (one relaxed load otherwise); a paused machine is collected on the reader's thread.
/// - **Rates** per second over the last 50 published frames, computed at publish time.
/// - **Baselines**: `reset_counters` stores the counters as a baseline id; a read with `since` subtracts it. The
///   machine keeps counting; clients keep separate baselines.
///
/// Statistics, not machine state: nothing here is in a TTD blob or the state hash.

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;
class GeneralSoundCard;
namespace ttd { class ITimeTravelHooks; }

/// Writes one object of the block: state leaves, and counters, which it records by their path
/// ("mailbox.host_commands") for the baselines and the rates
class GSStatsWriter
{
public:
    using Counters = std::vector<std::pair<std::string, uint64_t>>;

    GSStatsWriter(StateNode& node, std::string path, Counters& counters)
        : _node(node), _path(std::move(path)), _counters(counters)
    {
        if (!_node.isObject())
            _node = StateNode::Object();
    }

    /// A member object (created on first use, in the order of first use)
    GSStatsWriter Child(const std::string& key) { return GSStatsWriter(_node[key], Path(key), _counters); }
    void State(const std::string& key, StateNode value) { _node[key] = std::move(value); }
    void Counter(const std::string& key, uint64_t value)
    {
        _node[key] = value;
        _counters.emplace_back(Path(key), value);
    }

private:
    std::string Path(const std::string& key) const { return _path.empty() ? key : _path + "." + key; }

    StateNode& _node;
    std::string _path;
    Counters& _counters;
};

/// A collected block: the sections, the counters by path, the rates
struct GSStatsBlock
{
    std::string implementation;          ///< gsImplementationLabel: lle / lightweight / ngs
    std::vector<std::string> capabilities; ///< the sections present, plus rates and errors
    StateNode sections = StateNode::Object();
    GSStatsWriter::Counters counters;
    StateNode rates = StateNode::Object(); ///< {window_frames, <rate>: per second}; empty before two frames
    uint64_t frame = 0;                  ///< the publisher's frame count when collected
    bool replaying = false;              ///< collected while TTD replays (the card runs again, the counters count)
};

/// The block of a card: the common sections, then the card's own (GeneralSoundCard::collectStats)
void GSCollectStats(const GeneralSoundCard& card, GSStatsBlock& out);

/// The published block of the machine's GS slot (owned by SoundManager)
class GSStatsPublisher
{
public:
    static constexpr uint64_t kWantedFrames = 100; ///< a read keeps publishing on for 2 s
    static constexpr size_t kRateFrames = 50;
    static constexpr size_t kMaxBaselines = 16;

    /// Machine thread, at each frame end: one relaxed load unless a reader asked in the last 2 s
    void FrameEnd(const GeneralSoundCard* card, double frameSeconds, const ttd::ITimeTravelHooks* ttd)
    {
        const uint64_t frame = _frame.load(std::memory_order_relaxed) + 1;
        _frame.store(frame, std::memory_order_relaxed);
        if (frame > _wantedUntil.load(std::memory_order_relaxed)) [[likely]]
        {
            _ringCount = 0; // a reader coming back starts a fresh window
            return;
        }
        if (card)
            Publish(*card, frameSeconds, ttd);
    }

    /// Any thread: keep publishing for the next 2 s of frames
    void Want() { _wantedUntil.store(Frame() + kWantedFrames, std::memory_order_relaxed); }
    uint64_t Frame() const { return _frame.load(std::memory_order_relaxed); }

    /// Collect on the caller's thread: the machine is paused (nothing runs the card) or this is its thread. The
    /// rates stay those of the last frame end
    void CollectNow(const GeneralSoundCard& card, bool replaying);
    /// The last block (nullptr before the first)
    std::shared_ptr<const GSStatsBlock> Latest() const;
    /// Wait until a block of frame @p minFrame or later is published, at most @p waitMs; false on timeout
    bool WaitFor(uint64_t minFrame, uint32_t waitMs) const;

    /// A baseline of these counters; its id (the oldest of more than 16 is dropped)
    uint32_t AddBaseline(GSStatsWriter::Counters counters);
    std::optional<GSStatsWriter::Counters> Baseline(uint32_t id) const;

private:
    /// The rates' sources: a name and the counters summed into it
    struct RateSource
    {
        const char* name;
        std::array<const char*, 2> paths;
    };
    static const std::array<RateSource, 10> kRateSources;

    struct RateSample
    {
        std::array<uint64_t, 10> values{};
        uint16_t present = 0; ///< bit per source: the card has its counters
        double seconds = 0;   ///< the frame's emulated length
    };

    void Publish(const GeneralSoundCard& card, double frameSeconds, const ttd::ITimeTravelHooks* ttd);
    void PublishBlock(std::shared_ptr<GSStatsBlock> block);

    std::atomic<uint64_t> _frame{0};
    std::atomic<uint64_t> _wantedUntil{0};

    // Machine thread only: the rate window
    std::array<RateSample, kRateFrames + 1> _ring{};
    size_t _ringHead = 0;
    size_t _ringCount = 0;
    std::string _ringImplementation;

    mutable std::mutex _mutex;
    mutable std::condition_variable _published;
    std::shared_ptr<const GSStatsBlock> _latest;
    std::vector<std::pair<uint32_t, GSStatsWriter::Counters>> _baselines;
    uint32_t _nextBaseline = 1;
};

/// A read of the statistics (every plane: WebAPI `/state/audio/gs/stats`, CLI `gs stats`, MCP, Lua, Python)
struct GSStatsQuery
{
    std::string section;   ///< one section ("" = the whole block)
    uint32_t since = 0;    ///< a baseline id (0 = none)
    bool fresh = false;    ///< a block from a frame end after this read
    uint64_t minFrame = 0; ///< set by the first reply of a fresh read: pass it back to wait for the same frame
    uint32_t waitMs = 0;   ///< wait on this thread for the block (never on the machine thread)
    std::string cpu;       ///< the card by its debugger CPU: "" or "gs" the GS slot's, "ms" the ZX-MultiSound's
};

struct GSStatsReply
{
    int status = 200;      ///< as HTTP: 404 no card / section / baseline
    bool pending = false;  ///< no block yet for this read (the machine runs): read again with minFrame
    uint64_t minFrame = 0;
    StateNode body;        ///< the block or one section with {implementation, frame, age_frames, ...}, or {error}
    std::string text;      ///< the same for people
    std::vector<std::string> counters; ///< the paths of the body's counters ("mailbox.host_commands"); the rest is state
};

/// The General Sound card a read names by its debugger CPU (DebugCpuName): "" or "gs" the GS slot's card, "ms" the
/// one a slot card carries (the ZX-MultiSound's, ICard::DebugGeneralSound); nullptr when it is not fitted or the name
/// is none of these (`known` false then). `stats`: that card's statistics publisher
GeneralSoundCard* GSCardForCpu(EmulatorContext* context, const std::string& cpu, GSStatsPublisher** stats = nullptr,
                               bool* known = nullptr);

GSStatsReply GSStatsRead(EmulatorContext* context, GSStatsQuery query);
/// A new baseline from the counters now: {baseline, frame}. Pending like a read until a block exists
GSStatsReply GSStatsResetCounters(EmulatorContext* context, GSStatsQuery query);
/// A read's body as lines: "mailbox: host_commands 203, card_command_acks 3, ..."
std::string GSStatsText(const StateNode& body);
