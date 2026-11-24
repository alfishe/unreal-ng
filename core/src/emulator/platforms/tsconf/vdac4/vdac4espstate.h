#pragma once

/// @file vdac4espstate.h
/// @brief Plain little-endian writer / reader for the VDAC4 ESP device's saved state (Vdac4Esp::Save / Load).
/// Every field is written explicitly, so the blob does not depend on struct layout or the host's endianness.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class Vdac4EspStateWriter
{
public:
    explicit Vdac4EspStateWriter(std::vector<uint8_t>& out) : _out(out) {}

    void U8(uint8_t value) { _out.push_back(value); }
    void Bool(bool value) { _out.push_back(value ? 1 : 0); }
    void U16(uint16_t value)
    {
        U8(static_cast<uint8_t>(value));
        U8(static_cast<uint8_t>(value >> 8));
    }
    void U32(uint32_t value)
    {
        U16(static_cast<uint16_t>(value));
        U16(static_cast<uint16_t>(value >> 16));
    }
    void U64(uint64_t value)
    {
        U32(static_cast<uint32_t>(value));
        U32(static_cast<uint32_t>(value >> 32));
    }
    /// Fixed-size bytes (the reader knows the size)
    void Bytes(const uint8_t* data, size_t size) { _out.insert(_out.end(), data, data + size); }
    /// Length-prefixed bytes
    void Blob(const uint8_t* data, size_t size)
    {
        U32(static_cast<uint32_t>(size));
        Bytes(data, size);
    }
    void Blob(const std::vector<uint8_t>& data) { Blob(data.data(), data.size()); }
    void String(const std::string& text) { Blob(reinterpret_cast<const uint8_t*>(text.data()), text.size()); }

private:
    std::vector<uint8_t>& _out;
};

class Vdac4EspStateReader
{
public:
    Vdac4EspStateReader(const uint8_t* data, size_t size) : _data(data), _size(size) {}

    bool Ok() const { return _ok; }
    bool AtEnd() const { return _position == _size; }

    uint8_t U8()
    {
        if (!Need(1))
            return 0;
        return _data[_position++];
    }
    bool Bool() { return U8() != 0; }
    uint16_t U16()
    {
        const uint16_t low = U8();
        return static_cast<uint16_t>(low | (U8() << 8));
    }
    uint32_t U32()
    {
        const uint32_t low = U16();
        return low | (static_cast<uint32_t>(U16()) << 16);
    }
    uint64_t U64()
    {
        const uint64_t low = U32();
        return low | (static_cast<uint64_t>(U32()) << 32);
    }
    void Bytes(uint8_t* out, size_t size)
    {
        if (!Need(size))
            return;
        if (size != 0)
            std::memcpy(out, _data + _position, size);
        _position += size;
    }
    void Skip(size_t size)
    {
        if (Need(size))
            _position += size;
    }
    /// Length-prefixed bytes, at most `limit` (a larger length fails the read)
    bool Blob(std::vector<uint8_t>& out, size_t limit)
    {
        const uint32_t size = U32();
        if (!_ok || size > limit || !Need(size))
        {
            _ok = false;
            return false;
        }
        out.assign(_data + _position, _data + _position + size);
        _position += size;
        return true;
    }
    bool String(std::string& out, size_t limit)
    {
        std::vector<uint8_t> bytes;
        if (!Blob(bytes, limit))
            return false;
        out.assign(bytes.begin(), bytes.end());
        return true;
    }

private:
    bool Need(size_t size)
    {
        if (!_ok || size > _size - _position)
        {
            _ok = false;
            return false;
        }
        return true;
    }

    const uint8_t* _data;
    size_t _size;
    size_t _position = 0;
    bool _ok = true;
};
