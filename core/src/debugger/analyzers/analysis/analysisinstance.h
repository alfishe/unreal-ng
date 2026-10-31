#pragma once

/// @file analysisinstance.h
/// @brief The analysis instance (docs/emulator/analysis/analysis-layers.md, "L1: code behaviour", "L3: review"): a
/// headless, muted machine made like the source of a TraceBundle, never started (it runs on the calling thread,
/// RunTStates). With the recording loaded it is the L1 replay's clone and the experiment runner's base (seeked to a
/// frame); without it, a free machine an experiment gets the base's state into (MachineStateTransfer, clone path)
/// and runs with its own pokes and input. The source instance is never touched: only the bundle's bytes are read.
///
/// @code
///   analysis::AnalysisInstance base;
///   if (base.Make(bundle, "analysis-base", true, error)) ...       // the recording loaded
///   analysis::AnalysisInstance free;
///   free.Make(bundle, "analysis-experiment", false, error);         // a machine of the same model
///   free.RunFrames(50);
/// @endcode

#include <cstdint>
#include <memory>
#include <string>

class Emulator;
class EmulatorContext;

namespace analysis
{

struct TraceBundle;

class AnalysisInstance
{
public:
    AnalysisInstance() = default;
    ~AnalysisInstance();
    AnalysisInstance(const AnalysisInstance&) = delete;
    AnalysisInstance& operator=(const AnalysisInstance&) = delete;

    /// Make the machine: the bundle's model, cards and slot set, sound off and muted, logging off. @p session: load
    /// the recording (TTD on); otherwise TTD stays off. Instances are made and released one at a time (a process-wide
    /// lock), so experiment workers may make theirs in parallel
    bool Make(const TraceBundle& bundle, const char* name, bool session, std::string& error);

    Emulator* GetEmulator() const { return _emulator.get(); }
    EmulatorContext* Context() const;
    /// T-states of one frame at the CPU clock
    uint32_t FrameT() const;
    uint64_t Frame() const;
    /// Run to the boundary @p frames frames on (breakpoints skipped). False when the CPU stopped moving
    bool RunFrames(uint64_t frames);
    /// Run until the frame counter reaches @p frame
    bool RunToFrame(uint64_t frame);

private:
    std::unique_ptr<Emulator> _emulator;
};

}  // namespace analysis
