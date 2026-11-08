#pragma once

/// @file ttdreplaycheck.h
/// @brief TTD replay check (docs/inprogress/2026-11-05-ttd-replay-check/README.md): does the replay of a recorded
/// frame reproduce what the recording saw?
///
/// Every frame has a checkpoint at its start, taken from the live machine. Frame N's replay therefore has a
/// reference: checkpoint N + 1. The check restores N + 1 and takes the machine's whole state (the recorded truth),
/// restores N, replays the frame to its end and takes the whole state again, and names what differs: CPU registers,
/// chipset latches, model RAM per 16 KB page, every registered device's whole state, the engine's device regions.
/// TimeTravelController::QueryReplayCheck runs it and puts the machine back where it stood.

#include <cstdint>
#include <string>
#include <vector>

namespace ttd
{

struct TTDReplayCheckQuery
{
    bool hasFrom = false;   ///< else the first kept frame
    uint64_t fromFrame = 0;
    bool hasTo = false;     ///< else the last frame that has a next checkpoint
    uint64_t toFrame = 0;   ///< inclusive: the last frame whose replay is checked
    uint64_t stride = 1;    ///< every n-th frame
    uint32_t limit = 8;     ///< diverged frames described; the check stops after that many

    static constexpr uint32_t kMaxLimit = 200;
};

/// One part of the machine that differs between the recorded state and the replayed one
struct TTDReplayCheckItem
{
    std::string section;     ///< "cpu", "chipset", "ram", "device", "region"
    std::string name;        ///< register / latch name, "page 5", the device's instance, the region's name
    uint32_t size = 0;       ///< bytes compared
    uint32_t differing = 0;  ///< bytes that differ (a size difference counts its bytes)
    uint32_t firstOffset = 0;
    /// For a register or a latch (size <= 8): its value as recorded and as replayed
    bool hasValues = false;
    uint64_t recorded = 0;
    uint64_t replayed = 0;
};

struct TTDReplayCheckFrame
{
    uint64_t frame = 0;      ///< the frame whose replay diverged (from checkpoint frame to frame + 1)
    std::vector<TTDReplayCheckItem> items;
    uint32_t moreItems = 0;  ///< items past the list's cap
};

struct TTDReplayCheckResult
{
    std::string error;
    uint64_t fromFrame = 0;
    uint64_t toFrame = 0;
    uint64_t checked = 0;          ///< frames replayed and compared
    uint64_t matched = 0;
    uint64_t diverged = 0;
    uint64_t skippedBarrier = 0;   ///< a barrier inside the frame: an unrecorded change the replay cannot redo
    uint64_t skippedCut = 0;       ///< the next frame starts with a cut (a snapshot load): its checkpoint is not a replay
    uint64_t skippedNoNext = 0;    ///< no checkpoint for the next frame
    bool stoppedAtLimit = false;   ///< `limit` diverged frames found before the range ended
    uint64_t stoppedAfterFrame = 0;
    std::vector<TTDReplayCheckFrame> frames;
    double ms = 0;
};

/// Plain text of a result (the `text` option of the verb)
std::string TTDReplayCheckText(const TTDReplayCheckResult& r);

}  // namespace ttd
