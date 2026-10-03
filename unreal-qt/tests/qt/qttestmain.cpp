// unreal-qt-tests entry point: one QApplication on the offscreen platform for every suite

#include <QApplication>
#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
