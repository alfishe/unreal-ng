// unreal-qt-tests entry point: one QApplication on the offscreen platform for every suite

#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    // UNREAL_QT_TEST_PLATFORM=cocoa (or xcb, windows) runs the suites on the real platform: real fonts and metrics,
    // for the checks that depend on them (InkCentre_Test). Default: offscreen
    const QByteArray platform = qgetenv("UNREAL_QT_TEST_PLATFORM");
    qputenv("QT_QPA_PLATFORM", platform.isEmpty() ? QByteArray("offscreen") : platform);
    QApplication app(argc, argv);
    // Settings of their own, in a folder that goes away, never the user's: an INI file on every platform. Without an
    // organization name QSettings() writes nothing on Windows (the registry), so a choice kept per model was lost there
    QTemporaryDir settingsFolder;
    QCoreApplication::setOrganizationName(QStringLiteral("Unreal-NG-Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("unreal-qt-tests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsFolder.path());
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
