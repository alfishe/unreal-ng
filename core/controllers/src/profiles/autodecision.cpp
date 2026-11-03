#include "autodecision.h"

#include <cstdio>
#include <mutex>

namespace controllers
{

namespace
{
using slots::InputRole;

/// The key matrix's half-rows are the address lines A8-A15 of the ULA claim's channels: line 8 + row, Row0 = #FEFE
/// (CAPS..V) .. Row7 = #7FFE (SPACE..B)
constexpr int kRowLine = 8;

struct Reads
{
    const DeviceAccessEvidence& evidence;

    const DeviceAccessChannel* Of(InputRole input, int index = DeviceAccessChannel::kNoIndex) const
    {
        return evidence.Find(input, index);
    }
    bool Every(InputRole input, int index = DeviceAccessChannel::kNoIndex) const
    {
        const DeviceAccessChannel* channel = Of(input, index);
        return channel != nullptr && channel->frames >= kEveryFrame;
    }
    bool Row(unsigned row) const
    {
        return Every(InputRole::KeyMatrix, kRowLine + static_cast<int>(row));
    }
    uint8_t Tested(unsigned row) const
    {
        const DeviceAccessChannel* channel = Of(InputRole::KeyMatrix, kRowLine + static_cast<int>(row));
        return channel != nullptr ? channel->tested : uint8_t{0};
    }
    /// "kempston-joystick #1F read in 50 of 50 frames (1.0 per frame)"
    std::string Reason(InputRole input, int index, const char* what) const
    {
        const DeviceAccessChannel* channel = Of(input, index);
        if (channel == nullptr)
            return {};
        char text[160];
        std::snprintf(text, sizeof(text), "%s %s read in %u of %u frames (%.1f per frame)", channel->device.c_str(),
                      what, static_cast<unsigned>(channel->frames), evidence.window,
                      static_cast<double>(channel->perFrame));
        return text;
    }
    /// ", bits 0 1 2 3 4 tested" when the code was seen testing the half-row's bits; "" when not
    std::string Bits(unsigned row) const
    {
        const uint8_t bits = Tested(row);
        if (bits == 0)
            return {};
        std::string text = ", bits";
        for (unsigned bit = 0; bit < 8; ++bit)
            if ((bits >> bit) & 1u)
                text += " " + std::to_string(bit);
        return text + " tested";
    }
    std::string RowReason(unsigned row, const char* port) const
    {
        return Reason(InputRole::KeyMatrix, kRowLine + static_cast<int>(row), port) + Bits(row);
    }
};
}  // namespace

AutoDecision DecideFromAccess(const DeviceAccessEvidence& evidence)
{
    AutoDecision decision;
    if (evidence.window < DeviceAccessWatch::kWindow)
        return decision;
    const Reads reads{evidence};
    constexpr int kAny = DeviceAccessChannel::kNoIndex;

    auto decide = [&decision, &reads](const char* device, InputRole input, const char* label, std::string reason) {
        decision.decisive = true;
        decision.device = device;
        decision.input = input;
        if (const DeviceAccessChannel* channel = input != InputRole::None ? reads.Of(input) : nullptr)
            decision.source = channel->device;
        decision.label = label;
        decision.reason = std::move(reason);
    };

    // The dedicated devices first: a program reading one every frame wants it, whatever it scans besides.
    // NR #B2 holds X Y Z MODE of both Next pads: the program wants a 6-button pad, on the right when it reads the
    // second connector
    if (reads.Every(InputRole::NextReg, 0xB2))
    {
        const bool right = reads.Every(InputRole::KempstonJoystick2);
        decide("md6-pad", right ? InputRole::KempstonJoystick2 : InputRole::KempstonJoystick,
               right ? "MD 6-button pad (right)" : "MD 6-button pad",
               reads.Reason(InputRole::NextReg, 0xB2, "NR #B2") +
                   (right ? ", " + reads.Reason(InputRole::KempstonJoystick2, kAny, "#37") : std::string()));
        return decision;
    }
    if (reads.Every(InputRole::KempstonJoystick2))
    {
        decide("md6-pad", InputRole::KempstonJoystick2, "Kempston 2",
               reads.Reason(InputRole::KempstonJoystick2, kAny, "#37"));
        return decision;
    }
    if (reads.Every(InputRole::KempstonJoystick))
    {
        decide("kempston-joystick", InputRole::KempstonJoystick, "Kempston",
               reads.Reason(InputRole::KempstonJoystick, kAny, "#1F"));
        return decision;
    }
    if (reads.Every(InputRole::FullerJoystick))
    {
        decide("fuller-joystick", InputRole::FullerJoystick, "Fuller",
               reads.Reason(InputRole::FullerJoystick, kAny, "#7F"));
        return decision;
    }
    if (reads.Every(InputRole::KempstonMouse))
    {
        decide("kempston-mouse", InputRole::KempstonMouse, "Kempston mouse",
               reads.Reason(InputRole::KempstonMouse, kAny, "port"));
        return decision;
    }
    if (reads.Every(InputRole::AmxMouse))
    {
        decide("amx-mouse", InputRole::AmxMouse, "AMX mouse", reads.Reason(InputRole::AmxMouse, kAny, "PIO"));
        return decision;
    }

    // The half-rows: Row3 = #F7FE (1-5), Row4 = #EFFE (0-6), Row1 = #FDFE (A-G), Row2 = #FBFE (Q-T),
    // Row5 = #DFFE (P-Y), Row7 = #7FFE (SPACE-B)
    unsigned rows = 0;
    for (unsigned row = 0; row < 8; ++row)
        if (reads.Row(row))
            ++rows;
    if (rows >= 7)
    {
        decision.keyboard = true;
        decision.label = "keyboard";
        decision.reason = "every half-row read in nearly every frame";
        return decision;
    }
    const bool row3 = reads.Row(3);
    const bool row4 = reads.Row(4);
    // Cursor reads 5 (#F7FE bit 4) besides 6, 7, 8 and 0; a program testing keys 1-4 too reads both Sinclair
    // ports (two players): the first one is player 1's. Without tested bits both rows mean Cursor
    if (row4 && row3 && (reads.Tested(3) & 0x0F) != 0)
    {
        decide("sinclair-joystick", InputRole::None, "Sinclair 1",
               reads.RowReason(4, "#EFFE") + "; " + reads.RowReason(3, "#F7FE") + " (Sinclair 2 too: two players)");
        decision.option = "port";
        decision.optionValue = "1";
        return decision;
    }
    if (row4 && row3)
    {
        decide("cursor-joystick", InputRole::None, "Cursor",
               reads.RowReason(4, "#EFFE") + ", " + reads.RowReason(3, "#F7FE"));
        return decision;
    }
    if (row4)
    {
        decide("sinclair-joystick", InputRole::None, "Sinclair 1", reads.RowReason(4, "#EFFE"));
        decision.option = "port";
        decision.optionValue = "1";
        return decision;
    }
    if (row3)
    {
        decide("sinclair-joystick", InputRole::None, "Sinclair 2", reads.RowReason(3, "#F7FE"));
        decision.option = "port";
        decision.optionValue = "2";
        return decision;
    }
    if (reads.Row(1) && reads.Row(2) && reads.Row(5) && reads.Row(7))
    {
        decide("keys-joystick", InputRole::None, "QAOP + Space",
               "#FBFE, #FDFE, #DFFE and #7FFE read in nearly every frame");
        decision.option = "keys";
        decision.optionValue = "up:q,down:a,left:o,right:p,fire:space";
        return decision;
    }
    // Port A of the AY: nothing to plug (no Timex machine, keypad or AY mouse yet), said so
    if (reads.Every(InputRole::AyIo, 14))
        decision.hint = reads.Reason(InputRole::AyIo, 14, "R14 (I/O port A)") +
                        ": a Timex joystick, the 128 keypad or an AY mouse, none of which this emulator has";
    return decision;
}

namespace
{
std::mutex& MatcherMutex()
{
    static std::mutex mutex;
    return mutex;
}
std::shared_ptr<const ISignatureMatcher>& Matcher()
{
    static std::shared_ptr<const ISignatureMatcher> matcher;
    return matcher;
}
}  // namespace

void SetSignatureMatcher(std::shared_ptr<const ISignatureMatcher> matcher)
{
    std::lock_guard<std::mutex> lock(MatcherMutex());
    Matcher() = std::move(matcher);
}

std::shared_ptr<const ISignatureMatcher> GetSignatureMatcher()
{
    std::lock_guard<std::mutex> lock(MatcherMutex());
    return Matcher();
}

AutoDecision DecideAuto(const DeviceAccessEvidence& evidence, SignatureQuery query)
{
    if (const std::shared_ptr<const ISignatureMatcher> matcher = GetSignatureMatcher())
    {
        query.evidence = &evidence;
        AutoDecision decision;
        if (matcher->Match(query, decision))
        {
            decision.decisive = true;
            return decision;
        }
    }
    return DecideFromAccess(evidence);
}

}  // namespace controllers
