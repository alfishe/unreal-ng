#include "loaders/snapshot/szx/szxreader.h"

#include <cstdio>
#include <cstring>

using namespace szx;

bool SzxReader::Parse(const uint8_t* data, size_t size, Stage& stage, std::string& error)
{
    stage = Stage{};
    if (data == nullptr || size < kHeaderSize || Get32(data) != kMagic)
    {
        error = "not an SZX file (no ZXST header)";
        return false;
    }
    stage.versionMajor = data[4];
    stage.versionMinor = data[5];
    stage.machineId = data[6];
    stage.headerFlags = data[7];
    if (stage.versionMajor != kVersionMajor)
    {
        error = "SZX major version " + std::to_string(stage.versionMajor) + " is not supported";
        return false;
    }

    size_t offset = kHeaderSize;
    while (offset < size)
    {
        if (size - offset < kBlockHeaderSize)
        {
            error = "truncated block header at offset " + std::to_string(offset);
            return false;
        }
        const uint32_t id = Get32(data + offset);
        const uint32_t blockSize = Get32(data + offset + 4);
        offset += kBlockHeaderSize;
        if (blockSize > size - offset)
        {
            error = "block " + BlockName(id) + " runs past the end of the file";
            return false;
        }
        const uint8_t* body = data + offset;

        bool ok = true;
        switch (id)
        {
            case kCreator: ok = ParseCreator(body, blockSize, stage, error); break;
            case kZ80Regs: ok = ParseZ80Regs(body, blockSize, stage, error); break;
            case kSpecRegs: ok = ParseSpecRegs(body, blockSize, stage, error); break;
            case kRamPage: ok = ParseRamPage(body, blockSize, stage, error); break;
            case kAy: ok = ParseAy(body, blockSize, stage, error); break;
            case kBeta128: ok = ParseBeta128(body, blockSize, stage, error); break;
            default: stage.otherBlocks.emplace_back(BlockName(id), blockSize); break;
        }
        if (!ok)
            return false;
        offset += blockSize;
    }

    if (!stage.z80)
    {
        error = "no Z80R block";
        return false;
    }
    if (!stage.spec)
    {
        error = "no SPCR block";
        return false;
    }

    // libspectrum <= 0.5.0 swapped A / F in Z80R; only its CRTR tells
    if (stage.creator && IsSwappedAfCreator(*stage.creator))
    {
        Z80Regs& z = *stage.z80;
        z.af = static_cast<uint16_t>((z.af >> 8) | (z.af << 8));
        z.af1 = static_cast<uint16_t>((z.af1 >> 8) | (z.af1 << 8));
        stage.warnings.push_back("A / F swapped back (libspectrum 0.5.0 or older wrote them swapped)");
    }
    return true;
}

bool SzxReader::ParseCreator(const uint8_t* body, uint32_t size, Stage& stage, std::string& error)
{
    if (size < kCreatorSize)
    {
        error = "CRTR block too short";
        return false;
    }
    Creator creator;
    creator.name.assign(reinterpret_cast<const char*>(body), strnlen(reinterpret_cast<const char*>(body), 32));
    creator.major = Get16(body + 32);
    creator.minor = Get16(body + 34);
    creator.data.assign(body + kCreatorSize, body + size);
    stage.creator = std::move(creator);
    return true;
}

bool SzxReader::ParseZ80Regs(const uint8_t* body, uint32_t size, Stage& stage, std::string& error)
{
    if (size < kZ80RegsSize)
    {
        error = "Z80R block too short";
        return false;
    }
    Z80Regs z;
    z.af = Get16(body + 0);
    z.bc = Get16(body + 2);
    z.de = Get16(body + 4);
    z.hl = Get16(body + 6);
    z.af1 = Get16(body + 8);
    z.bc1 = Get16(body + 10);
    z.de1 = Get16(body + 12);
    z.hl1 = Get16(body + 14);
    z.ix = Get16(body + 16);
    z.iy = Get16(body + 18);
    z.sp = Get16(body + 20);
    z.pc = Get16(body + 22);
    z.i = body[24];
    z.r = body[25];
    z.iff1 = body[26] ? 1 : 0;
    z.iff2 = body[27] ? 1 : 0;
    z.im = static_cast<uint8_t>(body[28] & 0x03);
    z.cyclesStart = Get32(body + 29);

    // Bytes 33-36 by version: reserved in 1.0; hold / flags / chBitReg in
    // 1.1-1.3 (chBitReg = MEMPTR's high byte, the one BIT n,(HL) shows);
    // hold / flags / MEMPTR from 1.4; FSET means something only from 1.5
    const unsigned version = (static_cast<unsigned>(stage.versionMajor) << 8) | stage.versionMinor;
    if (version >= 0x0101)
    {
        z.holdIntReqCycles = body[33];
        z.flags = body[34];
        if (version >= 0x0104)
            z.memptr = Get16(body + 35);
        else
            z.memptr = static_cast<uint16_t>(body[35] << 8);
        if (version < 0x0105)
            z.flags &= static_cast<uint8_t>(~kFset);
    }
    else
    {
        stage.warnings.push_back("SZX 1.0: no interrupt state, flags or MEMPTR in Z80R");
    }
    stage.z80 = z;
    return true;
}

bool SzxReader::ParseSpecRegs(const uint8_t* body, uint32_t size, Stage& stage, std::string& error)
{
    if (size < kSpecRegsSize)
    {
        error = "SPCR block too short";
        return false;
    }
    SpecRegs s;
    s.border = static_cast<uint8_t>(body[0] & 0x07);
    s.port7FFD = body[1];
    s.port1FFDorEFF7 = body[2];
    // chFe exists from 1.1 (a reserved byte before)
    const unsigned version = (static_cast<unsigned>(stage.versionMajor) << 8) | stage.versionMinor;
    s.portFE = version >= 0x0101 ? body[3] : 0;
    stage.spec = s;
    return true;
}

bool SzxReader::ParseRamPage(const uint8_t* body, uint32_t size, Stage& stage, std::string& error)
{
    if (size < 3)
    {
        error = "RAMP block too short";
        return false;
    }
    const uint16_t flags = Get16(body);
    const uint8_t page = body[2];
    const uint8_t* payload = body + 3;
    const uint32_t payloadSize = size - 3;
    std::vector<uint8_t> bytes;
    if (flags & kPageCompressed)
    {
        if (!Inflate(payload, payloadSize, kPageSize, bytes))
        {
            error = "RAMP page " + std::to_string(page) + ": the compressed data is not exactly one 16 KB page";
            return false;
        }
    }
    else
    {
        if (payloadSize != kPageSize)
        {
            error = "RAMP page " + std::to_string(page) + ": " + std::to_string(payloadSize) + " bytes, not 16384";
            return false;
        }
        bytes.assign(payload, payload + kPageSize);
    }
    if (stage.pages.count(page))
        stage.warnings.push_back("RAMP page " + std::to_string(page) + " appears twice: the last one is used");
    stage.pages[page] = std::move(bytes);
    return true;
}

bool SzxReader::ParseAy(const uint8_t* body, uint32_t size, Stage& stage, std::string& error)
{
    if (size < kAySize)
    {
        error = "AY block too short";
        return false;
    }
    Ay ay;
    ay.flags = body[0];
    ay.currentRegister = static_cast<uint8_t>(body[1] & 0x0F);
    for (size_t i = 0; i < 16; i++)
        ay.registers[i] = body[2 + i];
    stage.ay = ay;
    return true;
}

bool SzxReader::ParseBeta128(const uint8_t* body, uint32_t size, Stage& stage, std::string& error)
{
    if (size < kBeta128Size)
    {
        error = "B128 block too short";
        return false;
    }
    Beta128 beta;
    beta.flags = Get32(body);
    beta.drives = body[4];
    beta.system = body[5];
    beta.track = body[6];
    beta.sector = body[7];
    beta.data = body[8];
    beta.status = body[9];
    // A custom TR-DOS ROM is reported, not loaded (design §18, decision 3)
    if ((beta.flags & kBetaCustomRom) && size > kBeta128Size)
        stage.warnings.push_back("B128 carries a custom TR-DOS ROM: the configured ROM stays");
    stage.beta = beta;
    return true;
}

bool SzxReader::IsSwappedAfCreator(const Creator& creator)
{
    // libspectrum writes "libspectrum: X.Y.Z" into the creator data
    const std::string data(creator.data.begin(), creator.data.end());
    const size_t at = data.find("libspectrum: ");
    if (at == std::string::npos)
        return false;
    int major = 0, minor = 0, patch = 0;
    if (std::sscanf(data.c_str() + at + 13, "%d.%d.%d", &major, &minor, &patch) != 3)
        return false;
    return major == 0 && (minor < 5 || (minor == 5 && patch == 0));
}
