#pragma once

/// @file chdhuffman.h
/// @brief The static Huffman coding of CHD files: canonical codes limited to
/// `maxBits`, and the two ways a code table travels in front of the data.
///
/// - **RLE tree** (`ExportTreeRle` / `ImportTreeRle`): the code length of every
///   symbol in 3, 4 or 5 bits, with a run escape. Used by the compressed v5 map
///   (16 symbols, at most 8 bits).
/// - **Huffman tree** (`ExportTreeHuffman` / `ImportTreeHuffman`): the code
///   lengths, run-length coded and themselves Huffman-coded with a small
///   24-symbol table. Used by the `huff` hunk codec (256 symbols, at most 16 bits).
///
/// Canonical code assignment gives the longest codes the lowest numbers, and the
/// bits are read MSB first: the layout MAME's `huffman.cpp` defines (format notes:
/// docs/file-formats/disk-images/chd.md). The tree builder is a length-limited
/// Huffman construction that scales the symbol weights down until the longest
/// code fits, as MAME's does; any complete code would decode the same way.

#include <cstdint>
#include <vector>

#include "emulator/io/storage/chd/chdutil.h"

namespace chd
{
    class HuffmanCoder
    {
    public:
        HuffmanCoder(int numCodes, int maxBits);

        /// region <Encoding>
        void HistoReset();
        void HistoOne(uint32_t symbol) { _histo[symbol]++; }
        /// Code lengths from the histogram. False when no complete code fits
        bool ComputeTreeFromHisto();
        bool ExportTreeRle(BitWriter& out) const;
        bool ExportTreeHuffman(BitWriter& out) const;
        void EncodeOne(BitWriter& out, uint32_t symbol) const { out.Write(_codes[symbol], _lengths[symbol]); }
        /// endregion </Encoding>

        /// region <Decoding>
        bool ImportTreeRle(BitReader& in);
        bool ImportTreeHuffman(BitReader& in);
        uint32_t DecodeOne(BitReader& in) const
        {
            const uint32_t entry = _lookup[in.Peek(_maxBits)];
            in.Remove(static_cast<int>(entry & 0x1F));
            return entry >> 5;
        }
        /// endregion </Decoding>

    private:
        struct Node
        {
            int parent = -1;
            uint32_t weight = 0;
        };

        int BuildTree(uint32_t totalData, uint32_t totalWeight);
        bool AssignCanonicalCodes();
        void BuildLookupTable();

        int _numCodes;
        int _maxBits;
        std::vector<uint32_t> _histo;
        std::vector<uint8_t> _lengths;
        std::vector<uint32_t> _codes;
        std::vector<uint32_t> _lookup;  ///< (symbol << 5) | length, indexed by the next maxBits bits
    };

    /// The `huff` codec: one 8-bit Huffman table per hunk. Encode fails when the
    /// result would not fit `capacity`
    bool Huffman8Encode(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t capacity, uint32_t& written);
    bool Huffman8Decode(const uint8_t* src, uint32_t length, uint8_t* dst, uint32_t dstLength);
}  // namespace chd
