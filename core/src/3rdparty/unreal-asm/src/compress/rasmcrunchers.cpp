#include "compress/rasmcrunchers.h"

#include <array>
#include <memory>

namespace unrealasm::compress::detail
{
namespace
{
// ---------------------------------------------------------------------------------------------------------------------
// LZ48 v005 / LZ49 v002 (rasm.c LZ48_encode_legacy, LZ49_encode_legacy). The two differ in the window (255 / 511), the
// literal count field (4 / 3 bits) and LZ49's offset bit 8 in the token's top bit.
// Token: literal count (high bits), match length - 3 (low nibble, 15 = extended from 18), then the literals, the
// extended lengths (runs of 255 ended by a smaller byte), and the offset byte (offset - 1). A block without a match
// (the last one) has length nibble 0 and offset byte 0xFF.

struct LzVariant
{
    int window;        // the farthest match: 255 / 511
    int literalField;  // largest literal count the token holds: 15 / 7
    bool offsetBit8;   // LZ49: offsets over 255 set the token's bit 7
};

constexpr LzVariant kLz48{255, 15, false};
constexpr LzVariant kLz49{511, 7, true};

void EncodeExtendedLength(std::vector<uint8_t>& out, int length)
{
    while (length >= 255)
    {
        out.push_back(0xFF);
        length -= 255;
    }
    out.push_back(static_cast<uint8_t>(length));
}

void EncodeBlock(const LzVariant& variant, std::vector<uint8_t>& out, const uint8_t* data, int literalOffset,
                 int literalCount, int offset, int maxLength)
{
    const size_t tokenAt = out.size();
    out.push_back(0);
    int token = 0;
    if (literalCount < variant.literalField)
    {
        token = literalCount << 4;
    }
    else
    {
        token = variant.literalField << 4;
        EncodeExtendedLength(out, literalCount - variant.literalField);
    }
    for (int i = 0; i < literalCount; i++)
    {
        out.push_back(data[literalOffset++]);
    }
    if (maxLength < 18)
    {
        if (maxLength > 2)
        {
            token |= maxLength - 3;
        }
        // else: the end block has no length
    }
    else
    {
        token |= 0xF;
        EncodeExtendedLength(out, maxLength - 18);
    }
    if (variant.offsetBit8 && offset > 255)
    {
        token |= 0x80;
        offset -= 256;
    }
    // rasm: (unsigned char)offset-1, an int stored back as a byte: offset 0 (the end block) gives 0xFF
    out.push_back(static_cast<uint8_t>(static_cast<uint8_t>(offset) - 1));
    out[tokenAt] = static_cast<uint8_t>(token);
}

std::vector<uint8_t> LzEncode(const LzVariant& variant, const uint8_t* data, size_t size)
{
    const int length = static_cast<int>(size);
    std::vector<uint8_t> out;
    out.reserve(size + size / 2 + 10);

    // The first byte is always a literal
    out.push_back(data[0]);

    // Short data: one block, no match
    if (length < 5)
    {
        out.push_back(static_cast<uint8_t>((length - 1) << 4));
        for (int i = 1; i < length; i++)
        {
            out.push_back(data[i]);
        }
        out.push_back(0xFF);
        return out;
    }

    int current = 1;
    int literal = 0;
    int literalOffset = 1;
    int maxOffset = 0;
    while (current < length)
    {
        int maxLength = 0;
        int startScan = current - variant.window;
        if (startScan < 0)
        {
            startScan = 0;
        }
        // The longest match, the farthest one on a tie (the scan goes from far to near and keeps the first)
        while (startScan < current)
        {
            int matchLength = 0;
            int curScan = current;
            for (int i = startScan; curScan < length; i++)
            {
                if (data[i] == data[curScan++])
                {
                    matchLength++;
                }
                else
                {
                    break;
                }
            }
            if (matchLength >= 3 && matchLength > maxLength)
            {
                maxOffset = startScan;
                maxLength = matchLength;
            }
            startScan++;
        }
        if (maxLength)
        {
            EncodeBlock(variant, out, data, literalOffset, literal, current - maxOffset, maxLength);
            current += maxLength;
            literalOffset = current;
            literal = 0;
        }
        else
        {
            literal++;
            current++;
        }
    }
    EncodeBlock(variant, out, data, literalOffset, literal, 0, 0);
    return out;
}

class ByteReader
{
public:
    ByteReader(const uint8_t* data, size_t size) : _data(data), _size(size) {}

    bool Read(int& value)
    {
        if (_pos >= _size)
        {
            return false;
        }
        value = _data[_pos++];
        return true;
    }

    bool ReadExtended(int& value)
    {
        int b = 0;
        do
        {
            if (!Read(b))
            {
                return false;
            }
            value += b;
        } while (b == 255);
        return true;
    }

    size_t Position() const
    {
        return _pos;
    }

private:
    const uint8_t* _data;
    size_t _size;
    size_t _pos = 0;
};

std::optional<std::vector<uint8_t>> LzDecode(const LzVariant& variant, const uint8_t* data, size_t size, size_t limit)
{
    ByteReader in(data, size);
    std::vector<uint8_t> out;
    int first = 0;
    if (!in.Read(first))
    {
        return std::nullopt;
    }
    out.push_back(static_cast<uint8_t>(first));
    for (;;)
    {
        int token = 0;
        if (!in.Read(token))
        {
            return std::nullopt;
        }
        int literals = (token >> 4) & variant.literalField;
        if (literals == variant.literalField && !in.ReadExtended(literals))
        {
            return std::nullopt;
        }
        for (int i = 0; i < literals; i++)
        {
            int b = 0;
            if (!in.Read(b) || out.size() >= limit)
            {
                return std::nullopt;
            }
            out.push_back(static_cast<uint8_t>(b));
        }
        int length = token & 0xF;
        if (length == 15)
        {
            length = 18;
            if (!in.ReadExtended(length))
            {
                return std::nullopt;
            }
        }
        else
        {
            length += 3;
        }
        int offsetByte = 0;
        if (!in.Read(offsetByte))
        {
            return std::nullopt;
        }
        const bool high = variant.offsetBit8 && (token & 0x80) != 0;
        if (offsetByte == 0xFF && !high)
        {
            return out;  // the end block
        }
        // The byte is offset-1 in 8 bits: with the high bit, offset 256 is stored as 0xFF (0 - 1)
        const size_t offset =
            high ? 256 + (static_cast<size_t>(offsetByte + 1) & 0xFF) : static_cast<size_t>(offsetByte) + 1;
        if (offset > out.size() || out.size() + static_cast<size_t>(length) > limit)
        {
            return std::nullopt;
        }
        for (int i = 0; i < length; i++)
        {
            out.push_back(out[out.size() - offset]);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// ZX7 (zx7.h: optimize.c and compress.c of ZX7 by Einar Saukas, as rasm carries them). The integer types follow the
// original (size_t and int mixed) so that every comparison takes the same branch.

constexpr int kZx7MaxOffset = 2176;   // range 1..2176
constexpr size_t kZx7MaxLen = 65536;  // range 2..65536

struct Zx7Match
{
    size_t index = 0;
    Zx7Match* next = nullptr;
};

struct Zx7Optimal
{
    size_t bits = 0;
    int offset = 0;
    int len = 0;
};

int Zx7EliasGammaBits(int value)
{
    int bits = 1;
    while (value > 1)
    {
        bits += 2;
        value >>= 1;
    }
    return bits;
}

int Zx7CountBits(int offset, int len)
{
    return 1 + (offset > 128 ? 12 : 8) + Zx7EliasGammaBits(len - 1);
}

std::vector<Zx7Optimal> Zx7Optimize(const uint8_t* inputData, int inputSize)
{
    std::vector<size_t> min(kZx7MaxOffset + 1, 0);
    std::vector<size_t> max(kZx7MaxOffset + 1, 0);
    std::vector<Zx7Match> matches(256 * 256);
    std::vector<Zx7Match> matchSlots(static_cast<size_t>(inputSize));
    std::vector<Zx7Optimal> optimal(static_cast<size_t>(inputSize));

    // The first byte is always a literal
    optimal[0].bits = 8;

    for (size_t i = 1; i < static_cast<size_t>(inputSize); i++)
    {
        optimal[i].bits = optimal[i - 1].bits + 9;
        const int matchIndex = inputData[i - 1] << 8 | inputData[i];
        size_t bestLen = 1;
        for (Zx7Match* match = &matches[static_cast<size_t>(matchIndex)];
             match->next != nullptr && bestLen < kZx7MaxLen; match = match->next)
        {
            const int offset = static_cast<int>(i - match->next->index);
            if (offset > kZx7MaxOffset)
            {
                match->next = nullptr;
                break;
            }
            const size_t uoffset = static_cast<size_t>(offset);
            size_t len = 2;
            for (; len <= kZx7MaxLen; len++)
            {
                if (len > bestLen)
                {
                    bestLen = len;
                    const size_t bits =
                        optimal[i - len].bits + static_cast<size_t>(Zx7CountBits(offset, static_cast<int>(len)));
                    if (optimal[i].bits > bits)
                    {
                        optimal[i].bits = bits;
                        optimal[i].offset = offset;
                        optimal[i].len = static_cast<int>(len);
                    }
                }
                else if (i + 1 == max[uoffset] + len && max[uoffset] != 0)
                {
                    len = i - min[uoffset];
                    if (len > bestLen)
                    {
                        len = bestLen;
                    }
                }
                if (i < uoffset + len || inputData[i - len] != inputData[i - len - uoffset])
                {
                    break;
                }
            }
            min[uoffset] = i + 1 - len;
            max[uoffset] = i;
        }
        matchSlots[i].index = i;
        matchSlots[i].next = matches[static_cast<size_t>(matchIndex)].next;
        matches[static_cast<size_t>(matchIndex)].next = &matchSlots[i];
    }
    return optimal;
}

class Zx7Writer
{
public:
    explicit Zx7Writer(size_t size) : _out(size, 0) {}

    void WriteByte(int value)
    {
        _out[_index++] = static_cast<uint8_t>(value);
    }

    void WriteBit(int value)
    {
        if (_mask == 0)
        {
            _mask = 128;
            _bitIndex = _index;
            WriteByte(0);
        }
        if (value > 0)
        {
            _out[_bitIndex] |= static_cast<uint8_t>(_mask);
        }
        _mask >>= 1;
    }

    void WriteEliasGamma(int value)
    {
        int i = 2;
        for (; i <= value; i <<= 1)
        {
            WriteBit(0);
        }
        while ((i >>= 1) > 0)
        {
            WriteBit(value & i);
        }
    }

    std::vector<uint8_t> Take()
    {
        return std::move(_out);
    }

private:
    std::vector<uint8_t> _out;
    size_t _index = 0;
    size_t _bitIndex = 0;
    int _mask = 0;
};

class BitReader
{
public:
    BitReader(const uint8_t* data, size_t size) : _bytes(data, size) {}

    bool Byte(int& value)
    {
        return _bytes.Read(value);
    }

    // ZX7: control bits from bit 7 down, a new control byte read in stream order when the last one is used up
    bool Bit(int& value)
    {
        if (_mask == 0)
        {
            if (!_bytes.Read(_bits))
            {
                return false;
            }
            _mask = 128;
        }
        value = (_bits & _mask) != 0 ? 1 : 0;
        _mask >>= 1;
        return true;
    }

private:
    ByteReader _bytes;
    int _bits = 0;
    int _mask = 0;
};
}  // namespace

std::vector<uint8_t> Lz48Encode(const uint8_t* data, size_t size)
{
    return LzEncode(kLz48, data, size);
}

std::vector<uint8_t> Lz49Encode(const uint8_t* data, size_t size)
{
    return LzEncode(kLz49, data, size);
}

std::optional<std::vector<uint8_t>> Lz48Decode(const uint8_t* data, size_t size, size_t limit)
{
    return LzDecode(kLz48, data, size, limit);
}

std::optional<std::vector<uint8_t>> Lz49Decode(const uint8_t* data, size_t size, size_t limit)
{
    return LzDecode(kLz49, data, size, limit);
}

std::vector<uint8_t> Zx7Encode(const uint8_t* data, size_t size)
{
    const int inputSize = static_cast<int>(size);
    std::vector<Zx7Optimal> optimal = Zx7Optimize(data, inputSize);

    size_t inputIndex = static_cast<size_t>(inputSize - 1);
    const int outputSize = static_cast<int>((optimal[inputIndex].bits + 18 + 7) / 8);
    Zx7Writer out(static_cast<size_t>(outputSize));

    // Un-reverse the optimal sequence: bits now links each step to the next one
    optimal[inputIndex].bits = 0;
    while (inputIndex > 0)
    {
        const size_t inputPrev =
            inputIndex - (optimal[inputIndex].len > 0 ? static_cast<size_t>(optimal[inputIndex].len) : 1);
        optimal[inputPrev].bits = inputIndex;
        inputIndex = inputPrev;
    }

    // The first byte is always a literal
    out.WriteByte(data[0]);
    while ((inputIndex = optimal[inputIndex].bits) > 0)
    {
        if (optimal[inputIndex].len == 0)
        {
            out.WriteBit(0);
            out.WriteByte(data[inputIndex]);
        }
        else
        {
            out.WriteBit(1);
            out.WriteEliasGamma(optimal[inputIndex].len - 1);
            int offset1 = optimal[inputIndex].offset - 1;
            if (offset1 < 128)
            {
                out.WriteByte(offset1);
            }
            else
            {
                offset1 -= 128;
                out.WriteByte((offset1 & 127) | 128);
                for (int mask = 1024; mask > 127; mask >>= 1)
                {
                    out.WriteBit(offset1 & mask);
                }
            }
        }
    }
    // End marker: a sequence whose length does not fit 16 bits
    out.WriteBit(1);
    for (int i = 0; i < 16; i++)
    {
        out.WriteBit(0);
    }
    out.WriteBit(1);
    return out.Take();
}

std::optional<std::vector<uint8_t>> Zx7Decode(const uint8_t* data, size_t size, size_t limit)
{
    BitReader in(data, size);
    std::vector<uint8_t> out;
    int value = 0;
    if (!in.Byte(value))
    {
        return std::nullopt;
    }
    out.push_back(static_cast<uint8_t>(value));
    for (;;)
    {
        int bit = 0;
        if (!in.Bit(bit))
        {
            return std::nullopt;
        }
        if (bit == 0)
        {
            if (!in.Byte(value) || out.size() >= limit)
            {
                return std::nullopt;
            }
            out.push_back(static_cast<uint8_t>(value));
            continue;
        }
        int zeros = 0;
        for (;;)
        {
            if (!in.Bit(bit))
            {
                return std::nullopt;
            }
            if (bit)
            {
                break;
            }
            zeros++;
        }
        if (zeros >= 16)
        {
            return out;  // the end marker
        }
        int gamma = 1;
        for (int i = 0; i < zeros; i++)
        {
            if (!in.Bit(bit))
            {
                return std::nullopt;
            }
            gamma = gamma << 1 | bit;
        }
        const size_t length = static_cast<size_t>(gamma) + 1;
        int offsetByte = 0;
        if (!in.Byte(offsetByte))
        {
            return std::nullopt;
        }
        int offset1 = offsetByte & 127;
        if (offsetByte & 128)
        {
            int high = 0;
            for (int i = 0; i < 4; i++)
            {
                if (!in.Bit(bit))
                {
                    return std::nullopt;
                }
                high = high << 1 | bit;
            }
            offset1 = (high << 7 | offset1) + 128;
        }
        const size_t offset = static_cast<size_t>(offset1) + 1;
        if (offset > out.size() || out.size() + length > limit)
        {
            return std::nullopt;
        }
        for (size_t i = 0; i < length; i++)
        {
            out.push_back(out[out.size() - offset]);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// aPLib decoder (apultra expand.c apultra_decompress): the first byte is a literal; then '0' literal, '10' gamma2
// offset (a rep-match when the high part is below the literal-follow count) and gamma2 length, '110' a byte of
// 7-bit offset and 1-bit length (0 = end), '111' a 4-bit offset of one byte (0 writes a zero). Bits as ZX7's.

namespace
{
bool ApGamma2(BitReader& in, unsigned int& value)
{
    value = 1;
    int bit = 0;
    do
    {
        int next = 0;
        if (!in.Bit(next) || value > 0x00FFFFFFu)
        {
            return false;
        }
        value = (value << 1) + static_cast<unsigned int>(next);
        if (!in.Bit(bit))
        {
            return false;
        }
    } while (bit);
    return true;
}

bool ApCopy(std::vector<uint8_t>& out, size_t offset, size_t length, size_t limit)
{
    if (offset == 0 || offset > out.size() || length > limit - out.size())
    {
        return false;
    }
    for (size_t i = 0; i < length; i++)
    {
        out.push_back(out[out.size() - offset]);
    }
    return true;
}
}  // namespace

std::optional<std::vector<uint8_t>> ApUltraDecode(const uint8_t* data, size_t size, size_t limit)
{
    constexpr size_t kMinMatch3Offset = 1280;
    constexpr size_t kMinMatch4Offset = 32000;
    BitReader in(data, size);
    std::vector<uint8_t> out;
    int value = 0;
    if (!in.Byte(value) || limit == 0)
    {
        return std::nullopt;
    }
    out.push_back(static_cast<uint8_t>(value));
    size_t matchOffset = 0;
    unsigned int followsLiteral = 3;
    for (;;)
    {
        int bit = 0;
        if (!in.Bit(bit))
        {
            return std::nullopt;
        }
        if (bit == 0)
        {
            if (!in.Byte(value) || out.size() >= limit)
            {
                return std::nullopt;
            }
            out.push_back(static_cast<uint8_t>(value));
            followsLiteral = 3;
            continue;
        }
        if (!in.Bit(bit))
        {
            return std::nullopt;
        }
        if (bit == 0)
        {
            unsigned int high = 0;
            unsigned int length = 0;
            if (!ApGamma2(in, high))
            {
                return std::nullopt;
            }
            if (high >= followsLiteral)
            {
                if (!in.Byte(value) || !ApGamma2(in, length))
                {
                    return std::nullopt;
                }
                matchOffset = static_cast<size_t>(high - followsLiteral) << 8 | static_cast<size_t>(value);
                if (matchOffset < 128 || matchOffset >= kMinMatch4Offset)
                {
                    length += 2;
                }
                else if (matchOffset >= kMinMatch3Offset)
                {
                    length++;
                }
            }
            else if (!ApGamma2(in, length))  // a rep-match: the last offset again
            {
                return std::nullopt;
            }
            followsLiteral = 2;
            if (!ApCopy(out, matchOffset, length, limit))
            {
                return std::nullopt;
            }
            continue;
        }
        if (!in.Bit(bit))
        {
            return std::nullopt;
        }
        if (bit == 0)
        {
            if (!in.Byte(value))
            {
                return std::nullopt;
            }
            if (value == 0)
            {
                return out;  // the end marker
            }
            matchOffset = static_cast<size_t>(value) >> 1;
            followsLiteral = 2;
            if (!ApCopy(out, matchOffset, static_cast<size_t>(value & 1) + 2, limit))
            {
                return std::nullopt;
            }
            continue;
        }
        int shortOffset = 0;
        for (int i = 0; i < 4; i++)
        {
            if (!in.Bit(bit))
            {
                return std::nullopt;
            }
            shortOffset = shortOffset << 1 | bit;
        }
        followsLiteral = 3;
        if (shortOffset == 0)
        {
            if (out.size() >= limit)
            {
                return std::nullopt;
            }
            out.push_back(0);
        }
        else if (!ApCopy(out, static_cast<size_t>(shortOffset), 1, limit))
        {
            return std::nullopt;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Exomizer 2 raw decoder (exomizer.h dec_ctx_init, dec_ctx_decrunch): bits LSB first from a bit buffer refilled with
// a byte when it holds only its marker; a table of 52 entries (4 bits each) gives base and extra bits of the lengths
// and offsets.

std::optional<std::vector<uint8_t>> ExomizerDecode(const uint8_t* data, size_t size, size_t limit)
{
    ByteReader in(data, size);
    unsigned int bitBuffer = 0;
    bool ok = true;

    auto getByte = [&]() -> int {
        int b = 0;
        if (!in.Read(b))
        {
            ok = false;
        }
        return b;
    };
    auto getBits = [&](int count) -> int {
        int value = 0;
        while (count-- > 0 && ok)
        {
            if ((bitBuffer & 0x1FF) == 1)
            {
                bitBuffer = static_cast<unsigned int>(getByte()) | 0x100;
            }
            value <<= 1;
            value |= static_cast<int>(bitBuffer & 1);
            bitBuffer >>= 1;
        }
        return value;
    };

    bitBuffer = static_cast<unsigned int>(getByte());

    std::array<int, 52> base{};
    std::array<int, 52> extraBits{};
    constexpr std::array<int, 3> kTableBit{2, 4, 4};
    constexpr std::array<int, 3> kTableOff{48, 32, 16};
    unsigned int a = 0;
    unsigned int b = 0;
    for (size_t i = 0; i < base.size(); i++)
    {
        if (i & 0xF)
        {
            a += 1u << b;
        }
        else
        {
            a = 1;
        }
        base[i] = static_cast<int>(a & 0xFFFF);
        b = static_cast<unsigned int>(getBits(4));
        extraBits[i] = static_cast<int>(b);
    }
    auto cooked = [&](int index) -> int {
        return base[static_cast<size_t>(index)] + getBits(extraBits[static_cast<size_t>(index)]);
    };

    std::vector<uint8_t> out;
    while (ok)
    {
        int length = 0;
        bool literal = false;
        size_t source = 0;
        if (getBits(1))
        {
            length = 1;
            literal = true;
        }
        else
        {
            int gamma = 0;
            while (ok && getBits(1) == 0)
            {
                ++gamma;
                if (gamma > 17)
                {
                    return std::nullopt;
                }
            }
            if (!ok)
            {
                break;
            }
            if (gamma == 16)
            {
                return out;  // done
            }
            if (gamma == 17)
            {
                length = getBits(16);
                literal = true;
            }
            else
            {
                length = cooked(gamma);
                const size_t i = static_cast<size_t>((length > 3 ? 3 : length) - 1);
                const int index = kTableOff[i] + getBits(kTableBit[i]);
                const int offset = cooked(index);
                if (static_cast<size_t>(offset) > out.size() || offset <= 0)
                {
                    return std::nullopt;
                }
                source = out.size() - static_cast<size_t>(offset);
            }
        }
        if (!ok || length <= 0 || out.size() + static_cast<size_t>(length) > limit)
        {
            return std::nullopt;
        }
        for (int i = 0; i < length; i++)
        {
            out.push_back(literal ? static_cast<uint8_t>(getByte()) : out[source++]);
        }
    }
    return std::nullopt;
}
}  // namespace unrealasm::compress::detail
