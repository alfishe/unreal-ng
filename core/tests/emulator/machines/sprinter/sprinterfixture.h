#pragma once

#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/rom.h"
#include "emulator/memory/sprinter/sprintermemory.h"
#include "emulator/platform.h"
#include "emulator/ports/models/portdecoder_sprinter.h"

/// @brief Full-machine Sprinter fixture (Sprinter test-plan §2).
///
/// A real EmulatorContext + Core::Init() for MM_SPRINTER with fast start, a
/// synthetic tagged ROM and tagged RAM, so a window's mapping is one read:
///   ROM page k (0-15): every byte = #E0 + k
///   RAM page n (0-255): every byte = n, except offset #2000 = kRamMarker
/// Tag(window) reads offset #0010; IsRam(window) checks offset #2000.
/// The port table (page #40) is cleared to "no port" by TagRam(); a test loads
/// the BIOS 3.04 table (LoadTable304) or writes the entries it needs (SetCode).
class SprinterFixture : public ::testing::Test
{
protected:
    static constexpr uint8_t kRamMarker = 0x5A;
    static constexpr uint8_t kRomTagBase = 0xE0;

    EmulatorContext* _context = nullptr;
    Core* _core = nullptr;
    Memory* _memory = nullptr;
    SprinterMemory* _sprinterMemory = nullptr;
    Z80* _z80 = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;
    std::string _romPath;
    bool _realRom = false;

    void SetUp() override
    {
        if (!BuildMachine())
            FAIL() << "SprinterFixture: machine construction failed";
    }

    void TearDown() override { DestroyMachine(); }

    /// region <Drive helpers>

    void Out(uint16_t port, uint8_t value) { _decoder->DecodePortOut(port, value, 0x0000); }
    uint8_t In(uint16_t port) { return _decoder->DecodePortIn(port, 0x0000); }

    SprinterPldState& Pld() { return _decoder->GetPldState(); }

    uint8_t Tag(uint16_t window) { return _memory->DirectReadFromZ80Memory(static_cast<uint16_t>(window + 0x0010)); }
    bool IsRam(uint16_t window) { return _memory->DirectReadFromZ80Memory(static_cast<uint16_t>(window + 0x2000)) == kRamMarker; }

    /// A CPU read / write through the Z80's current memory interface (bus overlays apply)
    uint8_t Peek(uint16_t addr) { return (_memory->*(_z80->MemIf->MemoryRead))(addr, false); }
    void Poke(uint16_t addr, uint8_t value) { (_memory->*(_z80->MemIf->MemoryWrite))(addr, value); }

    uint8_t& Ram(uint16_t page, uint16_t offset) { return _memory->RAMBase()[page * PAGE_SIZE + offset]; }
    uint8_t* Table() { return _memory->RAMPageAddress(SprinterMemory::kPortTablePage); }

    /// Put `code` into the port table for `port` (read or write) with the current CNF / PN5 / DOS state
    void SetCode(uint16_t port, bool isRead, uint8_t code) { Table()[_decoder->LookupIndex(port, isRead)] = code; }

    /// Put `code` into every map / PN5 / DOS variant of the table for `port`
    void SetCodeAll(uint16_t port, bool isRead, uint8_t code)
    {
        const uint16_t address = static_cast<uint16_t>(((port >> 14) & 0x03) << 7 | ((port >> 13) & 0x01) << 4 |
                                                       ((port >> 7) & 0x01) << 3 | (port & 0x67));
        for (uint16_t variant = 0; variant < 32; variant++)
            Table()[(variant << 10 & 0x3C00) | (isRead ? 0x200 : 0) | address] = code;
    }

    /// The first IN after a PLD reset opens the port decoder (through a code-#00 port)
    void OpenDcp() { In(0x00FE); }

    /// One instruction on the Sprinter's CPU: the Z84C15 engine (Z80::Z80Step is the native interpreter)
    void Step()
    {
        if (_z80->GetEngine())
            _z80->EngineStep();
        else
            _z80->Z80Step();
    }

    /// Run code placed at `origin` (a RAM window) until PC passes its end
    void RunCode(const std::vector<uint8_t>& code, uint16_t origin = 0x8000)
    {
        for (size_t i = 0; i < code.size(); i++)
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(origin + i), code[i]);
        _z80->pc = origin;
        const uint16_t end = static_cast<uint16_t>(origin + code.size());
        for (int guard = 0; guard < 100000 && _z80->pc != end; guard++)
            Step();
        ASSERT_EQ(_z80->pc, end);
    }

public:
    static bool Rom304Available()
    {
        return FileHelper::FileExists((TestPathHelper::FindProjectRoot() / "data" / "rom" / "sprinter" / "sp2k-3.04.rom").string());
    }

    /// The real BIOS 3.04 image (data/rom/sprinter), empty when it is not there
    static std::vector<uint8_t> Rom304()
    {
        const std::string path = (TestPathHelper::FindProjectRoot() / "data" / "rom" / "sprinter" / "sp2k-3.04.rom").string();
        std::vector<uint8_t> rom;
        if (!FileHelper::FileExists(path))
            return rom;
        rom.resize(16 * PAGE_SIZE);
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
            return {};
        const size_t got = std::fread(rom.data(), 1, rom.size(), f);
        std::fclose(f);
        if (got != rom.size())
            return {};
        return rom;
    }

    /// The port table BIOS 3.04 writes (page 8 DcpInit #0CA1): the packed stream at
    /// page 8 #1400 - a flag byte, then per bit (MSB first) a literal or a zero - into
    /// 16 KB, then map 3 = four copies of map 0's first KB (tools/sprinter/dcp-table.py)
    static std::vector<uint8_t> Table304(const std::vector<uint8_t>& rom)
    {
        std::vector<uint8_t> out;
        const uint8_t* p8 = rom.data() + 8 * PAGE_SIZE;
        size_t hl = 0x1400;
        while (out.size() < PAGE_SIZE)
        {
            uint8_t flags = p8[hl++];
            for (int bit = 0; bit < 8; bit++)
            {
                out.push_back((flags & 0x80) ? p8[hl++] : 0);
                flags = static_cast<uint8_t>(flags << 1);
            }
        }
        for (size_t k = 0; k < 4; k++)
            std::memcpy(out.data() + 0x3000 + k * 0x400, out.data(), 0x400);
        return out;
    }

protected:
    /// Load the 3.04 table into page #40; false when the ROM is missing
    bool LoadTable304()
    {
        const std::vector<uint8_t> rom = Rom304();
        if (rom.empty())
            return false;
        const std::vector<uint8_t> table = Table304(rom);
        std::memcpy(Table(), table.data(), table.size());
        return true;
    }

    /// Rebuild the machine with the real BIOS 3.04 ROM (and `fastStart`)
    bool RebuildWithRealRom(bool fastStart = true)
    {
        DestroyMachine();
        _realRom = true;
        _fastStart = fastStart;
        return BuildMachine();
    }

    /// Rebuild the synthetic machine (after changing _cmosPath / _fastStart)
    bool Rebuild()
    {
        DestroyMachine();
        return BuildMachine();
    }

    /// Destroy the machine now (the decoder saves the CMOS file)
    void Destroy() { DestroyMachine(); }

    bool _fastStart = true;
    std::string _cmosPath;

    /// endregion </Drive helpers>

private:
    bool BuildMachine()
    {
        _context = new EmulatorContext(LoggerLevel::LogError);

        CONFIG& config = _context->config;
        config.mem_model = MM_SPRINTER;
        config.ramsize = 4096;
        config.trdos_present = true;
        config.ramPowerOn = RamPowerOn::Zero;
        config.sprinter.fast_start = _fastStart ? 1 : 0;
        config.sprinter.turbo_allowed = 1;
        std::strncpy(config.sprinter.cmos_path, _cmosPath.c_str(), sizeof(config.sprinter.cmos_path) - 1);

        Config configHelper(_context);
        configHelper.ApplyModelTimingDefaults(config, true /* canonicalGeometry */);

        if (_realRom)
        {
            _romPath = (TestPathHelper::FindProjectRoot() / "data" / "rom" / "sprinter" / "sp2k-3.04.rom").string();
            if (!FileHelper::FileExists(_romPath))
                return false;
        }
        else
        {
            _romPath = TestPathHelper::GetUniqueTestScratchPath("sprinterfixture.rom");
            if (!WriteTaggedRom(_romPath))
                return false;
        }
        std::strncpy(config.sprinter_rom_path, _romPath.c_str(), sizeof(config.sprinter_rom_path) - 1);

        _core = new Core(_context);
        if (!_core->Init())
            return false;

        _memory = _context->pMemory;
        _sprinterMemory = dynamic_cast<SprinterMemory*>(_memory);
        _z80 = _core->GetZ80();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        if (!_decoder || !_sprinterMemory)
            return false;

        ROM rom(_context);
        if (!rom.LoadROM())
            return false;

        if (!_realRom)
            TagRam();
        _core->Reset();

        // The fast start leaves the Z84C15 as the loader does: WCR = #04, one memory wait per
        // cycle until the BIOS's InitCpuPorts clears it. The synthetic ROM has no BIOS: the
        // fixture clears it the same way (OUT (#EE),0 : OUT (#EF),0), so test code runs with the
        // PLD's waits only. The real ROM's BIOS does it itself
        if (!_realRom && _fastStart)
        {
            _decoder->GetZ84().Write(0xEE, 0x00);
            _decoder->GetZ84().Write(0xEF, 0x00);
        }
        return true;
    }

    void DestroyMachine()
    {
        delete _core;
        _core = nullptr;
        delete _context;
        _context = nullptr;
        _memory = nullptr;
        _sprinterMemory = nullptr;
        _z80 = nullptr;
        _decoder = nullptr;

        if (!_romPath.empty() && !_realRom)
        {
            std::error_code ec;
            std::filesystem::remove(_romPath, ec);
        }
        _romPath.clear();
    }

    void TagRam()
    {
        for (uint16_t page = 0; page < 256; page++)
        {
            uint8_t* base = _memory->RAMBase() + page * PAGE_SIZE;
            std::memset(base, static_cast<uint8_t>(page), PAGE_SIZE);
            base[0x2000] = kRamMarker;
        }
        std::memset(_memory->RAMPageAddress(SprinterMemory::kPortTablePage), 0, PAGE_SIZE);
    }

    static bool WriteTaggedRom(const std::string& path)
    {
        std::vector<uint8_t> image(static_cast<size_t>(16) * PAGE_SIZE);
        for (uint16_t page = 0; page < 16; page++)
            std::memset(image.data() + static_cast<size_t>(page) * PAGE_SIZE, kRomTagBase + page, PAGE_SIZE);
        return FileHelper::SaveBufferToFile(path, image.data(), image.size());
    }
};
