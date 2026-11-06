#pragma once

/// @file ttdtimeline.h
/// @brief The TTD timeline summary (TD-5, docs/inprogress/2026-10-31-ttd-timeline-summary): per bucket of frames the
/// facts a recording already holds, and the recording cut into phases of the same activity.
///
/// TimeTravelController::QueryTimeline reads the facts of every frame (no replay); CutTimelinePhases tags each frame
/// over a window of `min_phase_ms` and cuts the phases at frame precision - pure arithmetic over the frames, the same
/// rule for every recording (design.md §4). Buckets are only an overview the caller may ask for: they do not move a
/// phase.

#include <cstdint>
#include <string>
#include <vector>

namespace ttd
{

/// What a bucket's machine did (design.md §4); a phase is a run of buckets with the same set
enum TTDTimelineTag : uint32_t
{
    kTimelineTape = 1u << 0,    ///< a tape plays, or, with a tape in, the ULA is read as fast as a loader's edge loop
    kTimelineDisk = 1u << 1,    ///< sectors read, storage ports used, or a disk written
    kTimelineInput = 1u << 2,   ///< keys, joystick, mouse (an event: it marks a phase, it does not cut one)
    kTimelineNet = 1u << 3,     ///< network ports or network events
    kTimelineSound = 1u << 4,   ///< a sound device written, or the beeper toggled
    kTimelineScreen = 1u << 5,  ///< the displayed screen changed in at least a quarter of the frames
    kTimelineBurst = 1u << 6,   ///< many more addresses written than the window's usual bucket (an unpacker)
    kTimelineHalt = 1u << 7,    ///< most frames start halted: a HALT-paced loop
    kTimelinePaging = 1u << 8,  ///< the memory mapping changed (cuts a phase when it changes in a quarter of the frames)
};
/// The tags that cut phases; the others (input, a rare paging change) only mark the phase they fall in
constexpr uint32_t kTimelineRegimeTags = ~uint32_t{kTimelineInput};
constexpr unsigned kTimelineTagCount = 9;

/// "tape", "disk", "input", "net", "sound", "screen", "burst", "halt", "paging" (bit order)
const char* TimelineTagName(unsigned bit);
std::vector<std::string> TimelineTagNames(uint32_t tags);

/// One device of the machine's port map, or one media slot (TTDTimelineResult::devices / media)
struct TTDTimelineDevice
{
    std::string name;
    uint32_t tags = 0;   ///< PortTagSet of its port map rows (0 for a media slot)
};

struct TTDTimelineBucket
{
    uint64_t frameStart = 0;
    uint64_t frameEnd = 0;       ///< inclusive
    // Coverage: distinct keys in the bucket (as coverage-summary)
    uint32_t executed = 0;
    uint32_t written = 0;
    uint32_t read = 0;
    // Checkpoints
    uint64_t ramPieces = 0;       ///< RAM pieces (4 KB) that changed, summed over the frames
    uint32_t screenFrames = 0;    ///< frames that changed a piece of the displayed screen
    uint32_t haltedFrames = 0;    ///< frames that started halted
    uint32_t borderChanges = 0;   ///< frame-start border unlike the frame before
    uint32_t pagingChanges = 0;   ///< frame-start memory latches unlike the frame before
    uint32_t lengthChanges = 0;   ///< a frame of another length than the one before (turbo, clock)
    // Journal cursors
    uint64_t in = 0;
    uint64_t out = 0;
    uint64_t sectors = 0;
    uint64_t vectors = 0;
    // Input journal
    uint32_t keyEvents = 0;
    std::vector<uint8_t> keys;    ///< ZXKeysEnum values pressed in the bucket, distinct, ascending
    uint32_t otherInput = 0;      ///< joystick, mouse and the other input events
    uint32_t netEvents = 0;
    // Port records (tier 2, `ports`)
    uint64_t ulaReads = 0;
    uint64_t borderOuts = 0;
    uint64_t beeperToggles = 0;
    std::vector<uint64_t> deviceIn;    ///< per TTDTimelineResult::devices
    std::vector<uint64_t> deviceOut;
    std::vector<uint64_t> mediaSectors;   ///< per TTDTimelineResult::media
    /// Per TTDTimelineResult::regions: the frames whose checkpoint found the region changed (a card's own CPU or DMA,
    /// a video RAM, a font: memory, so no clock field moves it)
    std::vector<uint32_t> regionFrames;
    // Write journal (when on)
    uint64_t writes = 0;
    // Markers in the bucket
    bool tapePlaying = false;     ///< a tape played during part of the bucket
    bool diskWritten = false;

    uint32_t tags = 0;            ///< TTDTimelineTag bits: what happened in it
};

/// What CutTimelinePhases reads of one frame
struct TTDTimelineFrame
{
    uint32_t written = 0;         ///< distinct written keys (coverage)
    uint32_t ulaReads = 0;
    uint32_t storage = 0;         ///< IN / OUT of storage-tagged devices, sectors read
    uint32_t sound = 0;           ///< OUT to sound-tagged devices, beeper toggles
    uint32_t net = 0;             ///< network-tagged traffic, net events
    bool screen = false;          ///< changed a piece of the displayed screen
    bool halted = false;
    bool paging = false;
    bool input = false;
    bool tape = false;            ///< a tape played
    /// A tape was in the deck at the frame's start (true when the session does not know the tape slot): an edge
    /// loop's ULA reads count as tape only then, else they are a program polling the keyboard
    bool tapeIn = true;
    bool diskWritten = false;
};

struct TTDTimelinePhase
{
    uint64_t frameStart = 0;
    uint64_t frameEnd = 0;        ///< inclusive
    uint32_t tags = 0;
    /// The counts summed over the phase's buckets; executed / written / read are the peak bucket's (distinct keys
    /// do not add up across buckets)
    TTDTimelineBucket facts;
    uint64_t busiestFrame = 0;    ///< frameStart of the bucket that executed the most keys
};

struct TTDTimelineEvent
{
    uint64_t frame = 0;
    uint32_t tInFrame = 0;
    std::string kind;             ///< "marker:<kind>", "bookmark", "media", "config", "frame-length", "replay-source"
    std::string text;
};

/// A fact the session cannot give, with the reason (TL-6)
struct TTDTimelineMissing
{
    std::string fact;             ///< "writes", "ports", "coverage", "screen"
    std::string reason;
};

struct TTDTimelineOptions
{
    uint64_t fromFrame = 0;
    uint64_t toFrame = UINT64_MAX;
    uint64_t bucketSize = 0;      ///< 0: ceil(frames / limit)
    size_t limit = 20;            ///< 1..500 overview buckets
    bool ports = true;            ///< classify the port records (tier 2)
    bool buckets = false;         ///< also the overview buckets (`limit` of them)
    unsigned minPhaseMs = 200;    ///< the shortest phase, and the window a frame's tags are measured over
};

struct TTDTimelineResult
{
    std::string error;            ///< non-empty: no timeline (no history, a bad window)
    uint64_t fromFrame = 0;
    uint64_t toFrame = 0;
    uint64_t bucketSize = 0;
    double fps = 0.0;
    bool truncated = false;       ///< more buckets than `limit` at the asked bucket size: the window was cut
    std::vector<TTDTimelineBucket> buckets;
    std::vector<TTDTimelinePhase> phases;
    std::vector<TTDTimelineEvent> events;
    std::vector<TTDTimelineMissing> missing;
    std::vector<TTDTimelineDevice> devices;
    std::vector<TTDTimelineDevice> media;
    std::vector<TTDTimelineDevice> regions;   ///< the session's device memory regions (all but machine RAM)
    // What the call cost
    uint64_t portRecords = 0;
    uint64_t elapsedNs = 0;
};

/// The phases of @p frames (the first one is @p firstFrame): runs of frames with the same regime tags, a run shorter
/// than @p minFrames joining the neighbour closer in tags (the shortest first; the earlier neighbour on a tie). Fills
/// frameStart, frameEnd and tags (regime tags of the run, plus the event tags of its frames); the facts are the
/// caller's
std::vector<TTDTimelinePhase> CutTimelinePhases(const std::vector<TTDTimelineFrame>& frames, uint64_t firstFrame,
                                                unsigned minFrames);

}  // namespace ttd
