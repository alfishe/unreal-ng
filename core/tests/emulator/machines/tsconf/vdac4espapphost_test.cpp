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
