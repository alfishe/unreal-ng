#pragma once

/// @file zifistate.h
/// @brief Byte writer / reader for the ZIFI-NATIVE file bridge's TTD state (the variable-size part of the ZiFi
/// blob, id 40): little-endian integers, length-prefixed strings and byte runs. A reader that runs past the end
/// stays failed (every later read returns zero) so a short or foreign blob loads as "nothing".

#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

class ZiFiStateWriter
{
public:
    explicit ZiFiStateWriter(std::vector<uint8_t>& out) : _out(out) {}

    void U8(uint8_t v) { _out.push_back(v); }
    void Bool(bool v) { U8(v ? 1 : 0); }
    void U16(uint16_t v)
    {
        U8(static_cast<uint8_t>(v));
        U8(static_cast<uint8_t>(v >> 8));
    }
    void U32(uint32_t v)
    {
        U16(static_cast<uint16_t>(v));
        U16(static_cast<uint16_t>(v >> 16));
    }
    void U64(uint64_t v)
    {
        U32(static_cast<uint32_t>(v));
        U32(static_cast<uint32_t>(v >> 32));
    }
    void I64(int64_t v) { U64(static_cast<uint64_t>(v)); }
    void Str(const std::string& s)
    {
        U32(static_cast<uint32_t>(s.size()));
        _out.insert(_out.end(), s.begin(), s.end());
    }
    void Bytes(const uint8_t* data, size_t length)
    {
        U32(static_cast<uint32_t>(length));
        if (length)
            _out.insert(_out.end(), data, data + length);
    }
    void Bytes(const std::vector<uint8_t>& v) { Bytes(v.data(), v.size()); }
    void Bytes(const std::deque<uint8_t>& d)
    {
        U32(static_cast<uint32_t>(d.size()));
        _out.insert(_out.end(), d.begin(), d.end());
    }

private:
    std::vector<uint8_t>& _out;
};

class ZiFiStateReader
{
public:
    ZiFiStateReader(const uint8_t* data, size_t length) : _data(data), _length(length) {}

    bool Ok() const { return _ok; }
    size_t Position() const { return _pos; }

    uint8_t U8()
    {
        if (!_ok || _pos >= _length)
        {
            _ok = false;
            return 0;
        }
        return _data[_pos++];
    }
    bool Bool() { return U8() != 0; }
    uint16_t U16()
    {
        const uint16_t lo = U8();
        return static_cast<uint16_t>(lo | (U8() << 8));
    }
    uint32_t U32()
    {
        const uint32_t lo = U16();
        return lo | (static_cast<uint32_t>(U16()) << 16);
    }
    uint64_t U64()
    {
        const uint64_t lo = U32();
        return lo | (static_cast<uint64_t>(U32()) << 32);
    }
    int64_t I64() { return static_cast<int64_t>(U64()); }
    std::string Str(size_t max = 1u << 20)
    {
        const uint32_t n = U32();
        if (!_ok || n > max || _length - _pos < n)
        {
            _ok = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(_data + _pos), n);
        _pos += n;
        return s;
    }
    std::vector<uint8_t> Bytes(size_t max = 1u << 26)
    {
        const uint32_t n = U32();
        if (!_ok || n > max || _length - _pos < n)
        {
            _ok = false;
            return {};
        }
        std::vector<uint8_t> v(_data + _pos, _data + _pos + n);
        _pos += n;
        return v;
    }
    std::deque<uint8_t> Deque(size_t max = 1u << 26)
    {
        const std::vector<uint8_t> v = Bytes(max);
        return std::deque<uint8_t>(v.begin(), v.end());
    }

private:
    const uint8_t* _data = nullptr;
    size_t _length = 0;
    size_t _pos = 0;
    bool _ok = true;
};
