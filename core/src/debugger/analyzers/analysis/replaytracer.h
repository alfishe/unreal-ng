#pragma once

/// @file replaytracer.h
/// @brief The L1 replay (docs/emulator/analysis/analysis-layers.md, "L1: code behaviour"): a recording session is
/// captured as a TraceBundle, then replayed on an analysis instance - a muted, headless clone of the machine that
/// loads the TTD recording - from its first keyframe to its end, with the instruction-start and bus hooks set on the
/// clone only. Each instruction is folded into L1 at once (L1Builder), so nothing of the trace is stored. The source
/// instance runs on untouched: its hot path never sees the tracer.
///
/// @code
///   analysis::TraceBundle bundle;
///   if (analysis::TraceBundle::Capture(emulator, bundle, error))      // the CPU held by the caller
///   {
///       analysis::L1Result l1;
///       analysis::ReplayOptions options;
///       options.progress = [](uint64_t done, uint64_t total) { return true; };  // false cancels
///       analysis::ReplayBuild(bundle, options, l1, error);             // any thread
///   }
/// @endcode

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "debugger/analyzers/analysis/l1model.h"

class Emulator;
struct CONFIG;
struct SlotConfig;

namespace analysis
{

/// Everything one recording session produced, under one handle: the TTD recording (journals and keyframes, as the
/// session file's bytes), and what the source instance holds of the same session - the L0 access facts, the
/// coverage and the call trace stay there and are named here (they are read where they are)
struct TraceBundle
{
    std::string sourceId;
    std::string model;                         ///< short model name ("48K")
    std::shared_ptr<const CONFIG> config;      ///< the source's configuration (the clone is made to match it)
    /// The source's fitted slot set as it ran (a card plugged at run time, a Kempston for a game's joystick, is in
    /// it; the configuration's [SLOTS] is the set the machine was created with). Null: no slot manager
    std::shared_ptr<const SlotConfig> slots;
    std::shared_ptr<const std::string> session;  ///< the TTD session file, in memory
    uint64_t firstFrame = 0;                   ///< the first keyframe
    uint64_t endFrame = 0;                     ///< the frame the recording ended in
    uint64_t checkpoints = 0;
    bool facts = false;                        ///< L0 access facts were collected in the same session
    std::string factsMode;                     ///< "z80" / "physical"
    bool coverage = false;
    bool callTrace = false;

    /// Capture @p source's session. A recording in flight is stopped first (the bundle is one whole session).
    /// The caller holds the source's CPU. False with @p error when there is no recording
    static bool Capture(Emulator& source, TraceBundle& out, std::string& error);

    StateNode ToStateNode() const;
};

struct ReplayOptions
{
    uint64_t maxFrames = 0;     ///< 0: to the end of the recording
    uint32_t chunkFrames = 25;  ///< frames per run between progress reports and checkpoint comparisons
    uint64_t salt = 0;          ///< L1Builder's hash salt (0: random)
    /// Called after every chunk with the frames done and the frames to do; false cancels the build
    std::function<bool(uint64_t done, uint64_t total)> progress;
};

/// Replay @p bundle on a fresh analysis instance and fold L1 into @p out. Runs on the calling thread and touches
/// only the clone it makes. False with @p error when the clone cannot be made, the session does not load, or the
/// progress callback cancelled
bool ReplayBuild(const TraceBundle& bundle, const ReplayOptions& options, L1Result& out, std::string& error);

}  // namespace analysis
