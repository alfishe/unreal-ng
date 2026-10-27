// unreal-qt-tests entry point: one QApplication on the offscreen platform for every suite

#include <QApplication>
#include <gtest/gtest.h>

#include "emulator/emulator.h"

int main(int argc, char** argv)
{
    // UNREAL_QT_TEST_PLATFORM=cocoa (or xcb, windows) runs the suites on the real platform: real fonts and metrics,
    // for the checks that depend on them (InkCentre_Test). Default: offscreen
    const QByteArray platform = qgetenv("UNREAL_QT_TEST_PLATFORM");
    qputenv("QT_QPA_PLATFORM", platform.isEmpty() ? QByteArray("offscreen") : platform);
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    // The engine records, as in the application
    Emulator::SetDefaultTimeTravelBackend(Emulator::TimeTravelBackend::Engine);
    return RUN_ALL_TESTS();
}
