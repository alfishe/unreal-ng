#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/analyzers/basic-lang/romcontrolpoints.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

using ROMControlPoints::RomKind;

/// The control-point tables against the ROMs every machine really loads
/// (input-verification.md §4): each ROM page must be identified as the family
/// its model has there, with every control point's bytes matching. A changed
/// ROM image fails here instead of hooking the wrong code.
class ROMControlPoints_Test : public ::testing::Test
{
protected:
    void SetUp() override { MessageCenter::DisposeDefaultMessageCenter(); }
    void TearDown() override { MessageCenter::DisposeDefaultMessageCenter(); }

    /// Identifies ROM pages 0..expected.size()-1 of `model`
    void ExpectPages(const std::string& model, const std::vector<RomKind>& expected)
    {
        auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("rom-cp", model, LoggerLevel::LogError);
        ASSERT_TRUE(emulator) << model;
        Memory* memory = emulator->GetContext()->pMemory;

        for (size_t page = 0; page < expected.size(); page++)
        {
            const uint8_t* bytes = memory->ROMPageHostAddress(static_cast<uint8_t>(page));
            const RomKind found = ROMControlPoints::Identify(bytes);
            EXPECT_EQ(found, expected[page])
                << model << " ROM page " << page << ": found " << ROMControlPoints::RomName(found)
                << ", expected " << ROMControlPoints::RomName(expected[page]);

            if (expected[page] != RomKind::Unknown)
            {
                for (const std::string& bad : ROMControlPoints::Mismatches(expected[page], bytes))
                    ADD_FAILURE() << model << " ROM page " << page << ": control point " << bad << " does not match";
            }
        }

        EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
    }
};

TEST_F(ROMControlPoints_Test, Spectrum48K)
{
    ExpectPages("48K", { RomKind::Basic48 });
}

TEST_F(ROMControlPoints_Test, Spectrum128K)
{
    ExpectPages("128k", { RomKind::Editor128, RomKind::Basic48 });
}

TEST_F(ROMControlPoints_Test, SpectrumPlus3)
{
    // ROM2 is +3DOS: no editor there
    ExpectPages("PLUS3", { RomKind::Plus3Rom0, RomKind::Plus3Rom1, RomKind::Unknown, RomKind::Basic48 });
}

TEST_F(ROMControlPoints_Test, Pentagon)
{
    // Service ROM, TR-DOS, 128K editor (TR-DOS menu entry), 48 BASIC
    ExpectPages("PENTAGON", { RomKind::Unknown, RomKind::TrDos, RomKind::Editor128, RomKind::Basic48 });
}

TEST_F(ROMControlPoints_Test, Scorpion)
{
    // BASIC 128 editor, 48 BASIC, Shadow Service Monitor, TR-DOS (rom.cpp MM_SCORP)
    ExpectPages("SCORPION", { RomKind::Editor128, RomKind::Basic48, RomKind::Unknown, RomKind::TrDos });
}

TEST_F(ROMControlPoints_Test, ScorpionProf)
{
    ExpectPages("PROFSCORP", { RomKind::Editor128, RomKind::Basic48, RomKind::Unknown, RomKind::TrDos });
}

TEST_F(ROMControlPoints_Test, Atm710)
{
    ExpectPages("ATM710", { RomKind::Basic48, RomKind::TrDos, RomKind::Editor128 });
}

TEST_F(ROMControlPoints_Test, Atm3)
{
    ExpectPages("ATM3", { RomKind::Unknown, RomKind::TrDos, RomKind::Editor128, RomKind::Basic48 });
}

TEST_F(ROMControlPoints_Test, Profi)
{
    // SYS/menu, TR-DOS, 128K editor patched with the STS monitor (its control
    // points moved: no verified input there), 48 BASIC
    ExpectPages("PROFI", { RomKind::Unknown, RomKind::TrDos, RomKind::Unknown, RomKind::Basic48 });
}

TEST_F(ROMControlPoints_Test, AnyChangedControlPointMakesThePageUnknown)
{
    auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("rom-cp", "48K", LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    std::vector<uint8_t> page(0x4000);
    std::memcpy(page.data(), emulator->GetContext()->pMemory->ROMPageHostAddress(0), page.size());
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
    ASSERT_EQ(ROMControlPoints::Identify(page.data()), RomKind::Basic48);

    for (const ROMControlPoints::PointDef& point : ROMControlPoints::All())
    {
        if (point.rom != RomKind::Basic48)
            continue;
        std::vector<uint8_t> patched = page;
        patched[point.address] ^= 0xFF;
        EXPECT_EQ(ROMControlPoints::Identify(patched.data()), RomKind::Unknown) << point.label;
    }

    std::vector<uint8_t> blank(0x4000, 0xFF);
    EXPECT_EQ(ROMControlPoints::Identify(blank.data()), RomKind::Unknown);
    EXPECT_EQ(ROMControlPoints::Identify(nullptr), RomKind::Unknown);
}
