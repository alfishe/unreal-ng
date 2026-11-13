#include "handheldmode.h"

#include <QByteArray>
#include <QtGlobal>

#ifdef Q_OS_LINUX
#include "platform/linux/steamdeckgamemode.h"
#endif

namespace handheld
{

bool IsHandheldMode()
{
    const QByteArray forced = qgetenv("UNREAL_HANDHELD");
    if (!forced.isEmpty())
        return forced != "0";
#ifdef Q_OS_LINUX
    return steamdeck::IsGameMode();
#else
    return false;
#endif
}

}  // namespace handheld
