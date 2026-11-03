#pragma once

/// @file smoothtrace.h
/// @brief Smooth motion's live trace (UNREAL_SMOOTH_TRACE): what the screen window gave smooth motion and what it put
/// on the screen, refresh by refresh, so a tool replays the same inputs through the core and scores the pictures
/// (tools/verification/smoothmotion/live). The writer runs on the window's render thread; nothing when unset.
///
/// The trace starts at the first refresh after a file `start` appears in the folder (the caller loads and warms up
/// the machine first), past UNREAL_SMOOTH_TRACE_SKIP refreshes, and writes three files:
/// - `events.tsv`: one line per event, in order:
///   - `start <mode>`: smooth motion was reset at the trace's start (the frames before are not in it);
///   - `frame <serial> <targetNs> <width> <height>`: a frame pushed (its pixels the next ones of frames.rgba);
///   - `refresh <tNs> <kind> <serial> <from> <to> <weight> <drawn>`: a refresh planned for tNs (steady clock), the
///     plan, and whether it was drawn (0: the plan of the refresh before, its picture kept); its picture the next one
///     of refreshes.rgba (the frame due when the plan has none);
///   - `end`.
/// - `frames.rgba`, `refreshes.rgba`: the pixels, RGBA, width * height * 4 bytes each.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "emulator/video/smoothmotion.h"

namespace smoothmotion
{

class TraceWriter
{
public:
    /// From the environment: UNREAL_SMOOTH_TRACE = the folder (made by the caller), UNREAL_SMOOTH_TRACE_SKIP =
    /// refreshes after `start` appears before the trace starts (default 0), UNREAL_SMOOTH_TRACE_REFRESHES = refreshes
    /// traced (default 600; held in memory, about 0.8 MB a refresh at 352 x 288)
    static bool FromEnvironment(TraceWriter& out)
    {
        const char* folder = std::getenv("UNREAL_SMOOTH_TRACE");
        if (!folder || !*folder)
            return false;
        out._folder = folder;
        if (const char* skip = std::getenv("UNREAL_SMOOTH_TRACE_SKIP"))
            out._skip = std::strtoll(skip, nullptr, 10);
        if (const char* count = std::getenv("UNREAL_SMOOTH_TRACE_REFRESHES"))
            out._count = std::strtoll(count, nullptr, 10);
        return true;
    }

    bool Active() const { return _active; }
    bool Done() const { return _done; }

    /// Before a refresh's plan: true when the trace starts now (the caller resets smooth motion first)
    bool StartsNow(Mode mode)
    {
        if (_done || _active || _folder.empty())
            return false;
        if (!_started)
        {
            std::FILE* start = std::fopen((_folder + "/start").c_str(), "r");
            if (!start)
                return false;
            std::fclose(start);
            _started = true;
        }
        if (_seen++ < _skip)
            return false;
        _active = true;
        _text = std::string("start ") + ModeName(mode) + "\n";
        return true;
    }

    void Frame(uint64_t serial, int64_t targetNs, const uint32_t* pixels, int width, int height)
    {
        if (!_active)
            return;
        char line[96];
        std::snprintf(line, sizeof(line), "frame %llu %lld %d %d\n", static_cast<unsigned long long>(serial),
                      static_cast<long long>(targetNs), width, height);
        _text += line;
        Reserve(width, height);
        _frames.insert(_frames.end(), pixels, pixels + static_cast<size_t>(width) * height);
    }

    void Refresh(int64_t tNs, const SmoothMotion::Result& plan, bool drawn, const uint32_t* pixels, int width,
                 int height)
    {
        if (!_active)
            return;
        char line[160];
        std::snprintf(line, sizeof(line), "refresh %lld %d %llu %llu %llu %d %d\n", static_cast<long long>(tNs),
                      static_cast<int>(plan.kind), static_cast<unsigned long long>(plan.serial),
                      static_cast<unsigned long long>(plan.from), static_cast<unsigned long long>(plan.to),
                      plan.weight, drawn ? 1 : 0);
        _text += line;
        Reserve(width, height);
        _refreshes.insert(_refreshes.end(), pixels, pixels + static_cast<size_t>(width) * height);
        if (++_traced >= _count)
        {
            _text += "end\n";
            Flush();
        }
    }

    /// Unfinished (the window goes): what there is, without `end`
    ~TraceWriter() { Flush(); }

private:
    /// All the memory at the start: a vector growing by doubling copied hundreds of MB on the render thread (refreshes
    /// 16, 64, 128, 257, 513 took 50-200 ms and showed as skipped refreshes). Frames: one per refresh at most while
    /// the display is not slower than the machine, plus some
    void Reserve(int width, int height)
    {
        if (_reserved)
            return;
        _reserved = true;
        const size_t pixels = static_cast<size_t>(width) * height;
        const size_t count = static_cast<size_t>(_count > 0 ? _count : 0);
        _refreshes.reserve(count * pixels);
        _frames.reserve((count + count / 4 + 16) * pixels);
        _text.reserve(count * 2 * 64);
    }

    /// Held in memory while it runs (a write per refresh would take the render thread's time) and written at the end;
    /// events.tsv last, so its `end` says the pixel files are complete
    void Flush()
    {
        if (!_active)
            return;
        _active = false;
        _done = true;
        const auto write = [this](const char* name, const void* data, size_t bytes) {
            if (std::FILE* f = std::fopen((_folder + "/" + name).c_str(), "wb"))
            {
                std::fwrite(data, 1, bytes, f);
                std::fclose(f);
            }
        };
        write("frames.rgba", _frames.data(), _frames.size() * sizeof(uint32_t));
        write("refreshes.rgba", _refreshes.data(), _refreshes.size() * sizeof(uint32_t));
        write("events.tsv", _text.data(), _text.size());
        _frames = {};
        _refreshes = {};
        _text = {};
    }

    std::string _folder;
    long long _skip = 0;
    long long _count = 600;
    long long _seen = 0;
    long long _traced = 0;
    bool _active = false;
    bool _done = false;
    bool _started = false;
    bool _reserved = false;
    std::string _text;
    std::vector<uint32_t> _frames;
    std::vector<uint32_t> _refreshes;
};

}  // namespace smoothmotion
