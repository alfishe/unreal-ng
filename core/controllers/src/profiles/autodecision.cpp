#include "autodecision.h"

#include <cstdio>

namespace controllers
{

namespace
{
std::string Reason(const PortReadEvidence& evidence, PortReadClass cls, const char* port)
{
    char text[96];
    std::snprintf(text, sizeof(text), "%s read in %u of %u frames (%.1f per frame)", port,
                  static_cast<unsigned>(evidence.Frames(cls)), evidence.window, static_cast<double>(evidence.Reads(cls)));
    return text;
}

bool Every(const PortReadEvidence& evidence, PortReadClass cls)
{
    return evidence.Frames(cls) >= kEveryFrame;
}

PortReadClass Row(unsigned index)
{
    return static_cast<PortReadClass>(static_cast<unsigned>(PortReadClass::Row0) + index);
}

/// ", bits 0 1 2 3 4 tested" when the row bits saw the code test the half-row; "" when not
std::string Bits(const PortReadEvidence& evidence, unsigned row)
{
    const uint8_t bits = evidence.RowBits(Row(row));
    if (bits == 0)
        return {};
    std::string text = ", bits";
    for (unsigned bit = 0; bit < 8; ++bit)
        if ((bits >> bit) & 1u)
            text += " " + std::to_string(bit);
    return text + " tested";
}
}  // namespace

AutoDecision DecideFromPortReads(const PortReadEvidence& evidence)
{
    AutoDecision decision;
    if (evidence.window < kPortReadWindow)
        return decision;

    auto decide = [&decision](const char* device, const char* connector, const char* label, std::string reason) {
        decision.decisive = true;
        decision.device = device;
        decision.connector = connector;
        decision.label = label;
        decision.reason = std::move(reason);
    };

    // The dedicated ports first: a program reading one every frame wants that device, whatever it scans besides.
    // NR #B2 holds X Y Z MODE of both Next pads: the program wants a 6-button pad, on the right when it reads #37
    if (Every(evidence, PortReadClass::NextB2))
    {
        const bool right = Every(evidence, PortReadClass::Kempston2);
        decide("md6-pad", right ? "next.right" : "next.left", right ? "MD 6-button pad (right)" : "MD 6-button pad",
               Reason(evidence, PortReadClass::NextB2, "NR #B2") +
                   (right ? ", " + Reason(evidence, PortReadClass::Kempston2, "#37") : std::string()));
        return decision;
    }
    if (Every(evidence, PortReadClass::Kempston2))
    {
        decide("md6-pad", "next.right", "Kempston 2", Reason(evidence, PortReadClass::Kempston2, "#37"));
        return decision;
    }
    if (Every(evidence, PortReadClass::Kempston))
    {
        decide("kempston-joystick", "kempston", "Kempston", Reason(evidence, PortReadClass::Kempston, "#1F"));
        return decision;
    }
    if (Every(evidence, PortReadClass::Fuller))
    {
        decide("fuller-joystick", "fuller", "Fuller", Reason(evidence, PortReadClass::Fuller, "#7F"));
        return decision;
    }
    if (Every(evidence, PortReadClass::KMouse))
    {
        decide("kempston-mouse", "", "Kempston mouse", Reason(evidence, PortReadClass::KMouse, "#FBDF"));
        return decision;
    }

    // The half-rows: Row3 = #F7FE (1-5), Row4 = #EFFE (0-6), Row1 = #FDFE (A-G), Row2 = #FBFE (Q-T),
    // Row5 = #DFFE (P-Y), Row7 = #7FFE (SPACE-B)
    unsigned rows = 0;
    for (unsigned row = 0; row < 8; ++row)
        if (Every(evidence, Row(row)))
            ++rows;
    if (rows >= 7)
    {
        decision.keyboard = true;
        decision.label = "keyboard";
        decision.reason = "every half-row read in nearly every frame";
        return decision;
    }
    const bool row1 = Every(evidence, Row(1));
    const bool row2 = Every(evidence, Row(2));
    const bool row3 = Every(evidence, Row(3));
    const bool row4 = Every(evidence, Row(4));
    const bool row5 = Every(evidence, Row(5));
    const bool row7 = Every(evidence, Row(7));
    // Cursor reads 5 (#F7FE bit 4) besides 6, 7, 8 and 0; a program testing keys 1-4 too reads both Sinclair
    // ports (two players): the first one is player 1's. Without row bits both rows mean Cursor
    const uint8_t bits3 = evidence.RowBits(Row(3));
    if (row4 && row3 && (bits3 & 0x0F) != 0)
    {
        decide("sinclair-joystick", "", "Sinclair 1",
               Reason(evidence, Row(4), "#EFFE") + Bits(evidence, 4) + "; " + Reason(evidence, Row(3), "#F7FE") +
                   Bits(evidence, 3) + " (Sinclair 2 too: two players)");
        decision.option = "port";
        decision.optionValue = "1";
        return decision;
    }
    if (row4 && row3)
    {
        decide("cursor-joystick", "", "Cursor",
               Reason(evidence, Row(4), "#EFFE") + Bits(evidence, 4) + ", " + Reason(evidence, Row(3), "#F7FE") +
                   Bits(evidence, 3));
        return decision;
    }
    if (row4)
    {
        decide("sinclair-joystick", "", "Sinclair 1", Reason(evidence, Row(4), "#EFFE") + Bits(evidence, 4));
        decision.option = "port";
        decision.optionValue = "1";
        return decision;
    }
    if (row3)
    {
        decide("sinclair-joystick", "", "Sinclair 2", Reason(evidence, Row(3), "#F7FE") + Bits(evidence, 3));
        decision.option = "port";
        decision.optionValue = "2";
        return decision;
    }
    if (row1 && row2 && row5 && row7)
    {
        decide("keys-joystick", "", "QAOP + Space", "#FBFE, #FDFE, #DFFE and #7FFE read in nearly every frame");
        decision.option = "keys";
        decision.optionValue = "up:q,down:a,left:o,right:p,fire:space";
        return decision;
    }
    // Port A of the AY: nothing to plug (no Timex machine, keypad or AY mouse yet), said so
    if (Every(evidence, PortReadClass::AyR14))
        decision.hint = Reason(evidence, PortReadClass::AyR14, "AY R14 (I/O port A)") +
                        ": a Timex joystick, the 128 keypad or an AY mouse, none of which this emulator has";
    return decision;
}

}  // namespace controllers
