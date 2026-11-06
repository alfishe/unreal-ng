#include "gpupasstimes.h"

#include <QDebug>
#include <QOpenGLContext>
#include <algorithm>

namespace
{
constexpr GLenum kTimestamp = 0x8E28;             // GL_TIMESTAMP
constexpr GLenum kQueryResult = 0x8866;           // GL_QUERY_RESULT
constexpr GLenum kQueryResultAvailable = 0x8867;  // GL_QUERY_RESULT_AVAILABLE

const char* PassName(GpuPassTimes::Pass pass)
{
    switch (pass)
    {
        case GpuPassTimes::Smooth:
            return "smooth motion";
        case GpuPassTimes::SmoothCpu:
            return "smooth motion (CPU)";
        default:
            return "picture";
    }
}
}  // namespace

bool GpuPassTimes::Init(QOpenGLContext* context)
{
    if (_ready)
        return true;
    if (!context || context->isOpenGLES())
        return false;
    const QSurfaceFormat format = context->format();
    if (format.version() < qMakePair(3, 3) && !context->hasExtension("GL_ARB_timer_query"))
        return false;
    _gen = reinterpret_cast<GenQueries>(context->getProcAddress("glGenQueries"));
    _delete = reinterpret_cast<DeleteQueries>(context->getProcAddress("glDeleteQueries"));
    _counter = reinterpret_cast<QueryCounter>(context->getProcAddress("glQueryCounter"));
    _available = reinterpret_cast<GetQueryObjectiv>(context->getProcAddress("glGetQueryObjectiv"));
    _result = reinterpret_cast<GetQueryObjectui64v>(context->getProcAddress("glGetQueryObjectui64v"));
    if (!_gen || !_delete || !_counter || !_available || !_result)
        return false;
    // A GL that has the calls but no clock behind them (Apple's GL over Metal reads 0 for every timestamp) measures
    // nothing: not ready, rather than zeros
    using GetInteger64v = void(QOPENGLF_APIENTRYP)(GLenum, GLint64*);
    const auto now = reinterpret_cast<GetInteger64v>(context->getProcAddress("glGetInteger64v"));
    GLint64 clock = 0;
    if (now)
        now(kTimestamp, &clock);
    if (clock == 0)
        return false;
    std::lock_guard<std::mutex> lock(_mutex);
    for (PassState& state : _passes)
    {
        for (Slot& slot : state.queries)
        {
            _gen(static_cast<GLsizei>(kSegments), slot.begin.data());
            _gen(static_cast<GLsizei>(kSegments), slot.end.data());
            slot.segments = 0;
            slot.pending = false;
        }
        state.next = 0;
        state.current = nullptr;
        state.open = state.skipped = state.inSegment = false;
        state.recent.assign(kWindow, 0.0);
        state.recentNext = 0;
        state.longest = 0.0;
        state.measured = 0;
    }
    _ready = true;
    return true;
}

void GpuPassTimes::Release(bool contextLost)
{
    if (!_ready)
        return;
    for (size_t p = 0; p < PassCount; ++p)
    {
        PassState& state = _passes[p];
        size_t pending = 0;
        for (Slot& slot : state.queries)
        {
            pending += slot.pending ? 1 : 0;
            if (!contextLost)
            {
                _delete(static_cast<GLsizei>(kSegments), slot.begin.data());
                _delete(static_cast<GLsizei>(kSegments), slot.end.data());
            }
            slot = {};
        }
        state.current = nullptr;
        state.open = state.skipped = state.inSegment = false;
        // Lost with the context (a GPU reset): what the pass took until then, and how many were never finished
        if (contextLost)
            qWarning().nospace() << "GpuPassTimes: " << PassName(static_cast<Pass>(p)) << " pass at the loss: "
                                 << pending << " unfinished, " << state.measured << " measured, the longest "
                                 << state.longest << " ms";
    }
    _ready = false;
}

bool GpuPassTimes::Take(PassState& state)
{
    Slot& slot = state.queries[state.next];
    // Every slot still waits for the GPU: this one goes unmeasured
    if (slot.pending)
        return false;
    slot.segments = 0;
    state.current = &slot;
    return true;
}

void GpuPassTimes::Finish(PassState& state)
{
    if (!state.current)
        return;
    if (state.current->segments > 0)
    {
        state.current->pending = true;
        state.next = (state.next + 1) % kSlots;
    }
    state.current = nullptr;
}

void GpuPassTimes::Open(Pass pass)
{
    if (!_ready || pass == SmoothCpu)
        return;
    PassState& state = _passes[pass];
    if (state.open)
        Close(pass);
    state.open = true;
    state.skipped = !Take(state);
}

void GpuPassTimes::Close(Pass pass)
{
    if (!_ready || pass == SmoothCpu)
        return;
    PassState& state = _passes[pass];
    if (state.inSegment)
        End(pass);
    state.open = false;
    state.skipped = false;
    Finish(state);
}

void GpuPassTimes::Begin(Pass pass)
{
    if (!_ready || pass == SmoothCpu)
        return;
    PassState& state = _passes[pass];
    if (state.inSegment)
        return;
    if (!state.open && !Take(state))
        return;
    Slot* slot = state.current;
    if (!slot || slot->segments >= kSegments)
        return;
    _counter(slot->begin[slot->segments], kTimestamp);
    state.inSegment = true;
}

void GpuPassTimes::End(Pass pass)
{
    if (!_ready || pass == SmoothCpu)
        return;
    PassState& state = _passes[pass];
    if (!state.inSegment || !state.current)
        return;
    Slot& slot = *state.current;
    _counter(slot.end[slot.segments], kTimestamp);
    slot.segments++;
    state.inSegment = false;
    if (!state.open)
        Finish(state);
}

void GpuPassTimes::Add(Pass pass, PassState& state, double ms)
{
    if (ms >= kSlowMs)
        qWarning().nospace() << "GpuPassTimes: the " << PassName(pass) << " pass took " << ms << " ms"
                             << (pass == SmoothCpu ? "" : " on the GPU");
    std::lock_guard<std::mutex> lock(_mutex);
    if (state.recent.size() != kWindow)
        state.recent.assign(kWindow, 0.0);
    state.recent[state.recentNext] = ms;
    state.recentNext = (state.recentNext + 1) % kWindow;
    state.longest = std::max(state.longest, ms);
    state.measured++;
}

void GpuPassTimes::Record(Pass pass, double ms)
{
    Add(pass, _passes[pass], ms);
}

void GpuPassTimes::Collect()
{
    if (!_ready)
        return;
    for (size_t p = 0; p < PassCount; ++p)
    {
        PassState& state = _passes[p];
        // Oldest first: the GPU finishes them in order
        for (size_t i = 0; i < kSlots; ++i)
        {
            Slot& slot = state.queries[(state.next + i) % kSlots];
            if (!slot.pending)
                continue;
            GLint available = 0;
            _available(slot.end[slot.segments - 1], kQueryResultAvailable, &available);
            if (!available)
                break;
            double ms = 0.0;
            for (size_t s = 0; s < slot.segments; ++s)
            {
                GLuint64 begin = 0;
                GLuint64 end = 0;
                _result(slot.begin[s], kQueryResult, &begin);
                _result(slot.end[s], kQueryResult, &end);
                ms += end > begin ? static_cast<double>(end - begin) / 1e6 : 0.0;
            }
            slot.pending = false;
            Add(static_cast<Pass>(p), state, ms);
        }
    }
}

GpuPassTimes::Times GpuPassTimes::Get(Pass pass) const
{
    Times out;
    std::vector<double> recent;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const PassState& state = _passes[pass];
        out.longestMs = state.longest;
        out.measured = state.measured;
        const size_t count = static_cast<size_t>(std::min<uint64_t>(state.measured, kWindow));
        recent.assign(state.recent.begin(), state.recent.begin() + static_cast<std::ptrdiff_t>(count));
    }
    if (recent.empty())
        return out;
    std::sort(recent.begin(), recent.end());
    out.p50Ms = recent[recent.size() / 2];
    out.maxMs = recent.back();
    return out;
}
