#include "mainwindow.h"

#include <QApplication>
#include <QFontDatabase>
#include <QDebug>
#include <QDir>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    /// region <Load monospace font>
    // Note: All fonts and resources used on windows must be loaded before window object(s) instantiated

    // Load custom monospace font from TTF file
    int fontID = -1;
    QString appPath = app.applicationDirPath();
    QDir filePath(appPath);
    QString fontPath = filePath.filePath("fonts/consolas.ttf");
    qDebug() << fontPath;

    QFile fontFile(fontPath);
    if (fontFile.exists())
    {
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

    // Instantiate main application window
    MainWindow window;
    window.show();

    // Start application main loop
    int result =  app.exec();

    /// region <Unregister font>
    if (fontID != -1)
    {
        QFontDatabase::removeApplicationFont(fontID);
    }
    /// endregion </Unregister font>

    return result;
}
