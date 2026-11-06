#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

#include <qopengl.h>

class QOpenGLContext;

/// How long the GPU takes for each of the screen window's passes, from GL timestamps (GL 3.3 / ARB_timer_query):
/// smooth motion's steps and the picture (the CRT composition or a preset). Read back frames later, never waited for,
/// so measuring stalls nothing. Kept for the GPU resets the system does after a pass runs too long (TDR, ~2 s on
/// Windows): the times before one show which pass grew, and a pass over SLOW_MS is logged at once
class GpuPassTimes
{
public:
    enum Pass
    {
        Smooth,
        Picture,
        PassCount
    };

    struct Times
    {
        double p50Ms = 0.0;      ///< over the last WINDOW measurements
        double maxMs = 0.0;      ///< ... the longest of them
        double longestMs = 0.0;  ///< since the context was made
        uint64_t measured = 0;
    };

    static constexpr double SLOW_MS = 100.0;
    static constexpr size_t WINDOW = 600;

    /// The queries in the current context; false (and nothing measured) without timer queries
    bool Init(QOpenGLContext* context);
    /// The current context's queries deleted (current, or lost: then only forgotten)
    void Release(bool contextLost);
    bool Ready() const { return _ready; }

    /// The pass starts / ends in the GL stream (context current). A pass whose earlier slots all wait is skipped
    void Begin(Pass pass);
    void End(Pass pass);
    /// The finished measurements read (context current), never waiting
    void Collect();

    Times Get(Pass pass) const;

private:
    static constexpr size_t SLOTS = 8;
    struct Slot
    {
        GLuint begin = 0;
        GLuint end = 0;
        bool pending = false;
    };
    struct PassState
    {
        std::array<Slot, SLOTS> queries{};
        size_t next = 0;
        Slot* current = nullptr;
        std::vector<double> recent;  ///< a ring of WINDOW
        size_t recentNext = 0;
        double longest = 0.0;
        uint64_t measured = 0;
    };

    using GenQueries = void(QOPENGLF_APIENTRYP)(GLsizei, GLuint*);
    using DeleteQueries = void(QOPENGLF_APIENTRYP)(GLsizei, const GLuint*);
    using QueryCounter = void(QOPENGLF_APIENTRYP)(GLuint, GLenum);
    using GetQueryObjectiv = void(QOPENGLF_APIENTRYP)(GLuint, GLenum, GLint*);
    using GetQueryObjectui64v = void(QOPENGLF_APIENTRYP)(GLuint, GLenum, GLuint64*);

    bool _ready = false;
    GenQueries _gen = nullptr;
    DeleteQueries _delete = nullptr;
    QueryCounter _counter = nullptr;
    GetQueryObjectiv _available = nullptr;
    GetQueryObjectui64v _result = nullptr;
    std::array<PassState, PassCount> _passes{};
    mutable std::mutex _mutex;  ///< the times: read by the WebAPI's report (presentStats), written on the GL thread
};
