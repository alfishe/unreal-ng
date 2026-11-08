#include "mcp-gui.h"

#include "mcp-tool-utils.h"

#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace mcp
{

namespace
{
const char* const kUnreachable = "WebAPI unreachable — is the emulator running with WebAPI enabled (port 8090)?";
const char* const kTargetHelp =
    "The node: a path from gui_list (main/screen, debugger/memory), an interface (memory: the one focused last), or "
    "interface@instance (the view showing that emulator). Absent: the only node with the command, else the one "
    "focused last";

std::string Pretty(const Json::Value& value)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    return Json::writeString(builder, value);
}

/// The plane's result as a tool's: the body as text, or "error: message" (with the candidates of an ambiguous one);
/// the result itself as structured content, a refusal's too
void Answer(const std::string& what, const ToolCallback& done, int status, Json::Value reply)
{
    if (status == 0)
    {
        done(ToolResult::Error(kUnreachable));
        return;
    }
    if (!reply.isObject() || !reply.get("ok", false).asBool())
    {
        std::string text = what + ": " + reply.get("error", "error").asString() + ": " +
                           reply.get("message", DescribeErrorBody(reply)).asString();
        if (reply["body"].isMember("candidates"))
            text += "\ncandidates:\n" + Pretty(reply["body"]["candidates"]);
        ToolResult result = ToolResult::Error(text);
        if (reply.isObject())
            result.structured = std::move(reply);  // the error code for a client that reads it
        done(std::move(result));
        return;
    }
    const Json::Value& body = reply["body"];
    const std::string text = body.isObject() && !body.empty() ? Pretty(body) : what + ": ok";
    done(ToolResult::Ok(text, std::move(reply)));
}

/// A parameter's JSON Schema type by its kind; an address is text (0x6000, #6000, $6000) or a number
Json::Value TypeOf(const std::string& kind)
{
    Json::Value property(Json::objectValue);
    if (kind == "int")
        property["type"] = "integer";
    else if (kind == "bool")
        property["type"] = "boolean";
    else if (kind == "address")
    {
        property["type"] = Json::arrayValue;
        property["type"].append("string");
        property["type"].append("integer");
    }
    else
        property["type"] = "string";
    return property;
}

void RegisterInterface(ToolRegistry& registry, const Json::Value& spec)
{
    const std::string name = spec["name"].asString();
    const std::string prefix = name + ".";
    Json::Value schema(Json::objectValue);
    schema["type"] = "object";
    Json::Value& properties = schema["properties"];
    Json::Value actions(Json::arrayValue);
    std::string commands;
    // Each parameter once, its description naming the commands that take it
    std::map<std::string, std::string> kinds;
    std::map<std::string, std::string> uses;
    std::vector<std::string> order;
    for (const Json::Value& command : spec["commands"])
    {
        const std::string method = command["method"].asString();
        const std::string action = method.rfind(prefix, 0) == 0 ? method.substr(prefix.size()) : method;
        actions.append(action);
        commands += "\n- " + action + ": " + command["description"].asString();
        for (const Json::Value& param : command["params"])
        {
            const std::string paramName = param["name"].asString();
            const std::string kind = param["kind"].asString();
            if (!kinds.count(paramName))
            {
                kinds[paramName] = kind;
                order.push_back(paramName);
            }
            else if (kinds[paramName] != kind)
                kinds[paramName] = "string";
            uses[paramName] += (uses[paramName].empty() ? "" : "; ") + action + ": " +
                               param["description"].asString() + (param["optional"].asBool() ? " (optional)" : "");
        }
        if (!command["params"].empty())
        {
            commands += " (";
            bool first = true;
            for (const Json::Value& param : command["params"])
            {
                commands += (first ? "" : ", ") + param["name"].asString() + (param["optional"].asBool() ? "?" : "");
                first = false;
            }
            commands += ")";
        }
    }
    properties["action"]["type"] = "string";
    properties["action"]["enum"] = actions;
    properties["action"]["description"] = "The command";
    properties["target"]["type"] = "string";
    properties["target"]["description"] = kTargetHelp;
    for (const std::string& paramName : order)
    {
        Json::Value property = TypeOf(kinds[paramName]);
        property["description"] = uses[paramName];
        properties[paramName] = property;
    }
    schema["required"].append("action");

    const std::string tool = "gui_" + name;
    std::string description = spec["description"].asString();
    if (!spec["state"].empty())
    {
        description += " State (gui_state):";
        for (const Json::Value& field : spec["state"])
            description += " " + field.asString();
        description += ".";
    }
    description += " Commands:" + commands;
    registry.Register(
        tool, description, std::move(schema),
        [name, tool, actions](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();
            bool known = false;
            std::string offered;
            for (const Json::Value& each : actions)
            {
                known = known || each.asString() == action;
                offered += (offered.empty() ? "" : ", ") + each.asString();
            }
            if (!known)
            {
                done(ToolResult::Error(tool + ": action is one of " + offered));
                return;
            }
            const std::string method = name + "." + action;
            auto body = std::make_shared<Json::Value>(Json::objectValue);
            for (const std::string& member : args.getMemberNames())
                if (member != "action" && member != kResponseFormatArg)
                    (*body)[member] = args[member];
            caller.Call("POST", "/api/v1/gui/" + UrlEncodeSegment(method), body.get(),
                        [body, method, done](int status, Json::Value reply) {
                            Answer(method, done, status, std::move(reply));
                        });
        });
}
}  // namespace

void RegisterGui(ToolRegistry& registry, const std::string& catalogueJson)
{
    if (catalogueJson.empty())
        return;
    Json::Value catalogue;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream in(catalogueJson);
    if (!Json::parseFromStream(builder, in, &catalogue, &errors) || !catalogue["interfaces"].isArray())
        return;
    for (const Json::Value& spec : catalogue["interfaces"])
        if (spec["name"].isString() && spec["commands"].isArray() && !spec["commands"].empty())
            RegisterInterface(registry, spec);

    Json::Value listSchema(Json::objectValue);
    listSchema["type"] = "object";
    listSchema["properties"] = Json::objectValue;
    registry.Register(
        "gui_list",
        "The GUI programs on this machine (unreal-qt, unreal-debugger, the screen viewer, the video wall): each "
        "one's windows and views, their interfaces, commands, state and the machine they show, which took focus "
        "last. The gui_<interface> tools drive them: what the user sees and does in the windows (scroll a memory "
        "view, zoom the screen, focus a tile). The emulated machine itself is the other tools'.",
        std::move(listSchema), [](const Json::Value&, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            caller.Call("GET", "/api/v1/gui", nullptr,
                        [done](int status, Json::Value reply) { Answer("gui_list", done, status, std::move(reply)); });
        });

    Json::Value stateSchema(Json::objectValue);
    stateSchema["type"] = "object";
    stateSchema["properties"]["target"]["type"] = "string";
    stateSchema["properties"]["target"]["description"] = kTargetHelp;
    stateSchema["required"].append("target");
    registry.Register(
        "gui_state",
        "One window's or view's state (a view's top address, cursor, selection; a window's geometry; a screen's "
        "zoom), its interfaces and the machine it shows.",
        std::move(stateSchema), [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string target = args["target"].asString();
            if (target.empty())
            {
                done(ToolResult::Error("gui_state: target is required (gui_list shows the nodes)"));
                return;
            }
            caller.Call("GET", "/api/v1/gui/state?target=" + UrlEncodeSegment(target), nullptr,
                        [done](int status, Json::Value reply) { Answer("gui_state", done, status, std::move(reply)); });
        });
}

} // namespace mcp
