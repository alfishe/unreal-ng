#include "temporaleffects.h"

#include <algorithm>
#include <chrono>

#include "common/threadhelper.h"
#include "zxdlss/algorithm.h"

namespace
{
/// Frames the worker may be behind before the algorithm is restarted: the
/// present queue shows raw frames for the gap instead of an ever-growing lag
constexpr size_t kMaxQueuedJobs = 3;

/// The paper (256 x 192) inside the frames plane B describes: standard ZX
/// 352 x 288 and Pentagon overscan 384 x 304. Other sizes are not ZX screens.
bool PaperOrigin(int width, int height, int& paperX, int& paperY)
{
    if (width == 352 && height == 288)
    {
        paperX = 48;
        paperY = 48;
        return true;
    }
    if (width == 384 && height == 304)
    {
        paperX = 48;
        paperY = 56;
        return true;
    }
    return false;
}
}  // namespace

TemporalEffects::TemporalEffects(WriteBack writeBack) : _writeBack(std::move(writeBack))
{
    _worker = std::thread(&TemporalEffects::WorkerLoop, this);
}

TemporalEffects::~TemporalEffects()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stop = true;
        _queue.clear();
    }
    _wake.notify_all();
    if (_worker.joinable())
        _worker.join();
}

bool TemporalEffects::SetAlgorithm(const std::string& name)
{
    int delay = 0;
    if (!name.empty())
    {
        // The delay is a property of the algorithm, not of the frames: ask an instance
        std::unique_ptr<zxdlss::Algorithm> probe = zxdlss::createAlgorithm(name);
        if (!probe)
            return false;
        delay = probe->delay();
    }
    std::lock_guard<std::mutex> lock(_mutex);
    if (name == _algorithmName)
        return true;
    _algorithmName = name;
    _algorithmDelay = delay;
    _queue.clear();
    _generation++;
    _stats = Stats{};
    return true;
}

std::string TemporalEffects::GetAlgorithm() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _algorithmName;
}

int TemporalEffects::Submit(uint64_t serial, const uint16_t* planeB, int width, int height,
                            const uint32_t* palette)
{
    int paperX = 0;
    int paperY = 0;
    int videoDelay = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _active = false;
        if (_algorithmName.empty())
            _inactiveReason.clear();
        else if (!palette)
            _inactiveReason = "no palette";
        else if (!planeB)
            _inactiveReason = "no plane B (needs the zxdlss and screenhq features and a ZX screen mode)";
        else if (!PaperOrigin(width, height, paperX, paperY))
            _inactiveReason = "frame " + std::to_string(width) + "x" + std::to_string(height) + " is not a ZX screen";
        else
        {
            _active = true;
            _inactiveReason.clear();
            if (_queue.size() >= kMaxQueuedJobs)
            {
                // The worker cannot keep up: restart rather than fall further behind
                _queue.clear();
                _generation++;
                _stats.restarts++;
            }
            Job job;
            job.serial = serial;
            job.generation = _generation;
            job.width = width;
            job.height = height;
            job.paperX = paperX;
            job.paperY = paperY;
            if (!_spare.empty())
            {
                job.planeB = std::move(_spare.back());
                _spare.pop_back();
            }
            job.planeB.assign(planeB, planeB + static_cast<size_t>(width) * height);
            std::copy(palette, palette + 16, job.palette.begin());
            _queue.push_back(std::move(job));
            videoDelay = _algorithmDelay + 1;
        }
    }
    _videoDelay.store(videoDelay, std::memory_order_release);
    if (videoDelay)
        _wake.notify_one();
    return videoDelay;
}

void TemporalEffects::Reset()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_algorithmName.empty())
        return;
    _queue.clear();
    _generation++;
    _stats.restarts++;
}

TemporalEffects::Stats TemporalEffects::GetStats() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    Stats s = _stats;
    s.algorithm = _algorithmName;
    s.active = _active;
    s.inactiveReason = _inactiveReason;
    s.videoDelayFrames = _videoDelay.load(std::memory_order_acquire);
    return s;
}

void TemporalEffects::WorkerLoop()
{
    // The picture waits for this thread: the UI's priority (never real-time,
    // the emulation and audio threads stay above it)
    ThreadHelper::setThreadName("temporal-effects");
    ThreadHelper::setInteractivePriority();
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _wake.wait(lock, [this] { return _stop || !_queue.empty(); });
            if (_stop)
                return;
            job = std::move(_queue.front());
            _queue.pop_front();
            if (job.generation != _generation)
                continue;
            // The name the job was queued under (SetAlgorithm bumps the generation)
            if (_workerName != _algorithmName || _workerGeneration != job.generation)
            {
                _workerName = _algorithmName;
                _algorithm.reset();
            }
        }
        Process(job);
        std::lock_guard<std::mutex> lock(_mutex);
        if (_spare.size() < kMaxQueuedJobs + 1)
            _spare.push_back(std::move(job.planeB));
    }
}

void TemporalEffects::Process(Job& job)
{
    const auto start = std::chrono::steady_clock::now();

    if (!_algorithm || _workerGeneration != job.generation || job.width != _workerWidth ||
        job.height != _workerHeight || job.paperX != _workerPaperX || job.paperY != _workerPaperY)
    {
        _algorithm = zxdlss::createAlgorithm(_workerName);
        _workerGeneration = job.generation;
        _workerWidth = job.width;
        _workerHeight = job.height;
        _workerPaperX = job.paperX;
        _workerPaperY = job.paperY;
        _pushed = 0;
        _serials.clear();
        if (!_algorithm)
            return;
    }

    const size_t pixels = static_cast<size_t>(job.width) * job.height;
    zxdlss::decodePlaneB(job.planeB.data(), pixels, _plane, _attr, _ink);
    zxdlss::FrameInput in;
    in.width = job.width;
    in.height = job.height;
    in.plane = _plane.data();
    in.attr = _attr.data();
    in.ink = _ink.data();
    in.paperX = job.paperX;
    in.paperY = job.paperY;
    in.palette = job.palette.data();
    _algorithm->process(in, _rgb);
    _pushed++;

    // The output is frame (pushed - delay): the serial pushed delay frames ago
    const size_t delay = static_cast<size_t>(_algorithm->delay());
    _serials.push_back(job.serial);
    bool output = false;
    WriteResult result = WriteResult::Gone;
    const zxdlss::FrameReport report = _algorithm->lastFrame();
    if (_serials.size() > delay)
    {
        const uint64_t outSerial = _serials.front();
        _serials.erase(_serials.begin());
        result = _writeBack(outSerial, _rgb.data(), job.width, job.height, report);
        output = true;
    }

    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::lock_guard<std::mutex> lock(_mutex);
    _stats.processed++;
    if (output)
    {
        _stats.written += result == WriteResult::Written ? 1 : 0;
        _stats.shownRaw += result == WriteResult::WrittenAfterShown ? 1 : 0;
        _stats.late += result == WriteResult::Gone ? 1 : 0;
        _stats.correctedFrames += zxdlss::corrected(report) ? 1 : 0;
        _stats.lastFrame = report;
    }
    _stats.lastMs = ms;
    _stats.averageMs = _stats.processed == 1 ? ms : _stats.averageMs + 0.05 * (ms - _stats.averageMs);
}
