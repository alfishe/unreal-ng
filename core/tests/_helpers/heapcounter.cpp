#include "heapcounter.h"

#include <atomic>
#include <cstdlib>
#include <new>

#if defined(__APPLE__)
#include <malloc/malloc.h>
static size_t BlockSize(void* p) { return malloc_size(p); }
#define HEAP_COUNTER_AVAILABLE 1
#elif defined(__GLIBC__)
#include <malloc.h>
static size_t BlockSize(void* p) { return malloc_usable_size(p); }
#define HEAP_COUNTER_AVAILABLE 1
#endif

namespace
{
    std::atomic<bool> g_counting{false};
    std::atomic<int64_t> g_net{0};
    std::atomic<int64_t> g_peak{0};
    std::atomic<int64_t> g_allocations{0};
}  // namespace

#ifdef HEAP_COUNTER_AVAILABLE
void* operator new(size_t size)
{
    void* p = std::malloc(size ? size : 1);
    if (!p)
        throw std::bad_alloc();
    if (g_counting.load(std::memory_order_relaxed))
    {
        const int64_t now = g_net.fetch_add(static_cast<int64_t>(BlockSize(p)), std::memory_order_relaxed) +
                            static_cast<int64_t>(BlockSize(p));
        int64_t peak = g_peak.load(std::memory_order_relaxed);
        while (now > peak && !g_peak.compare_exchange_weak(peak, now, std::memory_order_relaxed))
        {
        }
        g_allocations.fetch_add(1, std::memory_order_relaxed);
    }
    return p;
}

void* operator new[](size_t size)
{
    return operator new(size);
}

void operator delete(void* p) noexcept
{
    if (p && g_counting.load(std::memory_order_relaxed))
        g_net.fetch_sub(static_cast<int64_t>(BlockSize(p)), std::memory_order_relaxed);
    std::free(p);
}

void operator delete[](void* p) noexcept
{
    operator delete(p);
}

void operator delete(void* p, size_t) noexcept
{
    operator delete(p);
}

void operator delete[](void* p, size_t) noexcept
{
    operator delete(p);
}

// The nothrow forms too: libstdc++ allocates temporary buffers (std::stable_sort) with them and frees through the
// plain delete above. Left to the runtime, AddressSanitizer saw its operator new paired with this free and reported
// alloc-dealloc-mismatch on every sort (Linux; docs/testing/sanitizers.md)
void* operator new(size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return operator new(size);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept
{
    return operator new(size, std::nothrow);
}

void operator delete(void* p, const std::nothrow_t&) noexcept
{
    operator delete(p);
}

void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    operator delete(p);
}
#endif

namespace HeapCounter
{
    bool Available()
    {
#ifdef HEAP_COUNTER_AVAILABLE
        return true;
#else
        return false;
#endif
    }

    void Start()
    {
        g_net = 0;
        g_peak = 0;
        g_allocations = 0;
        g_counting = true;
    }

    void Stop()
    {
        g_counting = false;
    }

    int64_t Net()
    {
        return g_net.load();
    }

    int64_t Peak()
    {
        return g_peak.load();
    }

    int64_t Allocations()
    {
        return g_allocations.load();
    }
}  // namespace HeapCounter
