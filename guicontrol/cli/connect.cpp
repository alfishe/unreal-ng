#include "connect.h"

#include "guicontrol/discovery.h"

namespace guicontrol::cli
{

std::unique_ptr<Client> Open(const Where& where, const QString& method, const QString& target, Result& failure)
{
    auto client = std::make_unique<Client>();
    QString error;
    if (!where.tcp.isEmpty())
    {
        const qsizetype colon = where.tcp.lastIndexOf(':');
        if (colon <= 0)
        {
            failure = Result::Failure(errors::kBadRequest, QStringLiteral("--tcp %1: not ADDRESS:PORT").arg(where.tcp));
            return nullptr;
        }
        if (!client->ConnectTcp(where.tcp.left(colon), where.tcp.mid(colon + 1).toUShort(), 3000, &error))
        {
            failure = Result::Failure(errors::kFailed, QStringLiteral("%1: %2").arg(where.tcp, error));
            return nullptr;
        }
        return client;
    }

    const std::vector<discovery::Entry> entries = discovery::List();
    if (entries.empty())
    {
        failure = Result::Failure(errors::kNotFound,
                                  QStringLiteral("no GUI program is running (%1)").arg(discovery::Directory()));
        return nullptr;
    }
    QString hostId = where.host;
    if (hostId.isEmpty())
    {
        const qsizetype slash = target.indexOf('/');
        const QString first = slash < 0 ? target : target.left(slash);
        for (const discovery::Entry& entry : entries)
            if (entry.id == first)
                hostId = first;
    }
    // Nothing to resolve (watch, list): the oldest host
    if (hostId.isEmpty() && entries.size() > 1 && !(method.isEmpty() && target.isEmpty()))
    {
        // Every host says what the target names there; the one that took focus last wins
        std::vector<HostChoice> choices;
        for (const discovery::Entry& entry : entries)
        {
            Client probe;
            if (!probe.Connect(entry.socket, 3000))
                continue;
            choices.push_back(
                {entry, method.isEmpty() ? probe.Call(QStringLiteral("state"), {}, QJsonObject{{"target", target}})
                                         : probe.Call(QStringLiteral("resolve"), {},
                                                      QJsonObject{{"method", method}, {"target", target}})});
        }
        size_t chosen = 0;
        const Result choice = ChooseHost(choices, chosen);
        if (!choice.ok)
        {
            failure = choice;
            return nullptr;
        }
        hostId = choices[chosen].entry.id;
    }
    if (hostId.isEmpty())
        hostId = entries.front().id;
    for (const discovery::Entry& entry : entries)
        if (entry.id == hostId)
        {
            if (!client->Connect(entry.socket, 3000, &error))
            {
                failure = Result::Failure(errors::kFailed, QStringLiteral("host %1: %2").arg(hostId, error));
                return nullptr;
            }
            return client;
        }
    failure = Result::Failure(errors::kNotFound, QStringLiteral("no host %1").arg(hostId));
    return nullptr;
}

Result ListAll(const Where& where, QJsonArray& hosts)
{
    std::vector<std::unique_ptr<Client>> clients;
    if (!where.host.isEmpty() || !where.tcp.isEmpty())
    {
        Result failure;
        auto client = Open(where, {}, {}, failure);
        if (!client)
            return failure;
        clients.push_back(std::move(client));
    }
    else
        for (const discovery::Entry& entry : discovery::List())
        {
            auto client = std::make_unique<Client>();
            if (client->Connect(entry.socket, 3000))
                clients.push_back(std::move(client));
        }
    for (const std::unique_ptr<Client>& client : clients)
    {
        const Result result = client->Call(QStringLiteral("list"));
        if (result.ok)
            hosts.append(result.body);
    }
    return Result::Success();
}

}  // namespace guicontrol::cli
