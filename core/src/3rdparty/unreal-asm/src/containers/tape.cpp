#include "unrealasm/containers.h"

#include <cstring>

namespace unrealasm::containers
{
namespace
{
uint32_t Le(std::span<const uint8_t> b, size_t at, int bytes)
{
    uint32_t v = 0;
    for (int k = bytes - 1; k >= 0; --k)
        v = (v << 8) | b[at + static_cast<size_t>(k)];
    return v;
}

/// A block as the ROM saves it: flag, data, checksum (the XOR of all three is 0)
bool AddBlock(std::span<const uint8_t> raw, std::vector<TapeBlock>& out)
{
    if (raw.size() < 2)
        return false;
    TapeBlock b;
    b.flag = raw[0];
    b.data.assign(raw.begin() + 1, raw.end() - 1);
    uint8_t x = 0;
    for (const uint8_t v : raw)
        x ^= v;
    b.checksumOk = x == 0;
    out.push_back(std::move(b));
    return true;
}

bool ReadTap(std::span<const uint8_t> image, std::vector<TapeBlock>& out, std::string& error)
{
    size_t at = 0;
    while (at < image.size())
    {
        if (at + 2 > image.size())
        {
            error = "TAP block length cut short";
            return false;
        }
        const size_t length = Le(image, at, 2);
        if (at + 2 + length > image.size())
        {
            error = "TAP block runs past the end";
            return false;
        }
        AddBlock(image.subspan(at + 2, length), out);
        at += 2 + length;
    }
    return true;
}

bool ReadTzx(std::span<const uint8_t> image, std::vector<TapeBlock>& out, std::string& error)
{
    size_t at = 10;   // "ZXTape!" #1A, major, minor
    auto need = [&](size_t n) { return at + n <= image.size(); };
    while (at < image.size())
    {
        const uint8_t id = image[at++];
        size_t skip = 0;
        switch (id)
        {
            case 0x10:   // standard speed data: pause(2) length(2) data
                if (!need(4))
                    break;
                skip = 4 + Le(image, at + 2, 2);
                if (need(skip))
                    AddBlock(image.subspan(at + 4, skip - 4), out);
                break;
            case 0x11:   // turbo speed data: 15 bytes of timings, length(3) data
                if (!need(18))
                    break;
                skip = 18 + Le(image, at + 15, 3);
                if (need(skip))
                    AddBlock(image.subspan(at + 18, skip - 18), out);
                break;
            case 0x14:   // pure data: 7 bytes of timings, length(3) data
                if (!need(10))
                    break;
                skip = 10 + Le(image, at + 7, 3);
                if (need(skip))
                    AddBlock(image.subspan(at + 10, skip - 10), out);
                break;
            case 0x12: skip = 4; break;
            case 0x13: skip = need(1) ? 1 + 2 * static_cast<size_t>(image[at]) : 1; break;
            case 0x15: skip = need(8) ? 8 + Le(image, at + 5, 3) : 8; break;
            case 0x18: case 0x19: skip = need(4) ? 4 + Le(image, at, 4) : 4; break;
            case 0x20: case 0x23: case 0x24: skip = 2; break;
            case 0x21: case 0x30: skip = need(1) ? 1 + static_cast<size_t>(image[at]) : 1; break;
            case 0x22: case 0x25: case 0x27: skip = 0; break;
            case 0x26: skip = need(2) ? 2 + 2 * Le(image, at, 2) : 2; break;
            case 0x28: case 0x32: skip = need(2) ? 2 + Le(image, at, 2) : 2; break;
            case 0x2A: skip = 4; break;
            case 0x2B: skip = 5; break;
            case 0x31: skip = need(2) ? 2 + static_cast<size_t>(image[at + 1]) : 2; break;
            case 0x33: skip = need(1) ? 1 + 3 * static_cast<size_t>(image[at]) : 1; break;
            case 0x35: skip = need(20) ? 20 + Le(image, at + 16, 4) : 20; break;
            case 0x5A: skip = 9; break;
            default:
                error = "TZX block #" + std::to_string(id) + " not known";
                return false;
        }
        if (!need(skip))
        {
            error = "TZX block runs past the end";
            return false;
        }
        at += skip;
    }
    return true;
}
}  // namespace

bool ReadTapeBlocks(std::span<const uint8_t> image, std::vector<TapeBlock>& out, std::string& error)
{
    out.clear();
    if (image.size() >= 10 && std::memcmp(image.data(), "ZXTape!\x1A", 8) == 0)
        return ReadTzx(image, out, error);
    if (!ReadTap(image, out, error))
        return false;
    if (out.empty())
    {
        error = "no tape blocks";
        return false;
    }
    return true;
}

namespace
{
/// GENS's T command (an include file for *F): header type 4, its length field the number of lines + 1, then data
/// blocks of the include buffer's size, each holding whole lines ([number][text] CR) up to a #00 #00 word; GENS4
/// leaves stale bytes after it. The lines of all blocks, joined
std::vector<uint8_t> GensIncludeLines(const std::vector<TapeBlock>& blocks, size_t first, size_t& next)
{
    std::vector<uint8_t> lines;
    size_t k = first;
    for (; k < blocks.size() && !(blocks[k].flag == 0 && blocks[k].data.size() == 17); ++k)
    {
        const std::vector<uint8_t>& b = blocks[k].data;
        size_t at = 0;
        while (at + 2 <= b.size() && (b[at] | b[at + 1]) != 0)
        {
            size_t end = at + 2;
            while (end < b.size() && b[end] != 0x0D)
                ++end;
            if (end == b.size())
                break;   // a line cut by the block's end: not GENS's
            lines.insert(lines.end(), b.begin() + static_cast<std::ptrdiff_t>(at), b.begin() + static_cast<std::ptrdiff_t>(end + 1));
            at = end + 1;
        }
    }
    next = k;
    return lines;
}
}  // namespace

bool ReadTape(std::span<const uint8_t> image, std::vector<TrdosFile>& out, std::string& error)
{
    std::vector<TapeBlock> blocks;
    if (!ReadTapeBlocks(image, blocks, error))
        return false;
    out.clear();
    int headerless = 0;
    for (size_t k = 0; k < blocks.size(); ++k)
    {
        const TapeBlock& b = blocks[k];
        const bool header = b.flag == 0 && b.data.size() == 17 && k + 1 < blocks.size() && blocks[k + 1].flag != 0;
        TrdosFile f;
        if (header && b.data[0] == 4)
        {
            size_t next = k + 1;
            f.type = 'C';
            f.name.assign(reinterpret_cast<const char*>(b.data.data() + 1), 10);
            f.data = GensIncludeLines(blocks, k + 1, next);
            f.length = static_cast<uint16_t>(f.data.size());
            k = next - 1;
        }
        else if (header && b.data[1] == 0xAF && (b.data[0] & 0x7F) == 0)
        {
            // A Laser Genius source (Oasis, 1986): its own header (block number, bit 7 on the last; #AF; the file's
            // length; the block's length; a 10-character name) before every block of up to 2048 bytes: the blocks
            // joined, type L
            f.type = 'L';
            f.name.assign(reinterpret_cast<const char*>(b.data.data() + 6), 10);
            size_t at = k;
            while (at + 1 < blocks.size())
            {
                const TapeBlock& h = blocks[at];
                if (h.flag != 0 || h.data.size() != 17 || h.data[1] != 0xAF || std::memcmp(h.data.data() + 6, b.data.data() + 6, 10) != 0 ||
                    (h.data[0] & 0x7F) != ((at - k) / 2))
                    break;
                f.data.insert(f.data.end(), blocks[at + 1].data.begin(), blocks[at + 1].data.end());
                at += 2;
                if (h.data[0] & 0x80)
                    break;
            }
            f.length = static_cast<uint16_t>(f.data.size());
            k = at - 1;
        }
        else if (header)
        {
            const TapeBlock& d = blocks[k + 1];
            static const char kTypes[] = {'B', 'D', 'D', 'C'};
            f.type = b.data[0] < 4 ? kTypes[b.data[0]] : 'C';
            f.name.assign(reinterpret_cast<const char*>(b.data.data() + 1), 10);
            f.start = static_cast<uint16_t>(Le(b.data, 13, 2));   // parameter 1: the start of Bytes, the autostart line of a Program
            f.data = d.data;
            f.length = static_cast<uint16_t>(f.data.size());
            ++k;
        }
        else
        {
            if (b.flag == 0)
                continue;   // a header without its data block
            f.type = 'C';
            const std::string n = std::to_string(++headerless);
            f.name = "BLOCK" + std::string(n.size() < 2 ? 2 - n.size() : 0, '0') + n;
            f.data = b.data;
            f.length = static_cast<uint16_t>(f.data.size());
        }
        f.sectors = static_cast<uint8_t>((f.data.size() + 255) / 256);
        out.push_back(std::move(f));
    }
    return true;
}
}  // namespace unrealasm::containers
