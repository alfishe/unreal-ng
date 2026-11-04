#include "externaldebuggers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

#ifdef ENABLE_WEBAPI_AUTOMATION
// Avoid name conflicts between Python and Qt "slot"
#undef slots
#include "automation/automation.h"
#define slots Q_SLOTS
#endif

namespace ExternalDebuggers
{
QString Binary(const QString& program)
{
#ifdef Q_OS_WIN
    const QString exe = program + QStringLiteral(".exe");
#else
    const QString exe = program;
#endif
    const QDir dir(QFileInfo(QCoreApplication::applicationFilePath()).canonicalPath());
    for (const QString& rel : {QStringLiteral("."), QStringLiteral("../../..")})
        if (const QString path = QDir::cleanPath(dir.filePath(rel + QLatin1Char('/') + exe)); QFileInfo(path).isExecutable())
            return path;
    return {};
}

int WebApiPort()
{
#ifdef ENABLE_WEBAPI_AUTOMATION
    return Automation::GetInstance().GetWebAPIPort();
#else
    return 0;
#endif
}

bool Launch(const QString& program, const QString& emulatorId, QString* error)
{
    const QString binary = Binary(program);
    if (binary.isEmpty())
    {
        *error = QCoreApplication::translate("ExternalDebuggers", "%1 is not beside unreal-qt").arg(program);
        return false;
    }
    if (emulatorId.isEmpty())
    {
        *error = QCoreApplication::translate("ExternalDebuggers", "no emulator to debug");
        return false;
    }
    // This window's own server, never a default: several emulators run side by side, each on its port
    const int port = WebApiPort();
    if (port == 0)
    {
        *error = QCoreApplication::translate("ExternalDebuggers",
                                             "the WebAPI of this emulator is not running (its port taken by another "
                                             "emulator? UNREAL_WEBAPI_PORT moves it)");
        return false;
    }
    const QString host = QStringLiteral("127.0.0.1:%1").arg(port);
    QStringList args;
    if (program == QLatin1String(kEcat))
        args << QStringLiteral("--connect") << host;
    else
        args << QStringLiteral("--endpoint") << QStringLiteral("ws://") + host;
    args << QStringLiteral("--emulator") << emulatorId;
    if (!QProcess::startDetached(binary, args, QFileInfo(binary).absolutePath()))
    {
        *error = QCoreApplication::translate("ExternalDebuggers", "%1 did not start").arg(binary);
        return false;
    }
    return true;
}
}  // namespace ExternalDebuggers
