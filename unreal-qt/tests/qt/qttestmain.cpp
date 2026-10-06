// unreal-qt-tests entry point: one QApplication on the offscreen platform for every suite

#include <QApplication>
#include <gtest/gtest.h>

#include "emulator/emulator.h"

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    // v1 records in these tests (they drive its manager); the engine is checked by core-tests
    Emulator::SetDefaultTimeTravelBackend(Emulator::TimeTravelBackend::V1);
    return RUN_ALL_TESTS();
}
