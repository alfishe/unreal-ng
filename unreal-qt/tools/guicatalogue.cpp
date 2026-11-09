// unreal-gui-catalogue FILE: writes the GUI control plane's catalogue (data/streamdeck/gui-catalogue.json: the plane's
// interfaces and unreal-qt's own `unrealqt.*`, guicontrol::CatalogueJson) to FILE when it differs. Run by the build
// (the gui-catalogue target) beside the core's action catalogue, so the Stream Deck tools read both offline (design
// docs/inprogress/2026-11-07-gui-control-plane §4.9).
#include "guicontrol/interfaces.h"
#include "guiplane/unrealqtinterface.h"

#include <QFile>
#include <QSaveFile>

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: unreal-gui-catalogue FILE\n");
        return 2;
    }
    DeclareUnrealQtInterface();
    const QByteArray text = guicontrol::CatalogueJson();
    const QString path = QString::fromLocal8Bit(argv[1]);
    {
        QFile in(path);
        if (in.open(QIODevice::ReadOnly) && in.readAll() == text)
            return 0;
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(text) != text.size() || !out.commit())
    {
        std::fprintf(stderr, "unreal-gui-catalogue: cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("unreal-gui-catalogue: wrote %s\n", argv[1]);
    return 0;
}
