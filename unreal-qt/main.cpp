#include "mainwindow.h"

#include <QApplication>
#include <QFontDatabase>
#include <QDebug>
#include <QDir>

int main(int argc, char *argv[])
{
    int fontID = -1;

    QApplication app(argc, argv);

    /// region <Load monospace font>

    // Load custom monospace font from TTF file
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
            int fontID = QFontDatabase::addApplicationFontFromData(fontdata);
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

    MainWindow window;

    window.show();

    int result =  app.exec();

    /// region <De-register font>
    if (fontID != -1)
    {
        QFontDatabase::removeApplicationFont(fontID);
    }
    /// endregion </De-register font>

    return result;
}
