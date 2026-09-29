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

    // BDSK after B128, as the format lists them
    for (const BetaDisk& disk : stage.betaDisks)
    {
        std::vector<uint8_t> body;
        std::vector<uint8_t> payload;
        uint32_t flags = disk.flags & kDiskWriteProtect;
        if (!disk.image.empty())
        {
            flags |= kDiskEmbedded;
            payload = Deflate(disk.image.data(), disk.image.size());
            if (!payload.empty() && payload.size() < disk.image.size())
                flags |= kDiskCompressed;
            else
                payload = disk.image;
        }
        else
        {
            payload.assign(disk.fileName.begin(), disk.fileName.end());
            payload.push_back(0);
        }
        Put32(body, flags);
        body.insert(body.end(), {disk.drive, disk.cylinder, disk.type});
        body.insert(body.end(), payload.begin(), payload.end());
        Block(out, kBetaDisk, body);
    }

    if (stage.plus3)
        Block(out, kPlus3, {stage.plus3->drives, stage.plus3->motorOn});
    for (const DskFile& file : stage.dskFiles)
    {
        std::vector<uint8_t> body;
        Put16(body, static_cast<uint16_t>(file.flags & 0x04));  // links only: SIDEB kept
        body.push_back(file.drive);
        Put32(body, static_cast<uint32_t>(file.fileName.size() + 1));
        body.insert(body.end(), file.fileName.begin(), file.fileName.end());
        body.push_back(0);
        Block(out, kDskFile, body);
    }

    if (stage.tape)
    {
        const Tape& tape = *stage.tape;
        std::vector<uint8_t> body;
        std::vector<uint8_t> payload;
        uint16_t flags = 0;
        uint32_t uncompressed = 0;
        if (!tape.image.empty())
        {
            flags |= kTapeEmbedded;
            uncompressed = static_cast<uint32_t>(tape.image.size());
            payload = Deflate(tape.image.data(), tape.image.size());
            if (!payload.empty() && payload.size() < tape.image.size())
                flags |= kTapeCompressed;
            else
                payload = tape.image;
        }
        else
        {
            payload.assign(tape.fileName.begin(), tape.fileName.end());
            payload.push_back(0);
        }
        Put16(body, tape.block);
        Put16(body, flags);
        Put32(body, uncompressed);
        Put32(body, static_cast<uint32_t>(payload.size()));
        std::vector<uint8_t> extension(16, 0);
        std::copy_n(tape.extension.begin(), std::min<size_t>(tape.extension.size(), 15), extension.begin());
        body.insert(body.end(), extension.begin(), extension.end());
        body.insert(body.end(), payload.begin(), payload.end());
        Block(out, kTape, body);
    }

    if (stage.gs)
    {
        const GeneralSound& gs = *stage.gs;
        const Z80Regs& z = gs.cpu;
        std::vector<uint8_t> body{gs.model, gs.upperPage};
        body.insert(body.end(), gs.volume.begin(), gs.volume.end());
        body.insert(body.end(), gs.output.begin(), gs.output.end());
        body.push_back(static_cast<uint8_t>(gs.flags & (kSuppressInts | kHalted)));  // no custom ROM written
        for (uint16_t word : {z.af, z.bc, z.de, z.hl, z.af1, z.bc1, z.de1, z.hl1, z.ix, z.iy, z.sp, z.pc})
            Put16(body, word);
        body.insert(body.end(), {z.i, z.r, z.iff1, z.iff2, z.im});
        Put32(body, z.cyclesStart);
        body.push_back(z.holdIntReqCycles);
        body.push_back(static_cast<uint8_t>(z.memptr >> 8));  // chBitReg
        Block(out, kGs, body);
        for (const auto& [page, bytes] : stage.gsPages)
        {
            std::vector<uint8_t> pageBody;
            const std::vector<uint8_t> packed = Deflate(bytes.data(), bytes.size());
            const bool compressed = !packed.empty() && packed.size() < bytes.size();
            Put16(pageBody, compressed ? kPageCompressed : 0);
            pageBody.push_back(page);
            const std::vector<uint8_t>& payload = compressed ? packed : bytes;
            pageBody.insert(pageBody.end(), payload.begin(), payload.end());
            Block(out, kGsRamPage, pageBody);
        }
    }

    if (stage.covox)
        Block(out, kCovox, {*stage.covox, 0, 0, 0});
    if (stage.keyboard)
    {
        std::vector<uint8_t> body;
        Put32(body, stage.keyboard->flags);
        body.push_back(stage.keyboard->joystick);
        Block(out, kKeyboard, body);
    }
    if (stage.mouse)
    {
        std::vector<uint8_t> body{stage.mouse->type};
        body.insert(body.end(), stage.mouse->ctrlA.begin(), stage.mouse->ctrlA.end());
        body.insert(body.end(), stage.mouse->ctrlB.begin(), stage.mouse->ctrlB.end());
        Block(out, kMouse, body);
    }
    return out;
}
