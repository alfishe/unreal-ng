// The rendered picture of every classic model, pinned: each model boots its
// ROM (and Pentagon runs a border / multicolor demo) and the framebuffer must
// hash exactly as recorded on the build before TS-Conf got its own renderer
// (master 686fd0d4). Guards the shared video code (mode tables, line
// geometry, renderer selection) against a change that moves any other
// machine's picture by a single pixel.
//
// Power-on RAM differs between builds (random or zero), so every run starts
// from zeroed RAM and a reset; real-time clocks are frozen.
//
// To re-record after an intended change of a model's picture, run with
// UNREALNG_RECORD_FRAME_GOLDEN=1 and paste the printed table below.
//
// Runtime justification: 12 runs x 2 render paths x 150 frames (booting a real ROM
// is the point); about a second on the development machine.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_atm3.h"
#include "emulator/ports/models/portdecoder_profi.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"
#include "emulator/video/screen.h"

namespace
{
struct FrameGolden
{
    const char* model;
    const char* snapshot;  // run after the boot, or nullptr
    uint16_t width;
    uint16_t height;
    uint64_t hashBeam;   // per-T-state rendering (screenhq on, the product default)
    uint64_t hashBatch;  // frame-end batch rendering (screenhq off)
};

const FrameGolden kFrameGolden[] = {
    {"48K", nullptr, 352, 288, 0xA41CF760F5AF9B37ull, 0x9F7E8F7927DE22B7ull},
    {"128k", nullptr, 352, 288, 0xA41CF760F5AF9B37ull, 0x9F7E8F7927DE22B7ull},
    {"PLUS2", nullptr, 352, 288, 0x64D88EBB4DC20FFFull, 0x95F7E711429E867Full},
    {"PLUS2A", nullptr, 352, 288, 0x64D88EBB4DC20FFFull, 0x95F7E711429E867Full},
    {"PLUS3", nullptr, 352, 288, 0x64D88EBB4DC20FFFull, 0x95F7E711429E867Full},
    {"PENTAGON", nullptr, 352, 288, 0xA41CF760F5AF9B37ull, 0x9F7E8F7927DE22B7ull},
    {"PENTAGON", "testdata/loaders/sna/eyeache1.sna", 352, 288, 0xB718B9E5C4AC6478ull, 0xB718B9E5C4AC6478ull},
    {"SCORPION", nullptr, 352, 288, 0xA41CF760F5AF9B37ull, 0x9F7E8F7927DE22B7ull},
    {"PROFSCORP", nullptr, 352, 288, 0xA41CF760F5AF9B37ull, 0x9F7E8F7927DE22B7ull},
    {"PROFI", nullptr, 352, 288, 0xA41CF760F5AF9B37ull, 0x9F7E8F7927DE22B7ull},
    {"ATM710", nullptr, 640, 288, 0x2FC6C35968C6F354ull, 0x2FC6C35968C6F354ull},
    {"ATM3", nullptr, 352, 288, 0xE274C408E6A1D406ull, 0xE274C408E6A1D406ull},
};

constexpr unsigned kFrames = 150;

uint64_t Fnv1a(const uint8_t* bytes, size_t size)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < size; i++)
    {
        h ^= bytes[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}
constexpr int64_t kFixedTime = 1767268830;  // 2026-01-01 12:00:30 UTC

struct Frame
{
    bool created = false;
    uint16_t width = 0, height = 0;
    uint64_t hash = 0;
};

Frame RunModel(const FrameGolden& g, bool beam)
{
    Frame f;
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(g.model, LoggerLevel::LogError);
    if (!emulator)
        return f;
    f.created = true;
    EmulatorContext* ctx = emulator->GetContext();
    ctx->pFeatureManager->setFeature(Features::kScreenHQ, beam);

    const MEM_MODEL model = ctx->config.mem_model;
    if (model == MM_ATM3)
        static_cast<PortDecoder_ATM3*>(ctx->pPortDecoder)->GetRtc().SetFixedTime(kFixedTime);
    if (model == MM_SCORP || model == MM_PROFSCORP)
        static_cast<PortDecoder_Scorpion256*>(ctx->pPortDecoder)->GetRtc().SetFixedTime(kFixedTime);
    if (model == MM_PROFI)
        static_cast<PortDecoder_Profi*>(ctx->pPortDecoder)->GetRtc().SetFixedTime(kFixedTime);

    std::memset(ctx->pMemory->RAMBase(), 0, static_cast<size_t>(ctx->config.ramsize) * 1024u);
    emulator->Reset();

    if (g.snapshot)
    {
        emulator->RunNFrames(50);  // past the ROM's own start-up
        EXPECT_TRUE(emulator->LoadSnapshot((TestPathHelper::FindProjectRoot() / g.snapshot).string())) << g.snapshot;
    }
    emulator->RunNFrames(kFrames);

    uint32_t* buffer = nullptr;
    size_t size = 0;
    ctx->pScreen->GetFramebufferData(&buffer, &size);
    const FramebufferDescriptor& fb = ctx->pScreen->GetFramebufferDescriptor();
    f.width = fb.width;
    f.height = fb.height;
    if (buffer && size)
        f.hash = Fnv1a(reinterpret_cast<const uint8_t*>(buffer), size);

    EmulatorTestHelper::CleanupEmulator(emulator);
    return f;
}
}  // namespace

TEST(ScreenZXFrames_Test, EveryClassicModelRendersExactlyAsRecorded)
{
    const bool record = std::getenv("UNREALNG_RECORD_FRAME_GOLDEN") != nullptr;
    for (const FrameGolden& g : kFrameGolden)
    {
        const std::string what = std::string(g.model) + (g.snapshot ? std::string(" + ") + g.snapshot : "");
        SCOPED_TRACE(what);
        const Frame beam = RunModel(g, true);
        const Frame batch = RunModel(g, false);
        ASSERT_TRUE(beam.created) << "model not creatable in this build";
        EXPECT_EQ(beam.width, batch.width);
        EXPECT_EQ(beam.height, batch.height);

        if (record)
        {
            printf("    {\"%s\", %s%s%s, %u, %u, 0x%016llXull, 0x%016llXull},\n", g.model, g.snapshot ? "\"" : "",
                   g.snapshot ? g.snapshot : "nullptr", g.snapshot ? "\"" : "", beam.width, beam.height,
                   static_cast<unsigned long long>(beam.hash), static_cast<unsigned long long>(batch.hash));
            continue;
        }
        EXPECT_EQ(beam.width, g.width);
        EXPECT_EQ(beam.height, g.height);
        EXPECT_EQ(beam.hash, g.hashBeam) << "the beam-rendered picture differs from the recorded one";
        EXPECT_EQ(batch.hash, g.hashBatch) << "the batch-rendered picture differs from the recorded one";
    }
}
