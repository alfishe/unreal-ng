// The TTD timeline scrubber draws the spans the write journal covers as a band
// along its bottom edge (D40): pixels inside a span carry the highlight color,
// pixels outside do not

#include <gtest/gtest.h>

#include <QImage>
#include <QPalette>

#include "widgets/journalspanslider.h"

TEST(JournalSpanSlider_Test, TheBandCoversExactlyTheSpans)
{
    JournalSpanSlider slider(Qt::Horizontal);
    slider.resize(400, 26);
    slider.setRange(0, 100);
    QPalette palette = slider.palette();
    palette.setColor(QPalette::Highlight, QColor(255, 0, 255));   // a color nothing else uses
    slider.setPalette(palette);

    auto bandAt = [&slider](double fraction) {
        const QImage image = slider.grab().toImage();
        const int x = static_cast<int>(fraction * (image.width() - 1));
        const int y = image.height() - 2;   // inside the 3-pixel band along the bottom edge
        return image.pixelColor(x, y) == QColor(255, 0, 255);
    };

    EXPECT_FALSE(bandAt(0.5)) << "no spans, no band";
    slider.setJournalSpans({{0.2, 0.4}, {0.7, 0.9}});
    EXPECT_TRUE(bandAt(0.3));
    EXPECT_TRUE(bandAt(0.8));
    EXPECT_FALSE(bandAt(0.55)) << "between the spans";
    EXPECT_FALSE(bandAt(0.05)) << "before the first";
    slider.setJournalSpans({});
    EXPECT_FALSE(bandAt(0.3)) << "cleared";
}
