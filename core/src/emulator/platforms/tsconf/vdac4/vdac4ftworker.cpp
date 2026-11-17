#include "stdafx.h"

#include "vdac4ftworker.h"

#include <chrono>

#include "vdac4tang.h"

namespace
{

uint64_t SteadyNs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

/// Spin a little, then block on `cv` until `ready` (waiters counted in `waiting`, so the other side notifies
/// only when someone sleeps)
template <typename Ready>
void WaitUntil(std::mutex& lock, std::condition_variable& cv, std::atomic<int>& waiting, Ready ready)
{
    constexpr uint64_t kSpinNs = 100'000;
    const uint64_t started = SteadyNs();
    while (!ready())
    {
        if (SteadyNs() - started < kSpinNs)
        {
            std::this_thread::yield();
            continue;
        }
        std::unique_lock<std::mutex> guard(lock);
        waiting.fetch_add(1, std::memory_order_seq_cst);
        cv.wait(guard, ready);
        waiting.fetch_sub(1, std::memory_order_seq_cst);
        return;
    }
}

}  // namespace

Vdac4FtWorker::Vdac4FtWorker(Vdac4Tang& tang) : _tang(tang), _ring(new Op[kRingOps]) {}

Vdac4FtWorker::~Vdac4FtWorker()
{
    StopWorker();
}

void Vdac4FtWorker::SetThreaded(bool on)
{
    WaitIdle();
    _threaded = on;
    if (!on)
        StopWorker();
}

void Vdac4FtWorker::Apply(const Op& op)
{
    _tang.PinNow(op.stamp);
    if (op.address != kSync)
    {
        _tang.LinkWrite(op.address, op.value);
        return;
    }
    // The job's end: the chip drawn up to its time here, not later on the owner's thread
    _tang.Synchronize();
    _hosted[op.value % kJobRing] = _tang.GetPersonality() == Vdac4Tang::Personality::Ft && _tang.IsEspHost();
    _tang.PinNow(EveHostCard::kNotPinned);
    _jobsDone.store(static_cast<uint64_t>(op.value) + 1, std::memory_order_seq_cst);
}

void Vdac4FtWorker::Write(uint64_t stamp, uint32_t address, uint32_t value)
{
    if (!_threaded)
    {
        Apply({stamp, address, value});
        return;
    }
    Push({stamp, address, value});
}

uint32_t Vdac4FtWorker::Read(uint64_t stamp, uint32_t address)
{
    _reads.fetch_add(1, std::memory_order_relaxed);
    if (_threaded)
    {
        Publish();
        const uint64_t started = SteadyNs();
        WaitApplied(_head);
        _producerWaitNs.fetch_add(SteadyNs() - started, std::memory_order_relaxed);
    }
    // The FT thread is idle: the read on this thread, at the job's time
    _tang.PinNow(stamp);
    return _tang.LinkRead(address);
}

void Vdac4FtWorker::EndJob(uint64_t stamp, uint64_t job)
{
    const Op op{stamp, kSync, static_cast<uint32_t>(job)};
    if (!_threaded)
    {
        Apply(op);
        return;
    }
    Push(op);
    Publish();
}

bool Vdac4FtWorker::JobHosted(uint64_t job)
{
    if (_threaded)
    {
        const uint64_t done = (job & 0xFFFF'FFFFu) + 1;
        auto ready = [&]() { return _jobsDone.load(std::memory_order_seq_cst) >= done; };
        if (!ready())
        {
            const uint64_t started = SteadyNs();
            WaitUntil(_lock, _drained, _waiting, ready);
            _ownerWaits.fetch_add(1, std::memory_order_relaxed);
            _ownerWaitNs.fetch_add(SteadyNs() - started, std::memory_order_relaxed);
        }
    }
    return _hosted[job % kJobRing];
}

void Vdac4FtWorker::WaitIdle()
{
    if (!_threaded)
        return;
    const uint64_t published = _published.load(std::memory_order_acquire);
    if (_applied.load(std::memory_order_acquire) >= published)
        return;
    const uint64_t started = SteadyNs();
    WaitApplied(published);
    _ownerWaits.fetch_add(1, std::memory_order_relaxed);
    _ownerWaitNs.fetch_add(SteadyNs() - started, std::memory_order_relaxed);
}

Vdac4FtWorker::Stats Vdac4FtWorker::GetStats() const
{
    Stats stats;
    stats.ops = _applied.load(std::memory_order_relaxed);
    stats.reads = _reads.load(std::memory_order_relaxed);
    stats.applyNs = _applyNs.load(std::memory_order_relaxed);
    stats.producerWaitNs = _producerWaitNs.load(std::memory_order_relaxed);
    stats.ownerWaits = _ownerWaits.load(std::memory_order_relaxed);
    stats.ownerWaitNs = _ownerWaitNs.load(std::memory_order_relaxed);
    return stats;
}

void Vdac4FtWorker::Push(const Op& op)
{
    if (_head - _applied.load(std::memory_order_acquire) >= kRingOps)
    {
        // The FT thread is a whole ring behind: let it catch up
        Publish();
        const uint64_t started = SteadyNs();
        WaitApplied(_head - kRingOps + 1);
        _producerWaitNs.fetch_add(SteadyNs() - started, std::memory_order_relaxed);
    }
    _ring[_head % kRingOps] = op;
    ++_head;
    if ((_head & (kPublishEvery - 1)) == 0)
        Publish();
}

void Vdac4FtWorker::Publish()
{
    if (!_worker.joinable())
    {
        _quit = false;
        _worker = std::thread([this]() { WorkerLoop(); });
    }
    _published.store(_head, std::memory_order_seq_cst);
    if (_sleeping.load(std::memory_order_seq_cst))
    {
        std::lock_guard<std::mutex> guard(_lock);
        _wake.notify_one();
    }
}

void Vdac4FtWorker::WaitApplied(uint64_t count)
{
    WaitUntil(_lock, _drained, _waiting, [&]() { return _applied.load(std::memory_order_seq_cst) >= count; });
}

void Vdac4FtWorker::WorkerLoop()
{
    constexpr uint64_t kSpinNs = 200'000;
    uint64_t tail = _applied.load(std::memory_order_relaxed);
    for (;;)
    {
        uint64_t published = _published.load(std::memory_order_acquire);
        if (published == tail)
        {
            const uint64_t until = SteadyNs() + kSpinNs;
            while ((published = _published.load(std::memory_order_acquire)) == tail && SteadyNs() < until)
                std::this_thread::yield();
            if (published == tail)
            {
                std::unique_lock<std::mutex> guard(_lock);
                _sleeping.store(true, std::memory_order_seq_cst);
                _wake.wait(guard, [&]() { return _quit || _published.load(std::memory_order_seq_cst) != tail; });
                _sleeping.store(false, std::memory_order_seq_cst);
                if (_quit)
                    return;
                continue;
            }
        }
        const uint64_t started = SteadyNs();
        while (tail < published)
        {
            Apply(_ring[tail % kRingOps]);
            ++tail;
            if ((tail & (kPublishEvery - 1)) == 0)
                _applied.store(tail, std::memory_order_seq_cst);
        }
        _applied.store(tail, std::memory_order_seq_cst);
        _applyNs.fetch_add(SteadyNs() - started, std::memory_order_relaxed);
        if (_waiting.load(std::memory_order_seq_cst) > 0)
        {
            std::lock_guard<std::mutex> guard(_lock);
            _drained.notify_all();
        }
    }
}

void Vdac4FtWorker::StopWorker()
{
    if (!_worker.joinable())
        return;
    {
        std::lock_guard<std::mutex> guard(_lock);
        _quit = true;
    }
    _wake.notify_all();
    _worker.join();
}
