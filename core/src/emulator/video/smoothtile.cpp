#include "smoothtile.h"

#include <chrono>
#include <cstring>

namespace smoothmotion
{

void Tile::SetMode(Mode mode)
{
    _engine.SetOwnThreads(false);
    if (mode == _engine.GetMode() || (mode == Mode::Auto && _engine.GetMode() == Mode::Interpolate))
        return;
    _engine.Reset();
    _engine.SetMode(mode);
    _engine.SetLite(false);
    _guard.Reset();
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
    _guard.Reset();
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
            _geometry = info.geometry;
            _stats.frames++;
            came = true;
        }
        if (!came)
            return _lastSerial ? Step::Unchanged : Step::Nothing;
        _picture.swap(_next);
        _shown = {Kind::Frame, _lastSerial, _lastSerial, _lastSerial, 0};
        _stats.pictures++;
        return Step::Updated;
    }

    // Every frame once, in order; no more than the engine keeps
    for (size_t k = 0; k < SmoothMotion::kFrames; k++)
    {
        PresentFrameInfo info;
        if (!next(_lastSerial, reinterpret_cast<uint8_t*>(_next.data()), pixels * sizeof(uint32_t), &info))
            break;
        if (info.serial <= _lastSerial)
            break;  // not a newer frame: a source that repeats itself would loop here
        _lastSerial = info.serial;
        if (info.atOnce != _atOnce)
        {
            _atOnce = info.atOnce;
            _engine.Reset();
            _shown = {};
        }
        _engine.Push(info.serial, info.targetNs, _next.data(), width, height);
        _pulled[_nextPulled] = {info.serial, info.geometry};
        _nextPulled = (_nextPulled + 1) % SmoothMotion::kFrames;
        _stats.frames++;
        _moving = true;
    }
    if (_lastSerial == 0 || _engine.LastSerial() == 0)
        return Step::Nothing;

    // Low latency: the newest frame as it is (the engine's times are of no use, the frame is due at once)
    SmoothMotion::Result plan = _atOnce ? _engine.Plan(INT64_MAX) : _engine.Plan(tNs);
    if (_atOnce && plan.kind != Kind::None)
        plan = {Kind::Frame, plan.serial, plan.serial, plan.serial, 0};
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
