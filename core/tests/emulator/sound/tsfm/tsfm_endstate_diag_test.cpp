#include "stdafx.h"
#include "pch.h"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"
#include "emulator/sound/soundmanager.h"

/// TEMPORARY DIAGNOSTIC (2026-09-13): renders the first N frames of
/// testdata/sound/tsfm/tech_support.sna headless through the SoundManager and
/// writes the master mix to the WAV named by TSFM_DIAG_WAV (default: none).
/// Used to compare renders across builds. No assertions beyond booting.

namespace
{
std::vector<int16_t> g_mix;
void Capture(void*, int16_t* samples, size_t numSamples)
{
    g_mix.insert(g_mix.end(), samples, samples + numSamples);
}
void WriteWav(const std::string& path, const std::vector<int16_t>& pcm, uint32_t rate)
{
    std::ofstream f(path, std::ios::binary);
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t bytes = uint32_t(pcm.size() * 2);
    f.write("RIFF", 4); w32(36 + bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16); w16(1); w16(2); w32(rate); w32(rate * 4); w16(4); w16(16);
    f.write("data", 4); w32(bytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), bytes);
}
}  // namespace

TEST(TsfmEndStateDiag, RenderMoebiusOpening)
{
    const char* out = std::getenv("TSFM_DIAG_WAV");
    if (!out)
        GTEST_SKIP() << "set TSFM_DIAG_WAV to render";
    const int frames = std::getenv("TSFM_DIAG_FRAMES") ? std::atoi(std::getenv("TSFM_DIAG_FRAMES")) : 750;

    Emulator* emu = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emu, nullptr);
    EmulatorContext* ctx = emu->GetContext();
    ASSERT_NE(dynamic_cast<SoundChip_TurboSoundFM*>(ctx->pSoundManager->getTurboSound()), nullptr);
    const auto sna = TestPathHelper::FindProjectRoot() / "testdata/sound/tsfm/tech_support.sna";
    ASSERT_TRUE(emu->LoadSnapshot(sna.string()));

    g_mix.clear();
    ctx->pAudioCallback.store(&Capture, std::memory_order_release);
    ctx->pAudioManagerObj.store(&g_mix, std::memory_order_release);  // callback fires only with an owner
    // Real frames (not the fast/turbo path): synthesis must be on for the mix
    auto* loop = reinterpret_cast<MainLoop_CUT*>(ctx->pMainLoop);
    for (int i = 0; i < frames; i++)
        loop->RunFrame();
    ctx->pAudioCallback.store(nullptr, std::memory_order_release);
    ctx->pAudioManagerObj.store(nullptr, std::memory_order_release);

    WriteWav(out, g_mix, uint32_t(ctx->pSoundManager->getCoreRate()));
    printf("wrote %s: %zu stereo samples, %d frames\n", out, g_mix.size() / 2, frames);
    EmulatorTestHelper::CleanupEmulator(emu);
}
