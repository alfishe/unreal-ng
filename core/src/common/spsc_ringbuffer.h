#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

/// @file spsc_ringbuffer.h
/// Lock-free single-producer, single-consumer ring buffer designed for shared memory IPC.
/// Variable-length messages with header + payload. Power-of-2 capacity, wrap-around via bitmask.
///
/// Cache-line separation between write_pos and read_pos eliminates false sharing.
/// Producer never blocks — drops message if buffer is full.
///
/// Usage:
///   Producer (emulator): SPSCRingBuffer::create(shmPtr, capacity) → try_write(...)
///   Consumer (observer): SPSCRingBuffer::attach(shmPtr) → try_read(...)

namespace emu {

struct alignas(64) SPSCRingBuffer
{
    /// Ring buffer header — lives at the start of the memory region.
    struct Header
    {
        std::atomic<uint64_t> write_pos;    // Only producer writes
        char _pad1[56];                      // Separate cache line
        std::atomic<uint64_t> read_pos;     // Only consumer writes
        char _pad2[56];                      // Separate cache line
        uint64_t capacity;                   // Power of 2 (data area size)
        uint64_t mask;                       // capacity - 1
        std::atomic<uint64_t> dropped_count; // Messages dropped due to full buffer
        uint64_t _reserved[5];
    };

    static_assert(sizeof(Header) == 192, "Header should be 192 bytes (3 cache lines)");

    /// Per-message header (24 bytes, 8-byte aligned)
    struct alignas(8) MessageHeader
    {
        uint32_t category_id;    // FNV-1a hash of category name
        uint32_t length;         // Payload bytes (excl. this header, excl. alignment padding)
        uint64_t timestamp;      // steady_clock nanoseconds since epoch
        uint8_t  level;          // emu::log::Level enum value
        uint8_t  reserved[7];
    };

    static_assert(sizeof(MessageHeader) == 24, "MessageHeader must be 24 bytes");

    /// Initialize a new ring buffer in pre-allocated memory.
    /// @param memory Pointer to memory (must be at least sizeof(Header) + capacity bytes)
    /// @param capacity Data area size in bytes (must be power of 2)
    /// @return Pointer to initialized ring buffer
    static SPSCRingBuffer* create(void* memory, uint64_t capacity)
    {
        auto* rb = static_cast<SPSCRingBuffer*>(memory);
        rb->header_.write_pos.store(0, std::memory_order_relaxed);
        rb->header_.read_pos.store(0, std::memory_order_relaxed);
        rb->header_.capacity = capacity;
        rb->header_.mask = capacity - 1;
        rb->header_.dropped_count.store(0, std::memory_order_relaxed);
        std::memset(rb->header_._reserved, 0, sizeof(rb->header_._reserved));
        // Zero data area
        std::memset(rb->data(), 0, capacity);
        return rb;
    }

    /// Attach to an existing ring buffer in shared memory (consumer side).
    /// @param memory Pointer to shared memory where ring buffer header lives
    /// @return Pointer to ring buffer (same address, typed)
    static SPSCRingBuffer* attach(void* memory)
    {
        return static_cast<SPSCRingBuffer*>(memory);
    }

    /// Attach (const version for read-only observers).
    static const SPSCRingBuffer* attachReadOnly(const void* memory)
    {
        return static_cast<const SPSCRingBuffer*>(memory);
    }

    // ── Producer side (emulator) ──

    /// Write a message to the ring buffer. Never blocks.
    /// @param category_id FNV-1a hash of category name
    /// @param level Log level
    /// @param payload Message payload (formatted text)
    /// @param len Payload length in bytes
    /// @return true if written, false if buffer full (message dropped)
    bool try_write(uint32_t category_id, uint8_t level,
                   const char* payload, uint32_t len)
    {
        // Total message size: header + payload + padding to 8-byte boundary
        uint32_t totalSize = sizeof(MessageHeader) + ((len + 7) & ~7u);

        uint64_t wp = header_.write_pos.load(std::memory_order_relaxed);
        uint64_t rp = header_.read_pos.load(std::memory_order_acquire);

        // Check available space
        uint64_t used = wp - rp;
        if (used + totalSize > header_.capacity)
        {
            header_.dropped_count.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        // Build message header
        MessageHeader hdr{};
        hdr.category_id = category_id;
        hdr.length = len;
        hdr.timestamp = currentTimestamp();
        hdr.level = level;

        // Write header
        writeWrapped(wp, &hdr, sizeof(hdr));

        // Write payload
        if (len > 0)
        {
            writeWrapped(wp + sizeof(MessageHeader), payload, len);
        }

        // Advance write position (release so consumer sees the data)
        header_.write_pos.store(wp + totalSize, std::memory_order_release);
        return true;
    }

    // ── Consumer side (observer) ──

    /// Read one message from the ring buffer.
    /// @param hdrOut Output: message header
    /// @param payloadOut Output: payload buffer (caller-provided)
    /// @param payloadCapacity Size of payloadOut buffer
    /// @return true if a message was read, false if empty
    bool try_read(MessageHeader& hdrOut, char* payloadOut, uint32_t payloadCapacity)
    {
        uint64_t rp = header_.read_pos.load(std::memory_order_relaxed);
        uint64_t wp = header_.write_pos.load(std::memory_order_acquire);

        if (rp == wp)
            return false;  // Empty

        // Read header
        readWrapped(rp, &hdrOut, sizeof(hdrOut));

        // Read payload (truncate if necessary)
        uint32_t toRead = hdrOut.length < payloadCapacity ? hdrOut.length : payloadCapacity;
        if (toRead > 0)
        {
            readWrapped(rp + sizeof(MessageHeader), payloadOut, toRead);
        }

        // Null-terminate if space
        if (toRead < payloadCapacity)
        {
            payloadOut[toRead] = '\0';
        }

        // Advance read position
        uint32_t totalSize = sizeof(MessageHeader) + ((hdrOut.length + 7) & ~7u);
        header_.read_pos.store(rp + totalSize, std::memory_order_release);
        return true;
    }

    /// Drain up to maxMsgs messages at once.
    /// @param hdrs Array of MessageHeader to fill
    /// @param payloads Array of payload buffers
    /// @param payloadCapacity Size of each payload buffer
    /// @param maxMsgs Maximum number of messages to read
    /// @return Number of messages actually read
    uint32_t drain(MessageHeader* hdrs, char** payloads, uint32_t payloadCapacity, uint32_t maxMsgs)
    {
        uint32_t count = 0;
        while (count < maxMsgs)
        {
            if (!try_read(hdrs[count], payloads[count], payloadCapacity))
                break;
            count++;
        }
        return count;
    }

    // ── Accessors ──

    uint64_t capacity() const { return header_.capacity; }
    uint64_t writePos() const { return header_.write_pos.load(std::memory_order_relaxed); }
    uint64_t readPos() const { return header_.read_pos.load(std::memory_order_relaxed); }
    uint64_t droppedCount() const { return header_.dropped_count.load(std::memory_order_relaxed); }

    uint64_t available() const
    {
        uint64_t wp = header_.write_pos.load(std::memory_order_acquire);
        uint64_t rp = header_.read_pos.load(std::memory_order_relaxed);
        return wp - rp;
    }

    /// Total memory required for a ring buffer with given data capacity.
    static constexpr uint64_t totalMemoryRequired(uint64_t dataCapacity)
    {
        return sizeof(Header) + dataCapacity;
    }

private:
    Header header_;
    // Data area follows immediately: data_[capacity]

    char* data()
    {
        return reinterpret_cast<char*>(this) + sizeof(Header);
    }

    const char* data() const
    {
        return reinterpret_cast<const char*>(this) + sizeof(Header);
    }

    void writeWrapped(uint64_t pos, const void* src, uint32_t len)
    {
        uint64_t offset = pos & header_.mask;
        uint64_t firstChunk = header_.capacity - offset;

        if (firstChunk >= len)
        {
            std::memcpy(data() + offset, src, len);
        }
        else
        {
            // Wrap around
            std::memcpy(data() + offset, src, static_cast<size_t>(firstChunk));
            std::memcpy(data(), static_cast<const char*>(src) + firstChunk,
                        len - static_cast<uint32_t>(firstChunk));
        }
    }

    void readWrapped(uint64_t pos, void* dst, uint32_t len)
    {
        uint64_t offset = pos & header_.mask;
        uint64_t firstChunk = header_.capacity - offset;

        if (firstChunk >= len)
        {
            std::memcpy(dst, data() + offset, len);
        }
        else
        {
            // Wrap around
            std::memcpy(dst, data() + offset, static_cast<size_t>(firstChunk));
            std::memcpy(static_cast<char*>(dst) + firstChunk, data(),
                        len - static_cast<uint32_t>(firstChunk));
        }
    }

    // Const version for observer (read-only mmap)
    void readWrapped(uint64_t pos, void* dst, uint32_t len) const
    {
        uint64_t offset = pos & header_.mask;
        uint64_t firstChunk = header_.capacity - offset;

        if (firstChunk >= len)
        {
            std::memcpy(dst, data() + offset, len);
        }
        else
        {
            std::memcpy(dst, data() + offset, static_cast<size_t>(firstChunk));
            std::memcpy(static_cast<char*>(dst) + firstChunk, data(),
                        len - static_cast<uint32_t>(firstChunk));
        }
    }

    static uint64_t currentTimestamp()
    {
        // Use steady_clock for monotonic, portable timestamps (~25ns overhead)
        auto now = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(now.time_since_epoch().count());
    }
};

}  // namespace emu
