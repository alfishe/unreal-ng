// Pentagon 2.666 LE: a headless runner for real software (disabled; run with --gtest_also_run_disabled_tests).
//
// Environment:
//   P2666_PROBE_DISK    a TRD / SCL / FDI for drive A (the RAM disk takes it at the first DOS access)
//   P2666_PROBE_SDDIR   a host folder as the SD card (the media manager's synthesized FAT volume)
//   P2666_PROBE_SD      a raw SD image (whole card, loaded into memory: session writes only)
//   P2666_PROBE_SCRIPT  steps "frames:key[+key]" separated by spaces: run `frames` frames, then tap the keys for
//                       5 frames; `frames:` alone just runs. Keys: A..Z, 0..9, ENTER, SPACE, CS, SS, UP, DOWN, LEFT,
//                       RIGHT, BREAK
//   P2666_PROBE_OUT     a directory for a PNG after every step (step-NN.png)
//   P2666_PROBE_TURBO   1: the emulator's turbo (no pacing; the machine's own clock stays)
//   P2666_PROBE_MODEL   another model to run the same script on (a reference: PENTAGON, ...)
//   P2666_PROBE_CONFIG  a config folder for that model (zxevo-ers with ATM3: the ZX-Evo as a NedoOS reference)
//   P2666_PROBE_RAM     its RAM in KB (with P2666_PROBE_CONFIG; 0: the model's default)
//   P2666_PROBE_DUMP    1: after every step the window pages, registers, the code around PC and the stack
//   P2666_PROBE_STEPS   N: single-step N instructions after the script and print each
//   P2666_PROBE_FIND    text: after every step the RAM pages (0..127) that hold it
//   P2666_PROBE_BUS     a bus to trace (zc.spi): its events and findings at the end
//   P2666_PROBE_AUTOSTART 1: boot the disk through Emulator::AutostartDisk instead of only inserting it
//   P2666_PROBE_UNTILPC hex PC: single-step until it, then print the last 400 instructions
//   P2666_PROBE_WATCH   "page:offset" (hex): every change of that RAM byte with the PC and the windows
//   P2666_PROBE_WATCHSTOP hex value: stop the watch when the byte takes it
//   P2666_PROBE_SAVEPAGES comma-separated hex RAM pages to save into P2666_PROBE_OUT after the script

#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/busreport.h"
#include "_helpers/emulatortesthelper.h"
#include "common/image/imagehelper.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/media/mediamanager.h"
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
    const char* model = Env("P2666_PROBE_MODEL") ? Env("P2666_PROBE_MODEL") : "P2666";
    std::shared_ptr<Emulator> configured;  // P2666_PROBE_CONFIG: owned here until the end of the run
    Emulator* emulator = nullptr;
    if (const char* config = Env("P2666_PROBE_CONFIG"))
    {
        configured = EmulatorManager::GetInstance()->CreateEmulatorWithModelAndRAM(
            config, model, static_cast<uint32_t>(std::strtoul(Env("P2666_PROBE_RAM") ? Env("P2666_PROBE_RAM") : "0", nullptr, 10)),
            LoggerLevel::LogError);
        emulator = configured.get();
    }
    else
        emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoderP2666*>(context->pPortDecoder);
    if (Env("P2666_PROBE_TURBO"))
        context->pCore->EnableTurboMode();

    if (const char* disk = Env("P2666_PROBE_DISK"))
    {
        if (Env("P2666_PROBE_AUTOSTART"))
        {
            const Emulator::DiskAutostartResult started = emulator->AutostartDisk(disk, 0);
            ASSERT_TRUE(started.mounted) << started.message;
            std::printf("autostart: %s\n", started.message.c_str());
        }
        else
        {
            std::string error;
            ASSERT_TRUE(emulator->LoadDisk(disk, 0, &error)) << error;
        }
    }
    if (const char* folder = Env("P2666_PROBE_SDDIR"))
    {
        MediaSource source;
        source.path = folder;
        InsertOptions options;
        options.freeBytes = 16 * 1024 * 1024;
        const MediaResult inserted = context->pMediaManager->Insert("sd.zc", source, options);
        ASSERT_TRUE(inserted.Ok()) << inserted.message;
    }
    if (const char* sd = Env("P2666_PROBE_SD"))
    {
        ASSERT_NE(decoder, nullptr) << "the SD card is the P2666's";
        std::ifstream file(sd, std::ios::binary);
        ASSERT_TRUE(file.good()) << sd;
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto card = std::make_unique<MemoryDisk>(static_cast<uint32_t>(bytes.size() / 512));
        std::copy(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 512 * 512), card->Data());
        ASSERT_TRUE(decoder->GetSdCard().insert(std::move(card), SdCardSpi::WriteMode::Session));
    }

    const char* bus = Env("P2666_PROBE_BUS");
    if (bus)
        ASSERT_TRUE(bustest::Enable(context, bus)) << "no bus " << bus;

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
        std::printf("step %02d frame %llu pc %04X x%d dos %d 7FFD %02X EFF7 %02X xx77 %02X ramdisk pos %d intrq %d sys %02X\n",
                    index, static_cast<unsigned long long>(state.frame_counter), z80->pc,
                    state.current_z80_frequency_multiplier, (state.flags & CF_TRDOS) ? 1 : 0, state.p7FFD,
                    state.pEFF7, state.p2666.pxx77,
                    decoder && decoder->GetRamDisk() ? decoder->GetRamDisk()->Position() : -1,
                    decoder && decoder->GetRamDisk() ? decoder->GetRamDisk()->Intrq() : 0,
                    decoder && decoder->GetRamDisk() ? decoder->GetRamDisk()->System() : 0xFF);
        if (const char* needle = Env("P2666_PROBE_FIND"))
        {
            // RAM pages holding the text (a shell prompt, a marker)
            const std::string text(needle);
            std::printf("  find '%s':", needle);
            for (uint16_t page = 0; page < 128; page++)
            {
                const uint8_t* bytes = context->pMemory->RAMPageAddress(page);
                const uint8_t* hit = bytes ? std::search(bytes, bytes + 16384, text.begin(), text.end()) : nullptr;
                if (bytes && hit != bytes + 16384)
                    std::printf(" %02X:%04X", page, static_cast<unsigned>(hit - bytes));
            }
            std::printf("\n");
        }
        if (Env("P2666_PROBE_DUMP"))
        {
            // Where the CPU is: the window pages, the code around PC, the stack
            std::printf("  tt %u windows", z80->tt >> 8);
            for (int w = 0; w < 4 && decoder; w++)
                std::printf(" %02X", decoder->WindowPage(static_cast<uint8_t>(w)));
            std::printf("  bc %04X de %04X hl %04X af %04X ix %04X iy %04X sp %04X im %d iff %d\n  code",
                        z80->bc, z80->de, z80->hl, z80->af, z80->ix, z80->iy, z80->sp, z80->im, z80->iff1);
            for (int k = -16; k < 16; k++)
                std::printf(k == 0 ? " [%02X" : (k == 1 ? "] %02X" : " %02X"),
                            context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(z80->pc + k)));
            std::printf("\n  stack");
            for (int k = 0; k < 8; k++)
                std::printf(" %04X", context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(z80->sp + 2 * k)) |
                                         context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(z80->sp + 2 * k + 1)) << 8);
            std::printf("\n");
        }
        for (ZXKeysEnum key : keys)
            context->pKeyboard->PressKey(key);
        for (int f = 0; f < 5 && !keys.empty(); f++)
            emulator->RunFrame();
        for (ZXKeysEnum key : keys)
            context->pKeyboard->ReleaseKey(key);
        index++;
    }
    if (bus)
    {
        // The bus trace: every protocol event as the analyzer reports it, then the findings
        BusEventQuery q;
        std::string error;
        BusEventQueryFromStrings("", "", "", "", "", "0", q, error);
        const StateNode report = DeviceState::BusEvents(context, bus, q);
        if (const StateNode* list = report.find("events"))
            for (const StateNode& e : list->items)
            {
                std::printf("  bus");
                for (const auto& [key, value] : e.members)
                    std::printf(" %s=%s", key.c_str(),
                                value.s.empty() ? std::to_string(value.i).c_str() : value.s.c_str());
                std::printf("\n");
            }
        for (const std::string& finding : bustest::Findings(context, bus))
            std::printf("  finding %s\n", finding.c_str());
    }
    if (const char* until = Env("P2666_PROBE_UNTILPC"))
    {
        // Step until PC hits the address (hex), then print the last instructions with their window page and stack
        const uint16_t target = static_cast<uint16_t>(std::strtoul(until, nullptr, 16));
        const auto* stepDecoder = dynamic_cast<PortDecoderP2666*>(context->pPortDecoder);
        struct Trace
        {
            uint16_t pc, sp, af, bc, de, hl;
            uint8_t page;
        };
        std::vector<Trace> ring(400);
        size_t n = 0;
        // P2666_PROBE_WATCH "page:offset" (hex): print every change of that RAM byte with the PC and the windows
        uint8_t* watched = nullptr;
        if (const char* watch = Env("P2666_PROBE_WATCH"))
        {
            char* colon = nullptr;
            const unsigned page = static_cast<unsigned>(std::strtoul(watch, &colon, 16));
            const unsigned offset = static_cast<unsigned>(std::strtoul(colon + 1, nullptr, 16));
            watched = context->pMemory->RAMPageAddress(static_cast<uint16_t>(page)) + offset;
        }
        uint8_t watchedValue = watched ? *watched : 0;
        for (uint64_t k = 0; k < 200'000'000 && z80->pc != target; k++)
        {
            if (watched && *watched != watchedValue)
            {
                const Trace& t = ring[(n + ring.size() - 1) % ring.size()];
                std::printf("  watch %02X -> %02X by pc %04X page %02X windows %02X %02X %02X %02X 7FFD %02X frame %llu\n",
                            watchedValue, *watched, t.pc, t.page, stepDecoder->WindowPage(0), stepDecoder->WindowPage(1),
                            stepDecoder->WindowPage(2), stepDecoder->WindowPage(3), context->emulatorState.p7FFD,
                            static_cast<unsigned long long>(context->emulatorState.frame_counter));
                watchedValue = *watched;
                if (Env("P2666_PROBE_WATCHSTOP") && watchedValue == std::strtoul(Env("P2666_PROBE_WATCHSTOP"), nullptr, 16))
                    break;
            }
            ring[n++ % ring.size()] = {z80->pc, z80->sp, z80->af, z80->bc, z80->de, z80->hl,
                                       stepDecoder ? stepDecoder->WindowPage(z80->pc >> 14) : uint8_t{0}};
            emulator->RunSingleCPUCycle(true);
        }
        if (const char* pages = Env("P2666_PROBE_SAVEPAGES"); pages && out)
        {
            // Hex page numbers separated by commas: each saved as until-page-NN.bin
            std::istringstream list(pages);
            std::string item;
            while (std::getline(list, item, ','))
            {
                const unsigned page = static_cast<unsigned>(std::strtoul(item.c_str(), nullptr, 16));
                const std::string path = std::string(out) + "/until-page-" + item + ".bin";
                if (FILE* f = std::fopen(path.c_str(), "wb"))
                {
                    std::fwrite(context->pMemory->RAMPageAddress(static_cast<uint16_t>(page)), 1, 0x4000, f);
                    std::fclose(f);
                }
            }
        }
        std::printf("  until pc %04X frame %llu\n", z80->pc, static_cast<unsigned long long>(context->emulatorState.frame_counter));
        for (size_t k = (n > ring.size() ? n - ring.size() : 0); k < n; k++)
        {
            const Trace& t = ring[k % ring.size()];
            std::printf("  trace pc %04X page %02X sp %04X af %04X bc %04X de %04X hl %04X\n", t.pc, t.page, t.sp, t.af,
                        t.bc, t.de, t.hl);
        }
    }
    if (const char* steps = Env("P2666_PROBE_STEPS"))
    {
        // Single instructions after the script: PC, T and the frame each time
        for (int k = 0, n = std::atoi(steps); k < n; k++)
        {
            emulator->RunSingleCPUCycle(true);
            const auto* stepDecoder = dynamic_cast<PortDecoderP2666*>(context->pPortDecoder);
            std::printf("  single pc %04X tt %u frame %llu halted %d af %04X page %02X sp %04X\n", z80->pc, z80->tt >> 8,
                        static_cast<unsigned long long>(context->emulatorState.frame_counter), z80->halted, z80->af,
                        stepDecoder ? static_cast<unsigned>(stepDecoder->WindowPage(z80->pc >> 14)) : 0u, z80->sp);
        }
        for (int k = 0; k < 3; k++)
        {
            const unsigned ran = emulator->RunNCPUCycles(20000);
            std::printf("  ncycles ran %u pc %04X t %u frame %llu\n", ran, z80->pc, z80->t,
                        static_cast<unsigned long long>(context->emulatorState.frame_counter));
        }
        for (int k = 0; k < 3; k++)
        {
            emulator->RunFrame();
            std::printf("  frame pc %04X t %u complete %d frame %llu\n", z80->pc, z80->t, z80->IsFrameComplete() ? 1 : 0,
                        static_cast<unsigned long long>(context->emulatorState.frame_counter));
        }
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
