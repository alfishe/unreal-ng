// ZX Spectrum Next co-simulation: the real firmware boot on this emulator, written as the common trace of
// tools/verification/next-cosim (trace-format.md) so diff-traces.py can compare it with jnext / ZEsarUX / MAME.
//
// Not a gate. Skipped unless both variables are set:
//   UNREAL_NEXT_FIRMWARE  a card folder (TBBLUE.FW, machines/next/..., optionally nextzxos/ ...), as in
//                         nextfirmware_test.cpp; /Volumes/TB4-4Tb/Projects/emulators/cosim-cards/full is the full one
//   UNREAL_NEXT_COSIM     the output folder: trace.txt, state.txt, screen.png come out in the references' format
// Optional: UNREAL_NEXT_COSIM_FRAMES (default 1500), and the references' own COSIM_KINDS / COSIM_PCS / COSIM_PCWIN /
// COSIM_PORT_SKIP (nextcosimtrace.h).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/nextcosimtrace.h"
#include "common/image/imagehelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/hostfolder/hostfolderfat.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"

class NextCosim_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        const char* folder = std::getenv("UNREAL_NEXT_FIRMWARE");
        const char* out = std::getenv("UNREAL_NEXT_COSIM");
        if (!folder || !out || !std::filesystem::exists(std::filesystem::path(folder) / "TBBLUE.FW"))
            GTEST_SKIP() << "UNREAL_NEXT_FIRMWARE (a card folder with TBBLUE.FW) and UNREAL_NEXT_COSIM (output folder) are not both set";
        std::filesystem::create_directories(out);

        _emulator = EmulatorTestHelper::CreateStandardEmulator(
            "NEXT", LoggerLevel::LogError, RamPowerOn::Zero,
            [](CONFIG& config) { std::strncpy(config.next_boot_rom_path, "rom/next/nextboot.rom", sizeof config.next_boot_rom_path - 1); });
        ASSERT_NE(_emulator, nullptr);
        EmulatorContext* context = _emulator->GetContext();
        auto* memory = dynamic_cast<NextMemory*>(context->pMemory);
        _ports = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
        ASSERT_TRUE(memory && _ports && memory->HasBootRom());
        context->emulatorState.p7FFD = 0;
        memory->UpdateZ80Banks();

        FolderSnapshot snapshot;
        FolderScanOptions scan;
        std::string error;
        ASSERT_TRUE(FolderSnapshot::Scan(folder, scan, snapshot, &error)) << error;
        FatVolumeOptions options;
        std::vector<std::string> report;
        auto card = HostFolderFat::Build(snapshot, options, &error, &report);
        ASSERT_NE(card, nullptr) << error;
        ASSERT_TRUE(_ports->InsertSdCard(0, std::move(card), SdCardSpi::WriteMode::Session));
        _emulator->EnableTurboMode();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
};

// Boot on the card, run the frames, leave the trace and the dump behind. The frame loop mirrors the references: the
// frame counter starts at 1 for the first frame, the dump follows frame N (state.txt "frames N")
TEST_F(NextCosim_Test, BootTraceInTheCommonFormat)
{
    const std::filesystem::path out = std::getenv("UNREAL_NEXT_COSIM");
    const char* framesEnv = std::getenv("UNREAL_NEXT_COSIM_FRAMES");
    const int frames = framesEnv ? std::atoi(framesEnv) : 1500;

    NextCosimTrace trace(_emulator, (out / "trace.txt").string());
    ASSERT_TRUE(trace.Active());
    for (int f = 0; f < frames; f++)
    {
        _emulator->RunFrame(true);
        trace.OnFrameEnd();
    }
    trace.Dump(out.string());
    EXPECT_TRUE(std::filesystem::exists(out / "state.txt"));
}
