// unreal-gui: the GUI control plane's command line (docs/inprogress/2026-11-07-gui-control-plane §4.9).
//
//   unreal-gui hosts                                  the running GUI programs (the discovery files)
//   unreal-gui list [--host ID]                       their windows and views, with state and commands
//   unreal-gui [--host ID] [--target T] METHOD [VALUE...] [KEY=VALUE...]
//                                                     run a command; values fill the declared parameters in order
//   unreal-gui state TARGET                           one window's or view's state, interfaces and machine
//   unreal-gui watch [TOPIC...] [--max-hz N]          print the pushes (state, nodes, focus, activity) until killed
//   unreal-gui mcp                                    the plane's MCP server on stdio (gui_list, gui_call, gui_state)
//   --tcp ADDRESS:PORT                                a host's TCP listener instead of the discovery directory
//   --json                                            the answer as JSON
//
// Exit codes: 0 done, 1 refused or failed, 2 wrong usage.

#include "connect.h"
#include "guicontrol/client.h"
#include "guicontrol/discovery.h"
#include "guicontrol/interfaces.h"
#include "mcp.h"

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
             "       unreal-gui state TARGET\n"
             "       unreal-gui watch [TOPIC...] [--max-hz N]\n"
             "       unreal-gui mcp\n"
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

void PrintFailure(const QString& what, const Result& failure)
{
    Err() << "unreal-gui: " << what << (what.isEmpty() ? "" : ": ") << failure.error << ": " << failure.message << '\n';
    if (failure.body.contains("candidates"))
        Err() << Json(failure.body.value("candidates")) << '\n';
}

/// The client of the host the command goes to; null with the reason printed when there is none
std::unique_ptr<Client> Open(const Options& options, const QString& method, const QString& target)
{
    Result failure;
    std::unique_ptr<Client> client = cli::Open(cli::Where{options.host, options.tcp}, method, target, failure);
    if (!client)
        PrintFailure({}, failure);
    return client;
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
    QJsonArray hosts;
    const Result result = cli::ListAll(cli::Where{options.host, options.tcp}, hosts);
    if (!result.ok)
    {
        PrintFailure({}, result);
        return 1;
    }
    if (options.json)
    {
        Out() << Json(hosts) << '\n';
        return 0;
    }
    for (const QJsonValue& value : hosts)
    {
        QJsonObject root = value.toObject();
        Out() << root.value("host").toString() << "  " << root.value("program").toString() << ' '
              << root.value("version").toString() << '\n';
        root["id"] = QStringLiteral("(host)");
        PrintNode(root, 1);
    }
    return 0;
}

int State(const Options& options)
{
    const QString target = options.words.size() > 1 ? options.words[1] : options.target;
    if (target.isEmpty())
        return Usage();
    auto client = Open(options, {}, target);
    if (!client)
        return 1;
    const Result result = client->Call(QStringLiteral("state"), {}, QJsonObject{{"target", target}});
    if (options.json)
        Out() << Json(result.ToJson()) << '\n';
    else if (result.ok)
        Out() << Json(result.body) << '\n';
    else
        PrintFailure(QStringLiteral("state"), result);
    return result.ok ? 0 : 1;
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
        PrintFailure(method, result);
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
    else if (verb == QLatin1String("state"))
        code = State(options);
    else if (verb == QLatin1String("watch"))
        code = Watch(options);
    else if (verb == QLatin1String("mcp"))
        code = cli::RunMcp(QStringLiteral(GUICONTROL_VERSION));
    else
        code = Run(options);
    Out().flush();
    Err().flush();
    return code;
}
