#pragma once

/// @file zeroinitbuffer.h
/// @brief Fixed-size zero-initialized POD buffer, used instead of std::vector
///        for large (multi-MB) arrays of a trivial type that are allocated
///        once and indexed, never grown incrementally.
///
/// @details `std::vector<T>::resize(n, 0)` constructs every element through the generic
/// per-element fill/copy-construct path (`__append`). An optimizing compiler can fold that
/// into a `memset` for a trivial `T` in high-optimization builds (`-O2`/`-O3`), but at `-O0`
/// (Debug builds, ASAN, TSAN, and unoptimized CI bots) it stays a real per-element loop:
/// measured 58.7 ms for a single 24 MiB `uint32_t` array, against 0.2 ms for `new uint32_t[n]()`
/// on the same machine — a ~290x difference.
///
/// Array-new with value-initialization (`new T[n]()`) is defined by standard C++ ([expr.new] p24)
/// to value-initialize (zero-initialize) every element. Mainstream compilers (Clang, GCC, MSVC)
/// lower that directly to a kernel zero-fill, `calloc`, or bulk `memset` for trivial `T` regardless
/// of optimization level, making it sub-millisecond in both Debug and Release builds.
///
/// First found in MemoryAccessTracker's per-frame counter arrays (three 24 MiB
/// `vector<uint32_t>`, allocated whenever memory tracking turns on — the flat cost showed up as
/// every test in that suite paying it three times over, once per physical-memory counter array).
/// The identical pattern recurred one layer down in TTDWriteJournal's ring buffer (a
/// 64-128 MiB `vector<TTDWriteRecord>`), disguised there by an async-allocation offload: the
/// slow `resize()` runs on a background thread so the emulator thread never blocks on it
/// directly, but `~TTDWriteJournal()` still calls `std::future::wait()` unconditionally, so any
/// caller that constructs and destroys the journal faster than the background fill completes —
/// a short-lived unit test above all — pays nearly the whole cost anyway, just relocated to
/// destruction. Fixing the allocation itself removes the need to reason about that race at all.
///
/// This class is deliberately minimal: it exposes only the operations its two current callers
/// actually use (subscript indexing, size/empty/data queries, pointer-based iteration, and
/// an explicit bulk `zero()` via `std::memset`) — not a general-purpose container.
///
/// @tparam T Element type. Must be a trivially copyable primitive/POD type (e.g. `uint32_t`, `uint8_t`).
#include <cstddef>
#include <cstring>
#include <memory>
#include <type_traits>

template <typename T>
class ZeroInitBuffer
{
    static_assert(std::is_trivially_copyable<T>::value, "ZeroInitBuffer only supports trivially copyable types");

public:
    /// @brief Default constructor creating an empty buffer with zero size and null storage.
    ZeroInitBuffer() = default;

    /// @brief Move constructor transferring ownership of backing storage.
    ZeroInitBuffer(ZeroInitBuffer&&) noexcept = default;

    /// @brief Move assignment operator transferring ownership of backing storage.
    ZeroInitBuffer& operator=(ZeroInitBuffer&&) noexcept = default;

    // Non-copyable to prevent accidental deep copies of multi-megabyte buffers.
    ZeroInitBuffer(const ZeroInitBuffer&) = delete;
    ZeroInitBuffer& operator=(const ZeroInitBuffer&) = delete;

    /// @brief Allocate n zero-initialized elements, discarding any previously allocated content.
    /// @details Employs array value-initialization (`new T[n]()`), lowering to kernel zero-paging
    ///          or bulk `memset`/`calloc` across compilers, avoiding per-element constructor loops.
    /// @param n Number of elements to allocate. If 0, frees existing storage and sets size to 0.
    void resize(size_t n)
    {
        _data.reset(n ? new T[n]() : nullptr);
        _size = n;
    }

    /// @brief Free backing storage immediately and reset size to zero.
    /// @details Directly releases the `std::unique_ptr<T[]>`, freeing memory immediately without
    ///          allocating temporary vector instances (replacing the previous `std::vector::swap` idiom).
    void reset()
    {
        _data.reset();
        _size = 0;
    }

    /// @brief Zero out every element in the allocated buffer without reallocating.
    /// @details Issues a vectorized `std::memset` over the contiguous memory block. Replaces
    ///          `std::fill` which under unoptimized builds generates scalar element-by-element loops.
    void zero()
    {
        if (_size)
            std::memset(_data.get(), 0, _size * sizeof(T));
    }

    /// @brief Check whether the buffer has no allocated elements.
    /// @return `true` if empty (size == 0), `false` otherwise.
    bool empty() const { return _size == 0; }

    /// @brief Get the number of allocated elements.
    /// @return Number of elements in the buffer.
    size_t size() const { return _size; }

    /// @brief Direct pointer access to the underlying memory block.
    /// @return Mutable pointer to the beginning of the contiguous array, or `nullptr` if empty.
    T* data() { return _data.get(); }

    /// @brief Direct pointer access to the underlying memory block (const overload).
    /// @return Const pointer to the beginning of the contiguous array, or `nullptr` if empty.
    const T* data() const { return _data.get(); }

    /// @brief Iterator to the beginning of the buffer.
    /// @return Pointer to the first element.
    T* begin() { return _data.get(); }

    /// @brief Iterator to the end of the buffer.
    /// @return Pointer one past the last element.
    T* end() { return _data.get() + _size; }

    /// @brief Iterator to the beginning of the buffer (const overload).
    /// @return Const pointer to the first element.
    const T* begin() const { return _data.get(); }

    /// @brief Iterator to the end of the buffer (const overload).
    /// @return Const pointer one past the last element.
    const T* end() const { return _data.get() + _size; }

    /// @brief Subscript access to element at specified index (unchecked, matching std::vector).
    /// @param i Zero-based index of element.
    /// @return Reference to the element at index `i`.
    T& operator[](size_t i) { return _data[i]; }

    /// @brief Subscript access to element at specified index (const overload, unchecked).
    /// @param i Zero-based index of element.
    /// @return Const reference to the element at index `i`.
    const T& operator[](size_t i) const { return _data[i]; }

private:
    std::unique_ptr<T[]> _data;
    size_t _size = 0;
};
