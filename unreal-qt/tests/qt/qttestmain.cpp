// unreal-qt-tests entry point: one QApplication on the offscreen platform for every suite

#include <QApplication>
#include <gtest/gtest.h>

#include "emulator/emulator.h"

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    // The engine records, as in the application
    Emulator::SetDefaultTimeTravelBackend(Emulator::TimeTravelBackend::Engine);
    return RUN_ALL_TESTS();
}
