/// @file ttdconfigcapture_test.cpp
/// @brief The live machine's configuration fingerprint (TTD v2, Phase 3, Step 4):
/// the shared settings on every model, the board options from the model's own
/// decoder only.

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "debugger/ttd/ttdconfigcapture.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

namespace
{
ttd::TTDConfigFingerprint Capture(const char* model)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
    EXPECT_NE(emulator, nullptr);
    if (!emulator)
        return {};
    ttd::TTDConfigFingerprint fp = ttd::CaptureConfigFingerprint(*emulator->GetContext(), 0x1234);
    EmulatorTestHelper::CleanupEmulator(emulator);
    return fp;
}
}  // namespace

TEST(TTDConfigCapture_Test, BoardOptionsComeFromTheModelsDecoder)
{
    const ttd::TTDConfigFingerprint pentagon = Capture("PENTAGON");
    const ttd::TTDConfigFingerprint sprinter = Capture("SPRINTER");
    const ttd::TTDConfigFingerprint profi = Capture("PROFI");
    for (const ttd::TTDConfigFingerprint* fp : {&pentagon, &sprinter, &profi})
    {
        EXPECT_NE(fp->Find("machine.model"), nullptr);
        EXPECT_NE(fp->Find("timing.frame"), nullptr);
        ASSERT_NE(fp->Find(ttd::kConfigRomSignature), nullptr);
        EXPECT_EQ(fp->Find(ttd::kConfigRomSignature)->value, 0x1234u);
    }
    EXPECT_NE(sprinter.Find("sprinter.turbo_allowed"), nullptr);
    EXPECT_NE(profi.Find("profi.turbo"), nullptr);
    EXPECT_EQ(pentagon.Find("sprinter.turbo_allowed"), nullptr);
    EXPECT_EQ(pentagon.Find("profi.turbo"), nullptr);
    EXPECT_EQ(sprinter.Find("profi.turbo"), nullptr);
    EXPECT_TRUE(ttd::Compare(pentagon, Capture("PENTAGON")).empty()) << "the same machine, the same settings";
}
