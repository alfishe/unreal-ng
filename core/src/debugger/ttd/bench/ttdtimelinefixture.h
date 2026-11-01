#pragma once

/// @file ttdtimelinefixture.h
/// @brief The synthetic phase program of the TTD timeline summary (docs/inprogress/2026-10-31-ttd-timeline-summary,
/// plan.md S1), recorded on a Pentagon. Shared by core-tests and core-benchmarks, so both measure the same session.
///
/// The program cycles through four phases of a fixed number of frames each (an IM 2 handler counts the frames):
///   A  "tape"   a tight IN loop on the ULA port, about 2,000 reads a frame (a loader's edge loop)
///   B  "burst"  fills 16 KB at #C000 again and again with a new value (an unpacker)
///   C  "idle"   HALT, then scans the eight keyboard half-rows once a frame; the fixture presses a key in it
///   D  "game"   once a frame: AY register writes, a beeper toggle, 256 screen bytes, a 7FFD page switch
/// Nothing here runs unless a test or a benchmark calls it.

#include <cstdint>
#include <memory>
#include <vector>

class Emulator;
class EmulatorContext;

namespace ttd
{
class TimeTravelController;

namespace bench
{

enum class TimelinePhaseKind : uint8_t
{
    Tape = 0,
    Burst = 1,
    Idle = 2,
    Game = 3
};

/// One phase as the program ran it: frames [firstFrame, endFrame)
struct TimelinePhase
{
    TimelinePhaseKind kind = TimelinePhaseKind::Tape;
    uint64_t firstFrame = 0;
    uint64_t endFrame = 0;
};

/// A key the fixture pressed: down at `downFrame`, up at `upFrame` (both in an idle phase)
struct TimelineKey
{
    int key = 0;   ///< ZXKeysEnum
    uint64_t downFrame = 0;
    uint64_t upFrame = 0;
};

struct TimelineSession
{
    std::shared_ptr<Emulator> emulator;
    EmulatorContext* context = nullptr;
    TimeTravelController* ttd = nullptr;
    uint64_t firstFrame = 0;   ///< the recording's first frame
    uint64_t endFrame = 0;     ///< one past its last complete frame
    std::vector<TimelinePhase> phases;
    std::vector<TimelineKey> keys;

    TimelineSession() = default;
    TimelineSession(const TimelineSession&) = delete;
    TimelineSession& operator=(const TimelineSession&) = delete;
    ~TimelineSession();

    bool Ok() const;
};

/// Records `cycles` passes of the four phases, `phaseFrames` frames each, on a fresh Pentagon (debug mode, turbo,
/// the write journal as asked). The recording is stopped and the machine positioned at its end
std::unique_ptr<TimelineSession> RecordTimelineProgram(unsigned phaseFrames, unsigned cycles, bool writeJournal = false);

}  // namespace bench
}  // namespace ttd
