// POC 055: a frame as bit planes of its colours' indices. 64 pixels a word, so "these two pixels are equal" for a whole
// row of a shifted picture is a few word operations per plane.
//
// A row is kStride words: one margin word, the picture's pixels from bit 0 of word 1 (pixel x is bit x & 63 of word
// 1 + x / 64), margin to the end. kMarginRows rows above and below. The margins repeat the edge pixel, as
// smoothmotion::Picture::At clamps: a read up to 63 pixels or kMarginRows rows outside is the reference's read.
#pragma once

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace poc055
{
constexpr int kMaxPlanes = 8;
constexpr int kMarginRows = 24;

/// The colours of a clip, in the order met: every frame's planes index the same list
struct Palette
{
    std::vector<uint32_t> colours;
    std::unordered_map<uint32_t, uint8_t> indexOf;

    /// -1: more than 256 colours
    int Index(uint32_t colour)
    {
        const auto it = indexOf.find(colour);
        if (it != indexOf.end())
            return it->second;
        if (colours.size() >= 256)
            return -1;
        indexOf.emplace(colour, static_cast<uint8_t>(colours.size()));
        colours.push_back(colour);
        return static_cast<int>(colours.size()) - 1;
    }
    int Planes() const
    {
        int planes = 1;
        while ((1u << planes) < colours.size())
            planes++;
        return planes;
    }
};

struct Planes
{
    int width = 0, height = 0, stride = 0, planes = 0;
    std::vector<uint64_t> bits;  ///< planes * (height + 2 * kMarginRows) * stride

    const uint64_t* Row(int plane, int y) const
    {
        return bits.data() + (static_cast<size_t>(plane) * (height + 2 * kMarginRows) + (y + kMarginRows)) * stride;
    }
    uint64_t* Row(int plane, int y)
    {
        return bits.data() + (static_cast<size_t>(plane) * (height + 2 * kMarginRows) + (y + kMarginRows)) * stride;
    }
    int Words() const { return (width + 63) / 64; }  ///< the picture's words of a row, from word 1

    /// `planeCount`: the clip's (the palette may grow after a frame is packed; every frame of a pair the same count)
    bool Pack(const uint32_t* pixels, int w, int h, Palette& palette, int planeCount)
    {
        width = w;
        height = h;
        planes = planeCount;
        stride = Words() + 2;
        bits.assign(static_cast<size_t>(planes) * (height + 2 * kMarginRows) * stride, 0);
        for (int y = 0; y < height; y++)
        {
            const uint32_t* row = pixels + static_cast<size_t>(y) * width;
            uint32_t lastColour = ~row[0];
            int index = 0;
            int first = 0, last = 0;
            // eight pixels' indices in a word, then each plane's eight bits out of it with one multiply
            for (int x = 0; x < width; x += 8)
            {
                const int count = width - x < 8 ? width - x : 8;
                uint64_t eight = 0;
                for (int i = 0; i < count; i++)
                {
                    if (row[x + i] != lastColour)
                    {
                        lastColour = row[x + i];
                        index = palette.Index(lastColour);
                        if (index < 0 || index >= (1 << planes))
                            return false;
                    }
                    eight |= static_cast<uint64_t>(index) << (8 * i);
                }
                if (x == 0)
                    first = static_cast<int>(eight & 0xFF);
                last = index;
                if (!eight)
                    continue;
                for (int p = 0; p < planes; p++)
                {
                    const uint64_t bits = (((eight >> p) & 0x0101010101010101ull) * 0x0102040810204080ull) >> 56;
                    Row(p, y)[1 + (x >> 6)] |= bits << (x & 63);
                }
            }
            // the margins: the edge pixels again
            for (int p = 0; p < planes; p++)
            {
                uint64_t* r = Row(p, y);
                if (first & (1 << p))
                    r[0] = ~0ull;
                if (last & (1 << p))
                {
                    const int tail = width & 63;
                    if (tail)
                        r[Words()] |= ~0ull << tail;
                    r[Words() + 1] = ~0ull;
                }
            }
        }
        for (int p = 0; p < planes; p++)
            for (int m = 1; m <= kMarginRows; m++)
            {
                std::memcpy(Row(p, -m), Row(p, 0), sizeof(uint64_t) * stride);
                std::memcpy(Row(p, height - 1 + m), Row(p, height - 1), sizeof(uint64_t) * stride);
            }
        return true;
    }
};

/// Word `j` of a row read `dx` pixels to the right: bit i is the row's pixel 64 (j - 1) + i + dx. |dx| < 64
inline uint64_t Shifted(const uint64_t* row, int j, int dx)
{
    if (dx == 0)
        return row[j];
    if (dx > 0)
        return (row[j] >> dx) | (row[j + 1] << (64 - dx));
    return (row[j] << -dx) | (row[j - 1] >> (64 + dx));
}

/// `words` words of a row from word 1, read `dx` pixels to the right: out[w] = Shifted(row, w + 1, dx). The shift's
/// direction is decided once a row, not once a word
inline void ShiftedRow(const uint64_t* row, int dx, int words, uint64_t* out)
{
    if (dx == 0)
        for (int w = 0; w < words; w++)
            out[w] = row[w + 1];
    else if (dx > 0)
        for (int w = 0; w < words; w++)
            out[w] = (row[w + 1] >> dx) | (row[w + 2] << (64 - dx));
    else
        for (int w = 0; w < words; w++)
            out[w] = (row[w + 1] << -dx) | (row[w] >> (64 + dx));
}

/// The ones of each byte of `word`, in that byte (8 at most)
inline uint64_t ByteOnes(uint64_t word)
{
    word = word - ((word >> 1) & 0x5555555555555555ull);
    word = (word & 0x3333333333333333ull) + ((word >> 2) & 0x3333333333333333ull);
    return (word + (word >> 4)) & 0x0F0F0F0F0F0F0F0Full;
}
}  // namespace poc055
