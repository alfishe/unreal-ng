// Tools > Video Synchronization (ui/videosyncwindow.h): every value sits in a cell of its own, so the cells keep their
// places while the values change, and a machine with other numbers, another mode or a long reason moves nothing.
// UNREAL_VIDEOSYNC_UI_PNG=<dir> saves the window as drawn; UNREAL_QT_TEST_PLATFORM=cocoa (xcb, windows) draws it with
// the platform's real fonts.

#include <gtest/gtest.h>

#include <QApplication>
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
    WindowDisplaySettings now = Quiet();
    VideoSyncWindow window([&now]() { return now; });
    window.show();
    QApplication::processEvents();

    std::map<const QLabel*, QRect> before;
    for (const QLabel* label : window.findChildren<QLabel*>())
        before[label] = label->geometry();
    const QSize size = window.size();
    EXPECT_EQ(window.readout("clock").left(5), "audio");
    EXPECT_EQ(window.readout("off_beat"), "0");
    EXPECT_EQ(window.readout("sound_p99"), "3.90 ms");
    Grab(window, "videosync-quiet.png");

    now = Busy();
    window.refresh();
    QApplication::processEvents();
    EXPECT_EQ(window.size(), size);
    for (QLabel* label : window.findChildren<QLabel*>())
        EXPECT_EQ(label->geometry(), before[label])
            << label->text().toStdString();
    EXPECT_EQ(window.readout("clock").left(8), "emulated");
    EXPECT_EQ(window.readout("off_beat"), QLocale().toString(54321));
    EXPECT_EQ(window.readout("sound_p50"), "-150.25 ms");
    // The widest numbers fit their cells: nothing cut
    for (QLabel* label : window.findChildren<QLabel*>())
        if (label->alignment() & Qt::AlignRight && !label->text().isEmpty() && label->isEnabled())
            EXPECT_LE(label->fontMetrics().horizontalAdvance(label->text()), label->width())
                << label->text().toStdString();
    Grab(window, "videosync-busy.png");

    // No machine: the cells empty, still in place
    now = WindowDisplaySettings();
    window.refresh();
    QApplication::processEvents();
    EXPECT_EQ(window.size(), size);
    EXPECT_EQ(window.readout("off_beat"), "");
}
