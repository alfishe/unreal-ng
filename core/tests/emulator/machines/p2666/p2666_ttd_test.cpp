// Pentagon 2.666 LE under the time-travel debugger: the board's own state (P2666Board, P2666Sound, P2666BetaDisk,
// P2666Uart, the MCU clock, the SD card) rides in every checkpoint, so a replay of each recorded frame reaches the next
// checkpoint byte for byte (CPU, chipset, RAM, every device blob).

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdreplaycheck.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/soundmanager.h"
#include "pch.h"
#include "stdafx.h"

class P2666Ttd_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("P2666", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        features->setFeature(Features::kSoundHQ, true);
        features->setFeature(Features::kScreenHQ, true);
        _context->pMemory->UpdateFeatureCache();
        _context->pSoundManager->UpdateFeatureCache();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
    }

    void TearDown() override
    {
        if (_ttd)
            _ttd->StopRecording();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Frames(int count) { _emulator->RunNFrames(count, /*skipBreakpoints=*/true); }

    void Tap(ZXKeysEnum key, int framesAfter)
    {
        _context->pKeyboard->PressKey(key);
        Frames(5);
        _context->pKeyboard->ReleaseKey(key);
        Frames(framesAfter);
    }

    /// Record `frames` frames, then replay every recorded frame and compare it with the next checkpoint
    void RecordAndCheck(int frames)
    {
        ASSERT_TRUE(_ttd->StartRecording());
        Frames(frames);
        _ttd->StopRecording();
        ASSERT_GE(_ttd->GetCheckpointCount(), static_cast<size_t>(frames));

        const ttd::TTDReplayCheckResult check = _ttd->QueryReplayCheck(ttd::TTDReplayCheckQuery{});
        ASSERT_TRUE(check.error.empty()) << check.error;
        EXPECT_GE(check.checked, static_cast<uint64_t>(frames - 2));
        EXPECT_EQ(check.diverged, 0u) << ttd::TTDReplayCheckText(check);
        EXPECT_EQ(check.matched, check.checked) << ttd::TTDReplayCheckText(check);
    }
};

/// A program at #8000 drives every board device each pass: both YM2149 cores (#FFFD / #BFFD, #FF / #FE select),
/// Covox (#FB), the border (#FE), the UART's transmitter (#F8EF), the CMOS (#EFF7 on, #DFF7 address, #BFF7 data)
TEST_F(P2666Ttd_Test, EveryFrameOfTheBoardsDevicesReplaysExactly)
{
    Frames(20);  // past the MCU upload and Gluk's start
    // clang-format off
    const std::vector<uint8_t> program = {
        0xF3,                    // DI
        0x1C,                    // loop: INC E
        0x01, 0xFD, 0xFF,        // LD BC,#FFFD
        0x3E, 0xFF, 0xED, 0x79,  // chip 0
        0x3E, 0x00, 0xED, 0x79,  // R0
        0x06, 0xBF, 0xED, 0x59,  //   = E
        0x06, 0xFF, 0x3E, 0x08, 0xED, 0x79,  // R8
        0x06, 0xBF, 0x3E, 0x0F, 0xED, 0x79,  //   = 15
        0x06, 0xFF, 0x3E, 0x07, 0xED, 0x79,  // R7
        0x06, 0xBF, 0x3E, 0x3E, 0xED, 0x79,  //   = tone A only
        0x06, 0xFF, 0x3E, 0xFE, 0xED, 0x79,  // chip 1
        0x3E, 0x08, 0xED, 0x79,              // R8
        0x06, 0xBF, 0xED, 0x59,              //   = E
        0x7B, 0xD3, 0xFB,        // Covox = E
        0xE6, 0x07, 0xD3, 0xFE,  // border = E & 7
        0x01, 0xEF, 0xF8, 0xED, 0x59,        // UART THR = E
        0x01, 0xF7, 0xEF, 0x3E, 0x80, 0xED, 0x79,  // #EFF7: CMOS on
        0x01, 0xF7, 0xDF, 0x3E, 0x20, 0xED, 0x79,  // CMOS cell #20
        0x01, 0xF7, 0xBF, 0xED, 0x59,              //   = E
        0x06, 0x00, 0x10, 0xFE,  // DJNZ $ (256 passes)
        0xC3, 0x01, 0x80,        // JP loop
    };
    // clang-format on
    for (size_t i = 0; i < program.size(); i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    _context->pCore->GetZ80()->pc = 0x8000;
    Frames(1);

    ASSERT_NO_FATAL_FAILURE(RecordAndCheck(12));
}

/// Gluk's "trdos" entry boots drive A from the RAM disk (the board's default Beta Disk): the frames of the boot, with
/// the controller's commands, DRQ and the DOS latches changing, replay exactly
TEST_F(P2666Ttd_Test, TrDosBootFromTheRamDiskReplaysExactly)
{
    const std::filesystem::path disk = TestPathHelper::FindProjectRoot() / "testdata/machines/p2666/software/pentagon-tests.trd";
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(disk.string(), 0, &error)) << error;

    Frames(50);  // the Gluk menu
    for (int k = 0; k < 7; k++)  // the cursor from "Gluk boot" down to "trdos"
        Tap(ZXKEY_EXT_DOWN, 10);
    _context->pKeyboard->PressKey(ZXKEY_ENTER);
    Frames(5);
    _context->pKeyboard->ReleaseKey(ZXKEY_ENTER);

    ASSERT_NO_FATAL_FAILURE(RecordAndCheck(30));
}

/// The model's blobs and their sizes as the TTD format document lists them (ttd-v1-architecture-and-format.md §5.8)
TEST_F(P2666Ttd_Test, TheBoardDeclaresAndSuppliesItsBlobs)
{
    PortDecoder* decoder = _context->pPortDecoder;
    ASSERT_NE(decoder, nullptr);
    std::map<ttd::TTDDeviceType, size_t> sizes;
    for (const auto& serializer : decoder->CreateTTDSerializers())
        sizes[serializer->TTDType()] = serializer->TTDStateSize();
    for (ttd::TTDDeviceType type : decoder->GetTTDModelStateTypes())
        EXPECT_EQ(sizes.count(type), 1u) << "declared type " << static_cast<unsigned>(type) << " has no serializer";
    EXPECT_EQ(sizes[ttd::TTDDeviceType::P2666Board], 407u);
    EXPECT_EQ(sizes[ttd::TTDDeviceType::P2666Sound], 224u);
    EXPECT_EQ(sizes[ttd::TTDDeviceType::P2666BetaDisk], 51u);
    EXPECT_EQ(sizes[ttd::TTDDeviceType::P2666Uart], 108u);
}
