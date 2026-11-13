// Pentagon 2.666 LE: a headless runner for real software (disabled; run with --gtest_also_run_disabled_tests).
//
// Environment:
//   P2666_PROBE_DISK    a TRD / SCL / FDI for drive A (the RAM disk takes it at the first DOS access)
//   P2666_PROBE_SD      a raw SD image (whole card, loaded into memory: session writes only)
//   P2666_PROBE_SCRIPT  steps "frames:key[+key]" separated by spaces: run `frames` frames, then tap the keys for
//                       5 frames; `frames:` alone just runs. Keys: A..Z, 0..9, ENTER, SPACE, CS, SS, UP, DOWN, LEFT,
//                       RIGHT, BREAK
//   P2666_PROBE_OUT     a directory for a PNG after every step (step-NN.png)
//   P2666_PROBE_TURBO   1: the emulator's turbo (no pacing; the machine's own clock stays)

#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "common/image/imagehelper.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_p2666.h"
#include "pch.h"
#include "stdafx.h"

namespace
{
const char* Env(const char* name)
{
    const char* value = std::getenv(name);
    return value && *value ? value : nullptr;
}

bool KeyByName(const std::string& name, ZXKeysEnum& key)
{
    static const std::pair<const char*, ZXKeysEnum> kNames[] = {
        {"ENTER", ZXKEY_ENTER}, {"SPACE", ZXKEY_SPACE}, {"CS", ZXKEY_CAPS_SHIFT}, {"SS", ZXKEY_SYM_SHIFT},
        {"UP", ZXKEY_EXT_UP},   {"DOWN", ZXKEY_EXT_DOWN}, {"LEFT", ZXKEY_EXT_LEFT}, {"RIGHT", ZXKEY_EXT_RIGHT},
        {"BREAK", ZXKEY_EXT_BREAK},
        {"A", ZXKEY_A}, {"B", ZXKEY_B}, {"C", ZXKEY_C}, {"D", ZXKEY_D}, {"E", ZXKEY_E}, {"F", ZXKEY_F},
        {"G", ZXKEY_G}, {"H", ZXKEY_H}, {"I", ZXKEY_I}, {"J", ZXKEY_J}, {"K", ZXKEY_K}, {"L", ZXKEY_L},
        {"M", ZXKEY_M}, {"N", ZXKEY_N}, {"O", ZXKEY_O}, {"P", ZXKEY_P}, {"Q", ZXKEY_Q}, {"R", ZXKEY_R},
        {"S", ZXKEY_S}, {"T", ZXKEY_T}, {"U", ZXKEY_U}, {"V", ZXKEY_V}, {"W", ZXKEY_W}, {"X", ZXKEY_X},
        {"Y", ZXKEY_Y}, {"Z", ZXKEY_Z},
        {"0", ZXKEY_0}, {"1", ZXKEY_1}, {"2", ZXKEY_2}, {"3", ZXKEY_3}, {"4", ZXKEY_4},
        {"5", ZXKEY_5}, {"6", ZXKEY_6}, {"7", ZXKEY_7}, {"8", ZXKEY_8}, {"9", ZXKEY_9},
    };
    for (const auto& [text, value] : kNames)
    {
        if (name == text)
        {
            key = value;
            return true;
        }
    }
    return false;
}
}  // namespace

TEST(P2666SoftwareProbe_Test, DISABLED_Run)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("P2666", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoderP2666*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    if (Env("P2666_PROBE_TURBO"))
        context->pCore->EnableTurboMode();

    if (const char* disk = Env("P2666_PROBE_DISK"))
    {
        std::string error;
        ASSERT_TRUE(emulator->LoadDisk(disk, 0, &error)) << error;
    }
    if (const char* sd = Env("P2666_PROBE_SD"))
    {
        std::ifstream file(sd, std::ios::binary);
        ASSERT_TRUE(file.good()) << sd;
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto card = std::make_unique<MemoryDisk>(static_cast<uint32_t>(bytes.size() / 512));
        std::copy(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 512 * 512), card->Data());
        ASSERT_TRUE(decoder->GetSdCard().insert(std::move(card), SdCardSpi::WriteMode::Session));
    }

    const char* out = Env("P2666_PROBE_OUT");
    std::istringstream script(Env("P2666_PROBE_SCRIPT") ? Env("P2666_PROBE_SCRIPT") : "200:");
    std::string step;
    int index = 0;
    Z80* z80 = context->pCore->GetZ80();
    while (script >> step)
    {
        const size_t colon = step.find(':');
        const int frames = std::atoi(step.substr(0, colon).c_str());
        for (int f = 0; f < frames; f++)
            emulator->RunFrame();
        std::vector<ZXKeysEnum> keys;
        if (colon != std::string::npos)
        {
            std::istringstream names(step.substr(colon + 1));
            std::string name;
            while (std::getline(names, name, '+'))
            {
                ZXKeysEnum key;
                if (!name.empty() && KeyByName(name, key))
                    keys.push_back(key);
                else if (!name.empty())
                    ADD_FAILURE() << "unknown key " << name;
            }
        }
        if (out)
        {
            FramebufferDescriptor fb = emulator->GetFramebuffer();
            char path[1024];
            std::snprintf(path, sizeof path, "%s/step-%02d.png", out, index);
            ImageHelper::SavePNG(path, fb.memoryBuffer, fb.memoryBufferSize, fb.width, fb.height);
            // RAM pages 5 and 7 as they are (the screens)
            for (const int page : {5, 7})
            {
                std::snprintf(path, sizeof path, "%s/step-%02d-page%d.bin", out, index, page);
                if (FILE* f = std::fopen(path, "wb"))
                {
                    std::fwrite(context->pMemory->RAMPageAddress(static_cast<uint16_t>(page)), 1, 16384, f);
                    std::fclose(f);
                }
            }
        }
        const EmulatorState& state = context->emulatorState;
        std::printf("step %02d frame %llu pc %04X x%d dos %d 7FFD %02X EFF7 %02X xx77 %02X ramdisk pos %d intrq %d\n",
                    index, static_cast<unsigned long long>(state.frame_counter), z80->pc,
                    state.current_z80_frequency_multiplier, (state.flags & CF_TRDOS) ? 1 : 0, state.p7FFD,
                    state.pEFF7, state.p2666.pxx77, decoder->GetRamDisk() ? decoder->GetRamDisk()->Position() : -1,
                    decoder->GetRamDisk() ? decoder->GetRamDisk()->Intrq() : 0);
        for (ZXKeysEnum key : keys)
            context->pKeyboard->PressKey(key);
        for (int f = 0; f < 5 && !keys.empty(); f++)
            emulator->RunFrame();
        for (ZXKeysEnum key : keys)
            context->pKeyboard->ReleaseKey(key);
        index++;
    }
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// Step the CPU from power-on until it fetches from the TR-DOS page's empty gap (#3735..#3C2F with the DOS ROM in),
/// then print the last instructions: where the boot leaves the ROM's code
TEST(P2666SoftwareProbe_Test, DISABLED_TraceToRomGap)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("P2666", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoderP2666*>(context->pPortDecoder);
    Z80* z80 = context->pCore->GetZ80();
    struct Entry
    {
        uint16_t pc, sp, af, hl, de, bc;
        uint8_t dos, p7ffd, w0, xx77, bf;
    };
    std::vector<Entry> ring(256);
    size_t n = 0;
    bool hit = false;
    for (uint64_t i = 0; i < 40'000'000 && !hit; i++)
    {
        const EmulatorState& s = context->emulatorState;
        ring[n++ % ring.size()] = {z80->pc, z80->sp, z80->af, z80->hl, z80->de, z80->bc,
                                   static_cast<uint8_t>((s.flags & CF_TRDOS) ? 1 : 0), s.p7FFD,
                                   static_cast<uint8_t>(decoder->WindowPage(0)), s.p2666.pxx77,
                                   static_cast<uint8_t>(s.p2666.cpm | (s.p2666.bf0 << 1) | (s.p2666.dos1Off << 2))};
        {
            // Every change of the paging state, with the PC that made it
            static uint32_t prev = 0xFFFFFFFFu;
            const uint32_t now = (static_cast<uint32_t>(s.p7FFD) << 24) | (static_cast<uint32_t>(s.p2666.pxx77) << 16) |
                                 (static_cast<uint32_t>(decoder->WindowPage(0)) << 8) |
                                 static_cast<uint32_t>(((s.flags & CF_TRDOS) ? 8 : 0) | s.p2666.cpm |
                                                       (s.p2666.bf0 << 1) | (s.p2666.dos1Off << 2));
            if (now != prev)
            {
                const Entry& last = ring[(n + ring.size() - 2) % ring.size()];
                std::printf("step %llu pc %04X (prev pc %04X): 7FFD %02X xx77 %02X w0 %02X dos %d cpm|bf|d1 %d\n",
                            static_cast<unsigned long long>(i), z80->pc, last.pc, s.p7FFD, s.p2666.pxx77,
                            decoder->WindowPage(0), (s.flags & CF_TRDOS) ? 1 : 0, s.p2666.cpm | (s.p2666.bf0 << 1) | (s.p2666.dos1Off << 2));
                prev = now;
            }
        }
        hit = z80->pc >= 0x3735 && z80->pc < 0x3C30 && decoder->WindowPage(0) == PortDecoderP2666::kRomFirstPage + 1;
        if (!hit)
            emulator->RunSingleCPUCycle(true);
    }
    ASSERT_TRUE(hit) << "never reached the gap";
    std::printf("hit after %zu steps, frame %llu\n", n, static_cast<unsigned long long>(context->emulatorState.frame_counter));
    for (size_t k = (n > ring.size() ? n - ring.size() : 0); k < n; k++)
    {
        const Entry& e = ring[k % ring.size()];
        std::printf("pc %04X sp %04X af %04X hl %04X de %04X bc %04X dos %d 7FFD %02X w0 %02X xx77 %02X cpm|bf|d1 %d\n", e.pc,
                    e.sp, e.af, e.hl, e.de, e.bc, e.dos, e.p7ffd, e.w0, e.xx77, e.bf);
    }
    EmulatorTestHelper::CleanupEmulator(emulator);
}
