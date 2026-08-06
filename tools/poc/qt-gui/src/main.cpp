#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("unreal-ng"));
    app.setOrganizationName(QStringLiteral("unreal-ng"));
    app.setApplicationDisplayName(QStringLiteral("unreal-ng"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));

    MainWindow w;
    w.show();
    return app.exec();
}
