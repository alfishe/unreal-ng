// The NEX loader (core/src/loaders/nex/loadernex.h) on synthetic files, and a bring-up run of any NEX file:
// UNREAL_NEX=<file> [UNREAL_NEX_FRAMES=200] [UNREAL_NEX_OUT=<folder>] runs it on the NEXT machine and writes the frame
// (frame.rgba: width, height, RGBA) there for scratch conversion and comparison with a reference emulator.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/cpu/z80.h"
#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/video/screen.h"
#include "loaders/nex/loadernex.h"
#include "loaders/snapshot/snapshotlauncher.h"

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

// V1.3 (ped7g's NEXLOAD2 test files tilescreen.nex, t320x256.nex, t640x256.nex): the tilemap screen's NextREG values, the copper block
// after the screens and the first bank's file offset. Without them the banks were read 2048 bytes early and the tilemap showed its
// power-on registers
TEST_F(LoaderNex_Test, V13TilemapScreenSetsTheTilemapRegistersFromTheHeader)
{
    std::vector<uint8_t> program = {0x18, 0xFE};
    auto image = MakeNex("V1.3", 64, 512, {{0, program}, {5, {1, 2, 3}}}, 0xC000, 0xBFF0, 0, 0);  // ext. screen: a palette block only
    image[152] = 3;                                                                               // tilemap screen
    image[154] = 0x83;                                                                            // NR #6B: 40x32, 512 tiles, on
    image[155] = 0x11;                                                                            // NR #6C
    image[156] = 0x40;                                                                            // NR #6E: map at #4000
    image[157] = 0x4A;                                                                            // NR #6F: tiles at #4A00
    LoaderNex loader(_context);
    ASSERT_TRUE(loader.Load(image)) << loader.Error();
    const NextBoard& board = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder)->Board();
    EXPECT_EQ(board.Stored(0x6B), 0x83);
    EXPECT_EQ(board.Stored(0x6C), 0x11);
    EXPECT_EQ(board.Stored(0x6E), 0x40);
    EXPECT_EQ(board.Stored(0x6F), 0x4A);
    EXPECT_EQ(dynamic_cast<NextMemory*>(_context->pMemory)->RAMPageAddress(5)[2], 3) << "bank 5 holds the tilemap data";
}

TEST_F(LoaderNex_Test, V13CopperBlockIsLoadedAndTheBanksStartAtTheirFileOffset)
{
    std::vector<uint8_t> program = {0x3E, 0x44, 0x32, 0x00, 0x80, 0x18, 0xFE};
    auto image = MakeNex("V1.3", 0, 2048, {{0, program}}, 0xC000, 0xBFF0, 0, 0);
    image[153] = 1;  // HASCOPPERCODE: 2048 bytes after the (here absent) screens
    image[144] = 0x00;
    image[145] = 0x0A;  // BANKSOFFSET = 512 + 2048
    image[512] = 0x80;  // the copper's first instruction: WAIT line 5, column 0
    image[513] = 0x05;
    image[514] = 0x12;  // the second: MOVE #12, #34
    image[515] = 0x34;
    LoaderNex loader(_context);
    ASSERT_TRUE(loader.Load(image)) << loader.Error();
    auto* memory = dynamic_cast<NextMemory*>(_context->pMemory);
    EXPECT_EQ(memory->PeekSlot(0xC000), 0x3E) << "the bank is where BANKSOFFSET says, not inside the copper block";
    NextBoard& board = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder)->Board();
    EXPECT_EQ(board.Copper().Instruction(0), 0x8005);
    EXPECT_EQ(board.Copper().Instruction(1), 0x1234);
    EXPECT_EQ(board.Copper().Mode(), 1) << "started from the first instruction";
}

TEST_F(LoaderNex_Test, V13BanksOffsetSkipsWhateverLiesBetween)
{
    std::vector<uint8_t> program = {0x3E, 0x44, 0x18, 0xFE};
    auto image = MakeNex("V1.3", 0, 700, {{0, program}}, 0xC000, 0xBFF0, 0, 0);  // 700 bytes of a block this loader does not know
    image[144] = 0xBC;
    image[145] = 0x04;  // 512 + 700 = 1212 = 0x04BC
    LoaderNex loader(_context);
    ASSERT_TRUE(loader.Load(image)) << loader.Error();
    EXPECT_EQ(dynamic_cast<NextMemory*>(_context->pMemory)->PeekSlot(0xC000), 0x3E);
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
    // UNREAL_NEX_BOOT=<card folder>: the real chain (boot ROM, TBBLUE.FW, NextZXOS to its main menu) first, then the NEX is
    // loaded into that running system: ROMs, DivMMC / esxDOS API, system variables are all there
    const char* boot = std::getenv("UNREAL_NEX_BOOT");
    const std::string card = boot ? boot : "";
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(std::getenv("UNREAL_NEX_MODEL") ? std::getenv("UNREAL_NEX_MODEL") : "NEXT", LoggerLevel::LogError, RamPowerOn::Zero, [&](CONFIG& config) {
        if (!card.empty())
        {
            std::strncpy(config.next_boot_rom_path, "rom/next/nextboot.rom", sizeof config.next_boot_rom_path - 1);
            std::strncpy(config.next_sd_path, card.c_str(), sizeof config.next_sd_path - 1);
        }
    });
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    if (!card.empty())
    {
        Z80* z80 = context->pCore->GetZ80();
        auto inked = [&]() {
            const uint8_t* screen = context->pMemory->RAMPageAddress(5);
            int n = 0;
            for (int i = 0; i < 0x1800; i++)
                n += screen[i] != 0 ? 1 : 0;
            return n;
        };
        emulator->EnableTurboMode();
        int waited = 0;
        for (; waited < 3000 && !(z80->halted && inked() > 2000); waited++)
            emulator->RunFrame(true);
        ASSERT_LT(waited, 3000) << "NextZXOS did not reach its welcome screen";
        for (int i = 0; i < 50; i++)
            emulator->RunFrame(true);
        context->pKeyboard->PressKey(ZXKEY_SPACE);
        for (int i = 0; i < 8; i++)
            emulator->RunFrame(true);
        context->pKeyboard->ReleaseKey(ZXKEY_SPACE);
        for (int i = 0; i < 150; i++)
            emulator->RunFrame(true);
        emulator->DisableTurboMode();
    }
    // A NEX is loaded into a running system: the 48K ROM has set up its system variables and channels. UNREAL_NEX_PREBOOT=0
    // loads into the bare machine instead
    const char* preboot = std::getenv("UNREAL_NEX_PREBOOT");
    if (card.empty() && !std::getenv("UNREAL_NEX_MODEL") && (!preboot || std::string(preboot) != "0"))
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
    // .sna / .snx (a 48K snapshot of the ZXSpectrumNextTests programs) go through the snapshot loader; UNREAL_NEX_TURBO=1 sets
    // 28 MHz first (those programs start their tests by themselves when the board is already turbo)
    const std::string loadName = path;
    const bool snapshot = loadName.size() > 4 && (loadName.substr(loadName.size() - 4) == ".sna" || loadName.substr(loadName.size() - 4) == ".snx");
    LoaderNex loader(context);
    if (snapshot)
    {
        if (const char* turbo = std::getenv("UNREAL_NEX_TURBO"))
            if (std::atoi(turbo))
                if (auto* next = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder))
                    next->Board().Write(0x07, 0x03);
        std::string sna = loadName;
        if (sna.substr(sna.size() - 4) == ".snx")
        {
            sna = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/nexrun.sna";
            std::ifstream in(loadName, std::ios::binary);
            std::ofstream out(sna, std::ios::binary);
            out << in.rdbuf();
        }
        ASSERT_TRUE(emulator->LoadSnapshot(sna)) << "the snapshot loader refused " << loadName;
        // The state the real-board programs were photographed in: after the boot into ZX48 mode - 48K machine type (7FFD locked),
        // core id 0 (the firmware leaves it), the ULA palette filled with the 16 defaults
        auto* decoder = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
        if (decoder)  // UNREAL_NEX_MODEL runs the same file on another machine: no Next set-up
        {
            // NextZXOS loads a 48K snapshot with #7FFD = #30 (ROM 48K, locked): the test programs unlock it through NR #08 bit 7
            context->emulatorState.p7FFD = 0x30;
            dynamic_cast<NextMemory*>(context->pMemory)->ApplyClassicPaging(0x30, 0);
            decoder->Board().SetCoreId(0);
            LoaderNex::FillUlaPalette(decoder->Board());
        }

    }
    else
        ASSERT_TRUE(loader.LoadFile(path)) << loader.Error();
    const char* frames = std::getenv("UNREAL_NEX_FRAMES");
    const int total = frames ? std::atoi(frames) : 200;
    // UNREAL_NEX_NRLOG: the NextREG writes of the run (register, value, pc) after the load, first 200 and the last distinct ones
    std::vector<NextRegWrite> nrLog;
    uint16_t logPc = 0;
    if (std::getenv("UNREAL_NEX_NRLOG"))
        if (auto* decoder = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder))
            decoder->Board().SetWriteLog(&nrLog, &logPc);
    // UNREAL_NEX_PORTS: the ports the program wrote / read after the load (port: count, last value)
    std::map<uint16_t, PortDecoder_Next::PortUse> portsOut, portsIn;
    if (std::getenv("UNREAL_NEX_PORTS"))
        if (auto* decoder = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder))
            decoder->SetPortLog(&portsIn, &portsOut);
    // no turbo: the picture is drawn as the beam passes, a turbo frame is not drawn
    // UNREAL_NEX_WAV=<file>: the final mix of the run (44.1 kHz stereo 16 bit), the frames after the load
    // the picture follows the beam (borders, copper): the tests turn this per-line drawing off for speed, the app has it on
    if (context->pFeatureManager)
        context->pFeatureManager->setFeature(Features::kScreenHQ, true);
    const char* wavPath = std::getenv("UNREAL_NEX_WAV");
    if (wavPath && context->pFeatureManager)
        context->pFeatureManager->setFeature(Features::kSoundGeneration, true);
    static std::vector<int16_t> wav;
    wav.clear();
    // UNREAL_NEX_KEYS="5@30:3,2@10:3": key (0-9 A-Z) pressed at frame @ for : frames (the test programs of the real-board suites
    // are driven by single keys)
    struct KeyPress { char key; int at, frames; };
    std::vector<KeyPress> keys;
    if (const char* spec = std::getenv("UNREAL_NEX_KEYS"))
    {
        std::string text = spec;
        for (size_t pos = 0; pos < text.size();)
        {
            const size_t end = text.find(',', pos);
            const std::string item = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            const size_t at = item.find('@'), colon = item.find(':');
            if (!item.empty() && at != std::string::npos)
                keys.push_back({item[0], std::atoi(item.c_str() + at + 1), colon == std::string::npos ? 3 : std::atoi(item.c_str() + colon + 1)});
            if (end == std::string::npos)
                break;
            pos = end + 1;
        }
    }
    for (int i = 0; i < total; i++)
    {
        for (const KeyPress& k : keys)
        {
            if (i == k.at)
                context->pKeyboard->PressKey(static_cast<ZXKeysEnum>(std::toupper(k.key)));
            if (i == k.at + k.frames)
                context->pKeyboard->ReleaseKey(static_cast<ZXKeysEnum>(std::toupper(k.key)));
        }
        emulator->RunFrame(true);
        if (wavPath && context->pSoundManager)
        {
            const size_t n = context->pSoundManager->lastFrameSamples();
            const int16_t* mix = context->pSoundManager->masterMix();
            wav.insert(wav.end(), mix, mix + n * 2);
        }
    }
    if (wavPath)
    {
        std::ofstream out(wavPath, std::ios::binary);
        const uint32_t dataBytes = static_cast<uint32_t>(wav.size() * 2), rate = 44100, byteRate = rate * 4, riff = 36 + dataBytes, fmt = 16;
        const uint16_t pcm = 1, channels = 2, align = 4, bits = 16;
        out.write("RIFF", 4); out.write(reinterpret_cast<const char*>(&riff), 4); out.write("WAVEfmt ", 8);
        out.write(reinterpret_cast<const char*>(&fmt), 4); out.write(reinterpret_cast<const char*>(&pcm), 2);
        out.write(reinterpret_cast<const char*>(&channels), 2); out.write(reinterpret_cast<const char*>(&rate), 4);
        out.write(reinterpret_cast<const char*>(&byteRate), 4); out.write(reinterpret_cast<const char*>(&align), 2);
        out.write(reinterpret_cast<const char*>(&bits), 2); out.write("data", 4); out.write(reinterpret_cast<const char*>(&dataBytes), 4);
        out.write(reinterpret_cast<const char*>(wav.data()), dataBytes);
    }
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
    for (const auto* log : {&portsOut, &portsIn})
    {
        if (log->empty())
            continue;
        std::cout << (log == &portsOut ? "PORTS OUT" : "PORTS IN") << ":";
        unsigned shown = 0;
        for (const auto& [port, use] : *log)
            if (shown++ < 60)
                std::cout << " " << std::hex << port << "x" << std::dec << use.count << "=" << std::hex << int(use.last);
        std::cout << std::dec << std::endl;
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
        std::cout << "L2 port123b " << std::hex << int(decoder->Board().Video().Port123b()) << " offset " << int(decoder->Board().Video().Layer2Offset()) << " nr12 " << int(decoder->Board().Stored(0x12))
                  << " nr13 " << int(decoder->Board().Stored(0x13)) << " nr69 " << int(decoder->Board().Video().ReadDisplayControl()) << " nr70 " << int(decoder->Board().Stored(0x70)) << std::dec << std::endl;
        for (unsigned b = 9; b < 12; b++)
        {
            unsigned histogram[256] = {};
            for (unsigned i = 0; i < 16384; i++)
                histogram[context->pMemory->RAMPageAddress(static_cast<uint16_t>(b))[i]]++;
            std::cout << "L2BANK " << b << ":";
            for (unsigned v = 0; v < 256; v++)
                if (histogram[v] > 200)
                    std::cout << " " << std::hex << v << "x" << std::dec << histogram[v];
            std::cout << std::endl;
        }
        std::cout << "L2PAL1 first entries:";
        for (unsigned i : {0u, 2u, 0xE3u, 0xFFu})
            std::cout << " " << std::hex << i << "=" << decoder->Board().Video().PaletteEntry(1, i);
        std::cout << std::dec << std::endl;
        {
            int peak = 0;
            for (int i = 0; i < 1600; i++)
                peak = std::max(peak, std::abs(int(decoder->Audio().AudioBuffer()[i])));
            std::cout << "AUDIO peak " << peak << " had " << decoder->Audio().AudioHadSoundLastFrame() << " chip0 reg8 " << int(decoder->Audio().Chip(0)->readRegister(8)) << " reg0 " << int(decoder->Audio().Chip(0)->readRegister(0))
                      << " lastFrameSamples " << context->pSoundManager->lastFrameSamples() << std::endl;
        }
        if (std::getenv("UNREAL_NEX_COPPER"))
        {
            std::cout << "COPPER mode " << int(decoder->Board().Copper().Mode()) << " pc " << decoder->Board().Copper().Pc() << ":";
            for (unsigned i = 0; i < 40; i++)
            {
                const uint16_t w = decoder->Board().Copper().Instruction(i);
                if (w & 0x8000)
                    std::cout << " W(" << std::dec << (w & 0x1FF) << "," << ((w >> 9) & 0x3F) << ")";
                else
                    std::cout << " M(" << std::hex << ((w >> 8) & 0x7F) << "=" << (w & 0xFF) << ")";
            }
            std::cout << std::dec << std::endl;
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

// The app opens a .nex like an .spg: a program of the Next only, so the launcher names the machine
TEST(LoaderNexLauncher_Test, NeedOfNamesTheNextAndSnapshotExtensionsListIt)
{
    SnapshotLauncher::Need need;
    std::string error;
    ASSERT_TRUE(SnapshotLauncher::NeedOf("game.NEX", MM_PENTAGON, 128, need, error)) << error;
    EXPECT_EQ(need.model, "NEXT");
    EXPECT_EQ(need.ramKb, 2048u);
    EXPECT_TRUE(need.programOnly);
    EXPECT_TRUE(need.differs) << "a Pentagon is not a Next";
    const auto extensions = Emulator::SupportedSnapshotExtensions();
    EXPECT_NE(std::find(extensions.begin(), extensions.end(), "nex"), extensions.end());
}
