// BreakpointSelection (one breakpoint, all, a kind, a group: what every surface switches and removes) and keyboard
// breakpoints (the first keyboard read that sees the key change)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <string>

#include "_helpers/emulatortesthelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/breakpoints/breakpointselection.h"
#include "emulator/emulator.h"
#include "emulator/io/keyboard/keyboard.h"

namespace
{
// The half-rows of the ZX keyboard (Keyboard::HandlePortIn: a cleared address bit A8..A15 selects row 0..7)
constexpr uint8_t kRowCapsToV = 0;  // CAPS Z X C V
constexpr uint8_t kRowSpaceToB = 7; // SPACE SYM M N B
constexpr uint8_t kAllRows = 0xFF;
constexpr uint8_t kNoKey = 0xFF;    // a row with no key down (5 key bits set)
constexpr uint8_t kSpaceBit = 0x01; // D0 of row 7
}  // namespace

class BreakpointSelection_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _manager = _emulator->GetBreakpointManager();
        ASSERT_NE(_manager, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    uint16_t Add(BreakpointTypeEnum type, uint8_t access, uint16_t address, const std::string& group = {},
                 const std::string& owner = {})
    {
        BreakpointSpec spec;
        spec.type = type;
        spec.access = access;
        spec.address = address;
        spec.group = group;
        spec.owner = owner;
        std::string error;
        const uint16_t id = _manager->AddBreakpoint(spec, error);
        EXPECT_NE(id, BRK_INVALID) << error;
        return id;
    }

    bool Active(uint16_t id) const
    {
        const BreakpointSnapshot bp = _manager->GetBreakpointById(id);
        return bp && bp->active;
    }

    Emulator* _emulator = nullptr;
    BreakpointManager* _manager = nullptr;
};

TEST_F(BreakpointSelection_Test, ParsesEveryForm)
{
    BreakpointSelection selection;
    std::string error;
    ASSERT_TRUE(BreakpointSelection::Parse("all", selection, error));
    EXPECT_EQ(selection.GetKind(), BreakpointSelection::Kind::All);
    ASSERT_TRUE(BreakpointSelection::Parse("0x0C", selection, error));
    EXPECT_EQ(selection.GetKind(), BreakpointSelection::Kind::Id);
    EXPECT_EQ(selection.GetId(), 12);
    ASSERT_TRUE(BreakpointSelection::Parse("#0c", selection, error));
    EXPECT_EQ(selection.GetId(), 12);
    ASSERT_TRUE(BreakpointSelection::Parse("NextReg_Write", selection, error));
    EXPECT_EQ(selection.GetKind(), BreakpointSelection::Kind::NextRegWrite);
    EXPECT_EQ(selection.Name(), "nextreg_write");
    ASSERT_TRUE(BreakpointSelection::Parse("group:Main Loop", selection, error));
    EXPECT_EQ(selection.GetGroup(), "Main Loop") << "the group keeps its case";
    EXPECT_EQ(selection.Name(), "group:Main Loop");
    for (const char* bad : {"", "group:", "sometimes", "0x"})
    {
        error.clear();
        EXPECT_FALSE(BreakpointSelection::Parse(bad, selection, error)) << bad;
        EXPECT_FALSE(error.empty()) << bad;
    }
}

TEST_F(BreakpointSelection_Test, SwitchesTogglesAndRemovesByKindAndGroup)
{
    const uint16_t exec = Add(BRK_MEMORY, BRK_MEM_EXECUTE, 0x8000, "loop");
    const uint16_t write = Add(BRK_MEMORY, BRK_MEM_WRITE, 0x9000, "loop");
    const uint16_t out = Add(BRK_IO, BRK_IO_OUT, 0x00FE);
    const uint16_t owned = Add(BRK_MEMORY, BRK_MEM_EXECUTE, 0x0038, "loop", "analyzer_manager");

    const BreakpointSelection loop = BreakpointSelection::Group("loop");
    EXPECT_EQ(loop.Select(*_manager).size(), 2u) << "an owner's breakpoint is not the user's";
    EXPECT_EQ(loop.IsActive(*_manager), std::optional<bool>(true));

    BreakpointSelection::Outcome outcome = loop.SetActive(*_manager, false);
    EXPECT_EQ(outcome.selected, 2u);
    EXPECT_EQ(outcome.changed, 2u);
    EXPECT_FALSE(Active(exec));
    EXPECT_FALSE(Active(write));
    EXPECT_TRUE(Active(owned)) << "the analyzer's breakpoint stays as its owner left it";
    EXPECT_TRUE(Active(out));

    // A toggle turns a part-on selection all on, then all off
    _manager->ActivateBreakpoint(exec);
    bool active = false;
    loop.Toggle(*_manager, active);
    EXPECT_TRUE(active);
    EXPECT_TRUE(Active(write));
    loop.Toggle(*_manager, active);
    EXPECT_FALSE(active);
    EXPECT_EQ(loop.IsActive(*_manager), std::optional<bool>(false));

    // Kinds: exec matches the execution breakpoint only, port the port one
    EXPECT_EQ(BreakpointSelection::OfKind(BreakpointSelection::Kind::Execute).Select(*_manager),
              std::vector<uint16_t>{exec});
    outcome = BreakpointSelection::OfKind(BreakpointSelection::Kind::Port).Remove(*_manager);
    EXPECT_EQ(outcome.changed, 1u);
    EXPECT_EQ(_manager->GetBreakpointById(out), nullptr);
    EXPECT_FALSE(BreakpointSelection::OfKind(BreakpointSelection::Kind::Beam).IsActive(*_manager).has_value())
        << "an empty selection has no state";

    // All: the user's two left; the owned one survives a remove of all
    outcome = BreakpointSelection::All().Remove(*_manager);
    EXPECT_EQ(outcome.changed, 2u);
    EXPECT_NE(_manager->GetBreakpointById(owned), nullptr);
}

TEST_F(BreakpointSelection_Test, RunAndGroupsAnswerTheScriptsShape)
{
    Add(BRK_MEMORY, BRK_MEM_EXECUTE, 0x8000, "loop");
    Add(BRK_MEMORY, BRK_MEM_READ, 0x8001, "loop");
    Add(BRK_IO, BRK_IO_IN, 0x00FE);

    StateNode out;
    std::string error;
    ASSERT_TRUE(BreakpointSelection::Run(*_manager, "group:loop", "disable", out, error)) << error;
    ASSERT_NE(out.find("selected"), nullptr);
    EXPECT_EQ(out.find("selected")->i, 2);
    EXPECT_FALSE(out.find("active")->b);
    EXPECT_FALSE(BreakpointSelection::Run(*_manager, "all", "explode", out, error));

    const StateNode groups = BreakpointSelection::Groups(*_manager);
    ASSERT_EQ(groups.items.size(), 2u);
    for (const StateNode& group : groups.items)
    {
        if (group.find("name")->s == "loop")
        {
            EXPECT_EQ(group.find("count")->i, 2);
            EXPECT_EQ(group.find("enabled")->i, 0);
        }
        else
            EXPECT_EQ(group.find("name")->s, "default");
    }
}

TEST_F(BreakpointSelection_Test, KeyboardBreakpointFiresOnTheReadThatSeesTheChange)
{
    uint8_t rows[8] = {kNoKey, kNoKey, kNoKey, kNoKey, kNoKey, kNoKey, kNoKey, kNoKey};
    EXPECT_EQ(_manager->HandleKeyboardRead(kAllRows, rows), BRK_INVALID) << "no keyboard breakpoint: the gate is shut";

    BreakpointSpec spec;
    std::string error;
    ASSERT_TRUE(BreakpointManager::ParseKeyboardInto("space", "press", spec, error)) << error;
    const uint16_t press = _manager->AddBreakpoint(spec, error);
    ASSERT_NE(press, BRK_INVALID) << error;
    EXPECT_TRUE(_manager->GetHotState()->hasKeyboard);

    const uint8_t spaceRow = static_cast<uint8_t>(1u << kRowSpaceToB);
    EXPECT_EQ(_manager->HandleKeyboardRead(spaceRow, rows), BRK_INVALID) << "SPACE is up";
    rows[kRowSpaceToB] = static_cast<uint8_t>(kNoKey & ~kSpaceBit);
    EXPECT_EQ(_manager->HandleKeyboardRead(static_cast<uint8_t>(1u << kRowCapsToV), rows), BRK_INVALID)
        << "a read of another row does not see SPACE";
    EXPECT_EQ(_manager->HandleKeyboardRead(spaceRow, rows), press) << "the first read that sees SPACE down";
    EXPECT_EQ(_manager->HandleKeyboardRead(kAllRows, rows), BRK_INVALID) << "still down: a scanning loop goes on";
    rows[kRowSpaceToB] = kNoKey;
    EXPECT_EQ(_manager->HandleKeyboardRead(spaceRow, rows), BRK_INVALID) << "released: a press breakpoint stays quiet";
    rows[kRowSpaceToB] = static_cast<uint8_t>(kNoKey & ~kSpaceBit);
    EXPECT_EQ(_manager->HandleKeyboardRead(spaceRow, rows), press) << "pressed again";
    EXPECT_EQ(_manager->GetBreakpointById(press)->hitCount, 2u);

    // Any key, on release
    ASSERT_TRUE(BreakpointManager::ParseKeyboardInto("any", "release", spec, error));
    const uint16_t release = _manager->AddBreakpoint(spec, error);
    ASSERT_NE(release, BRK_INVALID) << error;
    EXPECT_EQ(_manager->HandleKeyboardRead(kAllRows, rows), BRK_INVALID) << "seen down first";
    rows[kRowSpaceToB] = kNoKey;
    EXPECT_EQ(_manager->HandleKeyboardRead(kAllRows, rows), release);

    EXPECT_EQ(BreakpointSelection::OfKind(BreakpointSelection::Kind::Keyboard).Select(*_manager).size(), 2u);
    EXPECT_NE(_manager->FormatBreakpointInfo(press).find("SPACE"), std::string::npos) << _manager->FormatBreakpointInfo(press);

    // A combined key has no matrix place of its own; an unknown name, a range are refused
    EXPECT_FALSE(BreakpointManager::ParseKeyboardInto("nosuchkey", "press", spec, error));
    EXPECT_FALSE(BreakpointManager::ParseKeyboardInto("space", "sometimes", spec, error));
    ASSERT_TRUE(BreakpointManager::ParseKeyboardInto("space", "press", spec, error));
    spec.hasEnd = true;
    spec.addressEnd = static_cast<uint16_t>(spec.address + 1);
    EXPECT_EQ(_manager->AddBreakpoint(spec, error), BRK_INVALID);
}
