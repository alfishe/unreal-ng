#pragma once

/// @file vdac4ftworker.h
/// @brief The VDAC4 Tang and its FT812 core on a thread of their own while the ESP app drives them.
///
/// An ESP job (espxtensaruntime.h) talks to the Tang over the LINK: almost only writes (display lists, the
/// coprocessor FIFO, FT_DATA words), all at the job's stamp, and at its end the chip is drawn up to that stamp.
/// Threaded, those go into a queue the FT thread applies in order: LinkWrite at the op's stamp, Synchronize at
/// the job's end. A LINK read waits until the queue is applied, then reads on the caller's thread (the FT thread
/// is idle then). Not threaded, the same ops are applied at once on the caller's thread. Either way the Tang sees
/// the same accesses at the same times in the same order: the thread only moves where the chip's work is done.
///
/// Threads: one producer (the thread running the ESP job), the FT thread, and the owner's thread, which calls
/// WaitIdle before it touches the Tang (no job of the ESP runs then, so nothing is queued meanwhile).

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class Vdac4Tang;

class Vdac4FtWorker
{
public:
    explicit Vdac4FtWorker(Vdac4Tang& tang);
    ~Vdac4FtWorker();
    Vdac4FtWorker(const Vdac4FtWorker&) = delete;
    Vdac4FtWorker& operator=(const Vdac4FtWorker&) = delete;

    /// Ops on the FT thread (on) or at once on the producer's thread (off). Owner's thread, nothing queued
    void SetThreaded(bool on);
    bool Threaded() const { return _threaded; }

    /// region <Producer: the thread running the ESP job>
    void Write(uint64_t stamp, uint32_t address, uint32_t value);
    /// Wait for the queue, then the read at `stamp`
    uint32_t Read(uint64_t stamp, uint32_t address);
    /// The job ended: the chip drawn up to `stamp`; `job` (the runtime's job index) is done once applied, with
    /// `hosted` set to whether the ESP still drives the FT812 then (JobHosted)
    void EndJob(uint64_t stamp, uint64_t job);
    /// endregion

    /// The ops of job `job` are applied: whether the ESP drove the FT812 after it (owner's thread; waits)
    bool JobHosted(uint64_t job);
    /// Everything queued applied (any thread but the FT thread)
    void WaitIdle();

    /// Host time the FT thread applied ops, the producer waited for room or a read, the owner waited
    struct Stats
    {
        uint64_t ops = 0;
        uint64_t reads = 0;
        uint64_t applyNs = 0;
        uint64_t producerWaitNs = 0;
        uint64_t ownerWaits = 0;
        uint64_t ownerWaitNs = 0;
    };
    Stats GetStats() const;

private:
    static constexpr uint32_t kSync = 0xFFFF'FFFFu;  // op address: end of a job
    static constexpr size_t kRingOps = 1u << 16;      // 1 MB
    static constexpr size_t kJobRing = 64;
    static constexpr size_t kPublishEvery = 256;

    struct Op
    {
        uint64_t stamp;
        uint32_t address;
        uint32_t value;  ///< kSync: the job index (low 32 bits)
    };

    void Apply(const Op& op);
    void Push(const Op& op);
    void Publish();
    void WaitApplied(uint64_t count);
    void WorkerLoop();
    void StopWorker();

    Vdac4Tang& _tang;
    bool _threaded = false;

    std::unique_ptr<Op[]> _ring;
    uint64_t _head = 0;                     ///< producer: ops pushed
    std::atomic<uint64_t> _published{0};    ///< ops the FT thread may take
    std::atomic<uint64_t> _applied{0};      ///< ops the FT thread finished
    std::atomic<uint64_t> _jobsDone{0};     ///< jobs whose end op was applied
    std::array<bool, kJobRing> _hosted{};

    std::thread _worker;
    std::mutex _lock;
    std::condition_variable _wake;     ///< ops published, or quit
    std::condition_variable _drained;  ///< the FT thread applied what was published
    std::atomic<bool> _sleeping{false};
    std::atomic<int> _waiting{0};      ///< threads blocked on _drained
    bool _quit = false;

    std::atomic<uint64_t> _applyNs{0}, _reads{0}, _producerWaitNs{0}, _ownerWaits{0}, _ownerWaitNs{0};
};
