#include "smoothtile.h"

#include <chrono>
#include <cstring>

namespace smoothmotion
{

namespace
{
/// The frame is of the size asked for (a source that gives no geometry: taken as it is)
bool FitsFrame(const PresentFrameInfo& info, int width, int height)
{
    return info.geometry.width == 0 ||
           (static_cast<int>(info.geometry.width) == width && static_cast<int>(info.geometry.height) == height);
}
}  // namespace

void Tile::SetMode(Mode mode)
{
    _engine.SetOwnThreads(false);
    if (mode == _engine.GetMode() || (mode == Mode::Auto && _engine.GetMode() == Mode::Interpolate))
        return;
    _engine.Reset();
    _engine.SetMode(mode);
    _engine.SetLite(false);
    _guard.StartOver();
    _stats.steppedDown = _stats.tooSlow = _stats.framesOnly = false;
    _shown = {};
}

bool Tile::StepDown()
{
    if (_stats.framesOnly)
        return false;
    const Mode mode = _engine.GetMode();
    if (mode == Mode::Interpolate && !_engine.Lite())
    {
        _stats.steppedDown = true;
        _engine.SetLite(true);
    }
    else if ((mode == Mode::Interpolate || mode == Mode::InterpolateLite) && !_stats.tooSlow)
        _stats.tooSlow = true;
    else
    {
        _stats.framesOnly = true;
        _engine.Reset();
    }
    _guard.StartOver();  // the wall's step: for good (the wall's own guard decides it), no retry of the tile's own
    _shown = {};
    return true;
}

Tile::Step Tile::Advance(const NextFrame& next, int width, int height, int64_t tNs, int64_t refreshNs)
{
    const auto began = std::chrono::steady_clock::now();
    const Step step = AdvanceTimed(next, width, height, tNs, refreshNs);
    _stats.lastNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - began).count();
    return step;
}

Tile::Step Tile::AdvanceTimed(const NextFrame& next, int width, int height, int64_t tNs, int64_t refreshNs)
{
    using Kind = SmoothMotion::Kind;
    _moving = false;
    if (width <= 0 || height <= 0 || !next)
        return Step::Nothing;
    const size_t pixels = static_cast<size_t>(width) * height;
    if (width != _width || height != _height)
    {
        _width = width;
        _height = height;
        _next.assign(pixels, 0);
        _picture.assign(pixels, 0);
        _shown = {};
    }

    // Stepped down to the frames: the newest one as it is, nothing kept, nothing searched
    if (_stats.framesOnly)
    {
        bool came = false;
        for (size_t k = 0; k < SmoothMotion::kFrames; k++)
        {
            PresentFrameInfo info;
            if (!next(_lastSerial, reinterpret_cast<uint8_t*>(_next.data()), pixels * sizeof(uint32_t), &info) ||
                info.serial <= _lastSerial)
                break;
            _lastSerial = info.serial;
            if (!FitsFrame(info, width, height))
                continue;
            _picture.swap(_next);  // the next read may be of another size: what came is kept now
            _geometry = info.geometry;
            _stats.frames++;
            came = true;
        }
        if (!came)
            return _lastSerial ? Step::Unchanged : Step::Nothing;
        _shown = {Kind::Frame, _lastSerial, _lastSerial, _lastSerial, 0};
        _stats.pictures++;
        return Step::Updated;
    }

    // Every frame once, in order; no more than the engine keeps
    uint64_t unpacedSerial = 0;  // the newest unpaced frame that came now
    for (size_t k = 0; k < SmoothMotion::kFrames; k++)
    {
        PresentFrameInfo info;
        if (!next(_lastSerial, reinterpret_cast<uint8_t*>(_next.data()), pixels * sizeof(uint32_t), &info))
            break;
        if (info.serial <= _lastSerial)
            break;  // not a newer frame: a source that repeats itself would loop here
        _lastSerial = info.serial;
        // Of another size than the machine's frame now (a video mode switch between the two reads): its pixels are
        // not rows of `width`
        if (!FitsFrame(info, width, height))
            continue;
        // Low latency (due at once) or not paced to real time (turbo, stepping: timed by the latch): no picture
        // between the frames, the newest as it is. Nothing goes to the engine (its copies and flicker searches only
        // held the wall up; the window does the same, smoothActive)
        const bool unpaced = info.atOnce || info.source == FrameClockSource::Latch;
        if (unpaced != _unpaced)
        {
            _unpaced = unpaced;
            _engine.Reset();
            _shown = {};
        }
        if (unpaced)
        {
            _picture.swap(_next);
            _geometry = info.geometry;
            _stats.frames++;
            unpacedSerial = info.serial;
            _moving = true;
            continue;
        }
        _engine.Push(info.serial, info.targetNs, _next.data(), width, height);
        _pulled[_nextPulled] = {info.serial, info.geometry};
        _nextPulled = (_nextPulled + 1) % SmoothMotion::kFrames;
        _stats.frames++;
        _moving = true;
    }
    if (_unpaced)
    {
        if (unpacedSerial == 0)
            return _shown.kind == Kind::None ? Step::Nothing : Step::Unchanged;
        _shown = {Kind::Frame, unpacedSerial, unpacedSerial, unpacedSerial, 0};
        _stats.pictures++;
        return Step::Updated;
    }
    if (_lastSerial == 0 || _engine.LastSerial() == 0)
        return Step::Nothing;

    // A step down of the tile's own guard is for a while (SmoothGuard::SteppedDown): one level up again when due
    if ((_stats.tooSlow || _stats.steppedDown) && !_stats.framesOnly && _guard.RetryDue(tNs))
    {
        if (_stats.tooSlow)
            _stats.tooSlow = false;
        else
        {
            _stats.steppedDown = false;
            _engine.SetLite(false);
        }
        _guard.Reset();
        if (_stats.steppedDown)
            _guard.RetryLater(tNs);
        _shown = {};
    }
    SmoothMotion::Result plan = _engine.Plan(tNs);
    if (plan.kind == Kind::None)
        return Step::Nothing;
    // Never a picture older than the one shown (a late call after the frames moved on)
    if (_shown.kind != Kind::None && plan.serial < _shown.serial)
    {
        _stats.behind++;
        plan = _shown;
    }
    const bool interpolates = plan.kind == Kind::Interpolate || plan.kind == Kind::Cadence || plan.kind == Kind::Carry;
    if (_stats.tooSlow && plan.kind == Kind::Interpolate)
        plan.kind = Kind::Blend;
    else if (_stats.tooSlow && interpolates)
        plan = {Kind::Frame, plan.serial, plan.serial, plan.serial, 0};
    if (plan.kind != Kind::Frame)
        _moving = true;
    if (plan == _shown)
        return Step::Unchanged;

    const auto start = std::chrono::steady_clock::now();
    if (!_engine.Draw(plan, _picture.data()))
        return _shown.kind == Kind::None ? Step::Nothing : Step::Unchanged;
    const int64_t drawNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    // A tile has a share of the refresh, not the whole of it: the wall tells how long a refresh it may take
    if (interpolates && !_stats.tooSlow && _guard.Note(0, drawNs, refreshNs))
    {
        if (!_engine.Lite())
        {
            _stats.steppedDown = true;
            _engine.SetLite(true);
        }
        else
            _stats.tooSlow = true;
        _guard.SteppedDown(tNs);
    }
    _shown = plan;
    for (const Pulled& pulled : _pulled)
        if (pulled.serial == plan.serial)
            _geometry = pulled.geometry;
    _stats.pictures++;
    if (plan.kind != Kind::Frame)
        _stats.between++;
    return Step::Updated;
}

}  // namespace smoothmotion
