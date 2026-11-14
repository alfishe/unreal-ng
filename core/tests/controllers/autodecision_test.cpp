/// @file autodecision_test.cpp
/// @brief The heuristic that turns the device access evidence (docs/inprogress/2026-11-02-device-access-watch) into
/// the auto connector (mapping-and-profiles.md §6.1 step 2): input roles, not port numbers; dedicated devices beat
/// half-rows, the whole keyboard is a hint, half a window decides nothing; the signature matcher is asked first (C6).

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "emulator/ports/deviceaccesswatch.h"
#include "profiles/autodecision.h"

using namespace controllers;
using slots::InputRole;

namespace
{
constexpr uint16_t kWindow = DeviceAccessWatch::kWindow;

/// One channel read in @p frames of a full window, once per frame
struct Read
{
    InputRole input = InputRole::None;
    int index = DeviceAccessChannel::kNoIndex;
    uint8_t tested = 0;
    uint16_t frames = kWindow;
    const char* device = "dev";
};

/// A window of @p window frames with @p reads
DeviceAccessEvidence Evidence(std::initializer_list<Read> reads, uint32_t window = kWindow)
{
    DeviceAccessEvidence evidence;
    evidence.active = true;
    evidence.framesObserved = window;
    evidence.window = window;
    for (const Read& read : reads)
    {
        DeviceAccessChannel channel;
        channel.device = read.device;
        channel.input = read.input;
        channel.index = read.index;
        channel.frames = read.frames;
        channel.perFrame = 1.0f;
        channel.tested = read.tested;
        evidence.channels.push_back(channel);
    }
    return evidence;
}

/// The key matrix's half-row @p row (Row0 = #FEFE .. Row7 = #7FFE): address line 8 + row
Read Row(int row, uint8_t tested = 0)
{
    return { .input = InputRole::KeyMatrix, .index = 8 + row, .tested = tested, .device = "ula" };
}
}  // namespace

TEST(AutoDecision_Test, Decide_RolesRowsAndTheKeyboard)
{
    EXPECT_FALSE(DecideFromAccess(Evidence({ { .input = InputRole::KempstonJoystick } }, 30)).decisive)
        << "half a window";
    EXPECT_FALSE(DecideFromAccess(Evidence({ { .input = InputRole::KempstonJoystick, .frames = 20 } })).decisive)
        << "not nearly every frame";

    AutoDecision decision = DecideFromAccess(
        Evidence({ { .input = InputRole::KempstonJoystick, .device = "kempston-joystick" }, Row(4) }));
    ASSERT_TRUE(decision.decisive);
    EXPECT_EQ(decision.device, "kempston-joystick") << "the dedicated device beats the half-row";
    EXPECT_EQ(decision.input, InputRole::KempstonJoystick) << "the machine's connector for #1F, not a name";
    EXPECT_EQ(decision.source, "kempston-joystick");
    EXPECT_TRUE(decision.connector.empty());
    EXPECT_NE(decision.reason.find("kempston-joystick #1F read in 50 of 50"), std::string::npos) << decision.reason;

    // A read no input role claims (a TR-DOS #1F, a DAC) is nothing
    EXPECT_FALSE(DecideFromAccess(Evidence({ { .input = InputRole::None, .device = "beta128" } })).decisive);

    decision = DecideFromAccess(Evidence({ Row(4) }));
    EXPECT_EQ(decision.label, "Sinclair 1");
    EXPECT_EQ(decision.optionValue, "1");
    EXPECT_EQ(DecideFromAccess(Evidence({ Row(3) })).label, "Sinclair 2");
    EXPECT_EQ(DecideFromAccess(Evidence({ Row(4), Row(3) })).device, "cursor-joystick");
    decision = DecideFromAccess(Evidence({ Row(1), Row(2), Row(5), Row(7) }));
    EXPECT_EQ(decision.device, "keys-joystick");
    EXPECT_EQ(decision.optionValue, "up:q,down:a,left:o,right:p,fire:space");
    EXPECT_EQ(DecideFromAccess(Evidence({ { .input = InputRole::KempstonJoystick2 } })).input,
              InputRole::KempstonJoystick2);
    EXPECT_EQ(DecideFromAccess(Evidence({ { .input = InputRole::FullerJoystick } })).device, "fuller-joystick");
    EXPECT_EQ(DecideFromAccess(Evidence({ { .input = InputRole::KempstonMouse } })).device, "kempston-mouse");
    decision = DecideFromAccess(Evidence({ { .input = InputRole::AmxMouse } }));
    EXPECT_EQ(decision.device, "amx-mouse");
    EXPECT_EQ(decision.input, InputRole::AmxMouse);

    // The ROM's key scan: every half-row, a hint and no plug
    decision = DecideFromAccess(Evidence({ Row(0), Row(1), Row(2), Row(3), Row(4), Row(5), Row(6), Row(7) }));
    EXPECT_FALSE(decision.decisive);
    EXPECT_TRUE(decision.keyboard);
    EXPECT_FALSE(DecideFromAccess(Evidence({ Row(7) })).decisive) << "SPACE / BREAK alone decides nothing";

    // The 128K menu after a reset: its scan skips a few frames of the last half-rows; still the keyboard, not Sinclair
    auto rowIn = [](int row, uint16_t frames) {
        Read read = Row(row);
        read.frames = frames;
        return read;
    };
    decision = DecideFromAccess(
        Evidence({ Row(0), Row(1), Row(2), Row(3), Row(4), rowIn(5, 42), rowIn(6, 42), rowIn(7, 42) }));
    EXPECT_TRUE(decision.keyboard) << decision.label;
    EXPECT_FALSE(decision.decisive);
    decision = DecideFromAccess(
        Evidence({ Row(0), Row(1), Row(2), Row(3), Row(4), rowIn(5, 10), rowIn(6, 10), rowIn(7, 10) }));
    EXPECT_FALSE(decision.keyboard) << "a few rows now and then are no key scan";
}

TEST(AutoDecision_Test, Decide_NextB2RowBitsAndAyR14)
{
    const Read b2{ .input = InputRole::NextReg, .index = 0xB2, .device = "nextreg" };
    AutoDecision decision = DecideFromAccess(Evidence({ b2, { .input = InputRole::KempstonJoystick } }));
    ASSERT_TRUE(decision.decisive);
    EXPECT_EQ(decision.device, "md6-pad");
    EXPECT_EQ(decision.input, InputRole::KempstonJoystick) << "NR #B2 beats #1F: a 6-button pad on the left";
    EXPECT_NE(decision.reason.find("nextreg NR #B2 read in 50 of 50"), std::string::npos) << decision.reason;
    decision = DecideFromAccess(Evidence({ b2, { .input = InputRole::KempstonJoystick2 } }));
    EXPECT_EQ(decision.input, InputRole::KempstonJoystick2);
    EXPECT_FALSE(DecideFromAccess(Evidence({ { .input = InputRole::NextReg, .index = 0xB1 } })).decisive)
        << "another NextREG decides nothing";

    // #EFFE and #F7FE: key 5 alone on #F7FE is Cursor, keys 1-5 are a second Sinclair port
    decision = DecideFromAccess(Evidence({ Row(4, 0x1D), Row(3, 0x10) }));
    EXPECT_EQ(decision.device, "cursor-joystick");
    EXPECT_NE(decision.reason.find("bits 4 tested"), std::string::npos) << decision.reason;
    decision = DecideFromAccess(Evidence({ Row(4, 0x1D), Row(3, 0x1F) }));
    EXPECT_EQ(decision.label, "Sinclair 1");
    EXPECT_EQ(decision.optionValue, "1");
    EXPECT_NE(decision.reason.find("two players"), std::string::npos) << decision.reason;

    // AY port A: a hint, nothing to plug
    const Read r14{ .input = InputRole::AyIo, .index = 14, .device = "ay" };
    decision = DecideFromAccess(Evidence({ r14 }));
    EXPECT_FALSE(decision.decisive);
    EXPECT_NE(decision.hint.find("R14"), std::string::npos) << decision.hint;
    EXPECT_TRUE(DecideFromAccess(Evidence({ { .input = InputRole::AyIo, .index = 7 } })).hint.empty())
        << "another AY register is no hint";
    EXPECT_TRUE(DecideFromAccess(Evidence({ r14, { .input = InputRole::KempstonJoystick } })).hint.empty())
        << "a decision leaves no hint";
}

namespace
{
/// A matcher that knows one program by its media
class FakeMatcher : public ISignatureMatcher
{
public:
    mutable int asked = 0;
    bool Match(const SignatureQuery& query, AutoDecision& decision) const override
    {
        ++asked;
        if (query.mediaSha256 != "abc")
            return false;
        decision.decisive = true;
        decision.device = "fuller-joystick";
        decision.reason = "signature: media abc";
        return true;
    }
};
}  // namespace

/// The signature matcher is asked first; no match falls back to the evidence table
TEST(AutoDecision_Test, SignatureMatcherFirst)
{
    const DeviceAccessEvidence evidence = Evidence({ { .input = InputRole::KempstonJoystick } });
    EXPECT_EQ(DecideAuto(evidence).device, "kempston-joystick") << "no matcher: the table";

    auto matcher = std::make_shared<FakeMatcher>();
    SetSignatureMatcher(matcher);
    SignatureQuery query;
    query.mediaSha256 = "abc";
    AutoDecision decision = DecideAuto(evidence, query);
    EXPECT_EQ(decision.device, "fuller-joystick") << "the matcher beats the evidence";
    EXPECT_EQ(decision.reason, "signature: media abc");
    query.mediaSha256 = "other";
    EXPECT_EQ(DecideAuto(evidence, query).device, "kempston-joystick");
    EXPECT_EQ(matcher->asked, 2);
    SetSignatureMatcher(nullptr);
    EXPECT_EQ(GetSignatureMatcher(), nullptr);
}

namespace
{
/// Guest code for the tested-bits scan: @p bytes at address 0x8000, #FF elsewhere
struct Code
{
    std::vector<uint8_t> bytes;
    static uint8_t Read(const void* context, uint16_t address)
    {
        const auto& code = static_cast<const Code*>(context)->bytes;
        const unsigned offset = static_cast<uint16_t>(address - 0x8000);
        return offset < code.size() ? code[offset] : uint8_t{0xFF};
    }
    /// The bits tested after the IN at 0x8000 (two bytes long)
    uint8_t Tested() const
    {
        return TestedBitsAfterIn(&Code::Read, this, 0x8002);
    }
};
}  // namespace

TEST(AutoDecision_Test, InputRoleNames)
{
    EXPECT_STREQ(InputRoleName(InputRole::KeyMatrix), "key-matrix");
    EXPECT_STREQ(InputRoleName(InputRole::AyIo), "ay-io");
    EXPECT_STREQ(InputRoleName(InputRole::NextReg), "nextreg");
    EXPECT_STREQ(InputRoleName(InputRole::None), "");
}

TEST(AutoDecision_Test, RowBits_TheCodeAfterTheIn)
{
    // IN A,(#FE); CPL; AND #1F
    EXPECT_EQ((Code{{0xDB, 0xFE, 0x2F, 0xE6, 0x1F}}.Tested()), 0x1F);
    // IN A,(C); RRA; JR NC; RRA; JR NC; RRA; JP NC,nn: the shifts into the carry test bits 0, 1, 2
    EXPECT_EQ((Code{{0xED, 0x78, 0x1F, 0x30, 0x10, 0x1F, 0x30, 0x10, 0x1F, 0xD2, 0x00, 0x90}}.Tested()), 0x07);
    // IN E,(C); BIT 4,E; JR Z; BIT 0,E
    EXPECT_EQ((Code{{0xED, 0x58, 0xCB, 0x63, 0x28, 0x02, 0xCB, 0x43}}.Tested()), 0x11);
    // IN A,(#FE); RRA; AND 1: bit 1 of the read
    EXPECT_EQ((Code{{0xDB, 0xFE, 0x1F, 0xE6, 0x01}}.Tested()), 0x02);
    // IN D,(C); LD A,D; AND #03 (LD (IX+5),#12 in between: four bytes skipped)
    EXPECT_EQ((Code{{0xED, 0x50, 0xDD, 0x36, 0x05, 0x12, 0x7A, 0xE6, 0x03}}.Tested()), 0x03);
    // IN A,(#FE); JP nn; AND 1: the scan stops at the jump
    EXPECT_EQ((Code{{0xDB, 0xFE, 0xC3, 0x00, 0x90, 0xE6, 0x01}}.Tested()), 0);
    // IN A,(#FE); AND 1 past the eighth instruction: not seen
    EXPECT_EQ((Code{{0xDB, 0xFE, 0, 0, 0, 0, 0, 0, 0, 0, 0xE6, 0x01}}.Tested()), 0);
    // INI (ED A2) loads no register
    EXPECT_EQ((Code{{0xED, 0xA2, 0xE6, 0x01}}.Tested()), 0);
}

TEST(AutoDecision_Test, RowBits_StopWhereTheValueIsGone)
{
    // IN A,(#FE); LD A,#0E; AND #01: A holds something else
    EXPECT_EQ((Code{{0xDB, 0xFE, 0x3E, 0x0E, 0xE6, 0x01}}.Tested()), 0);
    // IN A,(#FE); OR #E0; AND #01
    EXPECT_EQ((Code{{0xDB, 0xFE, 0xF6, 0xE0, 0xE6, 0x01}}.Tested()), 0);
    // IN E,(C); LD E,B; BIT 0,E
    EXPECT_EQ((Code{{0xED, 0x58, 0x58, 0xCB, 0x43}}.Tested()), 0);
    // IN A,(#FE); CP #FF; AND #01: a compare leaves A
    EXPECT_EQ((Code{{0xDB, 0xFE, 0xFE, 0xFF, 0xE6, 0x01}}.Tested()), 0x01);
}

// A port nothing on the machine answers, read every frame: the card that would, as a hint (a card restarts the
// machine); a candidate without an input claim, a write or half a window says nothing
TEST(AutoDecision_Test, Decide_BoardReadsNameTheCard)
{
    auto board = [](uint16_t frames, bool out, InputRole input) {
        DeviceAccessEvidence evidence = Evidence({ { .frames = frames, .device = "board" } });
        DeviceAccessChannel& channel = evidence.channels.front();
        channel.port = 0x001F;
        channel.out = out;
        channel.candidates.push_back({ .card = "covox", .name = "Covox", .slot = "edge.1" });
        channel.candidates.push_back(
            { .card = "kempston", .name = "Kempston joystick interface", .slot = "edge.1", .input = input });
        return evidence;
    };
    AutoDecision decision = DecideFromAccess(board(kWindow, false, InputRole::KempstonJoystick));
    EXPECT_FALSE(decision.decisive);
    EXPECT_EQ(decision.card, "kempston");
    EXPECT_EQ(decision.cardSlot, "edge.1");
    EXPECT_EQ(decision.input, InputRole::KempstonJoystick);
    EXPECT_NE(decision.hint.find("#001F"), std::string::npos) << decision.hint;
    EXPECT_NE(decision.hint.find("Kempston joystick interface"), std::string::npos) << decision.hint;

    EXPECT_TRUE(DecideFromAccess(board(kWindow, false, InputRole::None)).card.empty()) << "no input claim";
    EXPECT_TRUE(DecideFromAccess(board(kWindow, true, InputRole::KempstonJoystick)).card.empty()) << "a write";
    EXPECT_TRUE(DecideFromAccess(board(20, false, InputRole::KempstonJoystick)).card.empty()) << "20 frames";
    // The keyboard joystick the program reads as well plays as the machine is
    DeviceAccessEvidence both = board(kWindow, false, InputRole::KempstonJoystick);
    both.channels.push_back(Evidence({ Row(4) }).channels.front());
    EXPECT_EQ(DecideFromAccess(both).device, "sinclair-joystick");
}
