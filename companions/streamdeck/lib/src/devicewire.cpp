#include "sdb/devicewire.h"

namespace sdb::wire
{

std::string EncodeFrame(std::uint8_t type, std::string_view payload)
{
    std::string f;
    f.reserve(5 + payload.size());
    f += static_cast<char>(type);
    const auto n = static_cast<std::uint32_t>(payload.size());
    for (int i = 0; i < 4; ++i)
        f += static_cast<char>((n >> (i * 8)) & 0xFF);
    f += payload;
    return f;
}

bool FrameReader::Next(Frame& out)
{
    if (_broken || _buffer.size() - _at < 5)
        return false;
    std::uint32_t n = 0;
    for (int i = 0; i < 4; ++i)
        n |= std::uint32_t{static_cast<std::uint8_t>(_buffer[_at + 1 + static_cast<std::size_t>(i)])} << (i * 8);
    if (n > kMaxFrame)
    {
        _broken = true;
        return false;
    }
    if (_buffer.size() - _at - 5 < n)
        return false;
    out.type = static_cast<std::uint8_t>(_buffer[_at]);
    out.payload.assign(_buffer, _at + 5, n);
    _at += 5 + n;
    // Compact now and then rather than erasing the front on every frame.
    if (_at == _buffer.size())
    {
        _buffer.clear();
        _at = 0;
    }
    else if (_at > (256u << 10))
    {
        _buffer.erase(0, _at);
        _at = 0;
    }
    return true;
}

}  // namespace sdb::wire
