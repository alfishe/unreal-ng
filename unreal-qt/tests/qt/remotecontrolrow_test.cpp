// The remote control bar (widgets/remotecontrolrow.h): its line sits on the bar's axis, level with the buttons' text.
// Measured on the rendered bar: the ink of the line's letters against the ink of the button's letters.
// UNREAL_QT_TEST_PLATFORM=cocoa (xcb, windows) measures with the platform's real fonts.

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QToolBar>
#include <QToolButton>

#include <cmath>
#include <cstdio>

#include "widgets/remotecontrolrow.h"

namespace
{
int Distance(const QColor& a, const QColor& b)
{
    return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
}

/// The vertical centre of the ink in `area`: the rows with a pixel far from the area's background
double InkCentre(const QImage& image, const QRect& area, const QColor& background)
{
    int top = -1, bottom = -1;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            if (Distance(image.pixelColor(x, y), background) > 200)
            {
                if (top < 0)
                    top = y;
                bottom = y;
                break;
            }
    return top < 0 ? -1.0 : (top + bottom) / 2.0;
}

QRect Scaled(const QRect& r, qreal scale)
{
    return QRect(std::lround(r.left() * scale), std::lround(r.top() * scale), std::lround(r.width() * scale),
                 std::lround(r.height() * scale));
}
}  // namespace

TEST(RemoteControlRow_Test, TheLineSitsOnTheBarsAxisLevelWithTheButtons)
{
    QToolBar bar;
    bar.setObjectName(QStringLiteral("remoteControlBar"));
    QAction takeBack(QStringLiteral("TAKE BACK CONTROL"), &bar);   // capitals: no descenders, the ink is the
    const RemoteControlRow row = BuildRemoteControlRow(&bar, {&takeBack});   // letters' height
    bar.setStyleSheet(RemoteControlBarStyle(true));
    row.label->setText(QStringLiteral("REMOTE CONTROL: PYTHON VIA WEBAPI · 127.0.0.1 · 00:00:18"));
    bar.resize(800, bar.sizeHint().height());
    bar.show();
    QApplication::processEvents();

    const QImage image = bar.grab().toImage();
    const qreal scale = image.devicePixelRatio();
    QToolButton* button = row.buttons.first().first;
    const QRect labelArea = Scaled(QRect(row.row->mapTo(&bar, row.label->geometry().topLeft()), row.label->size()), scale);
    const QRect buttonBox = Scaled(QRect(row.row->mapTo(&bar, button->geometry().topLeft()), button->size()), scale);
    // The button's face, inside its margin (3 px) and border: its own background
    const QRect buttonFace = buttonBox.adjusted(std::lround(6 * scale), std::lround(5 * scale),
                                                -std::lround(6 * scale), -std::lround(5 * scale));
    const QColor barBackground = image.pixelColor(labelArea.left() + 1, 1);
    const QColor faceBackground = image.pixelColor(buttonFace.left() + 1, buttonFace.center().y());

    const double line = InkCentre(image, labelArea.intersected(image.rect()), barBackground);
    const double text = InkCentre(image, buttonFace, faceBackground);
    const double axis = (image.height() - 1) / 2.0;
    ASSERT_GE(line, 0.0) << "no ink in the line";
    ASSERT_GE(text, 0.0) << "no ink on the button";
    EXPECT_NEAR(line, text, 0.5 * scale) << "the line is off the buttons' text (bar " << image.height() << " px)";
    EXPECT_NEAR(line, axis, 0.5 * scale) << "the line is off the bar's axis (bar " << image.height() << " px)";
    printf("bar %d px: line ink centre %.1f, button text %.1f, axis %.1f (scale %.0f)\n", image.height(), line, text,
           axis, scale);
}
