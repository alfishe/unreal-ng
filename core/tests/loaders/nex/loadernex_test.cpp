// The NEX loader (core/src/loaders/nex/loadernex.h) on synthetic files, and a bring-up run of any NEX file:
// UNREAL_NEX=<file> [UNREAL_NEX_FRAMES=200] [UNREAL_NEX_OUT=<folder>] runs it on the NEXT machine and writes the frame
// (frame.rgba: width, height, RGBA) there for scratch conversion and comparison with a reference emulator.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/video/screen.h"
#include "loaders/nex/loadernex.h"

namespace
{
std::vector<uint8_t> MakeNex(const char* version, uint8_t screenFlags, size_t screenBytes, const std::vector<std::pair<unsigned, std::vector<uint8_t>>>& banks,
                             uint16_t pc, uint16_t sp, uint8_t border, uint8_t entryBank)
{
    std::vector<uint8_t> image(512, 0);
    std::memcpy(image.data(), "Next", 4);
    std::memcpy(image.data() + 4, version, 4);
    image[9] = static_cast<uint8_t>(banks.size());
    image[10] = screenFlags;
    image[11] = border;
    image[12] = sp & 0xFF;
    image[13] = sp >> 8;
    image[14] = pc & 0xFF;
    image[15] = pc >> 8;
    image[139] = entryBank;
    for (const auto& b : banks)
        image[18 + b.first] = 1;
    image.resize(image.size() + screenBytes, 0xEE);
    // the file's bank order: 5, 2, 0, 1, 3, 4, 6, ...
    std::vector<unsigned> order = {5, 2, 0, 1, 3, 4};
    for (unsigned b = 6; b < 112; b++)
        order.push_back(b);
    for (unsigned bank : order)
        for (const auto& b : banks)
            if (b.first == bank)
            {
                std::vector<uint8_t> data = b.second;
                data.resize(0x4000, 0);
                image.insert(image.end(), data.begin(), data.end());
            }
    return image;
}
}  // namespace

class LoaderNex_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
};

TEST_F(LoaderNex_Test, BanksGoToTheirRamPagesAndTheEntryBankToSlot3)
{
    // bank 0 holds the program at #C000 (the entry bank); bank 5 a marker at #4000; bank 9 a marker
    std::vector<uint8_t> program = {0x3E, 0x55,        // LD A,#55
                                    0x32, 0x00, 0x80,  // LD (#8000),A
                                    0x18, 0xFE};       // JR $
    std::vector<uint8_t> bank5(1, 0x77);
    std::vector<uint8_t> bank9(1, 0x99);
    const auto image = MakeNex("V1.2", 0, 0, {{0, program}, {5, bank5}, {9, bank9}}, 0xC000, 0xBFF0, 3, 0);
    LoaderNex loader(_context);
    ASSERT_TRUE(loader.Load(image)) << loader.Error();
    auto* memory = dynamic_cast<NextMemory*>(_context->pMemory);
    EXPECT_EQ(memory->RAMPageAddress(5)[0], 0x77);
    EXPECT_EQ(memory->RAMPageAddress(9)[0], 0x99);
    EXPECT_EQ(memory->PeekSlot(0x4000), 0x77) << "bank 5 is at #4000";
    EXPECT_EQ(memory->PeekSlot(0xC000), 0x3E) << "the entry bank is at #C000";
    EXPECT_EQ(_z80->pc, 0xC000);
    EXPECT_EQ(_z80->sp, 0xBFF0);
    EXPECT_EQ(_context->emulatorState.pFE & 7, 3);
    for (int i = 0; i < 4; i++)
        _z80->EngineStep();
    EXPECT_EQ(memory->PeekSlot(0x8000), 0x55);
}

TEST_F(LoaderNex_Test, LoadingScreenBlocksAreSkippedByTheirSizes)
{
    std::vector<uint8_t> program = {0x3E, 0x66, 0x32, 0x00, 0x80, 0x18, 0xFE};
    // V1.1 with a Layer 2 screen: a palette block (512) and the screen (49152); the bank follows
    {
        const auto image = MakeNex("V1.1", 1, 512 + 49152, {{0, program}}, 0xC000, 0xBFF0, 0, 0);
        LoaderNex loader(_context);
        ASSERT_TRUE(loader.Load(image)) << loader.Error();
        for (int i = 0; i < 4; i++)
            _z80->EngineStep();
        EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x8000), 0x66);
    }
    // a ULA screen (6912) carries no palette; with the no-palette flag Layer 2 has none either
    for (auto [flags, bytes] : {std::pair<uint8_t, size_t>{2, 6912}, std::pair<uint8_t, size_t>{129, 49152}})
    {
        const auto image = MakeNex("V1.2", flags, bytes, {{0, program}}, 0xC000, 0xBFF0, 0, 0);
        LoaderNex loader(_context);
        ASSERT_TRUE(loader.Load(image)) << loader.Error() << " flags " << int(flags);
        EXPECT_EQ(dynamic_cast<NextMemory*>(_context->pMemory)->PeekSlot(0xC000), 0x3E) << "flags " << int(flags);
    }
}

TEST_F(LoaderNex_Test, RefusesWhatIsNotNex)
{
    LoaderNex loader(_context);
    EXPECT_FALSE(loader.Load(std::vector<uint8_t>(600, 0)));
    EXPECT_FALSE(loader.Error().empty());
    auto image = MakeNex("V9.9", 0, 0, {}, 0, 0, 0, 0);
    EXPECT_FALSE(loader.Load(image));
    auto truncated = MakeNex("V1.2", 0, 0, {{0, {1}}}, 0, 0, 0, 0);
    truncated.resize(600);
    EXPECT_FALSE(loader.Load(truncated)) << "the file ends inside a bank";
}

// Bring-up: run a NEX file and write the frame
TEST(LoaderNexRun_Test, RunsTheFileOfTheEnvironment)
{
    const char* path = std::getenv("UNREAL_NEX");
    if (!path)
        GTEST_SKIP() << "UNREAL_NEX not set";
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    // A NEX is loaded into a running system: the 48K ROM has set up its system variables and channels. UNREAL_NEX_PREBOOT=0
    // loads into the bare machine instead
    const char* preboot = std::getenv("UNREAL_NEX_PREBOOT");
    if (!preboot || std::string(preboot) != "0")
    {
        context->emulatorState.p7FFD = 0x10;
        context->emulatorState.p1FFD = 0x04;
        dynamic_cast<NextMemory*>(context->pMemory)->ApplyClassicPaging(0x10, 0x04);
        context->pCore->GetZ80()->pc = 0;
        emulator->EnableTurboMode();
        for (int i = 0; i < 100; i++)
            emulator->RunFrame(true);
        emulator->DisableTurboMode();
    }
    LoaderNex loader(context);
    ASSERT_TRUE(loader.LoadFile(path)) << loader.Error();
    const char* frames = std::getenv("UNREAL_NEX_FRAMES");
    const int total = frames ? std::atoi(frames) : 200;
    // UNREAL_NEX_NRLOG: the NextREG writes of the run (register, value, pc) after the load, first 200 and the last distinct ones
    std::vector<NextRegWrite> nrLog;
    uint16_t logPc = 0;
    if (std::getenv("UNREAL_NEX_NRLOG"))
        if (auto* decoder = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder))
            decoder->Board().SetWriteLog(&nrLog, &logPc);
    // no turbo: the picture is drawn as the beam passes, a turbo frame is not drawn
    for (int i = 0; i < total; i++)
        emulator->RunFrame(true);
    // UNREAL_NEX_NR="6B=00,15=1C": NextREG writes before the frame is taken (hex), to look at one layer
    if (const char* nr = std::getenv("UNREAL_NEX_NR"))
    {
        auto* decoder = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
        std::string list = nr;
        for (size_t at = 0; at < list.size();)
        {
            const size_t end = list.find(',', at);
            const std::string item = list.substr(at, end == std::string::npos ? std::string::npos : end - at);
            const size_t eq = item.find('=');
            if (decoder && eq != std::string::npos)
                decoder->Board().Write(static_cast<uint8_t>(std::stoul(item.substr(0, eq), nullptr, 16)),
                                       static_cast<uint8_t>(std::stoul(item.substr(eq + 1), nullptr, 16)));
            if (end == std::string::npos)
                break;
            at = end + 1;
        }
        for (int i = 0; i < 2; i++)
            emulator->RunFrame(true);
    }
    if (!nrLog.empty())
    {
        std::cout << "NRLOG " << nrLog.size() << " writes:";
        const char* filter = std::getenv("UNREAL_NEX_NRLOG");  // "1" = everything, else a list of registers (hex, comma separated)
        std::string wanted = filter;
        unsigned shown = 0;
        for (size_t i = 0; i < nrLog.size() && shown < 300; i++)
        {
            char one[8];
            std::snprintf(one, sizeof one, ",%x,", nrLog[i].reg);
            if (wanted != "1" && (',' + wanted + ',').find(one) == std::string::npos)
                continue;
            std::cout << " " << std::hex << int(nrLog[i].reg) << "=" << int(nrLog[i].value);
            shown++;
        }
        std::cout << std::dec << std::endl;
        if (auto* decoder = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder))
            decoder->Board().SetWriteLog(nullptr, nullptr);
    }
    if (std::getenv("UNREAL_NEX_SPRITES"))
    {
        auto* decoder = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
        for (unsigned i = 0; i < 4; i++)
        {
            std::cout << "SPRITE " << i << ":";
            for (unsigned b = 0; b < 5; b++)
                std::cout << " " << std::hex << int(decoder->Board().Sprites().Attribute(i, b));
            std::cout << std::endl;
        }
        {
            unsigned nonzero = 0, first = 99999;
            for (unsigned i = 0; i < 16384; i++)
                if (decoder->Board().Sprites().PatternByte(i))
                {
                    nonzero++;
                    first = std::min(first, i);
                }
            std::cout << "PATTERN nonzero " << nonzero << " first " << first << std::endl;
        }
        {
            const NextDma& d = decoder->Dma();
            std::cout << "DMA active " << d.Active() << " mode " << int(d.Mode()) << " prescaler " << int(d.Prescaler()) << " waiting " << d.Waiting() << " counter " << d.Counter() << " of " << d.BlockLength()
                      << " src " << std::hex << d.Source() << " dst " << d.Destination() << std::dec << std::endl;
        }
        std::cout << "PC " << std::hex << context->pCore->GetZ80()->pc << " sp " << context->pCore->GetZ80()->sp << " iff1 " << int(context->pCore->GetZ80()->iff1) << std::dec << std::endl;
        {
            Z80* z = context->pCore->GetZ80();
            const uint16_t target = static_cast<uint16_t>(context->pMemory->DirectReadFromZ80Memory(z->ix + 4) | (context->pMemory->DirectReadFromZ80Memory(z->ix + 5) << 8));
            std::cout << "WAITLINE target " << target << " hl " << z->hl << " ix " << z->ix << " speed " << int(decoder->Board().Stored(7)) << std::endl;
        }
        std::cout << "STACK";
        for (unsigned i = 0; i < 12; i += 2)
        {
            const uint16_t sp = context->pCore->GetZ80()->sp + i;
            std::cout << " " << std::hex << (context->pMemory->DirectReadFromZ80Memory(sp) | (context->pMemory->DirectReadFromZ80Memory(sp + 1) << 8));
        }
        std::cout << std::dec << std::endl;
        std::cout << "PATTERN0:";
        for (unsigned i = 0x680; i < 0x680 + 48; i++)
            std::cout << " " << std::hex << int(decoder->Board().Sprites().PatternByte(i));
        std::cout << std::dec << std::endl;
    }
    if (const char* out = std::getenv("UNREAL_NEX_OUT"))
    {
        std::ofstream ram(std::string(out) + "/ram.bin", std::ios::binary);  // the 64K the Z80 sees
        for (unsigned a = 0; a < 0x10000; a++)
        {
            const char b = static_cast<char>(context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(a)));
            ram.write(&b, 1);
        }
        const FramebufferDescriptor& fb = context->pScreen->GetFramebufferDescriptor();
        std::ofstream file(std::string(out) + "/frame.rgba", std::ios::binary);
        const uint32_t dims[2] = {fb.width, fb.height};
        file.write(reinterpret_cast<const char*>(dims), sizeof dims);
        file.write(reinterpret_cast<const char*>(fb.memoryBuffer), fb.memoryBufferSize);
    }
    EmulatorTestHelper::CleanupEmulator(emulator);
}
