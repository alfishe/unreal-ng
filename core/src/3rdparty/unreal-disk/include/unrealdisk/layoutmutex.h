#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace unrealdisk
{

/// The lock between a disk image's owner - the emulation thread changing the image's layout (DiskImage::LayoutLock)
/// - and readers on other threads copying it (FDD::SnapshotDisk / WithDisk).
///
/// The owner side never sleeps: a reader holds the lock for one track's memory copy (about a microsecond), so the
/// owner spins for it instead of parking in the kernel, where a loaded machine (tens of runnable threads per core)
/// wakes a sleeper only after tens of milliseconds - a frame lost for a WebAPI read. A reader gives way: it does
/// not take the lock while the owner waits for it, and it yields its time slice while it waits itself.
/// Recursive for the thread holding it (a layout change calls others; a copy runs under the drive's hold).
class LayoutMutex
{
public:
    LayoutMutex() = default;
    LayoutMutex(const LayoutMutex&) = delete;
    LayoutMutex& operator=(const LayoutMutex&) = delete;

    /// The owner's side (std::lock_guard compatible): spins, never sleeps, goes ahead of any waiting reader
    void lock()
    {
        if (Reenter())
            return;
        _ownerWaiting.fetch_add(1, std::memory_order_acq_rel);
        for (uint32_t spins = 0; !TryAcquire(); spins++)
        {
            Relax();
            // A reader the OS preempted while holding the lock: give the core back now and then, so it can finish
            if ((spins & 0xFFFF) == 0xFFFF)
                std::this_thread::yield();
        }
        _ownerWaiting.fetch_sub(1, std::memory_order_acq_rel);
    }

    /// A reader's side: waits (yielding) while the owner wants the lock or holds it
    void lockReader()
    {
        if (Reenter())
            return;
        for (;;)
        {
            if (_ownerWaiting.load(std::memory_order_acquire) == 0 && TryAcquire())
                return;
            std::this_thread::yield();
        }
    }

    void unlock()
    {
        if (--_depth == 0)
            _holder.store(std::thread::id(), std::memory_order_release);
    }

    /// Readers' RAII guard
    class ReaderGuard
    {
    public:
        explicit ReaderGuard(LayoutMutex& mutex) : _mutex(mutex) { _mutex.lockReader(); }
        ~ReaderGuard() { _mutex.unlock(); }
        ReaderGuard(const ReaderGuard&) = delete;
        ReaderGuard& operator=(const ReaderGuard&) = delete;

    private:
        LayoutMutex& _mutex;
    };

private:
    bool Reenter()
    {
        if (_holder.load(std::memory_order_acquire) != std::this_thread::get_id())
            return false;
        ++_depth;
        return true;
    }

    bool TryAcquire()
    {
        std::thread::id none;
        if (_holder.load(std::memory_order_relaxed) != none)
            return false;
        if (!_holder.compare_exchange_strong(none, std::this_thread::get_id(), std::memory_order_acquire,
                                             std::memory_order_relaxed))
            return false;
        _depth = 1;
        return true;
    }

    static void Relax()
    {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
        _mm_pause();
#elif defined(_MSC_VER) && defined(_M_ARM64)
        __yield();
#elif defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
        __asm__ __volatile__("yield");
#endif
    }

    std::atomic<std::thread::id> _holder{};
    std::atomic<int> _ownerWaiting{0};
    int _depth = 0;  // Touched by the holder only
};

}  // namespace unrealdisk
