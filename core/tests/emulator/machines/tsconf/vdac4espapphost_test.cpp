// VDAC4 ESP32-S3 app runtime (design.md §8): board app ELFs on the Xtensa interpreter with the HLE firmware.

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/platforms/tsconf/vdac4/esp/espapphost.h"
#include "emulator/platforms/tsconf/vdac4/esp/espxtensacpu.h"

using namespace vdac4;

namespace
{

std::vector<uint8_t> ReadFile(const std::string& relative)
{
    std::ifstream file(TestPathHelper::GetTestDataPath(relative), std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

struct Frame
{
    uint8_t command;
    std::vector<uint8_t> data;
};

class Vdac4EspAppHost_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        EspAppHostPorts ports;
        ports.reply = [this](uint8_t command, const uint8_t* data, size_t size) {
            frames.push_back({command, std::vector<uint8_t>(data, data + size)});
            return 0;
        };
        ports.event = ports.reply;
        ports.linkRead = [](uint32_t) { return 0u; };
        ports.linkWrite = [](uint32_t, uint32_t) {};
        ports.log = [this](const std::string& line) { log += line + "\n"; };
        host = std::make_unique<EspAppHost>(CreateEspXtensaCpu(), ports);
    }

    /// Start `elf` and run until start() answered (or `limitMs` of emulated time passed)
    bool Start(const std::string& name, const std::string& file, uint32_t limitMs = 200)
    {
        host->StartApp(name, ReadFile(file), [this](int r, const std::string& n, const std::string& v) {
            answered = true;
            result = r;
            appName = n;
            version = v;
        });
        for (uint32_t ms = 0; ms < limitMs && !answered; ms++)
            host->Advance(host->NowUs() + 1000);
        return answered;
    }

    std::unique_ptr<EspAppHost> host;
    std::vector<Frame> frames;
    std::string log;
    bool answered = false;
    int result = 1;
    std::string appName, version;
};

}  // namespace

TEST_F(Vdac4EspAppHost_Test, DoomStartsItsTasks)
{
    ASSERT_TRUE(Start("doom", "vdac4/DOOM.ELF")) << host->LastError() << "\n" << log;
    EXPECT_EQ(result, 0) << host->LastError() << "\n" << log;
    EXPECT_EQ(appName, "doom");
    EXPECT_EQ(version, "1.1");
    EXPECT_TRUE(host->IsRunning());
    EXPECT_TRUE(host->OwnsCommand(0x73));
    EXPECT_FALSE(host->OwnsCommand(0x77));
    EXPECT_NE(log.find("doom: tasks ready"), std::string::npos) << log;

    // The game task idles on its IO signal (20 ms timeouts) without faulting
    for (int ms = 0; ms < 100; ms++)
        host->Advance(host->NowUs() + 1000);
    EXPECT_TRUE(host->IsRunning()) << host->LastError() << "\n" << log;
    EXPECT_TRUE(host->LastError().empty()) << host->LastError();
}

TEST_F(Vdac4EspAppHost_Test, DoomStatusCommandReplies)
{
    ASSERT_TRUE(Start("doom", "vdac4/DOOM.ELF"));
    ASSERT_EQ(result, 0) << log;
    host->Command(0x73, nullptr, 0);
    for (int ms = 0; ms < 5 && frames.empty(); ms++)
        host->Advance(host->NowUs() + 1000);
    ASSERT_FALSE(frames.empty()) << host->LastError() << "\n" << log;
    EXPECT_EQ(frames.back().command, 0xF3);
    ASSERT_GE(frames.back().data.size(), 6u);
    EXPECT_EQ(frames.back().data[0], 0);  // vd_state: waiting for the WAD
    EXPECT_EQ(frames.back().data[1], 1);
}

TEST_F(Vdac4EspAppHost_Test, StopFreesTheApp)
{
    ASSERT_TRUE(Start("doom", "vdac4/DOOM.ELF"));
    const uint32_t freePsram = host->Memory().Psram().FreeBytes();
    host->StopApp();
    EXPECT_FALSE(host->IsRunning());
    EXPECT_GT(host->Memory().Psram().FreeBytes(), freePsram);
    // A fresh start works again
    answered = false;
    ASSERT_TRUE(Start("doom", "vdac4/DOOM.ELF"));
    EXPECT_EQ(result, 0) << log;
}

// TTD (ttd-design.md): the host's state round trip and the input journal

namespace
{

struct HostImage
{
    std::vector<uint8_t> state, psram, sram, rom;
};

HostImage Take(EspAppHost& host)
{
    HostImage image;
    host.SaveState(image.state);
    image.psram = host.Memory().PsramBytes();
    image.sram = host.Memory().SramBytes();
    image.rom = host.Memory().RomBytes();
    return image;
}

void Put(EspAppHost& host, const HostImage& image)
{
    // In place: the CPU maps these buffers
    std::copy(image.psram.begin(), image.psram.end(), host.Memory().PsramBytes().begin());
    std::copy(image.sram.begin(), image.sram.end(), host.Memory().SramBytes().begin());
    std::copy(image.rom.begin(), image.rom.end(), host.Memory().RomBytes().begin());
    ASSERT_TRUE(host.LoadState(image.state.data(), image.state.size()));
}

void ExpectSame(const HostImage& a, const HostImage& b)
{
    EXPECT_EQ(a.state, b.state);
    EXPECT_TRUE(a.psram == b.psram);
    EXPECT_TRUE(a.sram == b.sram);
    EXPECT_TRUE(a.rom == b.rom);
}

/// A second host whose LINK reads come from a script (the same words on every host)
struct ScriptedHost
{
    explicit ScriptedHost(std::vector<Frame>* replies = nullptr)
    {
        EspAppHostPorts ports;
        ports.reply = [this, replies](uint8_t command, const uint8_t* data, size_t size) {
            ++outputs;
            if (replies)
                replies->push_back({command, std::vector<uint8_t>(data, data + size)});
            return 0;
        };
        ports.event = [this](uint8_t, const uint8_t*, size_t) {
            ++outputs;
            return static_cast<int>(events++ % 3 == 0 ? -4 : 0);  // now and then the queue is full
        };
        ports.linkRead = [this](uint32_t address) {
            ++reads;
            // Polls see the same word for a while, then it moves on
            return address ^ static_cast<uint32_t>(reads / 7);
        };
        ports.linkWrite = [this](uint32_t, uint32_t) { ++outputs; };
        ports.log = [this](const std::string&) { ++outputs; };
        host = std::make_unique<EspAppHost>(CreateEspXtensaCpu(), ports);
    }

    bool Start()
    {
        bool done = false;
        host->StartApp("doom", ReadFile("vdac4/DOOM.ELF"), [&](int r, const std::string&, const std::string&) {
            done = r == 0;
        });
        host->RunUntilLoopIdle(200'000);
        return done;
    }

    std::unique_ptr<EspAppHost> host;
    uint64_t reads = 0;
    uint64_t events = 0;
    uint64_t outputs = 0;
};

/// A slice of the app's life with inputs: time, a status command, its LINK reads
void Drive(EspAppHost& host, int ms)
{
    for (int i = 0; i < ms; ++i)
    {
        if (i % 10 == 3)
        {
            host.Command(0x73, nullptr, 0);
            host.RunUntilLoopIdle(5'000);
        }
        host.Advance(host.NowUs() + 1000);
    }
}

}  // namespace

TEST(Vdac4EspAppHostState_Test, SaveLoadContinuesTheSame)
{
    ScriptedHost a;
    ASSERT_TRUE(a.Start());
    Drive(*a.host, 10);
    const HostImage saved = Take(*a.host);
    ScriptedHost b;
    Put(*b.host, saved);
    b.reads = a.reads;  // the script goes on from the same word
    b.events = a.events;
    ExpectSame(Take(*b.host), saved);

    Drive(*a.host, 20);
    Drive(*b.host, 20);
    EXPECT_TRUE(b.host->IsRunning());
    ExpectSame(Take(*a.host), Take(*b.host));
}

TEST(Vdac4EspAppHostState_Test, JournalReplayReachesTheRecordedState)
{
    std::vector<Frame> recordedReplies;
    ScriptedHost a(&recordedReplies);
    ASSERT_TRUE(a.Start());
    Drive(*a.host, 5);
    const HostImage key = Take(*a.host);
    a.host->JournalBegin();
    recordedReplies.clear();
    Drive(*a.host, 25);
    const std::vector<uint8_t> journal = a.host->JournalBytes();
    const HostImage end = Take(*a.host);
    ASSERT_FALSE(recordedReplies.empty());  // the commands answered
    EXPECT_LT(journal.size(), 4096u) << "LINK polls cost a run count, not a word each";

    std::vector<Frame> replayedReplies;
    ScriptedHost b(&replayedReplies);
    Put(*b.host, key);
    std::string error;
    ASSERT_TRUE(b.host->JournalReplay(journal.data(), journal.size(), error)) << error;
    EXPECT_EQ(b.reads, 0u) << "a replay asks no port";
    EXPECT_EQ(b.outputs, 0u) << "a replay sends nothing";
    EXPECT_TRUE(replayedReplies.empty());
    ExpectSame(Take(*b.host), end);

    // Recording goes on from the replayed journal: the same bytes as the recording host's
    EXPECT_EQ(b.host->Journal(), EspAppHost::JournalMode::Record);
    EXPECT_EQ(b.host->JournalBytes(), journal);
}

TEST(Vdac4EspAppHostState_Test, JournalReplayFromAnotherStateDiverges)
{
    ScriptedHost a;
    ASSERT_TRUE(a.Start());
    a.host->JournalBegin();
    Drive(*a.host, 10);
    const std::vector<uint8_t> journal = a.host->JournalBytes();

    // A host with no app: the command finds no owner, the reads are not asked as recorded
    ScriptedHost b;
    std::string error;
    EXPECT_FALSE(b.host->JournalReplay(journal.data(), journal.size(), error));
    EXPECT_FALSE(error.empty());
}

TEST(Vdac4EspAppHostState_Test, MalformedStateResets)
{
    ScriptedHost a;
    ASSERT_TRUE(a.Start());
    std::vector<uint8_t> state;
    a.host->SaveState(state);
    state.resize(state.size() / 2);
    EXPECT_FALSE(a.host->LoadState(state.data(), state.size()));
    EXPECT_FALSE(a.host->IsRunning());
}
