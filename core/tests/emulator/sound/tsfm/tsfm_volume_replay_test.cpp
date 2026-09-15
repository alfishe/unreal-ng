#include "stdafx.h"
#include "pch.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"

/// Hardware volume-balance correlation (TSFM design §7.1 / §14.2).
///
/// Replays the TFMD stream extracted from tsfm_volume_test.spg (a ZX-Evo
/// TS-Conf binary whose tune pulses the SSG channels at vol 15 against an FM
/// carrier at TL 0, both at ~1165 Hz, 72 frames each) through the device and
/// reports per-region RMS levels of the mixer taps, so the emulated SSG:FM
/// balance can be compared with the hardware recording shipped beside the .spg.
///
/// Player semantics (scratch/tvtest/program.asm): 0xFF ends a frame (and the
/// ISR then writes 0xF8 = chip-0 select), 0xFE nn pauses nn+3 frames, 0xFD/0xFC
/// write 0xF9/0xF8 (chip select), 0xFA/0xFB mark/jump the loop point. Tune:
/// cycles 1/3 play FM+SSG on board chip 0, cycles 2/4 on chip 1; within a
/// cycle the FM voice walks ch0/ch1/ch2 (24 frames each), then KEYON ch2 OFF
/// and the SSG A/B/C thirds (24 frames each) on the same chip.
///
/// Scratch analysis artefact: reads scratch/tvtest/tfmd_p10.bin.

namespace
{
constexpr uint32_t PENTAGON_FRAME = 71680;

struct RegionStat
{
    const char* name;
    int frameFrom;  // inclusive
    int frameTo;    // exclusive
    double fm0 = 0.;  // chip 0 FM (centre-panned) sum of squares
    double fm1 = 0.;
    double c0L = 0., c0R = 0.;  // chip 0 SSG sides
    double c1L = 0., c1R = 0.;
    int16_t fm0Peak = 0;
    int16_t c0LPeak = 0, c0RPeak = 0;
    size_t n = 0;
};
}  // namespace

class TsfmVolumeReplay_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";
        _context = _emulator->GetContext();
        _context->pCore->GetZ80()->tt = 0;
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    void SetT(uint64_t t)
    {
        _context->pCore->GetZ80()->tt = uint32_t(t) << 8;
    }
};

TEST_F(TsfmVolumeReplay_Test, ReplayTsfmVolumeTune)
{
    const auto streamPath = TestPathHelper::FindProjectRoot() / "scratch" / "tvtest" / "tfmd_p10.bin";
    std::ifstream stream(streamPath, std::ios::binary);
    if (!stream.good())
    {
        // scratch/ is gitignored: the stream is a local analysis artefact
        // (docs/inprogress/2026-09-10-turbosound-fm/materials/volume has the
        // .spg it comes from). Skip rather than fail on a clean checkout.
        GTEST_SKIP() << "TFMD stream not found: " << streamPath.string();
    }
    const std::vector<uint8_t> tfmd((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    ASSERT_GT(tfmd.size(), size_t(4)) << "TFMD stream truncated";

    auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);
    device->setCoreRate(44100);
    auto out = [&device](uint16_t port, uint8_t value)
    {
        device->portDeviceOutMethod(port, value);
    };

    // The program's init (program.asm 0x4003..0x40B4): zero every register of
    // both chips (so all TLs sit at 0 = loudest), then leave the 0xFE chip
    // with tones enabled, the 0xFF chip muted, FM unmuted via 0xF8
    SetT(100);
    out(PORT_FFFD, 0xFE);
    for (int reg = 0xBE; reg >= 0; --reg)
    {
        out(PORT_FFFD, uint8_t(reg));
        out(PORT_BFFD, 0x00);
    }
    out(PORT_FFFD, 0x07);
    out(PORT_BFFD, 0xF8);
    out(PORT_FFFD, 0x2F);
    out(PORT_BFFD, 0x00);
    out(PORT_FFFD, 0x2D);
    out(PORT_BFFD, 0x00);
    out(PORT_FFFD, 0xFF);
    for (int reg = 0xBE; reg >= 0; --reg)
    {
        out(PORT_FFFD, uint8_t(reg));
        out(PORT_BFFD, 0x00);
    }
    out(PORT_FFFD, 0x07);
    out(PORT_BFFD, 0xC0);
    out(PORT_FFFD, 0x2F);
    out(PORT_BFFD, 0x00);
    out(PORT_FFFD, 0x2D);
    out(PORT_BFFD, 0x00);
    out(PORT_FFFD, 0xF8);

    RegionStat regions[] = {
        {"FM f8..23   (ch0 third)", 8, 23},
        {"FM f26..47  (ch1 third)", 26, 47},
        {"FM f50..70  (ch2 third)", 50, 70},
        {"SSG A cyc1  f74..94 (chip0)", 74, 94},
        {"SSG B cyc1  f98..118(chip0)", 98, 118},
        {"SSG C cyc1  f122..142(chip0)", 122, 142},
        {"FM2 f152..167(ch0 third)", 152, 167},
        {"FM2 f170..191(ch1 third)", 170, 191},
        {"FM2 f194..214(ch2 third)", 194, 214},
        {"SSG A cyc2  f218..238(chip1)", 218, 238},
        {"SSG C cyc2  f266..286(chip1)", 266, 286},
    };

    const int totalFrames = 290;
    size_t pos = 4;  // the player skips the 4-byte TFMD header
    int frame = 0;
    while (frame < totalFrames && pos < tfmd.size())
    {
        SetT(100);
        int pauseFrames = 0;
        while (pos < tfmd.size())
        {
            const uint8_t token = tfmd[pos++];
            if (token == 0xFF)
                break;  // end of frame
            if (token == 0xFE)
            {
                pauseFrames = tfmd[pos++] + 3;
                break;
            }
            if (token == 0xFD || token == 0xFC)
            {
                out(PORT_FFFD, token == 0xFD ? 0xF9 : 0xF8);
                continue;
            }
            if (token == 0xFB || token == 0xFA)
            {
                pos = tfmd.size();  // loop point: replay covers two cycles
                break;
            }
            const uint8_t value = tfmd[pos++];
            out(PORT_FFFD, token);
            out(PORT_BFFD, value);
        }

        // The ISR's frame-end path (l4118): re-select board chip 0 with FM on
        SetT(90);
        out(PORT_FFFD, 0xF8);

        const int framesThisPass = 1 + pauseFrames;
        for (int pass = 0; pass < framesThisPass && frame < totalFrames; ++pass)
        {
            SetT(0);
            device->handleFrameStart();
            SetT(PENTAGON_FRAME);
            device->handleStep();
            device->handleFrameEnd();

            const size_t samples = device->getRenderedSamplesThisFrame();
            const int16_t* fm0 = device->getFmBuffer(0);
            const int16_t* fm1 = device->getFmBuffer(1);
            const int16_t* ssg0 = device->getChipBuffer(0);
            const int16_t* ssg1 = device->getChipBuffer(1);
            for (RegionStat& region : regions)
            {
                if (frame < region.frameFrom || frame >= region.frameTo)
                    continue;
                for (size_t s = 0; s < samples; ++s)
                {
                    const int16_t f0 = fm0[2 * s];
                    const int16_t f1 = fm1[2 * s];
                    const int16_t c0L = ssg0[2 * s];
                    const int16_t c0R = ssg0[2 * s + 1];
                    const int16_t c1L = ssg1[2 * s];
                    const int16_t c1R = ssg1[2 * s + 1];
                    region.fm0 += double(f0) * f0;
                    region.fm1 += double(f1) * f1;
                    region.c0L += double(c0L) * c0L;
                    region.c0R += double(c0R) * c0R;
                    region.c1L += double(c1L) * c1L;
                    region.c1R += double(c1R) * c1R;
                    const int16_t f0a = f0 < 0 ? -f0 : f0;
                    const int16_t c0La = c0L < 0 ? -c0L : c0L;
                    const int16_t c0Ra = c0R < 0 ? -c0R : c0R;
                    if (f0a > region.fm0Peak) region.fm0Peak = f0a;
                    if (c0La > region.c0LPeak) region.c0LPeak = c0La;
                    if (c0Ra > region.c0RPeak) region.c0RPeak = c0Ra;
                }
                region.n += samples;
            }
            ++frame;
        }
    }

    auto rms = [](const RegionStat& r, double energy)
    {
        return r.n ? std::sqrt(energy / double(r.n)) : 0.;
    };
    auto db = [](double ratio)
    {
        return ratio > 0. ? 20.0 * std::log10(ratio) : -99.0;
    };

    printf("\n%-28s %6s %6s %6s | %6s %6s %6s %6s\n",
           "region", "fm0pk", "c0Lpk", "c0Rpk", "fm0", "c0L", "c0R", "c1L");
    for (const RegionStat& region : regions)
    {
        printf("%-28s %6d %6d %6d | %6.1f %6.1f %6.1f %6.1f\n",
               region.name, region.fm0Peak, region.c0LPeak, region.c0RPeak,
               rms(region, region.fm0), rms(region, region.c0L), rms(region, region.c0R), rms(region, region.c1L));
    }

    // Same-side SSG/FM ratios, comparable with the hardware recording
    // (SSG A routes left, C routes right, FM is centre-panned)
    const double fm1rms = rms(regions[2], regions[2].fm0);
    const double fm2rms = rms(regions[8], regions[8].fm1);
    printf("\nSSG/FM RMS dB, emulator vs hardware recording:\n");
    printf("  A/FM cyc1  %+6.2f   (hw -1.2)\n", db(rms(regions[3], regions[3].c0L) / fm1rms));
    printf("  B/FM cyc1  %+6.2f   (hw -6.9; B sits -5.7 below A = half weight)\n", db(rms(regions[4], regions[4].c0L) / fm1rms));
    printf("  C/FM cyc1  %+6.2f   (hw -2.3)\n", db(rms(regions[5], regions[5].c0R) / fm1rms));
    printf("  A/FM cyc2  %+6.2f   (hw -0.2, chip 1)\n", db(rms(regions[9], regions[9].c1L) / fm2rms));
    printf("  C/FM cyc2  %+6.2f   (hw -1.4, chip 1)\n", db(rms(regions[10], regions[10].c1R) / fm2rms));

    SUCCEED();
}
