#include "gpupasstimes.h"

#include <QDebug>
#include <QOpenGLContext>
#include <algorithm>

namespace
{
constexpr GLenum TIMESTAMP = 0x8E28;               // GL_TIMESTAMP
constexpr GLenum QUERY_RESULT = 0x8866;            // GL_QUERY_RESULT
constexpr GLenum QUERY_RESULT_AVAILABLE = 0x8867;  // GL_QUERY_RESULT_AVAILABLE

const char* PassName(GpuPassTimes::Pass pass)
{
    return pass == GpuPassTimes::Smooth ? "smooth motion" : "picture";
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
    std::lock_guard<std::mutex> lock(_mutex);
    for (PassState& state : _passes)
    {
        for (Slot& slot : state.queries)
        {
            _gen(1, &slot.begin);
            _gen(1, &slot.end);
            slot.pending = false;
        }
        state.next = 0;
        state.current = nullptr;
        state.recent.assign(WINDOW, 0.0);
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
                _delete(1, &slot.begin);
                _delete(1, &slot.end);
            }
            slot = {};
        }
        state.current = nullptr;
        // Lost with the context (a GPU reset): what the pass took until then, and how many were never finished
        if (contextLost)
            qWarning().nospace() << "GpuPassTimes: " << PassName(static_cast<Pass>(p)) << " pass at the loss: "
                                 << pending << " unfinished, " << state.measured << " measured, the longest "
                                 << state.longest << " ms";
    }
    _ready = false;
}

void GpuPassTimes::Begin(Pass pass)
{
    if (!_ready)
        return;
    PassState& state = _passes[pass];
    Slot& slot = state.queries[state.next];
    // Every slot still waits for the GPU: this one goes unmeasured
    if (slot.pending)
        return;
    _counter(slot.begin, TIMESTAMP);
    state.current = &slot;
}

void GpuPassTimes::End(Pass pass)
{
    if (!_ready)
        return;
    PassState& state = _passes[pass];
    if (!state.current)
        return;
    _counter(state.current->end, TIMESTAMP);
    state.current->pending = true;
    state.current = nullptr;
    state.next = (state.next + 1) % SLOTS;
}

void GpuPassTimes::Collect()
{
    if (!_ready)
        return;
    for (size_t p = 0; p < PassCount; ++p)
    {
        PassState& state = _passes[p];
        // Oldest first: the GPU finishes them in order
        for (size_t i = 0; i < SLOTS; ++i)
        {
            Slot& slot = state.queries[(state.next + i) % SLOTS];
            if (!slot.pending)
                continue;
            GLint available = 0;
            _available(slot.end, QUERY_RESULT_AVAILABLE, &available);
            if (!available)
                break;
            GLuint64 begin = 0;
            GLuint64 end = 0;
            _result(slot.begin, QUERY_RESULT, &begin);
            _result(slot.end, QUERY_RESULT, &end);
            slot.pending = false;
            const double ms = end > begin ? static_cast<double>(end - begin) / 1e6 : 0.0;
            if (ms >= SLOW_MS)
                qWarning().nospace() << "GpuPassTimes: the " << PassName(static_cast<Pass>(p)) << " pass took " << ms
                                     << " ms on the GPU";
            std::lock_guard<std::mutex> lock(_mutex);
            state.recent[state.recentNext] = ms;
            state.recentNext = (state.recentNext + 1) % WINDOW;
            state.longest = std::max(state.longest, ms);
            state.measured++;
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
        const size_t count = static_cast<size_t>(std::min<uint64_t>(state.measured, WINDOW));
        recent.assign(state.recent.begin(), state.recent.begin() + static_cast<std::ptrdiff_t>(count));
    }
    if (recent.empty())
        return out;
    std::sort(recent.begin(), recent.end());
    out.p50Ms = recent[recent.size() / 2];
    out.maxMs = recent.back();
    return out;
}
