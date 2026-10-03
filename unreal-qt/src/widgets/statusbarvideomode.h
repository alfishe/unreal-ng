#pragma once

#include <QString>

struct ScreenState;

/// The status bar's video mode label of machines whose picture mixes modes per area (Sprinter): the text is
/// the machine's own few words (ScreenState::videoModeBrief), the tooltip the full description
/// (ScreenState::videoMode); other machines hide the label.
///
/// Worked example: a Sprinter in the Spectrum mode (the 128 menu) - text "Spectrum 256x192, screen 5",
/// tooltip "Sprinter 320 lines, mode page 0: ... spectrum 768, border 512, blank 0 squares", visible.
struct StatusBarVideoMode
{
    QString text;
    QString toolTip;
    bool visible = false;

    static StatusBarVideoMode From(const ScreenState& state);
};
