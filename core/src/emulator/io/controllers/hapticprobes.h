#pragma once

/// @file hapticprobes.h
/// @brief The haptic feed's breakpoint probes (docs/inprogress/2026-10-17-controller-manager/haptics.md §5): each
/// probe is a hidden breakpoint (owner "haptics") with the non-breaking "notify feed" action, so memory banking and
/// page selectors behave as in the debugger's breakpoints. A hit only notes itself here; the frame end hands the
/// frame's hits and events to the feed (HapticFrame::probeHits / probeEvents). Host output only: nothing is written
/// to the machine, and a TTD replay never fires a probe (BreakpointManager::ResolveMemory / ResolvePort).

#include <array>
#include <cstdint>
#include <vector>

#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/io/controllers/ihapticfeed.h"

class EmulatorContext;

namespace controllers
{

class HapticProbes : public IBreakpointProbeSink
{
public:
    explicit HapticProbes(EmulatorContext* context);
    ~HapticProbes() override;
    HapticProbes(const HapticProbes&) = delete;
    HapticProbes& operator=(const HapticProbes&) = delete;

    /// Replace the armed probes (at most HapticFrame::kMaxProbes). Emulation thread (or the CPU held)
    void Arm(const std::vector<HapticProbe>& probes);
    /// Remove every probe; nothing armed costs nothing
    void Disarm();
    bool Armed() const
    {
        return !_ids.empty();
    }
    /// Forget the hits so far (frames the feed does not get: replayed or owned by a replay)
    void Clear();
    /// The frame's hits and events into @p frame, then from zero
    void TakeFrame(HapticFrame& frame);

    void OnProbeHit(uint8_t probe, uint16_t address, int value) override;

    static constexpr const char* kOwner = "haptics";

private:
    BreakpointManager* Breakpoints() const;
    uint8_t ReadByte(const HapticProbe& probe) const;

    EmulatorContext* _context = nullptr;
    std::vector<HapticProbe> _probes;
    std::vector<uint16_t> _ids;
    bool _debugHold = false;
    std::array<uint16_t, HapticFrame::kMaxProbes> _hits{};
    std::array<uint8_t, HapticFrame::kMaxProbes> _last{};  ///< a write probe: the byte before the next write
    std::array<HapticProbeEvent, HapticFrame::kMaxProbeEvents> _events{};
    unsigned _eventCount = 0;
};

}  // namespace controllers
