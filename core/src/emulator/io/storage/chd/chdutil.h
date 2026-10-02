#pragma once

/// @file chdutil.h
/// @brief Small helpers every CHD part shares: big-endian fields, the MSB-first
/// bit streams of the compressed map and the Huffman codec, CRC-16 / CRC-32 and
/// SHA-1. The bit order, the zero padding past the end and the CRC-16 variant
/// follow MAME's `bitstream.h` and `hashing.cpp` (format: docs/file-formats/disk-images/chd.md).

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace chd
{
    using Sha1 = std::array<uint8_t, 20>;

    inline uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
    inline uint32_t Be24(const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2]; }
    inline uint32_t Be32(const uint8_t* p)
    {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
    }
    inline uint64_t Be48(const uint8_t* p) { return (static_cast<uint64_t>(Be16(p)) << 32) | Be32(p + 2); }
    inline uint64_t Be64(const uint8_t* p) { return (static_cast<uint64_t>(Be32(p)) << 32) | Be32(p + 4); }

    inline void PutBe16(uint8_t* p, uint32_t v)
    {
        p[0] = static_cast<uint8_t>(v >> 8);
        p[1] = static_cast<uint8_t>(v);
    }
    inline void PutBe24(uint8_t* p, uint32_t v)
    {
        p[0] = static_cast<uint8_t>(v >> 16);
        p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v);
    }
    inline void PutBe32(uint8_t* p, uint32_t v)
    {
        PutBe16(p, v >> 16);
        PutBe16(p + 2, v & 0xFFFF);
    }
    inline void PutBe48(uint8_t* p, uint64_t v)
    {
        PutBe16(p, static_cast<uint32_t>(v >> 32));
        PutBe32(p + 2, static_cast<uint32_t>(v));
    }
    inline void PutBe64(uint8_t* p, uint64_t v)
    {
        PutBe32(p, static_cast<uint32_t>(v >> 32));
        PutBe32(p + 4, static_cast<uint32_t>(v));
    }

    /// Bits needed for `value` (0 for 0): std::bit_width without C++20
    inline uint8_t BitWidth(uint64_t value)
    {
        uint8_t bits = 0;
        while (value)
        {
            bits++;
            value >>= 1;
        }
        return bits;
    }

    /// MSB-first bit reader. Reading past the end yields zero bits and sets Overflow()
    class BitReader
    {
    public:
        BitReader(const uint8_t* data, size_t length) : _data(data), _length(length) {}

        uint32_t Peek(int bits)
        {
            if (bits == 0)
                return 0;
            while (_count < bits)
            {
                const uint8_t next = _byte < _length ? _data[_byte] : 0;
                _byte++;
                _buffer = (_buffer << 8) | next;
                _count += 8;
            }
            return static_cast<uint32_t>((_buffer >> (_count - bits)) & ((1ULL << bits) - 1));
        }
        void Remove(int bits) { _count -= bits; }
        uint32_t Read(int bits)
        {
            const uint32_t value = Peek(bits);
            Remove(bits);
            return value;
        }
        /// More bits consumed than the data holds
        bool Overflow() const { return _byte * 8 - static_cast<size_t>(_count) > _length * 8; }

    private:
        const uint8_t* _data;
        size_t _length;
        size_t _byte = 0;
        uint64_t _buffer = 0;
        int _count = 0;
    };

    /// MSB-first bit writer. Writing past the capacity only counts (Overflow());
    /// Flush pads the last byte with zero bits and returns the bytes written
    class BitWriter
    {
    public:
        BitWriter(uint8_t* data, size_t capacity) : _data(data), _capacity(capacity) {}

        void Write(uint32_t value, int bits)
        {
            for (int i = bits - 1; i >= 0; i--)
            {
                _current = static_cast<uint8_t>((_current << 1) | ((value >> i) & 1));
                if (++_count == 8)
                    Emit();
            }
        }
        size_t Flush()
        {
            if (_count)
            {
                _current = static_cast<uint8_t>(_current << (8 - _count));
                Emit();
            }
            return _bytes;
        }
        bool Overflow() const { return _bytes > _capacity; }

    private:
        void Emit()
        {
            if (_bytes < _capacity)
                _data[_bytes] = _current;
            _bytes++;
            _current = 0;
            _count = 0;
        }
        uint8_t* _data;
        size_t _capacity;
        size_t _bytes = 0;
        uint8_t _current = 0;
        int _count = 0;
    };

    /// CRC-16/CCITT-FALSE (polynomial #1021, initial #FFFF, MSB first): the CRC of
    /// CHD v5 hunks and maps
    uint16_t Crc16(const uint8_t* data, size_t length);
    /// CRC-32 (zlib's): the CRC of CHD v3 / v4 hunks
    uint32_t Crc32(const uint8_t* data, size_t length);

    /// SHA-1 over data fed in pieces
    class Sha1Builder
    {
    public:
        Sha1Builder();
        ~Sha1Builder();
        Sha1Builder(const Sha1Builder&) = delete;
        Sha1Builder& operator=(const Sha1Builder&) = delete;
        void Append(const uint8_t* data, size_t length);
        Sha1 Finish();

    private:
        struct State;
        State* _state;
    };

    Sha1 Sha1Of(const uint8_t* data, size_t length);
    std::string Sha1Hex(const Sha1& sha1);
    inline bool IsNull(const Sha1& sha1)
    {
        for (uint8_t b : sha1)
            if (b)
                return false;
        return true;
    }
}  // namespace chd
