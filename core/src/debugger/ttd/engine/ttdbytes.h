#pragma once

/// @file ttdbytes.h
/// @brief Little-endian byte writing and bounds-checked reading for the TTD
/// session file (Phase 4): fixed-width integers, LEB128 varints, strings
/// (varint length, then UTF-8 bytes). Every read reports failure instead of
/// reading past the end, so a damaged record is refused, never trusted.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "the TTD session file is written little endian; this host is not"
#endif

namespace ttd
{

class TTDByteWriter
{
public:
    void U8(uint8_t v) { bytes.push_back(v); }
    void U16(uint16_t v) { Raw(&v, 2); }
    void U32(uint32_t v) { Raw(&v, 4); }
    void U64(uint64_t v) { Raw(&v, 8); }
    void Varint(uint64_t v)
    {
        while (v >= 0x80)
        {
            bytes.push_back(static_cast<uint8_t>(v | 0x80));
            v >>= 7;
        }
        bytes.push_back(static_cast<uint8_t>(v));
    }
    void Str(const std::string& s)
    {
        Varint(s.size());
        Bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }
    void Bytes(const uint8_t* p, size_t n)
    {
        if (n)
            bytes.insert(bytes.end(), p, p + n);
    }
    void Raw(const void* p, size_t n) { Bytes(static_cast<const uint8_t*>(p), n); }

    std::vector<uint8_t> bytes;
};

class TTDByteReader
{
public:
    TTDByteReader(const uint8_t* data, size_t size) : _p(data), _end(data + size) {}
    explicit TTDByteReader(const std::vector<uint8_t>& v) : TTDByteReader(v.data(), v.size()) {}

    bool U8(uint8_t& v) { return Raw(&v, 1); }
    bool U16(uint16_t& v) { return Raw(&v, 2); }
    bool U32(uint32_t& v) { return Raw(&v, 4); }
    bool U64(uint64_t& v) { return Raw(&v, 8); }
    bool Varint(uint64_t& v)
    {
        v = 0;
        for (int shift = 0; shift < 64; shift += 7)
        {
            uint8_t b = 0;
            if (!U8(b))
                return false;
            v |= static_cast<uint64_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0)
                return true;
        }
        return false;
    }
    template <typename T>
    bool VarintAs(T& v)
    {
        uint64_t x = 0;
        if (!Varint(x) || x > static_cast<uint64_t>(static_cast<T>(~T(0))))
            return false;
        v = static_cast<T>(x);
        return true;
    }
    bool Str(std::string& s)
    {
        uint64_t n = 0;
        if (!Varint(n) || n > Left())
            return false;
        s.assign(reinterpret_cast<const char*>(_p), static_cast<size_t>(n));
        _p += n;
        return true;
    }
    bool Bytes(std::vector<uint8_t>& out, size_t n)
    {
        if (Left() < n)
            return false;
        out.assign(_p, _p + n);
        _p += n;
        return true;
    }
    /// A view of the next @p n bytes, consumed
    bool View(const uint8_t*& p, size_t n)
    {
        if (Left() < n)
            return false;
        p = _p;
        _p += n;
        return true;
    }
    bool Skip(size_t n)
    {
        if (Left() < n)
            return false;
        _p += n;
        return true;
    }
    bool Raw(void* out, size_t n)
    {
        if (Left() < n)
            return false;
        std::memcpy(out, _p, n);
        _p += n;
        return true;
    }
    size_t Left() const { return static_cast<size_t>(_end - _p); }

private:
    const uint8_t* _p;
    const uint8_t* _end;
};

}  // namespace ttd
