// MCP smart tool: manage_symbols
//
// Actions and their WebAPI mappings:
//   load_labels  → POST /symbols/load {path}
//   list         → GET  /labels
//   resolve      → GET  /labels/resolve?name= | address=
//   load_listing → POST /listing/load {path, clear?}
//   source_at    → GET  /listing/source_at?address=&context=
//   step_line    → POST /listing/step_line {max_tstates?}
//   run_to_line  → POST /listing/run_to_line {line, max_tstates?}
//
// Drogon-free; all calls go through the loopback IApiCaller.

#include "mcp-symbols.h"

#include "mcp-tool-utils.h"

namespace mcp
{

/// region <manage_symbols>

namespace
{

void RegisterManageSymbolsImpl(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"load_labels", "list", "resolve", "load_listing", "source_at", "step_line", "run_to_line"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "Symbol/source operation. Labels come from .sld/.lbl symbol files; listings from sjasmplus .lst files.";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["path"]["type"] = "string";
    schema["properties"]["path"]["description"] = "File path for load_labels / load_listing";
    schema["properties"]["name"]["type"] = "string";
    schema["properties"]["name"]["description"] = "Label name for resolve (exactly one of name/address)";
    schema["properties"]["address"]["type"] = "string";
    schema["properties"]["address"]["description"] = "Address for resolve / source_at — integer or \"0x…\" hex string";
    schema["properties"]["clear"]["type"] = "boolean";
    schema["properties"]["clear"]["default"] = false;
    schema["properties"]["clear"]["description"] = "Replace a previously loaded listing instead of merging";
    schema["properties"]["context"]["type"] = "integer";
    schema["properties"]["context"]["description"] = "Surrounding source lines for source_at (default 3)";
    schema["properties"]["line"]["type"] = "integer";
    schema["properties"]["line"]["description"] = "Listing line number for run_to_line";
    schema["properties"]["max_tstates"]["type"] = "integer";
    schema["properties"]["max_tstates"]["description"] = "T-state budget for step_line (~2 s default) / run_to_line (~10 s default)";
    schema["required"].append("action");

    registry.Register(
        "manage_symbols",
        "Symbols and source-level debugging: load symbol files (.sld/.lbl) or sjasmplus .lst listings, list and resolve "
        "labels (name↔address), map an address to its source line, step to the next source line, or run until a source line.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            std::string action = args["action"].asString();

            if (action == "load_labels")
            {
                if (!args.isMember("path") || args["path"].asString().empty())
                {
                    done(ToolResult::Error("load_labels requires 'path'"));
                    return;
                }
                Json::Value body;
                body["path"] = args["path"].asString();
                ResolveAndForward(args, "POST", "/symbols/load", &body, caller, "Symbols loaded", done);
                return;
            }

            if (action == "list")
            {
                ResolveAndForward(args, "GET", "/labels", nullptr, caller, "Labels", done);
                return;
            }

            if (action == "resolve")
            {
                bool hasName = args.isMember("name") && args["name"].isString() && !args["name"].asString().empty();
                bool hasAddress = args.isMember("address") && !AddressArg(args["address"]).empty();
                if (hasName == hasAddress)
                {
                    done(ToolResult::Error("resolve requires exactly one of 'name' or 'address'"));
                    return;
                }
                std::string query = hasName ? "?name=" + args["name"].asString() : "?address=" + AddressArg(args["address"]);
                ResolveAndForward(args, "GET", "/labels/resolve" + query, nullptr, caller, "Label resolved", done);
                return;
            }

            if (action == "load_listing")
            {
                if (!args.isMember("path") || args["path"].asString().empty())
                {
                    done(ToolResult::Error("load_listing requires 'path'"));
                    return;
                }
                Json::Value body;
                body["path"] = args["path"].asString();
                if (args.isMember("clear"))
                {
                    body["clear"] = args["clear"].asBool();
                }
                ResolveAndForward(args, "POST", "/listing/load", &body, caller, "Listing loaded", done);
                return;
            }

            if (action == "source_at")
            {
                if (!args.isMember("address") || AddressArg(args["address"]).empty())
                {
                    done(ToolResult::Error("source_at requires 'address'"));
                    return;
                }
                std::string query = "?address=" + AddressArg(args["address"]);
                if (args.isMember("context"))
                {
                    query += "&context=" + std::to_string(args["context"].asUInt());
                }
                ResolveAndForward(args, "GET", "/listing/source_at" + query, nullptr, caller, "Source line", done);
                return;
            }

            if (action == "step_line")
            {
                Json::Value body;
                if (args.isMember("max_tstates"))
                {
                    body["max_tstates"] = args["max_tstates"].asUInt64();
                }
                ResolveAndForward(args, "POST", "/listing/step_line", body.isNull() ? nullptr : &body, caller,
                                  "Stepped to next source line", done);
                return;
            }

            if (action == "run_to_line")
            {
                if (!args.isMember("line"))
                {
                    done(ToolResult::Error("run_to_line requires 'line'"));
                    return;
                }
                Json::Value body;
                body["line"] = args["line"].asUInt();
                if (args.isMember("max_tstates"))
                {
                    body["max_tstates"] = args["max_tstates"].asUInt64();
                }
                ResolveAndForward(args, "POST", "/listing/run_to_line", &body, caller, "Ran to source line", done);
                return;
            }

            done(ToolResult::Error("Unknown action '" + action +
                                            "'. Valid: load_labels, list, resolve, load_listing, source_at, step_line, run_to_line"));
        });
}

} // namespace

void RegisterManageSymbols(ToolRegistry& registry)
{
    RegisterManageSymbolsImpl(registry);
}

/// endregion </manage_symbols>

} // namespace mcp
