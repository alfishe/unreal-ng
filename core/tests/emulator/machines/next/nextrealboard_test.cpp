// The real-board test programs of ZXSpectrumNextTests (https://github.com/MrKWatkins/ZXSpectrumNextTests, release/*.snx: 48K
// snapshots whose results were checked on real boards - photographs in the repository) on the whole NEXT machine, with the
// video drawn line by line. The machine is put in the state the programs expect (what NextZXOS leaves after loading a 48K
// snapshot: #7FFD = #30, core id 0, the ULA palette of the 16 defaults) and a program runs for some frames.
//
// Provisioned: UNREAL_NEXT_TESTS names the folder of the clone (it needs release/); without it the tests skip.
// Source: A (real boards: the programs' own pass / fail screens, readmes in release/*.txt)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "loaders/nex/loadernex.h"

namespace
{
std::filesystem::path Release()
{
    const char* folder = std::getenv("UNREAL_NEXT_TESTS");
    return folder ? std::filesystem::path(folder) / "release" : std::filesystem::path();
}
}  // namespace

class NextRealBoard_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// Loads release/<name> (copied as .sna) and runs `frames`; keys = {key, frame} pairs pressed for 3 frames
    bool Run(const std::string& name, int frames, const std::vector<std::pair<char, int>>& keys = {})
    {
        const std::filesystem::path source = Release() / name;
        if (Release().empty() || !std::filesystem::exists(source))
            return false;
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        if (!_emulator)
            return false;
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        _context->pFeatureManager->setFeature(Features::kScreenHQ, true);  // the borders and copper follow the beam
        const std::filesystem::path sna = std::filesystem::temp_directory_path() / ("nextrealboard-" + name + ".sna");
        std::filesystem::copy_file(source, sna, std::filesystem::copy_options::overwrite_existing);
        if (!_emulator->LoadSnapshot(sna.string()))
            return false;
        _context->emulatorState.p7FFD = 0x30;
        dynamic_cast<NextMemory*>(_context->pMemory)->ApplyClassicPaging(0x30, 0);
        _ports->Board().SetCoreId(0);
        LoaderNex::FillUlaPalette(_ports->Board());
        for (int i = 0; i < frames; i++)
        {
            for (const auto& key : keys)
            {
                if (i == key.second)
                    _context->pKeyboard->PressKey(static_cast<ZXKeysEnum>(key.first));
                if (i == key.second + 3)
                    _context->pKeyboard->ReleaseKey(static_cast<ZXKeysEnum>(key.first));
            }
            _emulator->RunFrame(true);
        }
        return true;
    }
    uint32_t Pixel(unsigned x, unsigned y) const
    {
        const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
        return reinterpret_cast<const uint32_t*>(fb.memoryBuffer)[y * fb.width + x];
    }
};

// NextReg_defaults: every register read and written, the 16 x 16 grid coloured by the result. Red (R/W/d ERROR) is a failure;
// the real board of core 3.1.5 shows none. (Yellow = default differs, accepted: the board shows a few.)
TEST_F(NextRealBoard_Test, NextRegDefaultsShowNoErrorCell)
{
    if (!Run("!NextReg.snx", 400))
        GTEST_SKIP() << "UNREAL_NEXT_TESTS (a ZXSpectrumNextTests clone with release/!NextReg.snx) is not set";
    const uint32_t red = 0xFF0000FF;  // RGBA bytes R G B A: bright red (255, 0, 0)
    unsigned redCells = 0;
    std::string where;
    for (unsigned row = 0; row < 16; row++)
        for (unsigned col = 0; col < 16; col++)
            if (Pixel(66 + col * 16 + 8, 64 + row * 16 + 8) == red)
            {
                redCells++;
                char buffer[16];
                std::snprintf(buffer, sizeof buffer, " %02X", row * 16 + col);
                where += buffer;
            }
    EXPECT_EQ(redCells, 0u) << "NextReg cells in error:" << where;
}

// NextReg #69 and the ports it aliases (#123B bit 1, #7FFD bit 3, #FF): 10 checks both ways; the border turns green when all pass
TEST_F(NextRealBoard_Test, NextReg69AndItsPortsAgree)
{
    if (!Run("NReg0x69.snx", 300))
        GTEST_SKIP() << "UNREAL_NEXT_TESTS (a ZXSpectrumNextTests clone with release/NReg0x69.snx) is not set";
    EXPECT_EQ(_context->emulatorState.pFE & 7, 4) << "the border is green when all ten pass, red otherwise";
}

// Z80N: every new instruction against its expected result ("5" runs all, "2" sets 28 MHz); no row may say ERR
TEST_F(NextRealBoard_Test, Z80NInstructionsAllPass)
{
    if (!Run("!Z80N.snx", 1200, {{'2', 10}, {'5', 40}}))
        GTEST_SKIP() << "UNREAL_NEXT_TESTS (a ZXSpectrumNextTests clone with release/!Z80N.snx) is not set";
    // the result column of the 23 rows is at ULA attribute column 28 onwards; ERR rows are painted red (paper 2), OK green / bright
    unsigned errRows = 0, okRows = 0;
    const uint8_t* attributes = _context->pMemory->RAMPageAddress(5) + 0x1800;
    for (unsigned row = 0; row < 24; row++)
        for (unsigned col = 28; col < 32; col++)
        {
            const uint8_t paper = (attributes[row * 32 + col] >> 3) & 7;
            errRows += paper == 2 ? 1 : 0;
            okRows += paper == 4 || paper == 5 ? 1 : 0;
        }
    EXPECT_EQ(errRows, 0u);
    EXPECT_GT(okRows, 0u);
}
