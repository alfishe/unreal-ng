#pragma once
#include <cstdint>

/// Back-pressure for the presenters that queue what they are handed (Metal's drawables, Vulkan's FIFO images):
/// one present per refresh. A present for a refresh another present already took would be shown a refresh after it
/// (the queue shows each after the one before), and every one after it too: frames coming faster than the display
/// (a machine above the display's rate, turbo, low-latency frames at once) piled up, the delay grew a refresh at a
/// time and the GUI thread blocked in nextDrawable / the wait for a free image (README 1.2).
///
/// Such a present is held, not handed over: the window wakes for the next free refresh (a lead before it) and hands
/// over the newest frame then. A newer frame due by that time takes the refresh (the one held gives way: dropped,
/// the delay stays); none, and the one held goes there, where the queue would have shown it (nothing lost).
///
/// Judged by the times asked for only, never by the screen times: a compositor a refresh behind (every present shown
/// a refresh after its time, evenly, D13) asks for no refresh twice and holds nothing. Frames slower than the refresh
/// (48.83 Hz on 60 Hz) are always more than a refresh apart and always take a refresh of their own. The display
/// link's ticks (macOS) and smooth motion ask for one refresh each already: a present of theirs never collides.
/// Without a refresh grid (VRR: every frame at its own time, none seen yet) nothing is held.
///
/// A frame is held the same way while the presenter's pool is full (NativePresenter::PoolFull: every drawable waits
/// for the screen, a compositor a refresh behind keeps one more): handed over then, its BeginFrame would block the GUI
/// thread until the next refresh gives one back (12-16 ms a present), the window late for every frame after it. Held,
/// it goes just after that refresh, as the blocked present would have, and a newer frame due by then goes instead.
class PresentSlot
{
public:
    struct Plan
    {
        bool hold = false;  ///< the refresh is taken: not handed over now
        /// hold: the time to ask the next free refresh for (half a refresh before it; the window wakes a lead before)
        int64_t retryAtNs = 0;
    };

    /// The first refresh at or after `timeNs` on the grid of `refreshNs` (a refresh seen) and `periodNs`
    static int64_t RefreshOf(int64_t timeNs, int64_t refreshNs, int64_t periodNs)
    {
        const int64_t d = timeNs - refreshNs;
        const int64_t k = d >= 0 ? (d + periodNs - 1) / periodNs : -((-d) / periodNs);
        return refreshNs + k * periodNs;
    }

    /// A present to be asked for `atNs` (0: at once, handed over `nowNs`) on the grid (`periodNs` <= 0: none)
    Plan Ask(int64_t atNs, int64_t nowNs, int64_t refreshNs, int64_t periodNs) const
    {
        if (periodNs <= 0 || _takenNs == 0)
            return {};
        const int64_t refresh = RefreshOf(atNs > 0 ? atNs : nowNs, refreshNs, periodNs);
        if (refresh > _takenNs + periodNs / 2)
            return {};
        return {true, _takenNs + periodNs - periodNs / 2};
    }

    /// A present handed over, asked for `atNs` (0: at once at `nowNs`): its refresh is taken
    void Handed(int64_t atNs, int64_t nowNs, int64_t refreshNs, int64_t periodNs)
    {
        if (periodNs <= 0)
        {
            _takenNs = 0;
            return;
        }
        const int64_t refresh = RefreshOf(atNs > 0 ? atNs : nowNs, refreshNs, periodNs);
        if (refresh > _takenNs)
            _takenNs = refresh;
    }

    /// The window's tick, as it stands before the present: `fresh` a new frame uploaded (serial `serial`, 0: a source
    /// without serials), `force` a repaint to go at once, `paced` the display link's tick, `smooth` smooth motion's
    /// picture; `atOnce` / `targetNs` the frame's time (low-latency video: at once); `poolFull` the presenter's
    /// pool full (PoolFull)
    struct Tick
    {
        bool fresh = false;
        bool force = false;
        bool paced = false;
        bool smooth = false;
        uint64_t serial = 0;
        bool atOnce = false;
        int64_t targetNs = 0;
        bool poolFull = false;
    };
    struct Turn
    {
        bool present = false;  ///< a new picture to hand over (a forced repaint goes regardless)
        bool retry = false;    ///< it is the frame held: ask for `atNs`, the next free refresh's time
        int64_t atNs = 0;
    };

    /// What the window hands over at this tick. A frame for a refresh taken is held; at its retry (a lead before the
    /// next free refresh) the frame held goes, unless a newer one came: that one is planned instead and the one held
    /// gave way. A forced repaint shows the frame on screen at once, the one held with it. The display link's ticks
    /// (one refresh each) take the frame held at the next tick. A frame held for the pool retries just after the next
    /// refresh, asking for its own time
    Turn Decide(const Tick& tick, int64_t nowNs, int64_t leadNs, int64_t refreshNs, int64_t periodNs)
    {
        bool fresh = tick.fresh;
        bool retry = false;
        if (_heldSerial != 0)
        {
            const bool newer = fresh && tick.serial != _heldSerial;
            if (newer)
                _gaveWay++;
            if (tick.paced || tick.force || newer)
            {
                if (!newer && !tick.force && !fresh && tick.serial == _heldSerial)
                    fresh = true;  // the link's tick: this refresh's
                _heldSerial = 0;
            }
            else if (!fresh && tick.serial == _heldSerial && nowNs >= _wakeNs)
                fresh = retry = true;
        }
        if (fresh && !tick.force && !tick.paced && !tick.smooth && periodNs > 0 && tick.serial != 0)
        {
            const int64_t asked = retry ? _heldAtNs : (tick.atOnce ? 0 : tick.targetNs);
            const Plan plan = Ask(asked, nowNs, refreshNs, periodNs);
            if (plan.hold || tick.poolFull)
            {
                if (_heldSerial != tick.serial)
                {
                    _held++;
                    if (!plan.hold)
                        _heldForPool++;
                }
                _heldSerial = tick.serial;
                _forPool = !plan.hold;
                if (plan.hold)
                {
                    _heldAtNs = plan.retryAtNs;
                    _wakeNs = plan.retryAtNs - leadNs;
                }
                else
                {
                    // The pool: a drawable comes back at the next refresh; the frame keeps its time (the refresh
                    // after a taken one stays asked for)
                    _heldAtNs = asked;
                    _wakeNs = RefreshOf(nowNs + 1, refreshNs, periodNs) + kPoolWakeNs;
                }
                return {};
            }
        }
        return {fresh, retry, retry ? _heldAtNs : 0};
    }

    /// A present handed over (any: a new frame, a forced repaint), asked for `atNs` (0: at once at `nowNs`)
    void Presented(int64_t atNs, int64_t nowNs, int64_t refreshNs, int64_t periodNs)
    {
        Handed(atNs, nowNs, refreshNs, periodNs);
        _heldSerial = 0;
    }

    /// A frame is held: the window wakes at WakeNs for it (a lead before the next free refresh, or just after the
    /// next refresh for the pool); HeldAtNs the time its present asks for (0: at once)
    bool Holding() const { return _heldSerial != 0; }
    /// Held for the pool: the window uploads no newer frame until it is handed over (a newer one is for a later
    /// refresh when frames are slower than the display, the one held would be lost with nothing faster to excuse it)
    bool HoldingForPool() const { return _heldSerial != 0 && _forPool; }
    int64_t WakeNs() const { return _wakeNs; }
    int64_t HeldAtNs() const { return _heldAtNs; }
    /// Frames held (for a refresh taken or the pool), of them for the pool full, and of all given up for a newer frame
    /// (never handed over)
    uint64_t Held() const { return _held; }
    uint64_t HeldForPool() const { return _heldForPool; }
    uint64_t GaveWay() const { return _gaveWay; }
    /// A frame held for the pool wakes this long after the refresh that gives a drawable back
    static constexpr int64_t kPoolWakeNs = 1'000'000;

    /// Nothing taken, nothing held (a presenter made anew, the window hidden, the grid lost); the counts stay
    void Reset()
    {
        _takenNs = 0;
        _heldSerial = 0;
    }
    int64_t TakenNs() const { return _takenNs; }

private:
    int64_t _takenNs = 0;  ///< the latest refresh a present handed over asked for (0: none known)
    uint64_t _heldSerial = 0;  ///< the frame held (0: none)
    int64_t _heldAtNs = 0;     ///< ... the time its retry asks for
    int64_t _wakeNs = 0;       ///< ... the time it is tried again
    bool _forPool = false;     ///< ... held for the pool full
    uint64_t _held = 0;
    uint64_t _heldForPool = 0;
    uint64_t _gaveWay = 0;
};
