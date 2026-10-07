#include "stdafx.h"

#include "loaderspg.h"

#include <algorithm>
#include <cstring>

#include "common/filehelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "loaders/snapshot/zxdepackers.h"

namespace
{
    constexpr size_t kHeaderSize = 0x400;
    constexpr size_t kDescriptors = 0x100;
    constexpr size_t kRamSize = 4u * 1024 * 1024;
    constexpr size_t kMaxDepacked = 0x10000;  // a block never depacks beyond 64 KB
}

LoaderSPG::LoaderSPG(EmulatorContext* context, const std::string& path)
    : _context(context), _path(path), _fromFile(true)
{
}

LoaderSPG::LoaderSPG(EmulatorContext* context, std::vector<uint8_t> data, const std::string& name)
    : _context(context), _path(name), _data(std::move(data)), _fromFile(false)
{
}

bool LoaderSPG::Probe(const std::string& path, std::string& error)
{
    std::vector<uint8_t> header(kHeaderSize, 0);
    if (!FileHelper::FileExists(path) || FileHelper::ReadFileToBuffer(path, header.data(), header.size()) != header.size() ||
        std::memcmp(header.data() + 0x20, "SpectrumProg", 12) != 0)
    {
        error = "not an SPG file: " + path;
        return false;
    }
    if ((header[0x2C] & 0xF0) != 0x10)
    {
        error = "SPG version " + std::to_string(header[0x2C] >> 4) + "." + std::to_string(header[0x2C] & 0x0F) +
                " is not supported (1.0 and 1.1 are)";
        return false;
    }
    return true;
}

bool LoaderSPG::Parse(const std::vector<uint8_t>& data, Image& image, std::string& error)
{
    if (data.size() < kHeaderSize || std::memcmp(data.data() + 0x20, "SpectrumProg", 12) != 0)
    {
        error = "not an SPG file (no \"SpectrumProg\" header)";
        return false;
    }

    image = Image{};
    image.version = data[0x2C];
    if ((image.version & 0xF0) != 0x10)
    {
        error = "SPG version " + std::to_string(image.version >> 4) + "." + std::to_string(image.version & 0x0F) +
                " is not supported (1.0 and 1.1 are)";
        return false;
    }
    image.pc = static_cast<uint16_t>(data[0x30] | (data[0x31] << 8));
    image.sp = static_cast<uint16_t>(data[0x32] | (data[0x33] << 8));
    image.page3 = data[0x34];
    image.clock = data[0x35] & 0x03;
    image.interrupts = (data[0x35] & 0x04) != 0;
    const size_t count = std::min<size_t>(static_cast<size_t>(data[0x3A] | (data[0x3B] << 8)), kDescriptors);

    size_t position = kHeaderSize;
    for (size_t i = 0; i < count; i++)
    {
        const uint8_t* d = data.data() + 0x100 + i * 3;
        const size_t size = ((d[1] & 0x1F) + 1u) * 512u;
        if (position + size > data.size())
        {
            error = "SPG block " + std::to_string(i) + " runs past the end of the file";
            return false;
        }

        Block block;
        block.address = static_cast<uint32_t>(d[2]) * 0x4000u + (d[0] & 0x1Fu) * 512u;
        block.compression = static_cast<uint8_t>((d[1] >> 6) & 0x03);
        const uint8_t* in = data.data() + position;
        const size_t capacity = std::min(kMaxDepacked, kRamSize - block.address);
        switch (block.compression)
        {
            case 0:
                block.data.assign(in, in + std::min(size, capacity));
                break;
            case 1:
            case 2:
            {
                block.data.resize(capacity);
                size_t length = 0;
                const bool ok = block.compression == 1 ? ZxDepack::MegaLz(in, size, block.data.data(), capacity, length)
                                                       : ZxDepack::Hrust(in, size, block.data.data(), capacity, length);
                if (!ok)
                {
                    error = "SPG block " + std::to_string(i) + ": the " + (block.compression == 1 ? "MegaLZ" : "Hrust") +
                            " stream is corrupt";
                    return false;
                }
                block.data.resize(length);
                break;
            }
            default:
                error = "SPG block " + std::to_string(i) + ": unknown compression 3";
                return false;
        }
        image.blocks.push_back(std::move(block));
        position += size;

        if (d[0] & 0x80)
            break;  // the last block marker
    }
    return true;
}

bool LoaderSPG::Commit(EmulatorContext* context, const Image& image, std::string& error)
{
    // The commit reads the neutral image, as the machine's policy does for a planned load
    if (!context)
    {
        error = "an SPG program runs on the TS-Conf machine (model TSL) only";
        return false;
    }
    snapshot::ISnapshotCommitPolicy* policy = context->pPortDecoder ? context->pPortDecoder->GetSnapshotPolicy() : nullptr;
    if (!policy || policy->Name() != "tsconf-program")
    {
        error = "an SPG program runs on the TS-Conf machine (model TSL) only";
        return false;
    }
    snapshot::Report report;
    const bool committed = policy->Commit(BuildSnapshotImage(image, std::string()), *context, report);
    if (!committed)
        error = report.reason;
    return committed;
}

bool LoaderSPG::load()
{
    if (_fromFile)
    {
        if (!FileHelper::FileExists(_path))
        {
            _error = "file not found: " + _path;
            return false;
        }
        const size_t size = FileHelper::GetFileSize(_path);
        _data.assign(size, 0);
        if (size && FileHelper::ReadFileToBuffer(_path, _data.data(), size) != size)
        {
            _error = "cannot read " + _path;
            return false;
        }
    }
    if (!Parse(_data, _image, _error))
        return false;

    // Snapshot pipeline: the image of the file and the plan step, then today's commit
    _snapshotImage = BuildSnapshotImage(_image, _path);
    _snapshotReport = snapshot::Report();
    _decision = snapshot::Pipeline::Plan(_snapshotImage, _context, _options, _snapshotReport);
    if (!_decision.Proceeds())
    {
        _error = _snapshotReport.reason;
        return false;
    }
    // Take: the machine's policy (the TS-Conf machine has one); Legacy: the same commit from the image, which refuses a
    // machine that is not a TS-Conf with the reason
    const bool committed = _decision.action == snapshot::Decision::Action::Take
                               ? _decision.Commit(_snapshotImage, *_context, _snapshotReport)
                               : Commit(_context, _image, _error);
    if (!committed)
    {
        if (_error.empty())
            _error = _snapshotReport.reason;
        if (!_snapshotReport.refused)
            _snapshotReport.Refuse(_error);
    }
    return committed;
}

bool LoaderSPG::ReadSnapshotImage(const std::string& path, snapshot::Image& image, std::string& error)
{
    if (!FileHelper::FileExists(path))
    {
        error = "file not found: " + path;
        return false;
    }
    const size_t size = FileHelper::GetFileSize(path);
    std::vector<uint8_t> data(size, 0);
    if (size && FileHelper::ReadFileToBuffer(path, data.data(), size) != size)
    {
        error = "cannot read " + path;
        return false;
    }
    Image spg;
    if (!Parse(data, spg, error))
        return false;
    image = BuildSnapshotImage(spg, path);
    return true;
}

snapshot::Image LoaderSPG::BuildSnapshotImage(const Image& spg, const std::string& path)
{
    snapshot::Image image;
    image.format = "spg";
    image.sourcePath = path;
    image.formatVersion = std::to_string(spg.version >> 4) + "." + std::to_string(spg.version & 0x0F);
    image.machineHint = "tsconf";
    image.memoryModel = snapshot::MemoryModel::Physical;
    image.timingHint = "tsconf";

    for (const Block& block : spg.blocks)
        image.physical.push_back(snapshot::PhysicalRun{block.address, block.data});

    // The CPU: the header's PC / SP / interrupt flag, and the state the format promises at start (see the class doc)
    snapshot::Cpu& cpu = image.cpu;
    cpu.pc = spg.pc;
    cpu.sp = spg.sp;
    cpu.iy = 0x5C3A;
    cpu.hl2 = 0x2758;
    cpu.i = 0x3F;
    cpu.im = 1;
    cpu.iff1 = cpu.iff2 = spg.interrupts;
    image.paging.p7FFD = 0x10;
    // The two TS-Conf registers the file names travel as the extension's payload: [0] the RAM page at #C000, [1] SYS_CONFIG[1:0]
    image.extensions.push_back({kHeaderOrigin, "tsconf-registers", 2,
                                "page at #C000 = " + std::to_string(spg.page3) + ", SYS_CONFIG[1:0] = " +
                                    std::to_string(spg.clock) + ", RAM 5 / 2 at #4000 / #8000, BASIC-48 ROM at #0000",
                                {spg.page3, spg.clock}});
    return image;
}
