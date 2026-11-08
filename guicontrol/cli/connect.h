#pragma once

// Which host a command of unreal-gui (the CLI and its MCP server) goes to: --tcp, --host, a host id leading the
// target, else every discovered host asked to resolve the target and the one that took focus last (design §4.5).

#include "guicontrol/client.h"

#include <QJsonArray>
#include <QString>

#include <memory>

namespace guicontrol::cli
{

struct Where
{
    QString host;  ///< a host id (the discovery file's)
    QString tcp;   ///< ADDRESS:PORT of a host's TCP listener
};

/// The host `method` at `target` goes to, connected; null with `failure` (its body may list candidates)
std::unique_ptr<Client> Open(const Where& where, const QString& method, const QString& target, Result& failure);

/// `list` of every host (one with --host / --tcp)
Result ListAll(const Where& where, QJsonArray& hosts);

}  // namespace guicontrol::cli
