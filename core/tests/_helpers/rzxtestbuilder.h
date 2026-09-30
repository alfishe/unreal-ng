#pragma once

/// @file rzxtestbuilder.h
/// @brief Builds RZX files and Z80 v1 (48K) start snapshots in memory for the
/// RZX tests: the byte layout of RZX 0.13 (docs/inprogress/2026-09-29-rzx-replay,
/// rzxformat.h) written the way Fuse / SkoolKit write it.
///
/// Example: a 48K program at #8000 that reads the keyboard twice, then an
/// interrupt:
///   RzxTestBuilder rzx;
///   rzx.Creator("Test", 1, 0);
///   rzx.Snapshot("z80", Z80Snapshot48K(program, 0x8000));
///   rzx.Input({{2, {0xBF, 0xFE}}});
///   std::vector<uint8_t> bytes = rzx.Build();

#include <cstdint>
#include <string>
#include <vector>

#include "loaders/rzx/rzxformat.h"

struct RzxTestFrame
{
    uint16_t fetchCount = 0;
    std::vector<uint8_t> ins;
    bool repeat = false;  ///< written with IN counter 65535 (ins ignored)
};

class RzxTestBuilder
{
public:
    RzxTestBuilder& Header(uint8_t major, uint8_t minor, uint32_t flags = 0)
    {
        _major = major;
        _minor = minor;
        _flags = flags;
        return *this;
    }

    RzxTestBuilder& Creator(const std::string& name, uint16_t major, uint16_t minor)
    {
        std::vector<uint8_t> body(24, 0);
        for (size_t i = 0; i < name.size() && i < 20; i++)
            body[i] = static_cast<uint8_t>(name[i]);
        Put16(body, 20, major);
        Put16(body, 22, minor);
        return Block(rzx::kBlockCreator, body);
    }

    RzxTestBuilder& Snapshot(const std::string& extension, const std::vector<uint8_t>& image, bool compress = false)
    {
        std::vector<uint8_t> body(12, 0);
        Put32(body, 0, compress ? rzx::kSnapshotFlagCompressed : 0);
        for (size_t i = 0; i < extension.size() && i < 4; i++)
            body[4 + i] = static_cast<uint8_t>(extension[i]);
        Put32(body, 8, static_cast<uint32_t>(image.size()));
        const std::vector<uint8_t> data = compress ? rzx::Deflate(image.data(), image.size()) : image;
        body.insert(body.end(), data.begin(), data.end());
        return Block(rzx::kBlockSnapshot, body);
    }

    RzxTestBuilder& ExternalSnapshot(const std::string& extension, const std::string& fileName,
                                     uint32_t checksum = 0)
    {
        std::vector<uint8_t> body(12, 0);
        Put32(body, 0, rzx::kSnapshotFlagExternal);
        for (size_t i = 0; i < extension.size() && i < 4; i++)
            body[4 + i] = static_cast<uint8_t>(extension[i]);
        std::vector<uint8_t> descriptor(4, 0);
        Put32(descriptor, 0, checksum);
        descriptor.insert(descriptor.end(), fileName.begin(), fileName.end());
        descriptor.push_back(0);
        Put32(body, 8, static_cast<uint32_t>(descriptor.size()));
        body.insert(body.end(), descriptor.begin(), descriptor.end());
        return Block(rzx::kBlockSnapshot, body);
    }

    RzxTestBuilder& Input(const std::vector<RzxTestFrame>& frames, uint32_t tstates = 0, bool compress = false,
                          uint32_t extraFlags = 0)
    {
        std::vector<uint8_t> data;
        for (const RzxTestFrame& frame : frames)
        {
            std::vector<uint8_t> header(4, 0);
            Put16(header, 0, frame.fetchCount);
            Put16(header, 2, frame.repeat ? rzx::kRepeatFrame : static_cast<uint16_t>(frame.ins.size()));
            data.insert(data.end(), header.begin(), header.end());
            if (!frame.repeat)
                data.insert(data.end(), frame.ins.begin(), frame.ins.end());
        }
        std::vector<uint8_t> body(13, 0);
        Put32(body, 0, static_cast<uint32_t>(frames.size()));
        Put32(body, 5, tstates);
        Put32(body, 9, (compress ? rzx::kInputFlagCompressed : 0) | extraFlags);
        const std::vector<uint8_t> payload = compress ? rzx::Deflate(data.data(), data.size()) : data;
        body.insert(body.end(), payload.begin(), payload.end());
        return Block(rzx::kBlockInput, body);
    }

    /// Any block, raw
    RzxTestBuilder& Block(uint8_t id, const std::vector<uint8_t>& body)
    {
        std::vector<uint8_t> header(5, 0);
        header[0] = id;
        Put32(header, 1, static_cast<uint32_t>(body.size() + 5));
        _blocks.insert(_blocks.end(), header.begin(), header.end());
        _blocks.insert(_blocks.end(), body.begin(), body.end());
        return *this;
    }

    std::vector<uint8_t> Build() const
    {
        std::vector<uint8_t> out = {'R', 'Z', 'X', '!', _major, _minor, 0, 0, 0, 0};
        Put32(out, 6, _flags);
        out.insert(out.end(), _blocks.begin(), _blocks.end());
        return out;
    }

    static void Put16(std::vector<uint8_t>& v, size_t at, uint16_t value)
    {
        v[at] = static_cast<uint8_t>(value);
        v[at + 1] = static_cast<uint8_t>(value >> 8);
    }

    static void Put32(std::vector<uint8_t>& v, size_t at, uint32_t value)
    {
        for (int i = 0; i < 4; i++)
            v[at + i] = static_cast<uint8_t>(value >> (8 * i));
    }

private:
    uint8_t _major = 0;
    uint8_t _minor = 13;
    uint32_t _flags = 0;
    std::vector<uint8_t> _blocks;
};

/// Registers of a Z80 v1 test snapshot
struct Z80TestRegisters
{
    uint16_t pc = 0x8000;
    uint16_t sp = 0xFF00;
    uint8_t a = 0, f = 0;
    uint16_t bc = 0, de = 0, hl = 0, ix = 0, iy = 0x5C3A;
    uint8_t i = 0x3F, r = 0;
    bool iff = false;
    uint8_t im = 1;
};

/// A 48K Z80 v1 snapshot (uncompressed): RAM #4000-#FFFF zero except `code`
/// at `address`
inline std::vector<uint8_t> Z80Snapshot48K(const std::vector<uint8_t>& code, uint16_t address,
                                           const Z80TestRegisters& regs = {})
{
    std::vector<uint8_t> image(30 + 49152, 0);
    image[0] = regs.a;
    image[1] = regs.f;
    RzxTestBuilder::Put16(image, 2, regs.bc);
    RzxTestBuilder::Put16(image, 4, regs.hl);
    RzxTestBuilder::Put16(image, 6, regs.pc);
    RzxTestBuilder::Put16(image, 8, regs.sp);
    image[10] = regs.i;
    image[11] = regs.r & 0x7F;
    image[12] = static_cast<uint8_t>(((regs.r >> 7) & 1) | (7 << 1));  // R bit 7, border 7, uncompressed
    RzxTestBuilder::Put16(image, 13, regs.de);
    RzxTestBuilder::Put16(image, 23, regs.iy);
    RzxTestBuilder::Put16(image, 25, regs.ix);
    image[27] = regs.iff ? 1 : 0;
    image[28] = regs.iff ? 1 : 0;
    image[29] = regs.im & 3;
    for (size_t i = 0; i < code.size(); i++)
        image[30 + (address - 0x4000) + i] = code[i];
    return image;
}
