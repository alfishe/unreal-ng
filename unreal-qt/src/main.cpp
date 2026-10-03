#include "mainwindow.h"
#include "crashhandler/crashhandler.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QFontDatabase>
#include <QIcon>

#include "common/macosplatform.h"
#include <QDebug>
#include <QDir>
#include <common/filehelper.h>
#include <common/threadhelper.h>

int fontID = -1;

/// Install the application icon from the bundled Qt resource.
/// Used for the window / taskbar icon on Windows and Linux. On macOS the Dock
/// shows the bundle's .icns natively and QApplication::setWindowIcon would
/// replace it with a low-resolution pixmap, so it is skipped there.
static void setApplicationIcon(QApplication& app)
{
#ifndef Q_OS_MACOS
    QIcon icon;
    for (int size : {16, 32, 48, 64, 128, 256, 512, 1024})
    {
        icon.addFile(QStringLiteral(":/icons/app/appicon_%1.png").arg(size), QSize(size, size));
    }
    app.setWindowIcon(icon);
#else
    Q_UNUSED(app);
#endif
}

void registerFonts(QApplication& app)
{
    //return;
    /// region <Load monospace font>
    // Note: All fonts and resources used on windows must be loaded before window object(s) instantiated

    // Use FileHelper to get the resources path (handles macOS app bundle automatically)
    std::string resourcesPathStr = FileHelper::GetResourcesPath();
    QString resourcesPath = QString::fromStdString(resourcesPathStr);
    QDir filePath(resourcesPath);
    
    QString fontPath = filePath.filePath("fonts/consolas.ttf");
    qDebug() << "Looking for font at:" << fontPath;

    QFile fontFile(fontPath);
    if (fontFile.open(QIODevice::ReadOnly))
    {
        qDebug() << "Font file found at:" << fontPath;
        QByteArray fontdata = fontFile.readAll();
        if (!fontdata.isEmpty())
        {
            fontID = QFontDatabase::addApplicationFontFromData(fontdata);
            if (fontID == -1)
            {
                qCritical() << "Unable to load fonts/consolas.ttf";
                exit(1);
            }
        }

        fontFile.close();
    }
    else
    {
        qCritical() << "Unable to open font at:" << fontPath << fontFile.errorString();
    }

    QStringList fontFamilies = QFontDatabase::families();

#ifdef _DEBUG
    for (int i = 0; i < fontFamilies.length(); i++)
    {
        if (fontFamilies.at(i) == "Consolas")
        {
            qDebug() << "Consolas font family registered";
            break;
        }
    }
#endif
    /// endregion </Load monospace font>
}

void unregisterFonts()
{
    if (fontID != -1)
    {
        QFontDatabase::removeApplicationFont(fontID);
    }
}

int main(int argc, char *argv[])
{
    ThreadHelper::setThreadName("qt-main");

    auto crashHandler = std::unique_ptr<CrashHandler>(CrashHandler::create());
    crashHandler->install();

#ifdef Q_OS_MACOS
    MacOSPlatform::disableAutomaticFullScreenMenuItem();  // Before any menu bar exists
#endif
    QApplication app(argc, argv);

    // Load non-system fonts before any GUI rendered
    registerFonts(app);
    setApplicationIcon(app);

    // Command line: an optional file to open (as File -> Open or drag and drop
    // would); --zxpoly-model picks the machine of a ZX-Poly group without asking
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Snapshot, tape, disk or ZX-Poly (.zxp) file to open"));
    QCommandLineOption zxpolyModelOption(QStringLiteral("zxpoly-model"),
                                         QStringLiteral("Model of the four ZX-Poly modules (e.g. PENTAGON, 128k)"),
                                         QStringLiteral("model"));
    parser.addOption(zxpolyModelOption);
    parser.process(app);

    // Instantiate main application window
    MainWindow window;
    window.show();

    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty())
    {
        const QString file = files.first();
        const QString zxpolyModel = parser.value(zxpolyModelOption);
        QTimer::singleShot(0, &window, [&window, file, zxpolyModel]() { window.openFromCommandLine(file, zxpolyModel); });
    }

    // Start application main loop
    int result =  app.exec();

    // Unload non-system fonts
    unregisterFonts();

    return result;
}
