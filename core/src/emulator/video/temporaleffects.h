#pragma once

/// @file temporaleffects.h
/// @brief Temporal effects in the core: a de-flicker algorithm (ZX DLSS,
/// core/src/emulator/video/zxdlss) run on every emulated frame, before
/// presentation, so every consumer of the present queue sees its result.
///
/// Design: docs/inprogress/2026-09-27-zxdlss-gigascreen/temporal-effects-manager.md
/// (this is its first step: the `dlss` mode; the GUI blend stays in unreal-qt).
///
/// Pipeline. At each frame end the emulation thread hands the frame's plane B
/// to a worker thread (Submit) and goes on emulating. The worker runs the
/// algorithm, whose output is frame (submitted - delay), and writes it back into
/// the present queue slot that holds that frame (the WriteBack callback). The
/// frame is shown delay + 1 frames after it was emulated (VideoDelayFrames):
/// the algorithm's look-ahead plus one frame for the worker to finish. The
/// caller delays the audio to match.
///
/// Every frame must reach the algorithm in order (its look-ahead and history
/// are frame sequences): a gap - a seek, a mode or size change, a queue that
/// overflows because the worker cannot keep up - restarts it (Reset).

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "emulator/video/zxdlss/algorithm.h"

class TemporalEffects
{
public:
    /// What became of an output written back into the present queue
    enum class WriteResult
    {
        Written,          ///< in its slot before the frame was shown
        WrittenAfterShown,///< in its slot, but the frame had already been shown raw
        Gone,             ///< the slot holds another frame now (far too late)
    };

    /// Write the output of the frame latched with `serial` into its present slot:
    /// RGB8, width x height. Called on the worker thread.
    /// `report`: what the algorithm did to this frame (kept with the slot: the
    /// details shown describe the frame on screen)
    using WriteBack = std::function<WriteResult(uint64_t serial, const uint8_t* rgb, int width, int height,
                                                const zxdlss::FrameReport& report)>;

    struct Stats
    {
        std::string algorithm;       ///< selected algorithm ("" = off)
        bool active = false;         ///< processing frames now
        std::string inactiveReason;  ///< why a selected algorithm is not active
        int videoDelayFrames = 0;    ///< frames the output trails emulation (0 while inactive)
        uint64_t processed = 0;      ///< frames the algorithm processed
        uint64_t written = 0;        ///< outputs written into their present slot in time
        uint64_t shownRaw = 0;       ///< frames shown raw: their output came after they were on screen
        uint64_t late = 0;           ///< outputs whose slot had already been reused (far too late)
        uint64_t correctedFrames = 0;   ///< outputs in which the algorithm changed something
        bool showingProcessed = false;  ///< the frame on screen now is the algorithm's (filled by Screen)
        bool correcting = false;        ///< ... and it changed something in it (the dialog's LED; filled by Screen)
        zxdlss::FrameReport shownFrame; ///< what the algorithm did to the frame on screen now (filled by Screen)
        zxdlss::FrameReport lastFrame;  ///< what the algorithm did to its last output frame (delay frames ahead)
        uint64_t restarts = 0;       ///< algorithm restarts (seek, size change, overflow)
        double lastMs = 0.0;         ///< last frame's processing time
        double averageMs = 0.0;      ///< running average (EMA) of the processing time
    };

    explicit TemporalEffects(WriteBack writeBack);
    ~TemporalEffects();

    TemporalEffects(const TemporalEffects&) = delete;
    TemporalEffects& operator=(const TemporalEffects&) = delete;

    /// Select the algorithm by registry name (zxdlss::algorithmNames()); "" = off.
    /// Any thread. Returns false (and keeps the previous choice) for an unknown name.
    bool SetAlgorithm(const std::string& name);
    std::string GetAlgorithm() const;

    /// Emulation thread, once per latched frame: hand the frame to the worker,
    /// with the 16 ZX colors the emulator draws it in now (RGBA8888, 0xAABBGGRR -
    /// Screen::GetRGBAPalette16; the algorithm mixes in these).
    /// planeB == nullptr (plane B off) or an unsupported frame size makes the
    /// effect inactive. Returns the video delay the present queue must apply
    /// (0 while inactive).
    int Submit(uint64_t serial, const uint16_t* planeB, int width, int height, const uint32_t* palette);

    /// Emulation thread: the frame sequence broke (seek, reset): restart the algorithm.
    void Reset();

    /// Frames the output trails the emulated frame while active (delay + 1).
    int VideoDelayFrames() const { return _videoDelay.load(std::memory_order_acquire); }

    Stats GetStats() const;

private:
    struct Job
    {
        uint64_t serial = 0;
        uint64_t generation = 0;
        int width = 0;
        int height = 0;
        int paperX = 0;
        int paperY = 0;
        std::vector<uint16_t> planeB;
        std::array<uint32_t, 16> palette{};
    };

    void WorkerLoop();
    void Process(Job& job);

    WriteBack _writeBack;

    mutable std::mutex _mutex;          // guards everything below up to the worker state
    std::condition_variable _wake;
    std::deque<Job> _queue;
    std::vector<std::vector<uint16_t>> _spare;  // recycled plane B buffers
    std::string _algorithmName;
    int _algorithmDelay = 0;
    uint64_t _generation = 0;           // bumped by every restart; stale jobs are dropped
    bool _stop = false;
    std::string _inactiveReason;
    bool _active = false;
    Stats _stats;

    std::atomic<int> _videoDelay{0};

    // Worker state (worker thread only)
    std::unique_ptr<zxdlss::Algorithm> _algorithm;
    std::string _workerName;
    uint64_t _workerGeneration = ~0ull;
    int _workerWidth = 0;
    int _workerHeight = 0;
    int _workerPaperX = 0;
    int _workerPaperY = 0;
    uint64_t _pushed = 0;               // frames pushed into the current instance
    std::vector<uint64_t> _serials;     // serial of each pushed frame, newest last (delay + 1 kept)
    std::vector<uint8_t> _plane, _attr, _ink, _rgb;

    std::thread _worker;
};
