// unreal-gui: the GUI control plane's command line (docs/inprogress/2026-11-07-gui-control-plane §4.9).
//
//   unreal-gui hosts                                  the running GUI programs (the discovery files)
//   unreal-gui list [--host ID]                       their windows and views, with state and commands
//   unreal-gui [--host ID] [--target T] METHOD [VALUE...] [KEY=VALUE...]
//                                                     run a command; values fill the declared parameters in order
//   unreal-gui watch [TOPIC...] [--max-hz N]          print the pushes (state, nodes, focus, activity) until killed
//   --tcp ADDRESS:PORT                                a host's TCP listener instead of the discovery directory
//   --json                                            the answer as JSON
//
// Exit codes: 0 done, 1 refused or failed, 2 wrong usage.

#include "guicontrol/client.h"
#include "guicontrol/discovery.h"
#include "guicontrol/interfaces.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

#include <cstdio>
#include <memory>

using namespace guicontrol;

namespace
{

QTextStream& Out()
{
    static QTextStream out(stdout);
    return out;
}

QTextStream& Err()
{
    static QTextStream err(stderr);
    return err;
}

int Usage()
{
    Err() << "usage: unreal-gui hosts\n"
             "       unreal-gui list [--host ID]\n"
             "       unreal-gui [--host ID] [--target T] METHOD [VALUE...] [KEY=VALUE...]\n"
             "       unreal-gui watch [TOPIC...] [--max-hz N]\n"
             "options: --tcp ADDRESS:PORT, --json\n";
    Err().flush();
    return 2;
}

QString Json(const QJsonValue& value)
{
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented)).trimmed();
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented)).trimmed();
    return value.toVariant().toString();
}

/// A value as its declared kind: numbers for Int, true / false for Bool, text otherwise
QJsonValue Typed(const QString& text, const ParamSpec* spec)
{
    if (spec != nullptr && spec->kind == ParamKind::Int)
    {
        bool ok = false;
        const int number = text.toInt(&ok, 0);
        if (ok)
            return number;
    }
    if (spec != nullptr && spec->kind == ParamKind::Bool)
    {
        if (text == QLatin1String("true") || text == QLatin1String("on") || text == QLatin1String("1"))
            return true;
        if (text == QLatin1String("false") || text == QLatin1String("off") || text == QLatin1String("0"))
            return false;
    }
    return text;
}

const ParamSpec* FindParam(const CommandSpec* command, const QString& name)
{
    if (command == nullptr)
        return nullptr;
    for (const ParamSpec& param : command->params)
        if (name == QLatin1String(param.name))
            return &param;
    return nullptr;
}

struct Options
{
    QString host;
    QString target;
    QString tcp;
    bool json = false;
    int maxHz = 20;
    QStringList words;
};

void PrintNode(const QJsonObject& node, int depth)
{
    const QString indent(depth * 2, ' ');
    QString line = indent + node.value("id").toString();
    const QStringList interfaces = node.value("interfaces").toVariant().toStringList();
    if (!interfaces.isEmpty())
        line += QStringLiteral("  [%1]").arg(interfaces.join(", "));
    if (node.contains("instance"))
        line += QStringLiteral("  instance %1").arg(node.value("instance").toObject().value("id").toString());
    Out() << line << '\n';
    const QJsonObject state = node.value("state").toObject();
    for (auto it = state.begin(); it != state.end(); ++it)
        Out() << indent << "    " << it.key() << ": "
              << QString::fromUtf8(QJsonDocument(QJsonArray{it.value()}).toJson(QJsonDocument::Compact)).mid(1).chopped(1)
              << '\n';
    for (const char* key : {"windows", "views"})
        for (const QJsonValue& child : node.value(key).toArray())
            PrintNode(child.toObject(), depth + 1);
}

/// The client of the host the command goes to; null with a message printed when there is none
std::unique_ptr<Client> Open(const Options& options, const QString& method, const QString& target)
{
    auto client = std::make_unique<Client>();
    QString error;
    if (!options.tcp.isEmpty())
    {
        const qsizetype colon = options.tcp.lastIndexOf(':');
        if (colon <= 0 || !client->ConnectTcp(options.tcp.left(colon), options.tcp.mid(colon + 1).toUShort(), 3000,
                                              &error))
        {
            Err() << "unreal-gui: " << options.tcp << ": " << (error.isEmpty() ? QStringLiteral("not ADDRESS:PORT") : error)
                  << '\n';
            return nullptr;
        }
        return client;
    }

    const std::vector<discovery::Entry> entries = discovery::List();
    if (entries.empty())
    {
        Err() << "unreal-gui: no GUI program is running (" << discovery::Directory() << ")\n";
        return nullptr;
    }
    QString hostId = options.host;
    if (hostId.isEmpty())
    {
        const qsizetype slash = target.indexOf('/');
        const QString first = slash < 0 ? target : target.left(slash);
        for (const discovery::Entry& entry : entries)
            if (entry.id == first)
                hostId = first;
    }
    if (hostId.isEmpty() && entries.size() > 1 && !method.isEmpty())
    {
        // Every host says what the target names there; the one that took focus last wins (design §4.5)
        std::vector<HostChoice> choices;
        for (const discovery::Entry& entry : entries)
        {
            Client probe;
            if (!probe.Connect(entry.socket, 3000))
                continue;
            choices.push_back({entry, probe.Call(QStringLiteral("resolve"), {},
                                                 QJsonObject{{"method", method}, {"target", target}})});
        }
        size_t chosen = 0;
        const Result choice = ChooseHost(choices, chosen);
        if (!choice.ok)
        {
            Err() << "unreal-gui: " << choice.message << '\n';
            if (choice.body.contains("candidates"))
                Err() << Json(choice.body.value("candidates")) << '\n';
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
                Err() << "unreal-gui: host " << hostId << ": " << error << '\n';
                return nullptr;
            }
            return client;
        }
    Err() << "unreal-gui: no host " << hostId << '\n';
    return nullptr;
}

int Hosts(const Options& options)
{
    const std::vector<discovery::Entry> entries = discovery::List();
    if (options.json)
    {
        QJsonArray array;
        for (const discovery::Entry& entry : entries)
            array.append(entry.ToJson());
        Out() << Json(array) << '\n';
        return 0;
    }
    for (const discovery::Entry& entry : entries)
    {
        Out() << entry.id << "  " << entry.program << ' ' << entry.version << "  " << entry.socket;
        if (entry.tcpPort > 0)
            Out() << "  tcp " << entry.tcpPort;
        Out() << '\n';
    }
    if (entries.empty())
        Out() << "no GUI program is running\n";
    return 0;
}

int List(const Options& options)
{
    std::vector<std::unique_ptr<Client>> clients;
    if (!options.host.isEmpty() || !options.tcp.isEmpty())
    {
        auto client = Open(options, {}, {});
        if (!client)
            return 1;
        clients.push_back(std::move(client));
    }
    else
        for (const discovery::Entry& entry : discovery::List())
        {
            auto client = std::make_unique<Client>();
            if (client->Connect(entry.socket, 3000))
                clients.push_back(std::move(client));
        }
    QJsonArray hosts;
    for (const std::unique_ptr<Client>& client : clients)
    {
        const Result result = client->Call(QStringLiteral("list"));
        if (!result.ok)
            continue;
        hosts.append(result.body);
        if (options.json)
            continue;
        Out() << result.body.value("host").toString() << "  " << result.body.value("program").toString() << ' '
              << result.body.value("version").toString() << '\n';
        QJsonObject root = result.body;
        root["id"] = QStringLiteral("(host)");
        PrintNode(root, 1);
    }
    if (options.json)
        Out() << Json(hosts) << '\n';
    return 0;
}

int Watch(const Options& options)
{
    QStringList topics = options.words.mid(1);
    if (topics.isEmpty())
        topics = {QStringLiteral("state"), QStringLiteral("nodes"), QStringLiteral("focus"),
                  QStringLiteral("activity")};
    auto client = Open(options, {}, {});
    if (!client)
        return 1;
    if (!client->Subscribe(topics, options.maxHz))
    {
        Err() << "unreal-gui: the host refused the topics " << topics.join(", ") << '\n';
        return 1;
    }
    while (client->IsConnected())
        if (const std::optional<QJsonObject> event = client->NextEvent(1000))
        {
            Out() << QString::fromUtf8(QJsonDocument(*event).toJson(QJsonDocument::Compact)) << '\n';
            Out().flush();
        }
    return 0;
}

int Run(const Options& options)
{
    const QString method = options.words.front();
    const CommandSpec* command = FindCommand(method);
    QJsonObject params;
    int position = 0;
    for (const QString& word : options.words.mid(1))
    {
        const qsizetype equals = word.indexOf('=');
        if (equals > 0)
        {
            const QString key = word.left(equals);
            params[key] = Typed(word.mid(equals + 1), FindParam(command, key));
            continue;
        }
        if (command == nullptr || position >= static_cast<int>(command->params.size()))
        {
            Err() << "unreal-gui: " << method << " takes no value " << word << " (use KEY=VALUE)\n";
            return 2;
        }
        const ParamSpec& spec = command->params[static_cast<size_t>(position++)];
        params[QLatin1String(spec.name)] = Typed(word, &spec);
    }

    auto client = Open(options, method, options.target);
    if (!client)
        return 1;
    const Result result = client->Call(method, options.target, params);
    if (options.json)
        Out() << Json(result.ToJson()) << '\n';
    else if (result.ok)
    {
        if (!result.body.isEmpty())
            Out() << Json(result.body) << '\n';
    }
    else
    {
        Err() << "unreal-gui: " << method << ": " << result.error << ": " << result.message << '\n';
        if (result.body.contains("candidates"))
            Err() << Json(result.body.value("candidates")) << '\n';
    }
    return result.ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Options options;
    const QStringList arguments = app.arguments().mid(1);
    for (qsizetype i = 0; i < arguments.size(); ++i)
    {
        const QString& argument = arguments[i];
        const auto value = [&](QString& into) {
            if (i + 1 >= arguments.size())
                return false;
            into = arguments[++i];
            return true;
        };
        if (argument == QLatin1String("--host"))
        {
            if (!value(options.host))
                return Usage();
        }
        else if (argument == QLatin1String("--target"))
        {
            if (!value(options.target))
                return Usage();
        }
        else if (argument == QLatin1String("--tcp"))
        {
            if (!value(options.tcp))
                return Usage();
        }
        else if (argument == QLatin1String("--max-hz"))
        {
            QString text;
            if (!value(text))
                return Usage();
            options.maxHz = text.toInt();
        }
        else if (argument == QLatin1String("--json"))
            options.json = true;
        else if (argument == QLatin1String("--help") || argument == QLatin1String("-h"))
            return Usage();
        else
            options.words.append(argument);
    }
    if (options.words.isEmpty())
        return Usage();

    int code;
    const QString verb = options.words.front();
    if (verb == QLatin1String("hosts"))
        code = Hosts(options);
    else if (verb == QLatin1String("list"))
        code = List(options);
    else if (verb == QLatin1String("watch"))
        code = Watch(options);
    else
        code = Run(options);
    Out().flush();
    Err().flush();
    return code;
}
