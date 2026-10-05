#include "stdafx.h"

#include "common/modulelogger.h"

#include "loaderzxp.h"

#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"

#include <fstream>
#include <iterator>

namespace
{
    uint16_t ReadBE16(const uint8_t* p)
    {
        return static_cast<uint16_t>((p[0] << 8) | p[1]);
    }

    uint32_t ReadBE32(const uint8_t* p)
    {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
    }
}

/// region <Constructors / destructors>

LoaderZXP::LoaderZXP(ModuleLogger* logger, const std::string& path) : _logger(logger), _path(path)
{
}

/// endregion </Constructors / destructors>

/// region <Methods>

bool LoaderZXP::Parse()
{
    std::ifstream file(_path, std::ios::binary);
    if (!file)
        return Fail("cannot open file '" + _path + "'");

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return ParseBuffer(data.data(), data.size());
}

bool LoaderZXP::ParseBuffer(const uint8_t* data, size_t size)
{
    _parsed = false;
    _snapshot = ZXPSnapshot{};

    if (data == nullptr || size < ZXPSnapshot::HEADER_SIZE)
        return Fail(StringHelper::Format("file too short (%zu bytes)", size));

    if (ReadBE32(data) != ZXPSnapshot::MAGIC)
        return Fail(StringHelper::Format("bad magic 0x%08X", ReadBE32(data)));

    _snapshot.flags = ReadBE32(data + 4);
    _snapshot.port3D00 = data[8];
    _snapshot.portFE = data[9];

    constexpr size_t N = ZXPSnapshot::MODULE_COUNT;
    for (size_t m = 0; m < N; m++)
    {
        ZXPModuleState& module = _snapshot.modules[m];
        const uint8_t* ports = data + 10 + m * 5;
        module.port7FFD = ports[0];
        for (size_t r = 0; r < 4; r++)
            module.reg[r] = ports[1 + r];
    }

    // Register arrays: each is 4 big-endian words, one per module
    const uint8_t* regs = data + 30;
    auto word = [&](size_t array, size_t m) { return ReadBE16(regs + array * N * 2 + m * 2); };
    for (size_t m = 0; m < N; m++)
    {
        ZXPModuleState& module = _snapshot.modules[m];
        module.af = word(0, m);
        module.afAlt = word(1, m);
        module.bc = word(2, m);
        module.bcAlt = word(3, m);
        module.de = word(4, m);
        module.deAlt = word(5, m);
        module.hl = word(6, m);
        module.hlAlt = word(7, m);
        module.ix = word(8, m);
        module.iy = word(9, m);
        module.ir = word(10, m);
        module.im = data[118 + m];
        module.iff1 = data[122 + m] != 0;
        module.iff2 = data[126 + m] != 0;
        module.pc = ReadBE16(data + 130 + m * 2);
        module.sp = ReadBE16(data + 138 + m * 2);
    }

    // Pages: per module a count, then (index, 16K) records
    size_t offset = ZXPSnapshot::HEADER_SIZE;
    for (size_t m = 0; m < N; m++)
    {
        ZXPModuleState& module = _snapshot.modules[m];
        module.pages.assign(8 * PAGE_SIZE, 0);

        if (offset + 1 > size)
            return Fail(StringHelper::Format("truncated page table of module %zu", m));
        const uint8_t count = data[offset++];

        for (uint8_t i = 0; i < count; i++)
        {
            if (offset + 1 + PAGE_SIZE > size)
                return Fail(StringHelper::Format("truncated page %u of module %zu", i, m));

            const uint8_t index = data[offset++];
            if (index >= 8)
                return Fail(StringHelper::Format("module %zu: page index %u out of range", m, index));

            std::copy(data + offset, data + offset + PAGE_SIZE, module.pages.begin() + index * PAGE_SIZE);
            module.pageUsed[index] = true;
            offset += PAGE_SIZE;
        }
    }

    if (offset != size)
        return Fail(StringHelper::Format("%zu trailing bytes after the page data", size - offset));

    _parsed = true;
    return true;
}

bool LoaderZXP::Apply(const std::array<EmulatorContext*, ZXPSnapshot::MODULE_COUNT>& contexts)
{
    if (!_parsed)
        return Fail("nothing parsed");

    for (size_t m = 0; m < ZXPSnapshot::MODULE_COUNT; m++)
    {
        if (contexts[m] == nullptr || contexts[m]->pCore == nullptr || contexts[m]->pMemory == nullptr ||
            contexts[m]->pPortDecoder == nullptr)
            return Fail(StringHelper::Format("instance for module %zu is not initialized", m));
    }

    for (size_t m = 0; m < ZXPSnapshot::MODULE_COUNT; m++)
    {
        if (!ApplyModule(contexts[m], _snapshot.modules[m], _snapshot.portFE))
            return false;
    }

    if (_logger)
    {
        MLOGINFO("ZXP '%s' applied: #3D00=#%02X (mode %u, %s, slaves %s), PC=$%04X",
                 _path.c_str(), _snapshot.port3D00, _snapshot.VideoMode(),
                 _snapshot.IsLocked() ? "locked" : "unlocked",
                 _snapshot.AreSlavesRunning() ? "running" : "waiting", _snapshot.modules[0].pc);
    }

    return true;
}

/// endregion </Methods>

/// region <Helper methods>

bool LoaderZXP::ApplyModule(EmulatorContext* context, const ZXPModuleState& module, uint8_t portFE)
{
    Core& core = *context->pCore;
    Memory& memory = *context->pMemory;
    Z80& z80 = *core.GetZ80();

    // Reset Z80 and all peripherals, then replace the state (the order the
    // SNA/Z80 loaders use)
    core.Reset();

    for (uint8_t page = 0; page < 8; page++)
    {
        if (module.pageUsed[page])
            memory.LoadRAMPageData(page, const_cast<uint8_t*>(module.pages.data() + page * PAGE_SIZE), PAGE_SIZE);
    }

    z80.af = module.af;
    z80.bc = module.bc;
    z80.de = module.de;
    z80.hl = module.hl;
    z80.alt.af = module.afAlt;
    z80.alt.bc = module.bcAlt;
    z80.alt.de = module.deAlt;
    z80.alt.hl = module.hlAlt;
    z80.ix = module.ix;
    z80.iy = module.iy;
    z80.i = static_cast<uint8_t>(module.ir >> 8);
    z80.r_low = static_cast<uint8_t>(module.ir & 0xFFu);
    z80.r_hi = static_cast<uint8_t>(module.ir & 0x80u);
    z80.im = module.im & 0x03u;
    z80.iff1 = module.iff1 ? 1 : 0;
    z80.iff2 = module.iff2 ? 1 : 0;
    z80.sp = module.sp;
    z80.pc = module.pc;
    z80.memptr = 0;
    z80.q = 0;

    // 128K paging from the module's own #7FFD. A snapshot starts outside any
    // TR-DOS session (the format has no TR-DOS flag)
    context->pPortDecoder->UnlockPaging();
    context->emulatorState.flags &= ~CF_TRDOS;
    memory.UpdateZ80Banks();
    memory.SetRAMPageToBank1(5);
    memory.SetRAMPageToBank2(2);
    memory.SetRAMPageToBank3(module.port7FFD & 0x07u);
    context->pPortDecoder->DecodePortOut(0x7FFD, module.port7FFD, z80.pc);
    context->emulatorState.p7FFD = module.port7FFD;

    if (memory.DirectReadFromZ80Memory(z80.pc) == 0x76)
    {
        z80.halted = 1;
        z80.halt_cycle = 0;
        z80.haltpos = 0;
    }

    // Border: the file has one #FE for the machine (the master drives it)
    const uint8_t border = portFE & 0x07u;
    EmulatorState& state = context->emulatorState;
    state.pFE = static_cast<uint8_t>((state.pFE & 0b1111'1000) | border);
    state.border_attr = border;
    if (context->pScreen)
    {
        context->pScreen->FillBorderWithColor(border);
        context->pScreen->RenderOnlyMainScreen();
    }

    return true;
}

snapshot::Image LoaderZXP::BuildImage(size_t module) const
{
    snapshot::Image image;
    image.format = "zxp";
    image.sourcePath = _path;
    image.formatVersion = "1";
    image.machineHint = "zxpoly";
    image.rawMachineId = "zxp module " + std::to_string(module);
    image.memoryModel = snapshot::MemoryModel::Mem128k;
    image.timingHint = "128k";
    if (module >= ZXPSnapshot::MODULE_COUNT)
    {
        image.warnings.push_back("no such module");
        return image;
    }

    const ZXPModuleState& m = _snapshot.modules[module];
    for (size_t bank = 0; bank < m.pageUsed.size(); ++bank)
    {
        if (m.pageUsed[bank])
        {
            const auto begin = m.pages.begin() + static_cast<std::ptrdiff_t>(bank * PAGE_SIZE);
            image.banks[static_cast<uint16_t>(bank)] = std::vector<uint8_t>(begin, begin + PAGE_SIZE);
        }
    }
    snapshot::Cpu& cpu = image.cpu;
    cpu.af = m.af;
    cpu.af2 = m.afAlt;
    cpu.bc = m.bc;
    cpu.bc2 = m.bcAlt;
    cpu.de = m.de;
    cpu.de2 = m.deAlt;
    cpu.hl = m.hl;
    cpu.hl2 = m.hlAlt;
    cpu.ix = m.ix;
    cpu.iy = m.iy;
    cpu.sp = m.sp;
    cpu.pc = m.pc;
    cpu.i = static_cast<uint8_t>(m.ir >> 8);
    cpu.r = static_cast<uint8_t>(m.ir & 0xFF);
    cpu.iff1 = m.iff1;
    cpu.iff2 = m.iff2;
    cpu.im = m.im;
    image.paging.p7FFD = m.port7FFD;
    image.border = _snapshot.portFE & 7u;
    image.extensions.push_back({"zxp:group", "zxpoly-registers", 0,
                                "#3D00 = " + std::to_string(_snapshot.port3D00) + ", module R0..R3 = " +
                                    std::to_string(m.reg[0]) + "," + std::to_string(m.reg[1]) + "," +
                                    std::to_string(m.reg[2]) + "," + std::to_string(m.reg[3]),
                                {}});
    return image;
}

bool LoaderZXP::Fail(const std::string& message)
{
    _error = message;
    if (_logger)
    {
        MLOGWARNING("ZXP '%s': %s", _path.c_str(), message.c_str());
    }
    return false;
}

/// endregion </Helper methods>
