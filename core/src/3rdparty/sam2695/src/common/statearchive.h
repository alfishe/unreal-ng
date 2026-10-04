// libsam2695 - the state blob: one field list per struct, three archives.
//
// Every stateful struct has `template <class Ar> void Serialize(Ar& ar)` listing its fields once.
// StateSizer counts, StateWriter writes, StateReader reads - so size, save and load can never disagree.
// The blob is platform independent: integers little-endian at their declared width, floats as their
// IEEE-754 bit patterns, bools as one byte.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace sam2695
{

template <class T>
constexpr size_t WireSize()
{
    if constexpr (std::is_same_v<T, bool>)
        return 1;
    else if constexpr (std::is_enum_v<T>)
        return sizeof(std::underlying_type_t<T>);
    else
        return sizeof(T);
}

template <class T>
struct IsStdArray : std::false_type
{
};
template <class T, size_t N>
struct IsStdArray<std::array<T, N>> : std::true_type
{
};

class StateSizer
{
public:
    template <class T>
    void operator()(T& v)
    {
        if constexpr (IsStdArray<T>::value)
        {
            for (auto& e : v)
                (*this)(e);
        }
        else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>)
            _size += WireSize<T>();
        else
            v.Serialize(*this);
    }
    size_t Size() const { return _size; }

private:
    size_t _size = 0;
};

class StateWriter
{
public:
    explicit StateWriter(uint8_t* out) : _p(out) {}

    template <class T>
    void operator()(T& v)
    {
        if constexpr (IsStdArray<T>::value)
        {
            for (auto& e : v)
                (*this)(e);
        }
        else if constexpr (std::is_same_v<T, bool>)
            Put(v ? 1u : 0u, 1);
        else if constexpr (std::is_enum_v<T>)
            Put(static_cast<uint64_t>(static_cast<std::underlying_type_t<T>>(v)), WireSize<T>());
        else if constexpr (std::is_floating_point_v<T>)
        {
            if constexpr (sizeof(T) == 4)
            {
                uint32_t bits;
                std::memcpy(&bits, &v, 4);
                Put(bits, 4);
            }
            else
            {
                uint64_t bits;
                std::memcpy(&bits, &v, 8);
                Put(bits, 8);
            }
        }
        else if constexpr (std::is_integral_v<T>)
            Put(static_cast<uint64_t>(v), sizeof(T));
        else
            v.Serialize(*this);
    }
    size_t Written() const { return _n; }

private:
    void Put(uint64_t v, size_t bytes)
    {
        for (size_t i = 0; i < bytes; i++)
            _p[_n++] = static_cast<uint8_t>(v >> (8 * i));
    }
    uint8_t* _p;
    size_t _n = 0;
};

class StateReader
{
public:
    StateReader(const uint8_t* in, size_t size) : _p(in), _size(size) {}

    template <class T>
    void operator()(T& v)
    {
        if constexpr (IsStdArray<T>::value)
        {
            for (auto& e : v)
                (*this)(e);
        }
        else if constexpr (std::is_same_v<T, bool>)
            v = Get(1) != 0;
        else if constexpr (std::is_enum_v<T>)
            v = static_cast<T>(static_cast<std::underlying_type_t<T>>(Get(WireSize<T>())));
        else if constexpr (std::is_floating_point_v<T>)
        {
            if constexpr (sizeof(T) == 4)
            {
                const uint32_t bits = static_cast<uint32_t>(Get(4));
                std::memcpy(&v, &bits, 4);
            }
            else
            {
                const uint64_t bits = Get(8);
                std::memcpy(&v, &bits, 8);
            }
        }
        else if constexpr (std::is_integral_v<T>)
            v = static_cast<T>(Get(sizeof(T)));
        else
            v.Serialize(*this);
    }
    bool Ok() const { return _ok; }
    size_t Consumed() const { return _n; }

private:
    uint64_t Get(size_t bytes)
    {
        if (_n + bytes > _size)
        {
            _ok = false;
            return 0;
        }
        uint64_t v = 0;
        for (size_t i = 0; i < bytes; i++)
            v |= static_cast<uint64_t>(_p[_n++]) << (8 * i);
        return v;
    }
    const uint8_t* _p;
    size_t _size;
    size_t _n = 0;
    bool _ok = true;
};

} // namespace sam2695
