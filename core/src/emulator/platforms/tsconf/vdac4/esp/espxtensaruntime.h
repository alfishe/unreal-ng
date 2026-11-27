#pragma once

/// @file espxtensaruntime.h
/// @brief The VDAC4 firmware's app runtime on the Xtensa interpreter: IVdac4AppRuntime over EspAppHost.
///
/// Vdac4EspFirmware (the ESP's command dispatcher) calls the runtime synchronously: APP_START waits for main and
/// start, a command for app->command, as the firmware's loopTask does. Here the ESP runs in small steps until
/// that call is finished, the app's other tasks running alongside; Poll brings the ESP up to the firmware's time.
///
/// Jobs (design.md §7.5). While the app owns the board's FT812 (the job hooks say "hosted": the Tang runs the FT
/// core with the ESP as its host) Poll runs the ESP as a job: Advance up to the firmware's time, the board's
/// accesses all at the time the job was posted (its stamp), what the app hands the firmware (events, PCM, log
/// lines, ft_open) kept and handed over at the next Flush. A job runs on the runtime's worker thread when
/// threading is on, else at once on the caller's thread; either way the outputs wait for the next Flush, so the
/// machine sees the same thing in the same emulated moment: the worker only moves where the work is done.
/// Flush (waits for the job, then hands its outputs over) runs at the start of every Poll, Command, StartApp and
/// StopApp and at the board's frame end; WaitIdle (waits only) before the main thread touches what a job uses
/// (the Tang and its FT812). Outside hosted mode Poll runs Advance directly, outputs at once, as before.
///
/// What an app asks during a job is answered from the job's own inputs: core->event takes as many events as the
/// firmware's queue had room for at the post (the queue only drains meanwhile), core->reply and core->app_done
/// answer as outside a command (a job never runs inside one), core->link_media is taken (kZmOk) and applied at
/// the Flush.
///
/// Design: docs/inprogress/2026-11-15-tsconf-vdac4/design.md §7.5, §8.

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "emulator/platforms/tsconf/vdac4/vdac4appruntime.h"

namespace vdac4
{

class EspAppHost;

/// How a board lets the ESP's Advance run as a job (all optional)
struct EspJobHooks
{
    /// Main thread, no job in flight: the next Advance may run as a job (the app owns what it touches)
    std::function<bool()> hosted;
    /// Main thread, at the post: the time the job's board accesses happen at (opaque to the runtime)
    std::function<uint64_t()> stamp;
    /// The job's thread, before and after Advance (`job`: the job's index, counting from 0)
    std::function<void(uint64_t stamp)> begin;
    std::function<void(uint64_t stamp, uint64_t job)> end;
    /// Main thread, job `job` completed: the app still owns the board after it (its end state). Unset: false
    std::function<bool(uint64_t job)> hostedAfter;
    /// Main thread: a job may be in flight (true at the post, false once WaitIdle saw it done)
    std::function<void(bool outstanding)> outstanding;
};

/// Job counters since the runtime was made (any thread reads them)
struct EspJobStats
{
    uint64_t jobs = 0;          ///< Advances run as jobs
    uint64_t threadedJobs = 0;  ///< of them on the worker
    uint64_t waits = 0;         ///< WaitIdle calls that found the worker busy
    uint64_t waitNs = 0;        ///< host time the main thread waited for the worker
    uint64_t jobNs = 0;         ///< host time the worker ran jobs
};

class EspXtensaRuntime final : public IVdac4AppRuntime
{
public:
    /// ESP time a start() or a command may take before the call is given up (the board's watchdog is 5 s)
    static constexpr uint64_t kCallLimitUs = 5'000'000;
    /// core->event payload limit (the firmware's kMaxEventPayload)
    static constexpr size_t kMaxEventPayload = 63;

    /// `audio`: played PCM, interleaved int16 stereo at `rate`; `log`: app log lines; `mhz`: the ESP core clock
    /// ([VDAC4] EspMhz; one instruction per cycle, 0 = the board's 240 MHz)
    EspXtensaRuntime(std::function<void(const int16_t*, size_t, uint32_t)> audio,
                     std::function<void(const std::string&)> log, uint32_t mhz = 0);
    ~EspXtensaRuntime() override;

    int StartApp(const std::string& module, const std::vector<uint8_t>& elf, IVdac4AppHost& host,
                 Vdac4AppInfo& info) override;
    void StopApp() override;
    int Command(uint8_t command, const uint8_t* data, size_t size) override;
    void Poll(uint32_t nowMs) override;
    /// The app or a module it opened (main thread: after the jobs in flight)
    bool UsesModule(const std::string& name) const override;
    /// The host's state is not part of the firmware's (Vdac4Esp::Save): TTD keeps it apart, as the device
    /// vdac4-esp-cpu (ttdvdac4.h), restored after the firmware
    void SaveState(std::vector<uint8_t>& out) const override { out.clear(); }
    /// The app goes on: the host is restored apart (vdac4-esp-cpu)
    bool LoadState(const std::string& module, const Vdac4AppInfo& info, IVdac4AppHost& host,
                   const std::vector<uint8_t>& state) override;
    /// A restore: wait for a job in flight and drop what it and the jobs before it would hand over (the
    /// machine they would reach is restored apart). Main thread
    void DropJobs();

    EspAppHost& Host() { return *_host; }

    /// region <Jobs>
    void SetJobHooks(EspJobHooks hooks);
    /// Jobs on the worker thread (on) or on the caller's thread (off). Main thread; takes effect at the next post
    void SetThreaded(bool on);
    bool Threaded() const { return _threaded; }
    /// Outputs reach the firmware `passes` Polls after their job was posted (1..kMaxLatency): the worker may run
    /// that many emulated milliseconds behind. Main thread
    static constexpr uint32_t kMaxLatency = 16;
    void SetLatency(uint32_t passes);
    uint32_t Latency() const { return _latency; }
    /// The ESP's code through the JIT or the interpreter (the `esp_jit` feature); any thread, the next Run on
    void SetJit(bool on);
    /// Main thread: wait until no job runs (nothing handed over)
    void WaitIdle();
    /// Main thread: WaitIdle, then hand the finished jobs' outputs to the firmware
    void Flush();
    /// The calling thread runs a job now
    static bool InJob();
    EspJobStats Stats() const;
    /// endregion

private:
    static constexpr size_t kRing = 64;  // > 2 x kMaxLatency + 1: a slot is not reused while a job may read it

    struct Job
    {
        uint64_t us = 0;
        uint64_t stamp = 0;
        size_t firmwareRoom = 0;       ///< the firmware's event queue room at the post
        uint64_t firstUnreplayed = 0;  ///< the oldest job not handed over at the post
    };

    void RunJob(uint64_t index);
    void Post(const Job& job);
    void WorkerLoop();
    void StopWorker();
    void WaitFor(uint64_t completed);
    void ReplayUpTo(uint64_t count);
    void SetOutstanding(bool on);
    void Defer(std::function<void()> output) { _current->push_back(std::move(output)); }

    std::unique_ptr<EspAppHost> _host;
    IVdac4AppHost* _appHost = nullptr;
    Vdac4AppInfo* _info = nullptr;
    std::function<void(const std::string&)> _log;
    std::function<void(const int16_t*, size_t, uint32_t)> _audio;

    EspJobHooks _hooks;
    bool _threaded = false;
    uint32_t _latency = 1;
    bool _outstanding = false;

    // Main thread: jobs posted and handed over
    uint64_t _posted = 0;
    uint64_t _replayed = 0;
    // The job's thread, while it runs
    bool _deferring = false;
    size_t _eventRoom = 0;
    uint32_t _currentEvents = 0;
    std::vector<std::function<void()>>* _current = nullptr;
    // Per job (index % kRing): written by its thread, read after it completed
    std::array<Job, kRing> _jobRing{};
    std::array<std::vector<std::function<void()>>, kRing> _outputs{};
    std::array<uint32_t, kRing> _eventsUsed{};

    std::thread _worker;
    std::mutex _lock;
    std::condition_variable _wake;  ///< a job was posted, or quit
    std::condition_variable _done;  ///< the worker finished a job
    std::atomic<uint64_t> _postedShared{0};
    std::atomic<uint64_t> _completed{0};
    bool _quit = false;

    std::atomic<uint64_t> _jobs{0}, _threadedJobs{0}, _waits{0}, _waitNs{0}, _jobNs{0};
};

}  // namespace vdac4
