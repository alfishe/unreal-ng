#pragma once

#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/rom.h"
#include "emulator/platform.h"
#include "emulator/ports/models/portdecoder_scorpiongmx.h"

/// @brief A synthetic Scorpion GMX machine for the register file, RAM and ROM mapping and TTD tests (GMX design 2.2-2.4, tdd-plan
///        sections 1-3).
///
/// Self-identifying images: RAM page n keeps n in its first byte and 0x00 in the second; every byte of ROM page k is
/// 0x80 | k, so the first two bytes behind a window name what is mapped there.
class ScorpionGmxFixture : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Core* _core = nullptr;
    Memory* _memory = nullptr;
    PortDecoder_ScorpionGMX* _gmx = nullptr;
    std::string _romPath;

    static constexpr uint16_t kRomPages = 32;    // 512 KB = 8 planes of four 16 KB pages
    static constexpr uint16_t kRamPages = 128;   // 2048 KB

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        CONFIG& config = _context->config;
        config.mem_model = MM_GMX;
        config.ramsize = RAM_2048;
        config.trdos_present = true;
        Config configHelper(_context);
        configHelper.ApplyModelTimingDefaults(config, true);

        _romPath = TestPathHelper::GetUniqueTestScratchPath("scorpiongmx.rom");
        std::vector<uint8_t> image(static_cast<size_t>(kRomPages) * PAGE_SIZE);
        for (uint16_t page = 0; page < kRomPages; page++)
            std::fill_n(image.begin() + static_cast<size_t>(page) * PAGE_SIZE, PAGE_SIZE, static_cast<uint8_t>(0x80 | page));
        ASSERT_TRUE(FileHelper::SaveBufferToFile(_romPath, image.data(), image.size()));
        std::strncpy(config.gmx_rom_path, _romPath.c_str(), sizeof(config.gmx_rom_path) - 1);

        _core = new Core(_context);
        ASSERT_TRUE(_core->Init());
        _memory = _context->pMemory;
        ROM rom(_context);
        ASSERT_TRUE(rom.LoadROM());
        _gmx = dynamic_cast<PortDecoder_ScorpionGMX*>(_context->pPortDecoder);
        ASSERT_NE(_gmx, nullptr) << "MM_GMX must get the GMX decoder";
        _gmx->reset();

        for (uint16_t page = 0; page < kRamPages; page++)
        {
            uint8_t* base = _memory->RAMBase() + static_cast<size_t>(page) * PAGE_SIZE;
            base[0] = static_cast<uint8_t>(page);
            base[1] = 0x00;
        }
    }

    void TearDown() override
    {
        EmulatorTestHelper::ReleaseBareCore(_core, _context);
        std::error_code ec;
        std::filesystem::remove(_romPath, ec);
    }

    void Out(uint16_t port, uint8_t value) { _gmx->DecodePortOut(port, value, 0x8000); }
    uint8_t In(uint16_t port) { return _gmx->DecodePortIn(port, 0x8000); }
    EmulatorState& State() { return _context->emulatorState; }

    /// The RAM page behind a window, or -1 when ROM is mapped there
    int RamPageAt(uint16_t window)
    {
        if (_memory->DirectReadFromZ80Memory(static_cast<uint16_t>(window + 1)) != 0x00)
            return -1;
        return _memory->DirectReadFromZ80Memory(window);
    }

    /// The ROM page behind a window, or -1 when RAM is mapped there
    int RomPageAt(uint16_t window)
    {
        const uint8_t tag = _memory->DirectReadFromZ80Memory(window);
        if (_memory->DirectReadFromZ80Memory(static_cast<uint16_t>(window + 1)) != tag || !(tag & 0x80))
            return -1;
        return tag & 0x7F;
    }
};
