#include "loaders/snapshot/szx/szxwriter.h"

#include <algorithm>

using namespace szx;

void SzxWriter::Block(std::vector<uint8_t>& out, uint32_t id, const std::vector<uint8_t>& body)
{
    Put32(out, id);
    Put32(out, static_cast<uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
}

std::vector<uint8_t> SzxWriter::Write(const Stage& stage)
{
    std::vector<uint8_t> out;
    Put32(out, kMagic);
    out.push_back(kVersionMajor);
    out.push_back(kVersionMinor);
    out.push_back(stage.machineId);
    out.push_back(stage.headerFlags);

    if (stage.creator)
    {
        std::vector<uint8_t> body(32, 0);
        const size_t length = std::min<size_t>(stage.creator->name.size(), 31);
        std::copy_n(stage.creator->name.begin(), length, body.begin());
        Put16(body, stage.creator->major);
        Put16(body, stage.creator->minor);
        body.insert(body.end(), stage.creator->data.begin(), stage.creator->data.end());
        Block(out, kCreator, body);
    }

    if (stage.z80)
    {
        const Z80Regs& z = *stage.z80;
        std::vector<uint8_t> body;
        for (uint16_t word : {z.af, z.bc, z.de, z.hl, z.af1, z.bc1, z.de1, z.hl1, z.ix, z.iy, z.sp, z.pc})
            Put16(body, word);
        body.push_back(z.i);
        body.push_back(z.r);
        body.push_back(z.iff1 ? 1 : 0);
        body.push_back(z.iff2 ? 1 : 0);
        body.push_back(z.im);
        Put32(body, z.cyclesStart);
        body.push_back(z.holdIntReqCycles);
        body.push_back(z.flags);
        Put16(body, z.memptr);
        Block(out, kZ80Regs, body);
    }

    if (stage.spec)
    {
        const SpecRegs& s = *stage.spec;
        Block(out, kSpecRegs, {static_cast<uint8_t>(s.border & 0x07), s.port7FFD, s.port1FFDorEFF7, s.portFE, 0, 0, 0, 0});
    }

    for (const auto& [page, bytes] : stage.pages)
    {
        std::vector<uint8_t> body;
        const std::vector<uint8_t> packed = Deflate(bytes.data(), bytes.size());
        const bool compressed = !packed.empty() && packed.size() < bytes.size();
        Put16(body, compressed ? kPageCompressed : 0);
        body.push_back(page);
        const std::vector<uint8_t>& payload = compressed ? packed : bytes;
        body.insert(body.end(), payload.begin(), payload.end());
        Block(out, kRamPage, body);
    }

    if (stage.ay)
    {
        std::vector<uint8_t> body{stage.ay->flags, static_cast<uint8_t>(stage.ay->currentRegister & 0x0F)};
        body.insert(body.end(), stage.ay->registers.begin(), stage.ay->registers.end());
        Block(out, kAy, body);
    }

    if (stage.beta)
    {
        const Beta128& b = *stage.beta;
        std::vector<uint8_t> body;
        Put32(body, b.flags & ~(kBetaCustomRom | 0x20u));  // no custom ROM written
        body.insert(body.end(), {b.drives, b.system, b.track, b.sector, b.data, b.status});
        Block(out, kBeta128, body);
    }
    return out;
}
