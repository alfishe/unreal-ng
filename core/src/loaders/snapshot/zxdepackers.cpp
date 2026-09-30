#include "zxdepackers.h"

namespace
{
    /// The shared machinery: input bytes, output with LZ copies, a bit stream
    class Stream
    {
    public:
        Stream(const uint8_t* in, size_t inSize, uint8_t* out, size_t capacity, bool words)
            : _in(in), _inSize(inSize), _out(out), _capacity(capacity), _words(words)
        {
        }

        bool ok() const { return _ok; }
        size_t written() const { return _written; }

        uint8_t Byte()
        {
            if (_pos >= _inSize)
            {
                _ok = false;
                return 0;
            }
            return _in[_pos++];
        }

        /// Load the first bit word (both formats begin with one)
        void LoadBits()
        {
            if (_words)
            {
                const uint8_t low = Byte();
                const uint8_t high = Byte();
                _bits = static_cast<uint16_t>((high << 8) | low);
                _left = 16;
            }
            else
            {
                _bits = static_cast<uint16_t>(Byte() << 8);
                _left = 8;
            }
        }

        uint32_t Bits(uint32_t count)
        {
            uint32_t value = 0;
            while (count--)
            {
                if (!_words && _left == 0)
                    LoadBits();  // MegaLZ: fetched when a bit is needed
                value = (value << 1) | ((_bits >> 15) & 1u);
                _bits = static_cast<uint16_t>(_bits << 1);
                if (--_left == 0 && _words)
                {
                    // Hrust: fetched as soon as the last bit is used; the stream
                    // may end right after its final bit
                    if (_pos + 2 <= _inSize)
                        LoadBits();
                    else if (count)
                        _ok = false;
                    else
                        _exhausted = true;
                }
                else if (_exhausted)
                {
                    _ok = false;
                }
            }
            return value;
        }

        void Put(uint8_t value)
        {
            if (_written >= _capacity)
            {
                _ok = false;
                return;
            }
            _out[_written++] = value;
        }

        /// Copy `length` bytes from `displacement` (negative) back; overlapping on purpose
        void Repeat(int32_t displacement, uint32_t length)
        {
            if (displacement >= 0 || static_cast<size_t>(-displacement) > _written)
            {
                _ok = false;
                return;
            }
            while (length-- && _ok)
                Put(_out[_written + displacement]);
        }

    private:
        const uint8_t* _in;
        size_t _inSize;
        size_t _pos = 0;
        uint8_t* _out;
        size_t _capacity;
        size_t _written = 0;
        bool _words;
        uint16_t _bits = 0;
        uint32_t _left = 0;
        bool _ok = true;
        bool _exhausted = false;
    };
}

namespace ZxDepack
{
    bool MegaLz(const uint8_t* in, size_t inSize, uint8_t* out, size_t capacity, size_t& length)
    {
        Stream s(in, inSize, out, capacity, /*words*/ false);
        s.Put(s.Byte());  // the first byte is a literal
        s.LoadBits();

        while (s.ok())
        {
            if (s.Bits(1))  // %1<byte>: a literal
            {
                s.Put(s.Byte());
                continue;
            }

            uint32_t matchLength = 3;
            switch (s.Bits(2))
            {
                case 0:  // %000abc: one byte from -8..-1
                    s.Repeat(static_cast<int32_t>(-8 | static_cast<int32_t>(s.Bits(3))), 1);
                    continue;
                case 1:  // %001<byte>: two bytes from -256..-1
                    s.Repeat(-256 | s.Byte(), 2);
                    continue;
                case 2:  // %010: three bytes
                    break;
                default:  // %011: length 4..255 as a gamma-like code; 9 bits = the end
                {
                    uint32_t bitLength = 0;
                    do
                        bitLength++;
                    while (!s.Bits(1) && s.ok());
                    if (bitLength == 9)
                    {
                        length = s.written();
                        return s.ok();
                    }
                    if (bitLength > 7)
                        return false;
                    matchLength = 2 + (1u << bitLength) + s.Bits(bitLength);
                    break;
                }
            }

            // The displacement of a 3+ byte match: -1..-256 or -257..-4352
            int32_t displacement;
            if (!s.Bits(1))
            {
                displacement = -256 | s.Byte();
            }
            else
            {
                const int32_t high = static_cast<int32_t>(-16 | static_cast<int32_t>(s.Bits(4))) - 1;
                displacement = high * 256 + s.Byte();
            }
            s.Repeat(displacement, matchLength);
        }
        return false;
    }

    bool Hrust(const uint8_t* in, size_t inSize, uint8_t* out, size_t capacity, size_t& length)
    {
        Stream s(in, inSize, out, capacity, /*words*/ true);
        s.LoadBits();
        s.Put(s.Byte());  // the first byte is a literal

        uint32_t expandBits = 2;  // bit length of the expandable displacement
        auto rotateLeft = [](uint32_t b) { return ((b << 1) & 0xFE) | ((b >> 7) & 0x01); };

        while (s.ok())
        {
            if (s.Bits(1))  // %1<byte>: a literal
            {
                s.Put(s.Byte());
                continue;
            }

            int32_t displacement = 0;
            int32_t matchLength = 0;     // -3: an insertion match (repeat, literal, repeat)
            bool needLength = false;
            bool common = false;         // the displacement of 3+ byte matches follows
            bool plusByte = false;       // add the next byte to `displacement`

            switch (s.Bits(2))
            {
                case 0:  // %000abc: one byte from -8..-1
                    s.Repeat(static_cast<int32_t>(-8 | static_cast<int32_t>(s.Bits(3))), 1);
                    continue;
                case 1:  // %001xx: two bytes, or an insertion match, or a window change
                    matchLength = 2;
                    switch (s.Bits(2))
                    {
                        case 0: displacement = -768; plusByte = true; break;  // FDxx
                        case 1: displacement = -512; plusByte = true; break;  // FExx
                        case 2:
                        {
                            uint32_t b = s.Byte();
                            if (b < 0xE0)
                            {
                                displacement = -256 | static_cast<int32_t>(b);
                            }
                            else if (b == 0xFE)
                            {
                                // Longer long displacements from now on (wraps as the Z80 depacker does)
                                if (++expandBits > 8)
                                    expandBits = 1;
                                continue;
                            }
                            else
                            {
                                b = ((rotateLeft(b) ^ 0x02) - 15) & 0xFF;
                                displacement = -256 | static_cast<int32_t>(b);
                                matchLength = -3;
                            }
                            break;
                        }
                        default:  // FFE0 + abcde
                            displacement = -32 | static_cast<int32_t>(s.Bits(5));
                            break;
                    }
                    break;
                case 2:  // %010: three bytes
                    matchLength = 3;
                    common = true;
                    break;
                default:  // %011: variable length
                    needLength = true;
                    common = true;
                    break;
            }

            if (needLength)
            {
                switch (s.Bits(2))
                {
                    case 0:
                        if (s.Bits(1))  // %01100 1abcd <byte>: insertion match from -1..-16
                        {
                            displacement = -16 | static_cast<int32_t>(s.Bits(4));
                            matchLength = -3;
                            common = false;
                        }
                        else if (s.Bits(1))  // %01100 01abcd: 12..42 literal bytes
                        {
                            const uint32_t count = (s.Bits(4) + 6) * 2;
                            for (uint32_t i = 0; i < count && s.ok(); i++)
                                s.Put(s.Byte());
                            continue;
                        }
                        else  // %01100 00abcdefg [<byte>]: long lengths, 15 = the end
                        {
                            const uint32_t b = s.Bits(7);
                            if (b == 15)
                            {
                                length = s.written();
                                return s.ok();
                            }
                            matchLength = b > 15 ? static_cast<int32_t>(b) : static_cast<int32_t>((b << 8) + s.Byte());
                        }
                        break;
                    case 1: matchLength = 4; break;
                    case 2: matchLength = 5; break;
                    default:
                    {
                        matchLength = 6;
                        uint32_t b;
                        do
                        {
                            b = s.Bits(2);
                            matchLength += static_cast<int32_t>(b);
                        } while (b == 3 && matchLength < 15 && s.ok());
                        break;
                    }
                }
            }

            if (common)
            {
                switch (s.Bits(2))
                {
                    case 0:  // FExx
                        displacement = -512;
                        plusByte = true;
                        break;
                    case 1:  // FF00..FFDF, or an insertion match
                    {
                        uint32_t b = s.Byte();
                        if (b < 0xE0)
                        {
                            displacement = -256 | static_cast<int32_t>(b);
                        }
                        else
                        {
                            b = ((rotateLeft(b) ^ 0x03) - 15) & 0xFF;
                            displacement = -256 | static_cast<int32_t>(b);
                            matchLength = -3;
                        }
                        break;
                    }
                    case 2:  // FFE0 + abcde
                        displacement = -32 | static_cast<int32_t>(s.Bits(5));
                        break;
                    default:  // the expandable displacement, then a byte
                    {
                        const int32_t top = -1 * (1 << expandBits);
                        displacement = (top | static_cast<int32_t>(s.Bits(expandBits))) * 256;
                        plusByte = true;
                        break;
                    }
                }
            }
            if (plusByte)
                displacement += s.Byte();

            if (matchLength == -3)
            {
                const uint8_t middle = s.Byte();
                s.Repeat(displacement, 1);
                s.Put(middle);
                s.Repeat(displacement, 1);
            }
            else
            {
                s.Repeat(displacement, static_cast<uint32_t>(matchLength));
            }
        }
        return false;
    }
}
