#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"

/// TSFM golden digests (ZX-MultiSound MS-1, docs/inprogress/2026-10-03-zx-multisound/architecture.md §4.1).
///
/// The Ym2203Pair extraction must leave the TSFM bit-identical. These tests drive one fixed, deterministic script
/// of port traffic through a standalone SoundChip_TurboSoundFM - FM notes on both chips, SSG tones / noise /
/// envelopes, control words switching chips, read mode and FM mute, timers with status reads, a prescaler
/// excursion, synthesis suppression and a core-synthesis skip - and hash everything the device produces: the five
/// output buffers, the native taps (SSG and both FM), every byte the CPU reads, and the TTD blob at fixed points.
/// The expected digests were captured on the pre-extraction binary (master at dd93445d8) and must never change
/// unless the TSFM's behavior is meant to change.

namespace
{

constexpr uint32_t kPentagonFrame = 71680;
constexpr int kFrames = 16;

/// FNV-1a, 64 bit
class Digest
{
public:
    void Bytes(const void* data, size_t size)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; i++)
        {
            _h ^= p[i];
            _h *= 0x100000001b3ull;
        }
    }

    template <typename T>
    void Value(T v)
    {
        Bytes(&v, sizeof v);
    }

    uint64_t Get() const
    {
        return _h;
    }

private:
    uint64_t _h = 0xcbf29ce484222325ull;
};

/// One port access of the script
struct Access
{
    int frame;
    uint32_t t;
    uint16_t port;
    bool read;
    uint8_t value;
};

/// Deterministic generator (LCG) of a busy TSFM session.
/// Every Next() sits in its own statement: two calls in one expression or argument list are evaluated in an
/// unspecified order (clang left to right, x86-64 gcc right to left for arguments), which made the script - and
/// so every digest - differ between compilers
class Script
{
public:
    explicit Script(uint32_t seed) : _lcg(seed * 2654435761u + 7)
    {
        Build();
    }

    const std::vector<Access>& Accesses() const
    {
        return _accesses;
    }

private:
    uint32_t Next(uint32_t bound)
    {
        _lcg = _lcg * 1664525u + 1013904223u;
        return (_lcg >> 8) % bound;
    }

    void Out(int frame, uint32_t& t, uint16_t port, uint8_t value)
    {
        _accesses.push_back({frame, t, port, false, value});
        t += 24 + Next(120);
    }

    void In(int frame, uint32_t& t, uint16_t port)
    {
        _accesses.push_back({frame, t, port, true, 0});
        t += 12 + Next(40);
    }

    void Reg(int frame, uint32_t& t, uint8_t reg, uint8_t value)
    {
        Out(frame, t, PORT_FFFD, reg);
        Out(frame, t, PORT_BFFD, value);
    }

    /// A full FM voice on channel ch (0..2) of the selected chip
    void Voice(int frame, uint32_t& t, uint8_t ch, uint8_t fnumLow, uint8_t block)
    {
        const uint32_t algorithm = Next(8);
        const uint32_t feedback = Next(8);
        Reg(frame, t, uint8_t(0xB0 + ch), uint8_t(algorithm | (feedback << 3)));
        for (uint8_t op = 0; op < 4; op++)
        {
            const uint8_t base = uint8_t(ch + op * 4);
            Reg(frame, t, uint8_t(0x30 + base), uint8_t(Next(128)));            // DT, MUL
            Reg(frame, t, uint8_t(0x40 + base), uint8_t(op == 3 ? Next(24) : Next(80)));  // TL
            Reg(frame, t, uint8_t(0x50 + base), uint8_t(0x10 | Next(256)));     // KS, AR
            Reg(frame, t, uint8_t(0x60 + base), uint8_t(Next(32)));             // DR
            Reg(frame, t, uint8_t(0x70 + base), uint8_t(Next(32)));             // SR
            Reg(frame, t, uint8_t(0x80 + base), uint8_t(Next(256)));            // SL, RR
        }
        Reg(frame, t, uint8_t(0xA4 + ch), uint8_t((block << 3) | Next(8)));
        Reg(frame, t, uint8_t(0xA0 + ch), fnumLow);
        Reg(frame, t, 0x28, uint8_t(0xF0 | ch));                                // key on
    }

    void Build()
    {
        for (int frame = 0; frame < kFrames; frame++)
        {
            uint32_t t = 40 + Next(200);

            if (frame == 0)
            {
                // Both chips: FM on, status read; voices on all channels; SSG tones and an envelope
                for (uint8_t chip = 0; chip < 2; chip++)
                {
                    Out(frame, t, PORT_FFFD, uint8_t(0xF8 | chip));
                    for (uint8_t ch = 0; ch < 3; ch++)
                    {
                        const uint32_t fnumLow = 0x40 + Next(0xA0);
                        const uint32_t block = 3 + Next(4);
                        Voice(frame, t, ch, uint8_t(fnumLow), uint8_t(block));
                    }
                    for (uint8_t r = 0; r < 6; r++)
                        Reg(frame, t, r, uint8_t(Next(r & 1 ? 4 : 256)));
                    Reg(frame, t, 6, uint8_t(Next(32)));
                    Reg(frame, t, 7, uint8_t(0x38 & Next(256)));
                    Reg(frame, t, 8, 0x0F);
                    Reg(frame, t, 9, 0x10);
                    Reg(frame, t, 10, uint8_t(Next(16)));
                    Reg(frame, t, 11, uint8_t(Next(256)));
                    Reg(frame, t, 12, uint8_t(1 + Next(4)));
                    Reg(frame, t, 13, uint8_t(8 + Next(8)));
                    // Timers A and B loaded and running, flags enabled
                    Reg(frame, t, 0x24, uint8_t(Next(256)));
                    Reg(frame, t, 0x25, uint8_t(Next(4)));
                    Reg(frame, t, 0x26, uint8_t(0xC0 + Next(64)));
                    Reg(frame, t, 0x27, 0x0F);
                }
            }

            // Per-frame traffic: status / register reads, control words, FM and SSG writes
            const int accesses = 60 + int(Next(40));
            for (int i = 0; i < accesses && t < kPentagonFrame - 2000; i++)
            {
                const uint32_t kind = Next(16);
                if (kind == 0)
                    Out(frame, t, PORT_FFFD, uint8_t(0xF8 | Next(8)));  // control word: chip, read mode, mute
                else if (kind < 3)
                    In(frame, t, PORT_FFFD);
                else if (kind == 3)
                    In(frame, t, PORT_BFFD);
                else if (kind < 8)
                {
                    const uint32_t reg = Next(16);  // SSG
                    const uint32_t value = Next(256);
                    Reg(frame, t, uint8_t(reg), uint8_t(value));
                }
                else if (kind < 11)
                {
                    // FM operator / channel registers
                    const uint8_t ch = uint8_t(Next(3));
                    const uint8_t op = uint8_t(Next(4));
                    const uint8_t bank = uint8_t(0x30 + 0x10 * Next(6));
                    Reg(frame, t, uint8_t(bank + ch + op * 4), uint8_t(Next(256)));
                }
                else if (kind < 13)
                {
                    const uint8_t ch = uint8_t(Next(3));
                    Reg(frame, t, uint8_t(0xA4 + ch), uint8_t(Next(64)));
                    Reg(frame, t, uint8_t(0xA0 + ch), uint8_t(Next(256)));
                }
                else if (kind == 13)
                {
                    const uint32_t slots = Next(16);  // key on / off
                    const uint32_t channel = Next(3);
                    Reg(frame, t, 0x28, uint8_t((slots << 4) | channel));
                }
                else if (kind == 14)
                {
                    const uint32_t control = Next(256) & 0x3F;  // timers, CSM
                    const uint32_t csm = Next(4) == 0 ? 0x80 : 0;
                    Reg(frame, t, 0x27, uint8_t(control | csm));
                }
                else
                {
                    // Address-only write, then a register read through the new address
                    Out(frame, t, PORT_FFFD, uint8_t(Next(256) & 0xF7));
                    In(frame, t, PORT_FFFD);
                }
            }

            // A prescaler excursion inside one frame (/3 then back to /6) on chip 1
            if (frame == 5)
            {
                Out(frame, t, PORT_FFFD, 0xF9);
                Out(frame, t, PORT_FFFD, 0x2E);
                Out(frame, t, PORT_FFFD, 0x2D);
            }
        }
    }

    uint32_t _lcg;
    std::vector<Access> _accesses;
};

}  // namespace

class TsfmGolden_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    // Coverage of the script (not part of the digest): it must reach FM output, SSG output and busy / timer flags
    size_t _fmSamples = 0;
    size_t _ssgSamples = 0;
    size_t _statusFlags = 0;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
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

    void SetT(uint32_t t)
    {
        _context->pCore->GetZ80()->tt = t << 8;
    }

    static void HashTap(Digest& d, const std::shared_ptr<NativeAudioTap>& tap)
    {
        std::vector<float> data(tap->available() * 2);
        const size_t frames = tap->pop(data.data(), data.size() / 2);
        d.Value(uint64_t(frames));
        d.Bytes(data.data(), frames * 2 * sizeof(float));
    }

    static void HashBlob(Digest& d, const SoundChip_TurboSoundFM& device)
    {
        std::vector<uint8_t> blob(device.TTDStateSize());
        device.TTDSaveState(blob.data());
        d.Value(uint64_t(blob.size()));
        d.Bytes(blob.data(), blob.size());
    }

    /// Run frames [first, last) of the script; returns the digest of everything produced. With saveAtFrame >= 0,
    /// the TTD blob is taken mid-frame (after half of that frame's accesses) into `saved` with its T-state
    void Run(SoundChip_TurboSoundFM& device, const Script& script, int first, int last, Digest& d, int saveAtFrame = -1,
             std::vector<uint8_t>* saved = nullptr, uint32_t* savedT = nullptr, size_t* savedIndex = nullptr,
             size_t startIndex = 0, bool resumeMidFrame = false)
    {
        const std::vector<Access>& acc = script.Accesses();
        size_t index = startIndex;
        if (!resumeMidFrame)
            while (index < acc.size() && acc[index].frame < first)
                index++;

        for (int frame = first; frame < last; frame++)
        {
            if (!(resumeMidFrame && frame == first))
            {
                SetT(0);
                device.handleFrameStart();
            }

            // Synthesis suppressed for frames 9-10, the core synthesis skipped (muted core) for frame 12
            device.setSynthesisSuppressed(frame == 9 || frame == 10);
            device.setCoreSynthesisSkipped(frame == 12);

            size_t frameAccesses = 0;
            for (size_t k = index; k < acc.size() && acc[k].frame == frame; k++)
                frameAccesses++;
            size_t done = 0;

            while (index < acc.size() && acc[index].frame == frame)
            {
                if (frame == saveAtFrame && done == frameAccesses / 2 && saved)
                {
                    SetT(acc[index].t);
                    device.handleStep();
                    saved->resize(device.TTDStateSize());
                    device.TTDSaveState(saved->data());
                    *savedT = acc[index].t;
                    *savedIndex = index;
                }
                const Access& a = acc[index++];
                done++;
                SetT(a.t);
                if (a.read)
                {
                    const uint8_t v = device.portDeviceInMethod(a.port);
                    d.Value(v);
                    if (v != 0 && v != 0xFF && (v & 0x7C) == 0)
                        _statusFlags++;  // busy or a timer flag, nothing else (a status byte)
                }
                else
                    device.portDeviceOutMethod(a.port, a.value);
                // Render as the machine does: several times per frame
                if ((index & 15) == 0)
                    device.handleStep();
            }

            SetT(kPentagonFrame);
            device.handleStep();
            device.handleFrameEnd();

            const size_t n = device.getRenderedSamplesThisFrame() * AUDIO_CHANNELS;
            d.Value(uint64_t(n));
            d.Bytes(device.getAudioBuffer(), n * sizeof(int16_t));
            for (int c = 0; c < 2; c++)
            {
                d.Bytes(device.getChipBuffer(c), n * sizeof(int16_t));
                d.Bytes(device.getFmBuffer(c), n * sizeof(int16_t));
                for (size_t k = 0; k < n; k++)
                {
                    _fmSamples += device.getFmBuffer(c)[k] != 0;
                    _ssgSamples += device.getChipBuffer(c)[k] != 0;
                }
            }
            HashTap(d, device.getNativeTap());
            HashTap(d, device.getFmNativeTap(0));
            HashTap(d, device.getFmNativeTap(1));
            if (frame % 4 == 3)
                HashBlob(d, device);
        }
    }

    uint64_t RunSession(uint32_t seed, bool hq, size_t rate)
    {
        const Script script(seed);
        auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);
        device->setCoreRate(rate);
        device->setHQEnabled(hq);
        device->getNativeTap()->activate();
        device->getFmNativeTap(0)->activate();
        device->getFmNativeTap(1)->activate();
        SetT(0);
        device->reset();

        Digest d;
        Run(*device, script, 0, kFrames, d);
        EXPECT_GT(_fmSamples, 1000u) << "the script never reached the FM output";
        EXPECT_GT(_ssgSamples, 1000u) << "the script never reached the SSG output";
        EXPECT_GT(_statusFlags, 0u) << "no status read saw busy or a timer flag";
        return d.Get();
    }

    static std::string Hex(uint64_t v)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "0x%016llxull", static_cast<unsigned long long>(v));
        return buf;
    }
};

// Each session renders 16 Pentagon frames through the full TSFM output stage (~10-20 ms in Release): a digest
// over a script that exercises every path once cannot be shorter

TEST_F(TsfmGolden_Test, HighQuality44100)
{
    const uint64_t digest = RunSession(1, true, 44100);
    EXPECT_EQ(digest, 0xab329741c19b0464ull) << Hex(digest);
}

TEST_F(TsfmGolden_Test, LowQuality44100)
{
    const uint64_t digest = RunSession(2, false, 44100);
    EXPECT_EQ(digest, 0xd06f2dfbe97dcbd7ull) << Hex(digest);
}

TEST_F(TsfmGolden_Test, HighQuality48000)
{
    const uint64_t digest = RunSession(3, true, 48000);
    EXPECT_EQ(digest, 0xa52b38e6c30c54a7ull) << Hex(digest);
}

TEST_F(TsfmGolden_Test, TtdBlobAndRestore)
{
    // A mid-frame checkpoint in frame 6, restored into a fresh device that continues the same script: the blob
    // bytes and everything both devices produce afterwards are part of the digest
    const Script script(4);
    auto a = std::make_unique<SoundChip_TurboSoundFM>(_context);
    SetT(0);
    a->reset();

    Digest da;
    std::vector<uint8_t> blob;
    uint32_t savedT = 0;
    size_t savedIndex = 0;
    Run(*a, script, 0, 8, da, 6, &blob, &savedT, &savedIndex);
    ASSERT_FALSE(blob.empty());
    da.Bytes(blob.data(), blob.size());

    auto b = std::make_unique<SoundChip_TurboSoundFM>(_context);
    SetT(savedT);
    b->TTDLoadState(blob.data());
    Digest db;
    Run(*b, script, 6, 10, db, -1, nullptr, nullptr, nullptr, savedIndex, true);

    std::vector<uint8_t> again(b->TTDStateSize());
    b->TTDSaveState(again.data());
    db.Bytes(again.data(), again.size());

    EXPECT_EQ(da.Get(), 0x5b0bcb56d816ec20ull) << Hex(da.Get());
    EXPECT_EQ(db.Get(), 0xbdac82cac320e190ull) << Hex(db.Get());
}
