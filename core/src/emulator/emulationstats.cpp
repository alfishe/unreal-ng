#include "emulator/emulationstats.h"

#include <algorithm>

#include <chrono>

namespace
{
constexpr uint64_t kWindowUs = static_cast<uint64_t>(EmulationStatsMeter::kWindowSeconds * 1e6);
}

void EmulationStatsMeter::OnFrame(uint64_t nowUs, uint32_t workUs, uint32_t frameDurationUs, uint32_t hostMultiplier,
                                  const EmulationCounters& counters)
{
    // A gap of two windows between frames is a pause or a stall, not a slow frame: start again from here
    if (_open && (nowUs < _lastUs || nowUs - _lastUs > 2 * kWindowUs))
        _open = false;
    _lastUs = nowUs;
    if (!_open)
    {
        _open = true;
        _startUs = nowUs;
        _frames = 0;
        _workUsSum = 0;
        _workedFrames = 0;
        _workUsMax = 0;
        _atStart = counters;
        _last = counters;
        _frameDurationUs = frameDurationUs;
        _hostMultiplier = hostMultiplier;
        return;  // the window starts at this frame's start: its frames are counted from the next one
    }
    _frames++;
    if (workUs > 0)
    {
        _workUsSum += workUs;
        _workedFrames++;
        _workUsMax = std::max(_workUsMax, workUs);
    }
    _last = counters;
    _frameDurationUs = frameDurationUs;
    _hostMultiplier = hostMultiplier;
    if (nowUs - _startUs >= kWindowUs)
        Publish(nowUs);
}

void EmulationStatsMeter::Restart()
{
    _open = false;
}

void EmulationStatsMeter::Publish(uint64_t nowUs)
{
    const double seconds = static_cast<double>(nowUs - _startUs) / 1e6;
    const double fps = seconds > 0 ? static_cast<double>(_frames) / seconds : 0.0;
    const double nominal = _frameDurationUs > 0 ? 1e6 / static_cast<double>(_frameDurationUs) : 0.0;
    const double workAvg =
        _workedFrames > 0 ? static_cast<double>(_workUsSum) / static_cast<double>(_workedFrames) : 0.0;
    // A counter that went back (a reset, a snapshot) counts as no change for this window
    auto delta = [](uint64_t from, uint64_t to) -> uint64_t { return to >= from ? to - from : 0; };
    const uint64_t interrupts = delta(_atStart.interrupts, _last.interrupts);
    const uint64_t halted = delta(_atStart.haltedTStates, _last.haltedTStates);
    const uint64_t frameT = delta(_atStart.frameTStates, _last.frameTStates);
    _windows++;

    _seq.fetch_add(1, std::memory_order_acq_rel);  // odd: writing
    _pubAtUs.store(nowUs, std::memory_order_relaxed);
    _pubFps.store(fps, std::memory_order_relaxed);
    _pubNominal.store(nominal, std::memory_order_relaxed);
    _pubSpeed.store(nominal > 0 ? fps / nominal * static_cast<double>(std::max<uint32_t>(_hostMultiplier, 1)) * 100.0
                                : 0.0,
                    std::memory_order_relaxed);
    _pubWorkAvg.store(workAvg, std::memory_order_relaxed);
    _pubWorkMax.store(static_cast<double>(_workUsMax), std::memory_order_relaxed);
    _pubBudget.store(_frameDurationUs > 0 ? workAvg / static_cast<double>(_frameDurationUs) * 100.0 : 0.0,
                     std::memory_order_relaxed);
    _pubInterrupts.store(_frames > 0 ? static_cast<double>(interrupts) / static_cast<double>(_frames) : 0.0,
                         std::memory_order_relaxed);
    _pubHalted.store(frameT > 0 ? std::min(100.0, static_cast<double>(halted) / static_cast<double>(frameT) * 100.0)
                                : 0.0,
                     std::memory_order_relaxed);
    _pubFrames.store(_frames, std::memory_order_relaxed);
    _pubSeconds.store(seconds, std::memory_order_relaxed);
    _pubWindows.store(_windows, std::memory_order_relaxed);
    _pubFrame.store(_last.frame, std::memory_order_relaxed);
    _pubTStates.store(_last.tStates, std::memory_order_relaxed);
    _seq.fetch_add(1, std::memory_order_release);  // even: done

    // The next window starts where this one ended
    _startUs = nowUs;
    _frames = 0;
    _workUsSum = 0;
    _workedFrames = 0;
    _workUsMax = 0;
    _atStart = _last;
}

EmulationStats EmulationStatsMeter::Read(uint64_t nowUs) const
{
    EmulationStats stats;
    uint64_t at = 0;
    for (int attempt = 0; attempt < 16; attempt++)
    {
        const uint32_t before = _seq.load(std::memory_order_acquire);
        if ((before & 1u) != 0)
            continue;  // the writer is in the middle of it (a few stores): try again
        at = _pubAtUs.load(std::memory_order_relaxed);
        stats.fps = _pubFps.load(std::memory_order_relaxed);
        stats.nominalFps = _pubNominal.load(std::memory_order_relaxed);
        stats.speedPercent = _pubSpeed.load(std::memory_order_relaxed);
        stats.frameWorkUsAvg = _pubWorkAvg.load(std::memory_order_relaxed);
        stats.frameWorkUsMax = _pubWorkMax.load(std::memory_order_relaxed);
        stats.frameBudgetPercent = _pubBudget.load(std::memory_order_relaxed);
        stats.interruptsPerFrame = _pubInterrupts.load(std::memory_order_relaxed);
        stats.haltedPercent = _pubHalted.load(std::memory_order_relaxed);
        stats.windowFrames = _pubFrames.load(std::memory_order_relaxed);
        stats.windowSeconds = _pubSeconds.load(std::memory_order_relaxed);
        stats.windows = _pubWindows.load(std::memory_order_relaxed);
        stats.frame = _pubFrame.load(std::memory_order_relaxed);
        stats.tStates = _pubTStates.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (_seq.load(std::memory_order_relaxed) == before)
            break;
    }
    // Nothing published for two windows: no frames run now (a pause, a stop); the figures were of the last run
    stats.measuring = stats.windows > 0 && nowUs >= at && nowUs - at <= 2 * kWindowUs;
    if (!stats.measuring)
    {
        stats.fps = 0;
        stats.speedPercent = 0;
    }
    return stats;
}

EmulationStats EmulationStatsMeter::Read() const
{
    return Read(NowUs());
}

uint64_t EmulationStatsMeter::NowUs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
