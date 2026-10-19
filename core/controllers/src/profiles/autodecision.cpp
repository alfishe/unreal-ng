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

    // The dedicated ports first: a program reading one every frame wants that device, whatever it scans besides
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
    if (row4 && row3)
    {
        decide("cursor-joystick", "", "Cursor",
               Reason(evidence, Row(4), "#EFFE") + ", " + Reason(evidence, Row(3), "#F7FE"));
        return decision;
    }
    if (row4)
    {
        decide("sinclair-joystick", "", "Sinclair 1", Reason(evidence, Row(4), "#EFFE"));
        decision.option = "port";
        decision.optionValue = "1";
        return decision;
    }
    if (row3)
    {
        decide("sinclair-joystick", "", "Sinclair 2", Reason(evidence, Row(3), "#F7FE"));
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
    return decision;
}

}  // namespace controllers
