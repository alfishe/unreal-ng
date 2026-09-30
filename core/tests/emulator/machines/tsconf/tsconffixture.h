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
#include "emulator/memory/tsconf/tsconfmemory.h"
#include "emulator/platform.h"
#include "emulator/ports/models/portdecoder_tsconf.h"

/// @brief Full-machine TS-Conf fixture (TSConf implementation-plan, phase 1).
///
/// A real EmulatorContext + Core::Init() for MM_TSL with a synthetic tagged
/// ROM and tagged RAM, so a window's mapping is one read:
///   ROM page k (0-31): every byte = k, except #3D00 = #C9 (RET, for the DOS trap)
///   RAM page n (0-255): every byte = n, except offset #2000 = kRamMarker
/// Tag(window) reads offset #0010 (the page number), IsRam(window) offset #2000.
class TsConfFixture : public ::testing::Test
{
protected:
    static constexpr uint8_t kRamMarker = 0x5A;  // never a ROM tag (0..31)

    EmulatorContext* _context = nullptr;
    Core* _core = nullptr;
    Memory* _memory = nullptr;
    Z80* _z80 = nullptr;
    PortDecoder_TSConf* _decoder = nullptr;
    std::string _romPath;
    uint16_t _romPages = 32;

    void SetUp() override
    {
        if (!BuildMachine())
            FAIL() << "TsConfFixture: machine construction failed";
    }

    void TearDown() override { DestroyMachine(); }

    /// region <Drive helpers>

    void Out(uint16_t port, uint8_t value) { _decoder->DecodePortOut(port, value, 0x0000); }
    uint8_t In(uint16_t port) { return _decoder->DecodePortIn(port, 0x0000); }
    /// `OUT (reg << 8 | #AF), value`
    void Reg(uint8_t reg, uint8_t value) { Out(static_cast<uint16_t>((reg << 8) | 0xAF), value); }

    uint8_t Tag(uint16_t window) { return _memory->DirectReadFromZ80Memory(static_cast<uint16_t>(window + 0x0010)); }
    bool IsRam(uint16_t window) { return _memory->DirectReadFromZ80Memory(static_cast<uint16_t>(window + 0x2000)) == kRamMarker; }

    /// A CPU read / write through the Z80's current memory interface (bus
    /// overlays, the cache and write protection all apply)
    uint8_t Peek(uint16_t addr) { return (_memory->*(_z80->MemIf->MemoryRead))(addr, false); }
    void Poke(uint16_t addr, uint8_t value) { (_memory->*(_z80->MemIf->MemoryWrite))(addr, value); }

    /// Byte of a physical RAM page, bypassing the windows
    uint8_t& Ram(uint16_t page, uint16_t offset) { return _memory->RAMBase()[page * PAGE_SIZE + offset]; }

    /// Run code placed at `origin` (a RAM window) until PC passes its end
    void RunCode(const std::vector<uint8_t>& code, uint16_t origin = 0x8000)
    {
        for (size_t i = 0; i < code.size(); i++)
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(origin + i), code[i]);
        _z80->pc = origin;
        const uint16_t end = static_cast<uint16_t>(origin + code.size());
        for (int guard = 0; guard < 1000 && _z80->pc != end; guard++)
            _z80->Z80Step();
        ASSERT_EQ(_z80->pc, end);
    }

    /// Rebuild with a ROM image of `pages` 16 KB pages
    bool RebuildWithRomPages(uint16_t pages)
    {
        DestroyMachine();
        _romPages = pages;
        return BuildMachine();
    }

    /// endregion </Drive helpers>

private:
    bool BuildMachine()
    {
        _context = new EmulatorContext(LoggerLevel::LogError);

        CONFIG& config = _context->config;
        config.mem_model = MM_TSL;
        config.ramsize = 4096;
        config.trdos_present = true;

        Config configHelper(_context);
        configHelper.ApplyModelTimingDefaults(config, true /* canonicalGeometry */);

        _romPath = TestPathHelper::GetUniqueTestScratchPath("tsconffixture.rom");
        if (!WriteTaggedRom(_romPath, _romPages))
            return false;
        std::strncpy(config.tsl_rom_path, _romPath.c_str(), sizeof(config.tsl_rom_path) - 1);

        _core = new Core(_context);
        if (!_core->Init())
            return false;

        _memory = _context->pMemory;
        _z80 = _core->GetZ80();
        _decoder = dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder);
        if (!_decoder)
            return false;

        ROM rom(_context);
        if (!rom.LoadROM())
            return false;

        _decoder->reset();
        TagRam();
        return true;
    }

    void DestroyMachine()
    {
        delete _core;
        _core = nullptr;
        delete _context;
        _context = nullptr;
        _memory = nullptr;
        _z80 = nullptr;
        _decoder = nullptr;

        if (!_romPath.empty())
        {
            std::error_code ec;
            std::filesystem::remove(_romPath, ec);
        }
    }

    void TagRam()
    {
        for (uint16_t page = 0; page < 256; page++)
        {
            uint8_t* base = _memory->RAMBase() + page * PAGE_SIZE;
            std::memset(base, static_cast<uint8_t>(page), PAGE_SIZE);
            base[0x2000] = kRamMarker;
        }
    }

    static bool WriteTaggedRom(const std::string& path, uint16_t pages)
    {
        std::vector<uint8_t> image(static_cast<size_t>(pages) * PAGE_SIZE);
        for (uint16_t page = 0; page < pages; page++)
        {
            uint8_t* base = image.data() + static_cast<size_t>(page) * PAGE_SIZE;
            std::memset(base, static_cast<uint8_t>(page), PAGE_SIZE);
            base[0x3D00] = 0xC9;
        }
        return FileHelper::SaveBufferToFile(path, image.data(), image.size());
    }
};
