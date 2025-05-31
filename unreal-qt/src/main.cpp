#include "mainwindow.h"

#include <QApplication>
#include <QFontDatabase>
#include <QDebug>
#include <QDir>
#include <common/filehelper.h>

int fontID = -1;

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
    if (fontFile.exists())
    {
        qDebug() << "Font file found at:" << fontPath;
        fontFile.open(QIODevice::ReadOnly);
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
        qCritical() << "Font file not found at:" << fontPath;
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
    QApplication app(argc, argv);

    // Load non-system fonts before any GUI rendered
    registerFonts(app);

    // Instantiate main application window
    MainWindow window;
    window.show();

    // Start application main loop
    int result =  app.exec();

    // Unload non-system fonts
    unregisterFonts();

    return result;
}
