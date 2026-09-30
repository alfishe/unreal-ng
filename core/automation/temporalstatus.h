#pragma once

/// @file temporalstatus.h
/// @brief Temporal effects (ZX DLSS de-flicker) status and control, shared by
/// every automation surface: CLI `video temporal`, WebAPI /video/temporal,
/// Lua / Python video_temporal() / video_temporal_set(), MCP capture_media
/// temporal_status / temporal_set (through the WebAPI). The report is one
/// StateNode; each surface converts it with its own generic converter.

#include <emulator/emulatorcontext.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/state/statenode.h>
#include <emulator/video/screen.h>
#include <emulator/video/zxdlss/algorithm.h>

#include <sstream>
#include <string>
#include <vector>

namespace TemporalStatus
{
/// Algorithm switched on by default / recommended
inline constexpr const char* kDefaultAlgorithm = "mod-tpgwafsd";

/// Algorithms offered to users: every registered one except "raw" (pass-through)
/// and the "-ref" reference variants
inline std::vector<std::string> OfferedAlgorithms()
{
    std::vector<std::string> offered;
    for (const std::string& name : zxdlss::algorithmNames())
    {
        const bool reference = name.size() >= 4 && name.compare(name.size() - 4, 4, "-ref") == 0;
        if (name != "raw" && !reference)
            offered.push_back(name);
    }
    return offered;
}

/// Offered algorithm names joined with ", " (for error messages)
inline std::string OfferedList()
{
    std::string list;
    for (const std::string& name : OfferedAlgorithms())
        list += (list.empty() ? "" : ", ") + name;
    return list;
}

/// Status report: algorithm ("" = off), active, inactive_reason, correcting (a
/// detector fired in the frame on screen: an averaging mask formed),
/// showing_processed (the frame on screen went through the algorithm, changed or
/// not), video_delay_frames, video_delay_ms, audio_extra_delay_frames, processed,
/// corrected_frames, written, shown_raw (output arrived after its frame was on
/// screen), late, restarts, last_ms, average_ms, shown_frame and last_frame
/// {pattern, period2..period5 and field (percent of pixels in the mask),
/// field_stage, whole_paper, scene_average} (null when unknown), algorithms[],
/// default_algorithm

/// A frame report as a node (null when the algorithm reported nothing)
inline StateNode FrameNode(const zxdlss::FrameReport& r)
{
    if (!r.valid || !r.pixels)
        return StateNode();
    auto percent = [&r](uint32_t n) { return 100.0 * n / r.pixels; };
    StateNode node = StateNode::Object();
    node["pattern"] = std::string(zxdlss::patternName(r));
    for (int k = 0; k < 4; ++k)
        node["period" + std::to_string(k + 2)] = percent(r.periodPixels[k]);
    node["field"] = percent(r.fieldPixels);
    node["field_stage"] = r.fieldStage;
    node["whole_paper"] = r.wholePaper;
    node["scene_average"] = r.sceneAverage;
    return node;
}
inline StateNode Report(EmulatorContext* context)
{
    StateNode node = StateNode::Object();
    Screen* screen = context ? context->pScreen : nullptr;
    TemporalEffects::Stats stats;
    uint32_t delayUs = 0;
    if (screen)
    {
        stats = screen->GetTemporalStats();
        stats.algorithm = screen->GetTemporalAlgorithm();
        delayUs = screen->GetPresentDelayUs();
    }
    const int audioExtra = (context && context->pSoundManager) ? context->pSoundManager->getOutputDelayFrames() : 0;

    node["algorithm"] = stats.algorithm;
    node["active"] = stats.active;
    node["inactive_reason"] = stats.inactiveReason;
    node["correcting"] = stats.correcting;
    node["showing_processed"] = stats.showingProcessed;
    node["video_delay_frames"] = screen ? int(screen->GetEffectivePresentDelayFrames()) : 0;
    node["video_delay_ms"] = double(delayUs) / 1000.0;
    node["audio_extra_delay_frames"] = audioExtra;
    node["processed"] = stats.processed;
    node["corrected_frames"] = stats.correctedFrames;
    node["written"] = stats.written;
    node["shown_raw"] = stats.shownRaw;
    node["late"] = stats.late;
    node["restarts"] = stats.restarts;
    node["last_ms"] = stats.lastMs;
    node["average_ms"] = stats.averageMs;
    node["shown_frame"] = FrameNode(stats.shownFrame);
    node["last_frame"] = FrameNode(stats.lastFrame);
    StateNode algorithms = StateNode::Array();
    for (const std::string& name : OfferedAlgorithms())
        algorithms.items.emplace_back(name);
    node["algorithms"] = algorithms;
    node["default_algorithm"] = kDefaultAlgorithm;
    return node;
}

/// Select the algorithm: a registered name, or "" / "off" to switch the effect off.
/// @return false (nothing changed) for an unknown name or a missing screen
inline bool Set(EmulatorContext* context, const std::string& name)
{
    if (!context || !context->pScreen)
        return false;
    return context->pScreen->SetTemporalAlgorithm(name == "off" ? std::string() : name);
}

/// One-line summary, e.g. "ZX DLSS mod-tpgwafsd active, video +7 frames (143 ms),
/// audio +5, 4.8 ms/frame, late 0, restarts 0"
inline std::string Summary(EmulatorContext* context)
{
    Screen* screen = context ? context->pScreen : nullptr;
    if (!screen)
        return "ZX DLSS unavailable";
    const TemporalEffects::Stats stats = screen->GetTemporalStats();
    const std::string algorithm = screen->GetTemporalAlgorithm();
    std::ostringstream out;
    if (algorithm.empty())
    {
        out << "ZX DLSS off, video +" << int(screen->GetEffectivePresentDelayFrames()) << " frames";
        return out.str();
    }
    out << "ZX DLSS " << algorithm;
    if (stats.active)
        out << " active";
    else
        out << " inactive" << (stats.inactiveReason.empty() ? "" : " (" + stats.inactiveReason + ")");
    const int audioExtra = context->pSoundManager ? context->pSoundManager->getOutputDelayFrames() : 0;
    out << ", video +" << int(screen->GetEffectivePresentDelayFrames()) << " frames ("
        << (screen->GetPresentDelayUs() + 500) / 1000 << " ms), audio +" << audioExtra;
    out.setf(std::ios::fixed);
    out.precision(1);
    out << ", " << stats.averageMs << " ms/frame, ";
    if (stats.correcting)
        out << "correcting (" << zxdlss::patternName(stats.shownFrame) << ")";
    else
        out << (stats.showingProcessed ? "idle (nothing detected)" : "raw on screen");
    out << ", corrected " << stats.correctedFrames << " of " << stats.processed << ", shown raw " << stats.shownRaw
        << ", late " << stats.late << ", restarts " << stats.restarts;
    return out.str();
}
} // namespace TemporalStatus
