// Tools > Video Synchronization (ui/videosyncwindow.h): a head card over tiles. Every value sits in a cell of its own,
// so the cells keep their places while the values change; the tiles and lines shown follow the mode in effect (VRR has
// no beat, display rate or interpolation; low latency no lead and no "late").
// UNREAL_VIDEOSYNC_UI_PNG=<dir> saves the window as drawn; UNREAL_QT_TEST_PLATFORM=cocoa (xcb, windows) draws it with
// the platform's real fonts.

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QLocale>

#include <cstdlib>
#include <map>

#include "ui/videosyncwindow.h"

namespace
{
WindowDisplaySettings Quiet()
{
    WindowDisplaySettings s;
    s.emulatorId = "test";
    WindowPresentStats& p = s.present;
    p.presenter = "dxgi";
    p.timedAt = "screen";
    p.sync = "vrr";
    p.vrr = true;
    p.fixedDisplay = false;
    p.minRefreshHz = 48.0;
    p.maxRefreshHz = 144.0;
    p.machineHz = 48.83;
    p.clock = "audio";
    p.shown = 100;
    p.beatFrames = 99;
    p.leadMs = 6.0;
    p.errorP50Ms = 0.4;
    p.errorP99Ms = 1.2;
    p.errorMaxMs = 2.0;
    p.soundOffsetP50Ms = 0.6;
    p.soundOffsetP99Ms = 3.9;
    return s;
}

WindowDisplaySettings Busy()
{
    WindowDisplaySettings s = Quiet();
    WindowPresentStats& p = s.present;
    p.sync = "interpolate";
    p.vrr = false;
    p.fixedDisplay = true;
    p.minRefreshHz = p.maxRefreshHz = 60.0;
    p.syncReason = "no VRR: the display's refreshes stay on their grid (a fixed display, or its variable refresh is off "
                   "for this window: in the background, a remote session); no display rate: no mode of the display "
                   "fits the machine's 48.83 Hz within 5 %, and some more words to wrap";
    p.clock = "emulated";
    p.shown = 2'000'000;
    p.dropped = 123'456;
    p.late = 98'765;
    p.offBeat = 54'321;
    p.beatFrames = 1'999'999;
    p.leadMs = 19.5;
    p.errorP50Ms = -12.34;
    p.errorP99Ms = 123.45;
    p.errorMaxMs = 9876.54;
    p.soundOffsetP50Ms = -150.25;
    p.soundOffsetP99Ms = 1234.5;
    p.smooth = "interpolate";
    p.smoothDevice = "gpu";
    p.game = true;
    p.frames = 3000;
    p.renders = 3600;
    p.interpolated = 3000;
    p.motionFields = 2500;
    return s;
}

void Grab(QWidget& widget, const char* name)
{
    if (const char* dir = std::getenv("UNREAL_VIDEOSYNC_UI_PNG"))
        widget.grab().save(QDir(dir).filePath(name));
}
}  // namespace

TEST(VideoSyncWindow_Test, TheCellsStayPutWhileTheValuesChange)
{
    WindowDisplaySettings now = Busy();
    now.present.shown = 100;
    now.present.dropped = now.present.late = now.present.offBeat = 0;
    now.present.errorP50Ms = 0.4;
    now.present.soundOffsetP50Ms = 0.6;
    now.present.soundOffsetP99Ms = 3.9;
    VideoSyncWindow window([&now]() { return now; });
    window.show();
    QApplication::processEvents();

    std::map<const QLabel*, QRect> before;
    for (const QLabel* label : window.findChildren<QLabel*>())
        before[label] = label->geometry();
    const QSize size = window.size();
    EXPECT_EQ(window.readout("clock").left(8), "emulated");
    EXPECT_EQ(window.readout("off_beat"), "0 (0.00 %)");
    EXPECT_EQ(window.readout("sound_p99"), "3.90 ms");
    Grab(window, "videosync-quiet.png");

    // The same mode with the widest numbers: nothing moves
    now = Busy();
    window.refresh();
    QApplication::processEvents();
    EXPECT_EQ(window.size(), size);
    for (QLabel* label : window.findChildren<QLabel*>())
        EXPECT_EQ(label->geometry(), before[label]) << label->text().toStdString();
    EXPECT_EQ(window.readout("off_beat"), QLocale().toString(54321) + " (2.72 %)");
    EXPECT_EQ(window.readout("sound_p50"), "-150.25 ms");
    // The widest numbers fit their cells: nothing cut
    for (QLabel* label : window.findChildren<QLabel*>())
        if (label->alignment() & Qt::AlignRight && !label->text().isEmpty() && !label->isHidden())
            EXPECT_LE(label->fontMetrics().horizontalAdvance(label->text()), label->width())
                << label->text().toStdString();
    Grab(window, "videosync-busy.png");

    // No machine: the display tile alone, its values empty
    now = WindowDisplaySettings();
    window.refresh();
    QApplication::processEvents();
    EXPECT_EQ(window.readout("off_beat"), "");
    EXPECT_FALSE(window.tileShown("frames"));
    EXPECT_TRUE(window.tileShown("display"));
}

TEST(VideoSyncWindow_Test, VrrShowsOnlyWhatJudgesIt)
{
    WindowDisplaySettings now = Quiet();
    VideoSyncWindow window([&now]() { return now; });
    window.show();
    QApplication::processEvents();

    // Each frame at its own time: no beat with the display, no display rate to fit, no interpolation
    EXPECT_TRUE(window.readout("mode").startsWith("VRR")) << window.readout("mode").toStdString();
    EXPECT_EQ(window.readout("why"), "");
    for (const char* tile : {"display", "frames", "timing", "sound"})
        EXPECT_TRUE(window.tileShown(tile)) << tile;
    for (const char* tile : {"smooth", "motion"})
        EXPECT_FALSE(window.tileShown(tile)) << tile;
    EXPECT_FALSE(window.lineShown("off_beat"));
    EXPECT_FALSE(window.lineShown("display_rate"));
    EXPECT_EQ(window.readout("off_beat"), "—");
    EXPECT_TRUE(window.lineShown("late"));
    EXPECT_TRUE(window.lineShown("lead"));
    EXPECT_EQ(window.readout("lead"), "6.00 ms");
    EXPECT_EQ(window.readout("refresh"), "48 – 144 Hz");
    Grab(window, "videosync-vrr.png");

    // Interpolation on a fixed display: the beat, the display rate and the interpolation's tiles come
    now = Busy();
    window.refresh();
    QApplication::processEvents();
    for (const char* tile : {"display", "frames", "timing", "sound", "smooth", "motion"})
        EXPECT_TRUE(window.tileShown(tile)) << tile;
    EXPECT_TRUE(window.lineShown("off_beat"));
    EXPECT_TRUE(window.lineShown("display_rate"));
    EXPECT_NE(window.readout("why"), "");

    // Low latency: each frame at once, nothing to be late against, no lead, no beat
    now = Quiet();
    now.present.lowLatency = true;
    now.present.clock = "latch";
    window.refresh();
    QApplication::processEvents();
    EXPECT_FALSE(window.lineShown("late"));
    EXPECT_FALSE(window.lineShown("lead"));
    EXPECT_EQ(window.readout("late"), "—");
    Grab(window, "videosync-lowlatency.png");
}

TEST(VideoSyncWindow_Test, TheTimedGlSwapHasScreenTimesToo)
{
    // Linux X11: the presenter is "gl-timed", its times the screen's (it read as not measured)
    WindowDisplaySettings now = Quiet();
    now.present.presenter = "gl-timed";
    now.present.timedAt = "screen";
    now.present.dropped = 7;
    VideoSyncWindow window([&now]() { return now; });
    window.refresh();
    EXPECT_TRUE(window.tileShown("timing"));
    EXPECT_EQ(window.readout("dropped"), "7");
    EXPECT_EQ(window.readout("error_p99"), "1.20 ms");
}

TEST(VideoSyncWindow_Test, TheDisplayRateSaysHowManyRefreshesAFrameTakes)
{
    WindowDisplaySettings now = Quiet();
    now.present.sync = "display_rate";
    now.present.vrr = false;
    now.present.displayHz = 50.0;
    now.present.machineHz = 50.0;
    VideoSyncWindow window([&now]() { return now; });
    window.refresh();
    EXPECT_TRUE(window.readout("mode").endsWith("each frame for a refresh")) << window.readout("mode").toStdString();

    now.present.displayHz = 100.0;
    window.refresh();
    EXPECT_TRUE(window.readout("mode").endsWith("each frame for 2 refreshes")) << window.readout("mode").toStdString();
}

TEST(VideoSyncWindow_Test, TheSoftwareScreenShowsItsPaintsAndNoScreenTimes)
{
    WindowDisplaySettings now = Quiet();
    WindowPresentStats& p = now.present;
    p.presenter = "software";
    p.timedAt = "paint";
    p.sync = "interpolate";
    p.vrr = false;
    p.fixedDisplay = true;
    p.minRefreshHz = p.maxRefreshHz = 60.0;
    p.displayState = "unchanged";
    p.displayReason = "the software screen does not switch the display's rate (gpu on does)";
    p.presented = p.shown = 500;
    p.dropped = 13;
    VideoSyncWindow window([&now]() { return now; });
    window.show();
    window.refresh();
    QApplication::processEvents();
    Grab(window, "videosync-software.png");

    // A paint is not a refresh: no grid to be late or off the beat on, no lead, no screen-to-sound offset
    EXPECT_TRUE(window.tileShown("timing"));
    EXPECT_FALSE(window.tileShown("sound"));
    EXPECT_EQ(window.readout("late"), "—");
    EXPECT_EQ(window.readout("off_beat"), "—");
    EXPECT_EQ(window.readout("lead"), "—");
    EXPECT_EQ(window.readout("dropped"), "13");
    EXPECT_EQ(window.readout("display_rate"), "unchanged");
}

// The box says the setting (smooth motion auto), not the step in effect: a mode set by hand has the step off, and a
// box made from the step unticked itself at the next refresh after the owner ticked it
TEST(VideoSyncWindow_Test, TheBoxSaysTheSettingNotTheStep)
{
    WindowDisplaySettings now = Quiet();
    VideoSyncWindow window([&now]() { return now; });
    window.show();
    QCheckBox* box = window.findChild<QCheckBox*>();
    ASSERT_NE(box, nullptr);
    now.smoothMotion = "interpolate";
    now.present.sync = "off";
    window.refresh();
    EXPECT_FALSE(box->isChecked());
    now.smoothMotion = "auto";  // ticked; the step is not decided yet
    window.refresh();
    EXPECT_TRUE(box->isChecked());
    now.present.sync = "interpolate";
    window.refresh();
    EXPECT_TRUE(box->isChecked());
    now.smoothMotion = "off";
    window.refresh();
    EXPECT_FALSE(box->isChecked());
}

// An information window keeps its active colors when it is not the active window: on macOS the inactive Highlight is
// gray, and the "Steady" pill and the frames graph went gray while the owner watched the emulator window beside it.
// Again after a palette change (a theme switch)
TEST(VideoSyncWindow_Test, InactiveLooksLikeActive)
{
    const QPalette before = QApplication::palette();
    QPalette themed = before;
    themed.setColor(QPalette::Active, QPalette::Highlight, QColor(10, 100, 250));
    themed.setColor(QPalette::Inactive, QPalette::Highlight, QColor(128, 128, 128));
    QApplication::setPalette(themed);
    WindowDisplaySettings now = Quiet();
    VideoSyncWindow window([&now]() { return now; });
    EXPECT_EQ(window.palette().color(QPalette::Inactive, QPalette::Highlight), QColor(10, 100, 250));
    QPalette dark = window.palette();
    dark.setColor(QPalette::Active, QPalette::Highlight, QColor(200, 50, 50));
    dark.setColor(QPalette::Inactive, QPalette::Highlight, QColor(60, 60, 60));
    window.setPalette(dark);
    EXPECT_EQ(window.palette().color(QPalette::Inactive, QPalette::Highlight), QColor(200, 50, 50));
    QApplication::setPalette(before);
}
