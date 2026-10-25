/// @file gamehaptics_test.cpp
/// @brief H5 (haptics.md §13): the first built-in game haptic profiles (Manic Miner, Exolon, Chase H.Q.) are found by
/// the hash of their disk image, parse, and fire their effects on a Pentagon. The games themselves are not in the
/// repository, so a small program stands in for each: it calls the game's addresses one at a time (a RET stub at
/// each) and writes its values, and each source must fire alone and play through the mixer. The addresses were
/// checked by play on the real images (haptics.md §13, H5).

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/memory/memory.h"
#include "haptics/hapticmixer.h"
#include "_helpers/fixedmixerclock.h"
#include "haptics/hapticprofile.h"
#include "haptics/profilehaptics.h"
#include "profiles/profilestore.h"
#include "stdafx.h"

using namespace controllers;

namespace
{
/// IM 2 with its own vector (no ROM code runs), then once a frame CALL the address at kOperand
const uint8_t kProgram[] = {
    0xF3,              // 6000 DI
    0xED, 0x5E,        // 6001 IM 2
    0x3E, 0x61,        // 6003 LD A,#61
    0xED, 0x47,        // 6005 LD I,A
    0xFB,              // 6007 EI
    0x76,              // 6008 HALT
    0xCD, 0x00, 0x64,  // 6009 CALL #6400 (the operand is rewritten per step)
    0xC3, 0x07, 0x60,  // 600C JP #6007
};
constexpr uint16_t kOperand = 0x600A;
constexpr uint16_t kIdle = 0x6400;  // a RET no probe watches, past the vector table (#6100-#6200)

struct Game
{
    const char* id;
    const char* sha256;
    std::vector<std::pair<const char*, uint16_t>> calls;  // source, the address that fires it
};

const Game kGames[] = {
    {"builtin/game/manic-miner",
     "38e952ec0c06b3ac4abe287771176be891e095daef4304212966d3a6de350efa",
     {{"died", 0x8906},
      {"item", 0x8F82},
      {"jump", 0x8C7B},
      {"game-over", 0x89AA},
      {"air-to-score", 0x90D9},
      {"kong", 0x91B5},
      {"ending", 0x907D}}},
    {"builtin/game/manic-miner",  // a mod on the same engine (Manic Miner 2, Welcome Corp.)
     "186c8b80469487d5196bb250dddedf3cb78fd3121cebd9fb40ea0315c28a4573",
     {{"died", 0x8906}}},
    {"builtin/game/manic-miner-phantom",
     "64a6411419e3209668c301a6f4321a5c557e5dd8541c02d3c8a390c34f19e472",
     {{"died", 0x890C}, {"item", 0x8F8D}, {"jump", 0x8C86}}},
    {"builtin/game/exolon",
     "598021f0f0f850f0ad6c214369be8eb65a71ff3e878c60ed716066add15cb35a",
     {{"shot", 0x8366},
      {"grenade", 0x9221},
      {"blast", 0x92D4},
      {"died", 0x9CB9},
      {"grenades", 0x99CE},
      {"ammo", 0x9978},
      {"kill", 0xC155},
      {"ricochet", 0xC0E7}}},
    {"builtin/game/exolon-m",  // the Breeze release, M fires
     "9eecb12b1b5c1a3aaeea7a1b6dc27b4bf02af94dd1e96c1d830907e24f65be5e",
     {{"shot", 0x8366}, {"kill", 0xC155}}},
    {"builtin/game/exolon-demo",
     "fd4a6ac0da1daa7088dec4658a544688da3a8ad57cfa70bbe34661f5b3d487ee",
     {{"shot", 0x83E5}, {"grenade", 0x92AD}, {"blast", 0x9360}, {"died", 0x9D45}}},
    {"builtin/game/chase-hq",
     "8ecfe859f329794319ef940d9ed66cf608194c86fc9402d37e33d50b8de348b6",
     {{"crash", 0xA4B1}, {"rammed", 0xA78A}, {"turbo", 0x889C}, {"landing", 0xB076}, {"scrape", 0xA437}}},
    {"builtin/game/chase-hq",  // the 48K beeper build (Mazanov & Kverznev 1992)
     "0b567987e05c8bf16662fd9b75799c9ac868fbf8eadaf8d07a31d05fedf8beb2",
     {{"crash", 0xA4B1}}},
    {"builtin/game/chase-hq-gs",
     "8074c8253b6d22940e7f6a47bf34fa064be4afca06cf5b26870e61aeb3dfd8be",
     {{"crash", 0xA4B1}, {"rammed", 0xA78A}, {"turbo", 0x889C}}},
    {"builtin/game/chase-hq-demo",
     "5d51ecbd2c9aea0feb40ed838f21257f90bc35d998fbfe004c90c8e7b264fc63",
     {{"crash", 0x9F8E}, {"rammed", 0xA1B0}, {"turbo", 0x8327}}},
};

/// The game's haptics section, found as the emulator finds it: the image's hash on a Pentagon
HapticProfile Resolve(const Game& game)
{
    ProfileStore store;
    std::vector<std::string> errors;
    EXPECT_TRUE(store.OpenGames((TestPathHelper::FindProjectRoot() / "data" / "controllers" / "profiles" / "games").string(),
                                errors));
    ProfileContext context;
    context.machine = "pentagon";
    context.fileSha256 = {game.sha256};
    EXPECT_EQ(store.LoadGamesFor(context, errors), 1u) << (errors.empty() ? "" : errors.front());
    const ResolvedProfiles resolved = store.Resolve(context);
    EXPECT_TRUE(resolved.warnings.empty()) << resolved.warnings.front();
    EXPECT_EQ(resolved.hapticsProfile, game.id);
    HapticProfile haptics;
    std::string error;
    EXPECT_TRUE(ParseHapticProfile(resolved.haptics, haptics, error)) << game.id << ": " << error;
    return haptics;
}
}  // namespace

class GameHaptics_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    HapticMixer _mixer{[](const HostDeviceId&, const RumbleLevels&) { return true; }, false, FixedMixerClock()};
    std::shared_ptr<ProfileHaptics> _feed;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        Memory* memory = _context->pMemory;
        for (size_t i = 0; i < sizeof(kProgram); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x6000 + i), kProgram[i]);
        for (unsigned i = 0; i <= 256; i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x6100 + i), 0x63);
        const uint8_t isr[] = {0xFB, 0xED, 0x4D};  // 6363 EI; RETI
        for (size_t i = 0; i < sizeof(isr); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x6363 + i), isr[i]);
        memory->DirectWriteToZ80Memory(kIdle, 0xC9);
        memory->DirectWriteToZ80Memory(0xA24A, 0);  // Chase H.Q.'s speed: standing still
        memory->DirectWriteToZ80Memory(0xA24B, 0);
        memory->DirectWriteToZ80Memory(0x9D36, 0);  // and its demo's
        memory->DirectWriteToZ80Memory(0x9D37, 0);
        for (const Game& game : kGames)
            for (const auto& [source, address] : game.calls)
                memory->DirectWriteToZ80Memory(address, 0xC9);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = 0x6000;
        z80->sp = 0x5F00;
    }

    void TearDown() override
    {
        if (_context != nullptr)
            _context->pControllerManager->SetHapticFeed(nullptr);
        if (_emulator != nullptr)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Call(uint16_t address)
    {
        _context->pMemory->DirectWriteToZ80Memory(kOperand, static_cast<uint8_t>(address & 0xFF));
        _context->pMemory->DirectWriteToZ80Memory(kOperand + 1, static_cast<uint8_t>(address >> 8));
    }

    /// The highest level of each source over two frames (a CALL right after the interrupt lands in the next frame)
    std::vector<std::pair<std::string, float>> Levels()
    {
        std::vector<std::pair<std::string, float>> peak = _feed->SourceLevels();
        for (int frame = 0; frame < 2; frame++)
        {
            _emulator->RunNFrames(1, false);  // the probes are breakpoints: not skipped
            const auto now = _feed->SourceLevels();
            for (size_t i = 0; i < peak.size(); i++)
                peak[i].second = frame == 0 ? now[i].second : std::max(peak[i].second, now[i].second);
        }
        return peak;
    }
};

class GameHapticsGames_Test : public GameHaptics_Test, public ::testing::WithParamInterface<size_t>
{
};

TEST_P(GameHapticsGames_Test, EachSourceFiresAndPlays)
{
    const Game& game = kGames[GetParam()];
    const HapticProfile haptics = Resolve(game);
    ASSERT_FALSE(haptics.sources.empty()) << game.id;
    _feed = std::make_shared<ProfileHaptics>(haptics, game.id, _mixer, [] { return std::vector<HostDeviceId>{"pad"}; });
    _context->pControllerManager->SetHapticFeed(_feed);
    _emulator->RunNFrames(2, false);  // the first frame attaches the feed and arms its probes

    for (const auto& [name, level] : Levels())
        EXPECT_EQ(level, 0.0f) << game.id << " " << name << ": the idle loop fires nothing";

    for (const auto& [source, address] : game.calls)
    {
        Call(address);
        _emulator->RunNFrames(1, false);  // the Pentagon's INT sits by the frame end: the old CALL may land in this one
        _mixer.StopAll();
        for (const auto& [name, level] : Levels())
            EXPECT_EQ(level, name == source ? 1.0f : 0.0f) << game.id << ": a CALL #" << std::hex << address
                                                           << " and source " << name;
        _mixer.Tick(FixedMixerTime() + std::chrono::milliseconds(10));  // past the effects' attacks
        const RumbleLevels written = _mixer.Written("pad");
        EXPECT_GT(std::max({written.low, written.high, written.leftTrigger, written.rightTrigger}), 0.0f)
            << game.id << " " << source << ": its effect plays";
    }
}

INSTANTIATE_TEST_SUITE_P(Games, GameHapticsGames_Test, ::testing::Range<size_t>(0, std::size(kGames)),
                         [](const ::testing::TestParamInfo<size_t>& info) {
                             const std::string id = kGames[info.param].id;
                             std::string name;
                             for (char c : id.substr(id.rfind('/') + 1))
                                 if (c != '-')
                                     name += c;
                             return name + "_" + std::string(kGames[info.param].sha256, 8);  // a release per row
                         });

TEST_F(GameHaptics_Test, ChaseHqSpeedDrivesTheEngine)
{
    const Game& chase = *std::find_if(std::begin(kGames), std::end(kGames), [](const Game& game) {
        return std::string_view(game.id) == "builtin/game/chase-hq";
    });
    const HapticProfile haptics = Resolve(chase);
    _feed = std::make_shared<ProfileHaptics>(haptics, chase.id, _mixer,
                                             [] { return std::vector<HostDeviceId>{"pad"}; });
    _context->pControllerManager->SetHapticFeed(_feed);
    Memory* memory = _context->pMemory;
    auto speed = [&](uint16_t value) {
        memory->DirectWriteToZ80Memory(0xA24A, static_cast<uint8_t>(value & 0xFF));
        memory->DirectWriteToZ80Memory(0xA24B, static_cast<uint8_t>(value >> 8));
        _emulator->RunNFrames(2, false);
        for (const auto& [name, level] : _feed->SourceLevels())
            if (name == "speed")
                return level;
        return -1.0f;
    };
    EXPECT_EQ(speed(0), 0.0f) << "standing still: no engine";
    const float low = speed(229);  // the top of the LO gear
    const float high = speed(330);  // the HI gear
    EXPECT_NEAR(low, 229 * 0.5f / 255.0f, 1e-5f);
    EXPECT_NEAR(high, 330 * 0.5f / 255.0f, 1e-5f) << "the word: the high byte counts";
    EXPECT_EQ(speed(511), 1.0f) << "the turbo's top speed is full";
    _mixer.Tick(FixedMixerTime() + std::chrono::milliseconds(10));
    EXPECT_GT(_mixer.Written("pad").low, 0.0f) << "the engine on the low motor";
    EXPECT_EQ(_mixer.Written("pad").high, 0.0f);
}
