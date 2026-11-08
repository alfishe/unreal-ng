// unreal-gui mcp: the GUI control plane's thin MCP server (design §4.9, owner decision 2026-11-07). Stdio,
// newline-delimited JSON-RPC 2.0; three tools over the same protocol as the CLI: gui_list, gui_call, gui_state. It
// adds no verbs of its own and reads nothing of the machine: the emulator has its own MCP server.

#include "mcp.h"

#include "guicontrol/route.h"
#include "guicontrol/interfaces.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <iostream>
#include <string>

namespace guicontrol::cli
{

namespace
{

constexpr const char* kLatestProtocol = "2025-06-18";

QJsonObject Schema(QJsonObject properties, QStringList required)
{
    QJsonObject schema{{"type", "object"}, {"properties", properties}, {"additionalProperties", false}};
    if (!required.isEmpty())
        schema["required"] = QJsonArray::fromStringList(required);
    return schema;
}

QJsonArray Tools()
{
    const QJsonObject host{{"type", "string"}, {"description", "a host id from gui_list; absent: the one the target "
                                                               "resolves on (the one focused last)"}};
    QString commands;
    for (const InterfaceSpec& spec : Interfaces())
        for (const CommandSpec& command : spec.commands)
        {
            commands += QStringLiteral("\n- %1").arg(QLatin1String(command.method));
            QStringList names;
            for (const ParamSpec& param : command.params)
                names << QStringLiteral("%1%2").arg(QLatin1String(param.name), param.optional ? "?" : "");
            if (!names.isEmpty())
                commands += QStringLiteral(" {%1}").arg(names.join(", "));
            commands += QStringLiteral(": %1").arg(QLatin1String(command.description));
        }
    return {
        QJsonObject{{"name", "gui_list"},
                    {"title", "List the GUI programs"},
                    {"description",
                     "The running GUI programs of unreal-ng (unreal-qt, the debuggers, the video wall): each host with "
                     "its windows and views, their interfaces, commands, state, and the emulator instance a view "
                     "shows (endpoint + id: read the machine through the emulator's own MCP or WebAPI)."},
                    {"inputSchema", Schema({{"host", host}}, {})}},
        QJsonObject{{"name", "gui_call"},
                    {"title", "Run a GUI command"},
                    {"description",
                     QStringLiteral("Run a command on a GUI program's window or view. target: a path (debugger, "
                                    "debugger/memory), an interface (memory: the focused view of it), or "
                                    "interface@instance (the view showing that emulator). Ambiguous targets list the "
                                    "candidates. Commands:%1")
                         .arg(commands)},
                    {"inputSchema",
                     Schema({{"method", QJsonObject{{"type", "string"}, {"description", "memory.goto, window.raise, ..."}}},
                             {"target", QJsonObject{{"type", "string"}, {"description", "absent: the one node with the command"}}},
                             {"params", QJsonObject{{"type", "object"}, {"description", "the command's parameters"}}},
                             {"host", host}},
                            {"method"})}},
        QJsonObject{{"name", "gui_state"},
                    {"title", "State of a window or view"},
                    {"description", "One node's state (a view's top address, cursor, selection; a window's geometry), "
                                    "its interfaces and the machine it shows."},
                    {"inputSchema",
                     Schema({{"target", QJsonObject{{"type", "string"}, {"description", "as for gui_call"}}}, {"host", host}},
                            {"target"})}},
    };
}

QString Text(const QJsonValue& value)
{
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented));
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented));
    return value.toVariant().toString();
}

/// A tool's answer: the text an agent reads, the same as structured content
QJsonObject ToolResult(const Result& result)
{
    QString text;
    if (result.ok)
        text = result.body.isEmpty() ? QStringLiteral("ok") : Text(result.body);
    else
    {
        text = QStringLiteral("%1: %2").arg(result.error, result.message);
        if (result.body.contains("candidates"))
            text += QStringLiteral("\ncandidates:\n%1").arg(Text(result.body.value("candidates")));
    }
    return QJsonObject{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}}},
                       {"structuredContent", result.ToJson()},
                       {"isError", !result.ok}};
}

Result CallTool(const QString& name, const QJsonObject& arguments)
{
    const route::Where where{arguments.value("host").toString(), {}};
    if (name == QLatin1String("gui_list"))
    {
        QJsonArray hosts;
        const Result result = route::ListAll(where, hosts);
        if (!result.ok)
            return result;
        return Result::Success(QJsonObject{{"hosts", hosts}});
    }
    if (name == QLatin1String("gui_call") || name == QLatin1String("gui_state"))
    {
        const bool call = name == QLatin1String("gui_call");
        const QString method = call ? arguments.value("method").toString() : QString();
        const QString target = arguments.value("target").toString();
        if (call && method.isEmpty())
            return Result::Failure(errors::kBadRequest, QStringLiteral("method: a command, as gui_list shows"));
        if (!call && target.isEmpty())
            return Result::Failure(errors::kBadRequest, QStringLiteral("target: a node, as gui_list shows"));
        Result failure;
        std::unique_ptr<Client> client = route::Open(where, method, target, failure);
        if (!client)
            return failure;
        return call ? client->Call(method, target, arguments.value("params").toObject())
                    : client->Call(QStringLiteral("state"), {}, QJsonObject{{"target", target}});
    }
    return Result::Failure(errors::kNotFound, QStringLiteral("no tool %1").arg(name));
}

void Send(const QJsonObject& message)
{
    std::cout << QJsonDocument(message).toJson(QJsonDocument::Compact).toStdString() << '\n' << std::flush;
}

QJsonObject Reply(const QJsonValue& id, const QJsonObject& result)
{
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

QJsonObject Fail(const QJsonValue& id, int code, const QString& message)
{
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

}  // namespace

int RunMcp(const QString& version)
{
    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.empty())
            continue;
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(line), &parseError);
        if (!document.isObject())
        {
            Send(Fail(QJsonValue::Null, -32700, QStringLiteral("parse error: %1").arg(parseError.errorString())));
            continue;
        }
        const QJsonObject message = document.object();
        const QString method = message.value("method").toString();
        const bool request = message.contains("id");
        const QJsonValue id = message.value("id");
        const QJsonObject params = message.value("params").toObject();
        if (!request)
            continue;  // notifications (initialized, cancelled): nothing to answer
        if (method == QLatin1String("initialize"))
        {
            const QString asked = params.value("protocolVersion").toString();
            Send(Reply(id, QJsonObject{{"protocolVersion", asked.isEmpty() ? QString(kLatestProtocol) : asked},
                                       {"capabilities", QJsonObject{{"tools", QJsonObject{}}}},
                                       {"serverInfo", QJsonObject{{"name", "unreal-gui"}, {"version", version}}},
                                       {"instructions",
                                        "Drives the windows and views of the running unreal-ng GUI programs. "
                                        "Start with gui_list. The emulated machine itself is read and controlled "
                                        "through the emulator's own MCP server."}}));
        }
        else if (method == QLatin1String("ping"))
            Send(Reply(id, {}));
        else if (method == QLatin1String("tools/list"))
            Send(Reply(id, QJsonObject{{"tools", Tools()}}));
        else if (method == QLatin1String("tools/call"))
            Send(Reply(id, ToolResult(CallTool(params.value("name").toString(), params.value("arguments").toObject()))));
        else
            Send(Fail(id, -32601, QStringLiteral("no method %1").arg(method)));
    }
    return 0;
}

}  // namespace guicontrol::cli
