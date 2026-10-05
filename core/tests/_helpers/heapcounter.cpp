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
        g_net.fetch_add(static_cast<int64_t>(BlockSize(p)), std::memory_order_relaxed);
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

    int64_t Allocations()
    {
        return g_allocations.load();
    }
}  // namespace HeapCounter
