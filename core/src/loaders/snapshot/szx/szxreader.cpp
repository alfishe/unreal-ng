#include "loaders/snapshot/szx/szxreader.h"

#include <algorithm>
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
            case kBetaDisk: ok = ParseBetaDisk(body, blockSize, stage, error); break;
            case kDskFile: ok = ParseDskFile(body, blockSize, stage, error); break;
            case kTape: ok = ParseTape(body, blockSize, stage, error); break;
            case kGs: ok = ParseGs(body, blockSize, stage, error); break;
            case kGsRamPage: ok = ParseGsRamPage(body, blockSize, stage, error); break;
            case kPlus3:
                if (!(ok = !Short(blockSize, 2, "+3", error)))
                    break;
                stage.plus3 = Plus3{body[0], body[1]};
                break;
            case kKeyboard:
            {
                // 4 bytes in 1.0 (no joystick byte), 5 from 1.1
                if (!(ok = !Short(blockSize, 4, "KEYB", error)))
                    break;
                Keyboard keyboard;
                keyboard.flags = Get32(body);
                keyboard.joystick = blockSize >= 5 ? body[4] : kKeyboardJoystickNone;
                stage.keyboard = keyboard;
                break;
            }
            case kJoystick:
                if (!(ok = !Short(blockSize, 6, "JOY", error)))
                    break;
                stage.joysticks = std::array<uint8_t, 2>{body[4], body[5]};
                break;
            case kMouse:
            {
                if (!(ok = !Short(blockSize, 7, "AMXM", error)))
                    break;
                Mouse mouse;
                mouse.type = body[0];
                std::copy_n(body + 1, 3, mouse.ctrlA.begin());
                std::copy_n(body + 4, 3, mouse.ctrlB.begin());
                stage.mouse = mouse;
                break;
            }
            case kCovox:
                if (!(ok = !Short(blockSize, 1, "COVX", error)))
                    break;
                stage.covox = body[0];
                break;
            case kSpecDrum:
                if (!(ok = !Short(blockSize, 1, "DRUM", error)))
                    break;
                stage.specDrum = static_cast<int8_t>(body[0]);
                break;
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

bool SzxReader::Short(uint32_t size, size_t minimum, const char* name, std::string& error)
{
    if (size >= minimum)
        return false;
    error = std::string(name) + " block too short";
    return true;
}

namespace
{
    /// A null-terminated name of at most `size` bytes
    std::string Name(const uint8_t* data, size_t size)
    {
        return std::string(reinterpret_cast<const char*>(data), strnlen(reinterpret_cast<const char*>(data), size));
    }
}  // namespace

bool SzxReader::ParseBetaDisk(const uint8_t* body, uint32_t size, Stage& stage, std::string& error)
{
    if (Short(size, 7, "BDSK", error))
        return false;
    BetaDisk disk;
    disk.flags = Get32(body);
    disk.drive = body[4];
    disk.cylinder = body[5];
    disk.type = body[6];
    const uint8_t* data = body + 7;
    const size_t length = size - 7;
    if (disk.flags & kDiskEmbedded)
    {
        // No uncompressed size in the block: bounded by the largest image
        if (disk.flags & kDiskCompressed)
        {
            if (!InflateBounded(data, length, kMaxEmbeddedImage, disk.image))
            {
                error = "BDSK drive " + std::to_string(disk.drive) + ": the embedded image does not inflate";
                return false;
            }
        }
        else
        {
            disk.image.assign(data, data + length);
        }
    }
    else
    {
        disk.fileName = Name(data, length);
    }
    stage.betaDisks.push_back(std::move(disk));
    return true;
}

bool SzxReader::ParseDskFile(const uint8_t* body, uint32_t size, Stage& stage, std::string& error)
{
    if (Short(size, 7, "DSK", error))
        return false;
    DskFile file;
    file.flags = Get16(body);
    file.drive = body[2];
    const uint32_t nameLength = Get32(body + 3);
    file.fileName = Name(body + 7, std::min<size_t>(nameLength, size - 7));
    stage.dskFiles.push_back(std::move(file));
    return true;
}

bool SzxReader::ParseTape(const uint8_t* body, uint32_t size, Stage& stage, std::string& error)
{
    if (Short(size, 28, "TAPE", error))
        return false;
    Tape tape;
    tape.block = Get16(body);
    tape.flags = Get16(body + 2);
    const uint32_t uncompressed = Get32(body + 4);
    const uint32_t compressed = Get32(body + 8);
    tape.extension = Name(body + 12, 16);
    const uint8_t* data = body + 28;
    const size_t length = std::min<size_t>(compressed, size - 28);
    if (tape.flags & kTapeEmbedded)
    {
        if (tape.flags & kTapeCompressed)
        {
            if (uncompressed > kMaxEmbeddedImage || !Inflate(data, length, uncompressed, tape.image))
            {
                error = "TAPE: the embedded image does not inflate to its stated size";
                return false;
            }
        }
        else
        {
            tape.image.assign(data, data + length);
        }
    }
    else
    {
        tape.fileName = Name(data, length);
    }
    stage.tape = std::move(tape);
    return true;
}

bool SzxReader::ParseGs(const uint8_t* body, uint32_t size, Stage& stage, std::string& error)
{
    if (Short(size, kGsSize, "GS", error))
        return false;
    GeneralSound gs;
    gs.model = body[0];
    gs.upperPage = body[1];
    std::copy_n(body + 2, 4, gs.volume.begin());
    std::copy_n(body + 6, 4, gs.output.begin());
    gs.flags = body[10];
    Z80Regs& z = gs.cpu;
    const uint8_t* r = body + 11;
    z.af = Get16(r + 0);
    z.bc = Get16(r + 2);
    z.de = Get16(r + 4);
    z.hl = Get16(r + 6);
    z.af1 = Get16(r + 8);
    z.bc1 = Get16(r + 10);
    z.de1 = Get16(r + 12);
    z.hl1 = Get16(r + 14);
    z.ix = Get16(r + 16);
    z.iy = Get16(r + 18);
    z.sp = Get16(r + 20);
    z.pc = Get16(r + 22);
    z.i = body[35];
    z.r = body[36];
    z.iff1 = body[37] ? 1 : 0;
    z.iff2 = body[38] ? 1 : 0;
    z.im = static_cast<uint8_t>(body[39] & 0x03);
    z.cyclesStart = Get32(body + 40);
    z.holdIntReqCycles = body[44];
    z.memptr = static_cast<uint16_t>(body[45] << 8);  // chBitReg: MEMPTR's high byte
    z.flags = static_cast<uint8_t>(gs.flags & (kSuppressInts | kHalted));
    if ((gs.flags & kGsCustomRom) && size > kGsSize)
        stage.warnings.push_back("GS carries a custom ROM: the configured GS ROM stays");
    stage.gs = gs;
    return true;
}

bool SzxReader::ParseGsRamPage(const uint8_t* body, uint32_t size, Stage& stage, std::string& error)
{
    if (Short(size, 3, "GSRP", error))
        return false;
    const uint16_t flags = Get16(body);
    const uint8_t page = body[2];
    std::vector<uint8_t> bytes;
    if (flags & kPageCompressed)
    {
        if (!Inflate(body + 3, size - 3, kGsPageSize, bytes))
        {
            error = "GSRP page " + std::to_string(page) + ": the compressed data is not exactly one 32 KB page";
            return false;
        }
    }
    else
    {
        if (size - 3 != kGsPageSize)
        {
            error = "GSRP page " + std::to_string(page) + ": not 32768 bytes";
            return false;
        }
        bytes.assign(body + 3, body + 3 + kGsPageSize);
    }
    stage.gsPages[page] = std::move(bytes);
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
