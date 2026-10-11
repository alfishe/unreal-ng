#pragma once

/// @file inkcentre.h
/// @brief Centre a line of text on its ink, not on its font's line box. A QLabel centres the line box (ascent +
/// descent); with a fixed-pitch or scaled font the letters' ink sits above or below that centre, and the text looks
/// off the axis of the icons and buttons beside it. The margins this returns move the ink onto the centre.

#include <QFont>
#include <QFontMetricsF>
#include <QMargins>
#include <QRectF>
#include <QString>

#include <cmath>

/// Contents margins for a vertically centred label in `font`: the ink of `sample` (capitals and ascenders, no
/// descenders: what the eye takes for the text's height) lands on the label's vertical centre
inline QMargins InkCentringMargins(const QFont& font, const QString& sample)
{
    const QFontMetricsF metrics(font);
    const QRectF ink = metrics.tightBoundingRect(sample);   // relative to the baseline: top < 0
    // The line box is centred: its baseline sits ascent below the box top, the box centre (ascent + descent) / 2
    // below it. The ink centre is baseline + (ink.top + ink.bottom) / 2
    const double inkBelowCentre = (metrics.ascent() - metrics.descent()) / 2.0 + (ink.top() + ink.bottom()) / 2.0;
    const int shift = static_cast<int>(std::lround(2.0 * inkBelowCentre));   // a margin moves the centre by half
    return shift > 0 ? QMargins(0, 0, 0, shift) : QMargins(0, -shift, 0, 0);
}
