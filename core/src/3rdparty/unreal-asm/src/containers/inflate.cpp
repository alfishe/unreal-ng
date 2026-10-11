#include "inflate.h"

#include <array>

namespace unrealasm::containers
{
namespace
{
/// Huffman code: how many codes of each length, the symbols in code order (canonical codes, RFC 1951 §3.2.2)
struct Huffman
{
    std::array<uint16_t, 16> count{};
    std::array<uint16_t, 288> symbol{};
};

class Inflater
{
public:
    explicit Inflater(std::span<const uint8_t> in) : _in(in) {}

    bool Run(std::vector<uint8_t>& out, std::string& error)
    {
        _out = &out;
        bool last = false;
        while (!last)
        {
            last = Bits(1) == 1;
            const int type = Bits(2);
            bool ok = false;
            if (type == 0)
                ok = Stored();
            else if (type == 1)
                ok = Fixed();
            else if (type == 2)
                ok = Dynamic();
            if (_overrun)
            {
                error = "the deflate data ends early";
                return false;
            }
            if (!ok)
            {
                error = type == 3 ? "a deflate block of type 3" : _error;
                return false;
            }
        }
        return true;
    }

    size_t Position() const { return _at; }

private:
    int Bits(int need)
    {
        uint32_t value = _bitBuffer;
        while (_bitCount < need)
        {
            if (_at >= _in.size())
            {
                _overrun = true;
                return 0;
            }
            value |= static_cast<uint32_t>(_in[_at++]) << _bitCount;
            _bitCount += 8;
        }
        _bitBuffer = value >> need;
        _bitCount -= need;
        return static_cast<int>(value & ((1u << need) - 1));
    }

    bool Stored()
    {
        _bitBuffer = 0;
        _bitCount = 0;
        if (_at + 4 > _in.size())
        {
            _overrun = true;
            return false;
        }
        const unsigned length = _in[_at] | (_in[_at + 1] << 8);
        const unsigned check = _in[_at + 2] | (_in[_at + 3] << 8);
        _at += 4;
        if (length != (~check & 0xFFFF))
        {
            _error = "a stored block's length and its complement disagree";
            return false;
        }
        if (_at + length > _in.size())
        {
            _overrun = true;
            return false;
        }
        _out->insert(_out->end(), _in.begin() + static_cast<std::ptrdiff_t>(_at), _in.begin() + static_cast<std::ptrdiff_t>(_at + length));
        _at += length;
        return true;
    }

    int Decode(const Huffman& h)
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len)
        {
            code |= Bits(1);
            const int count = h.count[len];
            if (code - count < first)
                return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
            if (_overrun)
                return -1;
        }
        return -1;
    }

    /// Builds a code from lengths; false when lengths over-subscribe (an incomplete code is allowed, as zlib does for one distance)
    static bool Build(Huffman& h, const uint16_t* lengths, int n)
    {
        h.count.fill(0);
        for (int s = 0; s < n; ++s)
            h.count[lengths[s]]++;
        if (h.count[0] == n)
            return true;
        int left = 1;
        for (int len = 1; len < 16; ++len)
        {
            left <<= 1;
            left -= h.count[len];
            if (left < 0)
                return false;
        }
        std::array<uint16_t, 16> offsets{};
        for (int len = 1; len < 15; ++len)
            offsets[len + 1] = static_cast<uint16_t>(offsets[len] + h.count[len]);
        for (int s = 0; s < n; ++s)
            if (lengths[s] != 0)
                h.symbol[offsets[lengths[s]]++] = static_cast<uint16_t>(s);
        return true;
    }

    bool Codes(const Huffman& lengthCode, const Huffman& distanceCode)
    {
        static const uint16_t base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const uint16_t extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const uint16_t distanceBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                                  1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const uint16_t distanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;)
        {
            int symbol = Decode(lengthCode);
            if (symbol < 0)
            {
                _error = "a bad literal / length code";
                return false;
            }
            if (symbol < 256)
                _out->push_back(static_cast<uint8_t>(symbol));
            else if (symbol == 256)
                return true;
            else
            {
                symbol -= 257;
                if (symbol >= 29)
                {
                    _error = "a length code beyond 285";
                    return false;
                }
                const size_t length = base[symbol] + static_cast<size_t>(Bits(extra[symbol]));
                const int d = Decode(distanceCode);
                if (d < 0 || d >= 30)
                {
                    _error = "a bad distance code";
                    return false;
                }
                const size_t distance = distanceBase[d] + static_cast<size_t>(Bits(distanceExtra[d]));
                if (distance > _out->size())
                {
                    _error = "a distance back beyond the start";
                    return false;
                }
                for (size_t k = 0; k < length; ++k)
                    _out->push_back((*_out)[_out->size() - distance]);
            }
            if (_overrun)
                return false;
        }
    }

    bool Fixed()
    {
        Huffman lengthCode, distanceCode;
        uint16_t lengths[288];
        int s = 0;
        for (; s < 144; ++s)
            lengths[s] = 8;
        for (; s < 256; ++s)
            lengths[s] = 9;
        for (; s < 280; ++s)
            lengths[s] = 7;
        for (; s < 288; ++s)
            lengths[s] = 8;
        Build(lengthCode, lengths, 288);
        for (s = 0; s < 30; ++s)
            lengths[s] = 5;
        Build(distanceCode, lengths, 30);
        return Codes(lengthCode, distanceCode);
    }

    bool Dynamic()
    {
        static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const int nlen = Bits(5) + 257;
        const int ndist = Bits(5) + 1;
        const int ncode = Bits(4) + 4;
        if (nlen > 286 || ndist > 30)
        {
            _error = "too many length or distance codes";
            return false;
        }
        uint16_t lengths[320] = {};
        for (int i = 0; i < ncode; ++i)
            lengths[order[i]] = static_cast<uint16_t>(Bits(3));
        Huffman lengthCode, distanceCode;
        if (!Build(lengthCode, lengths, 19))
        {
            _error = "a bad code length code";
            return false;
        }
        int index = 0;
        while (index < nlen + ndist)
        {
            int symbol = Decode(lengthCode);
            if (symbol < 0 || _overrun)
            {
                _error = "a bad code length";
                return false;
            }
            if (symbol < 16)
            {
                lengths[index++] = static_cast<uint16_t>(symbol);
                continue;
            }
            uint16_t value = 0;
            int repeat = 0;
            if (symbol == 16)
            {
                if (index == 0)
                {
                    _error = "a repeat with no length before it";
                    return false;
                }
                value = lengths[index - 1];
                repeat = 3 + Bits(2);
            }
            else if (symbol == 17)
                repeat = 3 + Bits(3);
            else
                repeat = 11 + Bits(7);
            if (index + repeat > nlen + ndist)
            {
                _error = "code lengths beyond their count";
                return false;
            }
            while (repeat--)
                lengths[index++] = value;
        }
        if (lengths[256] == 0)
        {
            _error = "no end-of-block code";
            return false;
        }
        if (!Build(lengthCode, lengths, nlen) || !Build(distanceCode, lengths + nlen, ndist))
        {
            _error = "over-subscribed codes";
            return false;
        }
        return Codes(lengthCode, distanceCode);
    }

    std::span<const uint8_t> _in;
    size_t _at = 0;
    uint32_t _bitBuffer = 0;
    int _bitCount = 0;
    bool _overrun = false;
    std::string _error;
    std::vector<uint8_t>* _out = nullptr;
};

uint32_t Adler32(const std::vector<uint8_t>& bytes)
{
    uint32_t a = 1, b = 0;
    for (const uint8_t byte : bytes)
    {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}
}  // namespace

bool InflateZlib(std::span<const uint8_t> stream, std::vector<uint8_t>& out, std::string& error)
{
    out.clear();
    if (stream.size() < 6 || (stream[0] & 0x0F) != 8 || ((stream[0] << 8) | stream[1]) % 31 != 0 || (stream[1] & 0x20))
    {
        error = "not a zlib stream";
        return false;
    }
    Inflater inflater(stream.subspan(2));
    if (!inflater.Run(out, error))
        return false;
    const size_t end = 2 + inflater.Position();
    if (end + 4 > stream.size())
    {
        error = "the zlib stream has no checksum";
        return false;
    }
    const uint32_t stored = (static_cast<uint32_t>(stream[end]) << 24) | (stream[end + 1] << 16) | (stream[end + 2] << 8) | stream[end + 3];
    if (stored != Adler32(out))
    {
        error = "the zlib checksum is wrong";
        return false;
    }
    return true;
}
}  // namespace unrealasm::containers
