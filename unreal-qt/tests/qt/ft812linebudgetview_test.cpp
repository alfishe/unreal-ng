/// @file ft812linebudgetview_test.cpp
/// @brief What the FT812 Debug window draws (ft812linebudgetview.h): line
/// classes against the soft and hard budget, and the frame in flight composed
/// over the previous frame, as the main screen shows it (line-budget-metrics.md §3.3);
/// the chart that paints it

#include <gtest/gtest.h>

#include <QImage>

#include <cstdlib>

#include "debugger/vdac2/ft812linebudgetview.h"
#include "debugger/vdac2/ft812linechart.h"

using namespace Ft812LineBudget;

namespace
{
Vdac2Control::FrameMetrics Block(std::vector<uint16_t> lines, bool valid = true)
{
    Vdac2Control::FrameMetrics m;
    m.valid = valid;
    m.frame = 42;
    m.lines = static_cast<uint32_t>(lines.size());
    m.hardBudget = 100;
    m.softBudget = 90;
    m.margin = 10;
    m.lineClocks = std::move(lines);
    return m;
}
}  // namespace

TEST(Ft812LineBudgetView_Test, ClassesFollowTheBudgets)
{
    EXPECT_EQ(Classify(90, true, 90, 100), LineClass::Ok) << "at the soft budget: still ok";
    EXPECT_EQ(Classify(91, true, 90, 100), LineClass::OverSoft);
    EXPECT_EQ(Classify(100, true, 90, 100), LineClass::OverSoft) << "at the hard budget: fits";
    EXPECT_EQ(Classify(101, true, 90, 100), LineClass::OverHard);
    EXPECT_EQ(Classify(500, false, 90, 100), LineClass::NotMeasured);
}

TEST(Ft812LineBudgetView_Test, FinishedFrameIsShownAsIs)
{
    const View v = Build(Block({10, 95, 120, 50}));
    ASSERT_EQ(v.lines.size(), 4u);
    EXPECT_FALSE(v.composite);
    EXPECT_EQ(v.lines[1].cls, LineClass::OverSoft);
    EXPECT_EQ(v.lines[2].cls, LineClass::OverHard);
    EXPECT_EQ(v.worstLine, 2u);
    EXPECT_EQ(v.worstClocks, 120u);
    EXPECT_EQ(v.overSoft, 1u);
    EXPECT_EQ(v.overHard, 1u);
    EXPECT_GE(v.axisClocks, 125u) << "the axis reaches past the hard budget";
    EXPECT_FALSE(v.lines[0].previousFrame);
}

TEST(Ft812LineBudgetView_Test, NotDrawnFrameIsNotMeasured)
{
    const View v = Build(Block({10, 95, 120, 50}, false));
    for (const Line& line : v.lines)
        EXPECT_EQ(line.cls, LineClass::NotMeasured);
    EXPECT_EQ(v.overHard, 0u);
    EXPECT_NE(Summary(Block({1}, false), Build(Block({1}, false))).find("not measured"), std::string::npos);
}

TEST(Ft812LineBudgetView_Test, FrameInFlightIsComposedOverThePreviousFrame)
{
    Vdac2Control::FrameMetrics m = Block({10, 10, 10, 10});
    m.inFlightKnown = true;
    m.inFlightLinesPassed = 2;
    m.inFlightLineClocks = {150, -1};
    const View v = Build(m);
    ASSERT_TRUE(v.composite);
    EXPECT_EQ(v.boundary, 2u);
    EXPECT_EQ(v.lines[0].clocks, 150u);
    EXPECT_EQ(v.lines[0].cls, LineClass::OverHard) << "the frame in flight";
    EXPECT_EQ(v.lines[1].cls, LineClass::NotMeasured) << "passed without drawing";
    EXPECT_TRUE(v.lines[2].previousFrame);
    EXPECT_EQ(v.lines[2].clocks, 10u) << "below the boundary: the previous frame";
    EXPECT_NE(Summary(m, v).find("2 line(s) of the frame in flight"), std::string::npos) << Summary(m, v);

    // All lines passed (the frame ended) or none: the finished frame alone
    m.inFlightLinesPassed = 4;
    m.inFlightLineClocks = {1, 2, 3, 4};
    EXPECT_FALSE(Build(m).composite);
    m.inFlightLinesPassed = 0;
    m.inFlightLineClocks.clear();
    EXPECT_FALSE(Build(m).composite);
}

/// The chart paints what the view says: each line's bar in its class color and
/// length, nothing past it, and the previous frame faded below the in-flight boundary
TEST(Ft812LineChart_Test, BarsHaveTheirClassColorAndLength)
{
    std::vector<uint16_t> clocks(768, 0);
    for (int i = 0; i < 768; ++i)
        clocks[i] = i < 200 ? 50 : (i < 400 ? 95 : (i < 600 ? 130 : 40));
    Vdac2Control::FrameMetrics m = Block(clocks);
    m.inFlightKnown = true;
    m.inFlightLinesPassed = 500;
    m.inFlightLineClocks.assign(clocks.begin(), clocks.begin() + 500);
    const View view = Build(m);
    ASSERT_TRUE(view.composite);
    ASSERT_EQ(view.axisClocks, 130u);

    Ft812LineChart chart;
    chart.resize(390, 768);  // one pixel row per line; 3 pixels per clock
    chart.setView(view);
    const QImage image = chart.grab().toImage().convertToFormat(QImage::Format_RGB32);
    if (const char* save = std::getenv("FT812_CHART_PNG"))
        image.save(save);
    ASSERT_EQ(image.height(), 768);

    auto at = [&](int x, int y) { return QColor(image.pixel(x, y)); };
    EXPECT_EQ(at(30, 100), Ft812LineColor(LineClass::Ok));
    EXPECT_EQ(at(30, 300), Ft812LineColor(LineClass::OverSoft));
    EXPECT_EQ(at(30, 450), Ft812LineColor(LineClass::OverHard));
    EXPECT_EQ(at(140, 100), Ft812LineColor(LineClass::Ok)) << "a 50-clock bar reaches x = 150";
    EXPECT_NE(at(160, 100), Ft812LineColor(LineClass::Ok)) << "and stops there";
    EXPECT_NE(at(30, 650), Ft812LineColor(LineClass::Ok)) << "below the boundary: faded, the previous frame";
    EXPECT_EQ(at(30, 500).blue() > at(30, 500).red(), true) << "the in-flight boundary line";
}

/// In the main screen's scale: a picture shown 384 pixels tall for 768 lines puts
/// two lines in a pixel row, level with the picture when the window sits beside it
TEST(Ft812LineChart_Test, RowsFollowTheMainScreensScale)
{
    std::vector<uint16_t> clocks(768, 50);
    for (int i = 400; i < 600; ++i)
        clocks[i] = 130;
    Ft812LineChart chart;
    chart.resize(390, 800);
    chart.setView(Build(Block(clocks)));
    chart.setPictureGeometry([&chart](QRect& global, double& firstRow, double& rows) {
        global = QRect(chart.mapToGlobal(QPoint(0, 16)), QSize(512, 384));  // 16 pixels below the chart's top
        firstRow = 0;
        rows = 768;
        return true;
    });
    const QImage image = chart.grab().toImage().convertToFormat(QImage::Format_RGB32);
    if (const char* save = std::getenv("FT812_CHART_SCALED_PNG"))
        image.save(save);
    auto at = [&](int x, int y) { return QColor(image.pixel(x, y)); };
    EXPECT_EQ(at(30, 16 + 100), Ft812LineColor(LineClass::Ok)) << "line 200";
    EXPECT_EQ(at(30, 16 + 210), Ft812LineColor(LineClass::OverHard)) << "line 420: half the picture's height down";
    EXPECT_EQ(at(30, 16 + 290), Ft812LineColor(LineClass::OverHard)) << "line 580";
    EXPECT_EQ(at(30, 16 + 310), Ft812LineColor(LineClass::Ok)) << "line 620";
    EXPECT_NE(at(30, 16 + 400), Ft812LineColor(LineClass::Ok)) << "below the picture's last line: nothing";
}
