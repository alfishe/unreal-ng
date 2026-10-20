#pragma once

/// @file inkcentre.h
/// @brief Centre a line of text on its ink, not on its font's line box. A QLabel centres the line box (ascent +
/// descent); with a fixed-pitch or scaled font the letters' ink sits above or below that centre, and the text looks
/// off the axis of the icons and buttons beside it. The margins this returns move the ink onto the centre.

#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QMargins>
#include <QPainter>
#include <QString>

#include <cmath>

/// Contents margins for a vertically centred label in `font`: the ink of `sample` (capitals and digits: what the eye
/// takes for the text's height; ascenders poke above it, descenders below) lands on the label's vertical centre
inline QMargins InkCentringMargins(const QFont& font, const QString& sample)
{
    const QFontMetricsF metrics(font);
    // The ink as the screen draws it: tightBoundingRect pads it on Windows (a pixel below the baseline in Courier
    // New), so the sample is rendered at the screen's pixel ratio and its rows read back
    const qreal ratio = qApp ? qApp->devicePixelRatio() : 1.0;
    const int baseline = static_cast<int>(std::ceil(metrics.ascent())) + 2;
    const QSize size(static_cast<int>(std::ceil(metrics.horizontalAdvance(sample))) + 4,
                     baseline + static_cast<int>(std::ceil(metrics.descent())) + 2);
    QImage image(size * ratio, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setFont(font);
        painter.setPen(Qt::black);
        painter.drawText(QPointF(2, baseline), sample);
    }
    int first = -1, last = -1;
    for (int y = 0; y < image.height(); ++y)
    {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(row[x]) >= 128)
            {
                if (first < 0)
                    first = y;
                last = y;
                break;
            }
    }
    if (first < 0)
        return {};
    const double inkTop = first / ratio - baseline;        // relative to the baseline: < 0
    const double inkBottom = (last + 1) / ratio - baseline;
    // The line box is centred: its baseline sits ascent below the box top, the box centre (ascent + descent) / 2
    // below it. The ink centre is baseline + (inkTop + inkBottom) / 2
    const double inkBelowCentre = (metrics.ascent() - metrics.descent()) / 2.0 + (inkTop + inkBottom) / 2.0;
    const int shift = static_cast<int>(std::lround(2.0 * inkBelowCentre));   // a margin moves the centre by half
    return shift > 0 ? QMargins(0, 0, 0, shift) : QMargins(0, -shift, 0, 0);
}
