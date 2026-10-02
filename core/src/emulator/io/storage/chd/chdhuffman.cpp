#include "stdafx.h"

#include "chdhuffman.h"

#include <algorithm>

namespace chd
{
    HuffmanCoder::HuffmanCoder(int numCodes, int maxBits)
        : _numCodes(numCodes),
          _maxBits(maxBits),
          _histo(static_cast<size_t>(numCodes), 0),
          _lengths(static_cast<size_t>(numCodes), 0),
          _codes(static_cast<size_t>(numCodes), 0),
          _lookup(static_cast<size_t>(1) << maxBits, 0)
    {
    }

    void HuffmanCoder::HistoReset()
    {
        std::fill(_histo.begin(), _histo.end(), 0u);
    }

    /// region <Tree construction>

    int HuffmanCoder::BuildTree(uint32_t totalData, uint32_t totalWeight)
    {
        // Leaves 0..n-1, inner nodes after them
        std::vector<Node> nodes(static_cast<size_t>(_numCodes) * 2);
        std::vector<int> list;
        list.reserve(static_cast<size_t>(_numCodes) * 2);
        for (int code = 0; code < _numCodes; code++)
        {
            if (_histo[code] == 0)
                continue;
            uint64_t weight = static_cast<uint64_t>(_histo[code]) * totalWeight / totalData;
            nodes[code].weight = weight ? static_cast<uint32_t>(weight) : 1u;
            list.push_back(code);
        }

        // Heaviest first; equal weights by symbol
        std::stable_sort(list.begin(), list.end(), [&nodes](int a, int b) {
            if (nodes[a].weight != nodes[b].weight)
                return nodes[a].weight > nodes[b].weight;
            return a < b;
        });

        int next = _numCodes;
        while (list.size() > 1)
        {
            const int low1 = list.back();
            list.pop_back();
            const int low0 = list.back();
            list.pop_back();
            const int inner = next++;
            nodes[low0].parent = nodes[low1].parent = inner;
            nodes[inner].weight = nodes[low0].weight + nodes[low1].weight;
            // Before the first lighter node: after every node as heavy
            size_t at = 0;
            while (at < list.size() && nodes[inner].weight <= nodes[list[at]].weight)
                at++;
            list.insert(list.begin() + static_cast<std::ptrdiff_t>(at), inner);
        }

        int maxBits = 0;
        for (int code = 0; code < _numCodes; code++)
        {
            _lengths[code] = 0;
            if (_histo[code] == 0)
                continue;
            int depth = 0;
            for (int n = code; nodes[n].parent >= 0; n = nodes[n].parent)
                depth++;
            if (depth == 0)
                depth = 1;  // a lone symbol still takes one bit
            _lengths[code] = static_cast<uint8_t>(std::min(depth, 255));
            maxBits = std::max(maxBits, depth);
        }
        return maxBits;
    }

    bool HuffmanCoder::ComputeTreeFromHisto()
    {
        uint32_t total = 0;
        for (uint32_t count : _histo)
            total += count;
        if (total == 0)
        {
            std::fill(_lengths.begin(), _lengths.end(), static_cast<uint8_t>(0));
            return AssignCanonicalCodes();
        }

        // Scale the weights down until the longest code fits maxBits
        uint32_t lower = 0;
        uint32_t upper = total * 2;
        while (true)
        {
            const uint32_t weight = (upper + lower) / 2;
            const int bits = BuildTree(total, weight);
            if (bits <= _maxBits)
            {
                lower = weight;
                if (weight == total || upper - lower <= 1)
                    break;
            }
            else
            {
                upper = weight;
            }
        }
        // The last build may be one that did not fit: build the good one again
        if (BuildTree(total, lower) > _maxBits)
            return false;
        return AssignCanonicalCodes();
    }

    bool HuffmanCoder::AssignCanonicalCodes()
    {
        uint32_t histo[33] = {};
        for (int code = 0; code < _numCodes; code++)
        {
            if (_lengths[code] > _maxBits)
                return false;
            histo[_lengths[code]]++;
        }
        // Longest codes take the lowest numbers
        uint32_t start = 0;
        for (int length = 32; length > 0; length--)
        {
            const uint32_t next = (start + histo[length]) >> 1;
            if (length != 1 && next * 2 != start + histo[length])
                return false;
            histo[length] = start;
            start = next;
        }
        for (int code = 0; code < _numCodes; code++)
            _codes[code] = _lengths[code] ? histo[_lengths[code]]++ : 0;
        return true;
    }

    void HuffmanCoder::BuildLookupTable()
    {
        // A complete code fills every entry; only an incomplete one (a lone
        // symbol) leaves holes, which read as symbol 0 taking no bits, as in MAME
        const uint32_t size = static_cast<uint32_t>(_lookup.size());
        uint64_t covered = 0;
        for (int code = 0; code < _numCodes; code++)
        {
            if (_lengths[code])
                covered += static_cast<uint64_t>(size) >> _lengths[code];
        }
        if (covered < size)
            std::fill(_lookup.begin(), _lookup.end(), 0u);
        for (int code = 0; code < _numCodes; code++)
        {
            const int length = _lengths[code];
            if (length == 0)
                continue;
            const int shift = _maxBits - length;
            const uint32_t first = _codes[code] << shift;
            const uint32_t last = std::min(size, (_codes[code] + 1) << shift);
            for (uint32_t i = first; i < last; i++)
                _lookup[i] = (static_cast<uint32_t>(code) << 5) | static_cast<uint32_t>(length);
        }
    }

    /// endregion </Tree construction>

    /// region <RLE tree>

    namespace
    {
        int RleFieldBits(int maxBits)
        {
            return maxBits >= 16 ? 5 : (maxBits >= 8 ? 4 : 3);
        }

        void WriteRleRun(BitWriter& out, int value, int count, int bits)
        {
            while (count > 0)
            {
                if (value == 1)
                {
                    // 1 is the escape: a literal 1 is written twice
                    out.Write(1, bits);
                    out.Write(1, bits);
                    count--;
                }
                else if (count <= 2)
                {
                    out.Write(static_cast<uint32_t>(value), bits);
                    count--;
                }
                else
                {
                    const int repeats = std::min(count - 3, (1 << bits) - 1);
                    out.Write(1, bits);
                    out.Write(static_cast<uint32_t>(value), bits);
                    out.Write(static_cast<uint32_t>(repeats), bits);
                    count -= repeats + 3;
                }
            }
        }
    }  // namespace

    bool HuffmanCoder::ExportTreeRle(BitWriter& out) const
    {
        const int bits = RleFieldBits(_maxBits);
        int last = -1;
        int count = 0;
        for (int code = 0; code < _numCodes; code++)
        {
            const int value = _lengths[code];
            if (value == last)
            {
                count++;
                continue;
            }
            if (count)
                WriteRleRun(out, last, count, bits);
            last = value;
            count = 1;
        }
        WriteRleRun(out, last, count, bits);
        return !out.Overflow();
    }

    bool HuffmanCoder::ImportTreeRle(BitReader& in)
    {
        const int bits = RleFieldBits(_maxBits);
        int code = 0;
        while (code < _numCodes)
        {
            uint32_t value = in.Read(bits);
            if (value != 1)
            {
                _lengths[code++] = static_cast<uint8_t>(value);
                continue;
            }
            value = in.Read(bits);
            if (value == 1)
            {
                _lengths[code++] = 1;
                continue;
            }
            const int repeats = static_cast<int>(in.Read(bits)) + 3;
            if (code + repeats > _numCodes)
                return false;
            for (int i = 0; i < repeats; i++)
                _lengths[code++] = static_cast<uint8_t>(value);
            if (in.Overflow())
                return false;
        }
        if (!AssignCanonicalCodes())
            return false;
        BuildLookupTable();
        return !in.Overflow();
    }

    /// endregion </RLE tree>

    /// region <Huffman-coded tree>

    namespace
    {
        uint8_t RleFullBits(int numCodes)
        {
            return BitWidth(static_cast<uint32_t>(numCodes - 9));
        }
    }  // namespace

    bool HuffmanCoder::ExportTreeHuffman(BitWriter& out) const
    {
        // Run-length code the lengths: a token is length + 1, or 0 for "the last
        // length again, count in the side list"
        std::vector<uint8_t> tokens;
        std::vector<int> runs;
        HuffmanCoder small(24, 6);
        int last = -1;
        int repeats = 0;
        auto flush = [&]() {
            if (repeats == 1)
            {
                tokens.push_back(static_cast<uint8_t>(last + 1));
                small.HistoOne(static_cast<uint32_t>(last + 1));
            }
            else if (repeats > 1)
            {
                tokens.push_back(0);
                small.HistoOne(0);
                runs.push_back(repeats - 2);
            }
        };
        for (int code = 0; code < _numCodes; code++)
        {
            const int value = _lengths[code];
            if (value != last && repeats > 0)
                flush();
            if (value == last)
            {
                repeats++;
            }
            else
            {
                tokens.push_back(static_cast<uint8_t>(value + 1));
                small.HistoOne(static_cast<uint32_t>(value + 1));
                last = value;
                repeats = 0;
            }
        }
        flush();

        if (!small.ComputeTreeFromHisto())
            return false;

        int firstNonZero = 31;
        int lastNonZero = 0;
        for (int i = 1; i < 24; i++)
        {
            if (small._lengths[i] != 0)
            {
                if (firstNonZero == 31)
                    firstNonZero = i;
                lastNonZero = i;
            }
        }
        firstNonZero = std::min(firstNonZero, 8);
        if (lastNonZero >= 23)
            return false;  // the terminator would not be read back (never happens: lengths are at most 16)

        out.Write(small._lengths[0], 3);
        out.Write(static_cast<uint32_t>(firstNonZero - 1), 3);
        for (int i = firstNonZero; i <= lastNonZero; i++)
            out.Write(small._lengths[i], 3);
        out.Write(7, 3);

        const uint8_t fullBits = RleFullBits(_numCodes);
        size_t run = 0;
        for (uint8_t token : tokens)
        {
            small.EncodeOne(out, token);
            if (token == 0)
            {
                const int count = runs[run++];
                if (count < 7)
                {
                    out.Write(static_cast<uint32_t>(count), 3);
                }
                else
                {
                    out.Write(7, 3);
                    out.Write(static_cast<uint32_t>(count - 7), fullBits);
                }
            }
        }
        return !out.Overflow();
    }

    bool HuffmanCoder::ImportTreeHuffman(BitReader& in)
    {
        HuffmanCoder small(24, 6);
        small._lengths[0] = static_cast<uint8_t>(in.Read(3));
        const int start = static_cast<int>(in.Read(3)) + 1;
        int count = 0;
        for (int i = 1; i < 24; i++)
        {
            if (i < start || count == 7)
            {
                small._lengths[i] = 0;
            }
            else
            {
                count = static_cast<int>(in.Read(3));
                small._lengths[i] = static_cast<uint8_t>(count == 7 ? 0 : count);
            }
        }
        if (!small.AssignCanonicalCodes())
            return false;
        small.BuildLookupTable();

        const uint8_t fullBits = RleFullBits(_numCodes);
        int last = 0;
        int code = 0;
        while (code < _numCodes)
        {
            const int value = static_cast<int>(small.DecodeOne(in));
            if (value != 0)
            {
                _lengths[code++] = static_cast<uint8_t>(last = value - 1);
                continue;
            }
            int repeats = static_cast<int>(in.Read(3)) + 2;
            if (repeats == 7 + 2)
                repeats += static_cast<int>(in.Read(fullBits));
            for (; repeats != 0 && code < _numCodes; repeats--)
                _lengths[code++] = static_cast<uint8_t>(last);
            if (in.Overflow())
                return false;
        }
        if (!AssignCanonicalCodes())
            return false;
        BuildLookupTable();
        return !in.Overflow();
    }

    /// endregion </Huffman-coded tree>

    bool Huffman8Encode(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t capacity, uint32_t& written)
    {
        HuffmanCoder coder(256, 16);
        for (uint32_t i = 0; i < length; i++)
            coder.HistoOne(src[i]);
        if (!coder.ComputeTreeFromHisto())
            return false;
        BitWriter out(dst, capacity);
        if (!coder.ExportTreeHuffman(out))
            return false;
        for (uint32_t i = 0; i < length; i++)
            coder.EncodeOne(out, src[i]);
        written = static_cast<uint32_t>(out.Flush());
        return !out.Overflow();
    }

    bool Huffman8Decode(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t dstLength)
    {
        HuffmanCoder coder(256, 16);
        BitReader in(src, length);
        if (!coder.ImportTreeHuffman(in))
            return false;
        for (uint32_t i = 0; i < dstLength; i++)
            dst[i] = static_cast<uint8_t>(coder.DecodeOne(in));
        return !in.Overflow();
    }
}  // namespace chd
