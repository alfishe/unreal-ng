// The ZX range renderer's whole-cell path (ScreenZX::DrawRangeZXImpl): a range that covers the four T of a character
// cell draws its eight pixels at once. A frame drawn in one range must equal the same frame drawn one T at a time (the
// path never taken), pixel for pixel in the picture and in ZX DLSS plane B.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/zx/screenzx.h"

namespace
{
/// Both machines boot the same, get the same random screen (pixels, attributes with flash and bright, border), and draw
/// the frame: `whole` in one range, the other one T at a time
void CompareCellPath(const std::string& model, bool planeB, bool flashPhase)
{
    Emulator* whole = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
    Emulator* perT = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(whole, nullptr);
    ASSERT_NE(perT, nullptr);
    std::vector<ScreenZX*> screens;
    for (Emulator* emulator : {whole, perT})
    {
        EmulatorContext* context = emulator->GetContext();
        context->pFeatureManager->setFeature(Features::kScreenHQ, true);
        for (int frame = 0; frame < (flashPhase ? 18 : 2); frame++)  // the flash swaps ink and paper every 16 frames
            emulator->RunFrame(true);
        uint8_t* page5 = context->pMemory->RAMPageAddress(5);
        uint32_t seed = 0x5EED1234u;
        for (size_t i = 0; i < 6912; i++)
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            page5[i] = static_cast<uint8_t>(seed);
        }
        auto* screen = dynamic_cast<ScreenZX*>(context->pScreen);
        EXPECT_NE(screen, nullptr);
        if (!screen)
            return;
        screen->SetActiveScreen(SCREEN_NORMAL);
        screen->SetBorderColor(5);
        screen->SetPlaneBEnabled(planeB);
        screens.push_back(screen);
    }

    const uint32_t frame = screens[0]->GetMaxFrameTiming();
    screens[0]->DrawRange(0, frame - 1);
    for (uint32_t t = 0; t < frame; t++)
        screens[1]->DrawRange(t, t);

    const FramebufferDescriptor& a = screens[0]->GetFramebufferDescriptor();
    const FramebufferDescriptor& b = screens[1]->GetFramebufferDescriptor();
    ASSERT_EQ(a.memoryBufferSize, b.memoryBufferSize);
    if (std::memcmp(a.memoryBuffer, b.memoryBuffer, a.memoryBufferSize) != 0)
    {
        size_t first = 0;
        while (a.memoryBuffer[first] == b.memoryBuffer[first])
            first++;
        FAIL() << model << ": the picture differs first at pixel x " << (first / 4) % a.width << " y " << (first / 4) / a.width;
    }
    if (planeB)
    {
        size_t countA = 0;
        size_t countB = 0;
        const uint16_t* pa = screens[0]->GetPlaneB(&countA);
        const uint16_t* pb = screens[1]->GetPlaneB(&countB);
        ASSERT_NE(pa, nullptr);
        ASSERT_EQ(countA, countB);
        EXPECT_EQ(std::memcmp(pa, pb, countA * sizeof(uint16_t)), 0) << model << ": plane B";
    }
    EmulatorTestHelper::CleanupEmulator(whole);
    EmulatorTestHelper::CleanupEmulator(perT);
}
}  // namespace

// Machine-bound (two machines boot a few frames each): longer than 50 ms
TEST(ZxScreenCells_Test, AFrameInOneRangeEqualsTheFrameDrawnOneTAtATime)
{
    for (const char* model : {"PENTAGON", "48K", "128K", "PLUS3", "SCORPION"})
    {
        CompareCellPath(model, false, false);
        CompareCellPath(model, false, true);
    }
}

TEST(ZxScreenCells_Test, PlaneBInOneRangeEqualsPlaneBDrawnOneTAtATime)
{
    CompareCellPath("PENTAGON", true, false);
    CompareCellPath("48K", true, true);
}
