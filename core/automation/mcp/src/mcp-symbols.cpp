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
//   formats      → GET  /symbols/formats
//   detect       → GET  /symbols/detect?path=
//   sets         → GET  /symbols/sets
//   set_enable   → PUT  /symbols/sets {id, enabled?, priority?}
//   drop         → DELETE /symbols/sets?id=
//   import       → POST /symbols/import {path | data + name, format?, set?, space?, base?, policy?}
//   export       → POST /symbols/export {path, format?, sets?, pages?}
//   scan         → GET  /symbols/scan
//   import_live  → POST /symbols/import/live {scanner?, page?, offset?, set?, policy?}
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
    for (const char* action : {"load_labels", "list", "resolve", "load_listing", "source_at", "step_line", "run_to_line", "formats",
                               "detect", "sets", "set_enable", "drop", "import", "export", "scan", "import_live"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "Symbol/source operation. Labels come from symbol files in any format (formats lists them); listings from "
        "sjasmplus .lst files. import / export / sets / set_enable / drop work on the symbol sets: user (labels set by "
        "hand, wins), one per loaded file (a later load wins), named sets.";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["path"]["type"] = "string";
    schema["properties"]["path"]["description"] = "File path for load_labels / load_listing / detect / import / export";
    schema["properties"]["data"]["type"] = "string";
    schema["properties"]["data"]["description"] = "import: the symbol file itself as base64 instead of a path (a client on another host)";
    schema["properties"]["format"]["type"] = "string";
    schema["properties"]["format"]["description"] = "import / export: a symbol format id (formats); default: by the extension, else detected";
    schema["properties"]["set"]["type"] = "string";
    schema["properties"]["set"]["description"] = "import: merge into this set (made when missing) instead of the file's own set";
    schema["properties"]["space"]["type"] = "string";
    schema["properties"]["space"]["description"] = "import: records without a page go here; export: only symbols of this space (cpu:main, rom0, ram3, cache0, const, port)";
    schema["properties"]["base"]["type"] = "string";
    schema["properties"]["base"]["description"] = "import: added to every offset (decimal, 0x, #, $)";
    schema["properties"]["policy"]["type"] = "string";
    schema["properties"]["policy"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* policy : {"both", "keep", "replace", "fail"})
        schema["properties"]["policy"]["enum"].append(policy);
    schema["properties"]["policy"]["description"] = "import into a set: both (second name = alias, default), keep, replace, fail";
    schema["properties"]["sets"]["type"] = "array";
    schema["properties"]["sets"]["items"]["type"] = "string";
    schema["properties"]["sets"]["description"] = "export: only these set ids (default: every enabled set)";
    schema["properties"]["from"]["type"] = "string";
    schema["properties"]["from"]["description"] = "export: only CPU addresses from this one (decimal, 0x, #, $)";
    schema["properties"]["to"]["type"] = "string";
    schema["properties"]["to"]["description"] = "export: only CPU addresses up to this one";
    schema["properties"]["kinds"]["type"] = "string";
    schema["properties"]["kinds"]["description"] = "export: only these kinds, a comma list (code, data, const, entry ... or a file's own type word)";
    schema["properties"]["pages"]["type"] = "string";
    schema["properties"]["pages"]["description"] = "export: page symbols in a format without pages: fold (default), comment, drop";
    schema["properties"]["scanner"]["type"] = "string";
    schema["properties"]["scanner"]["description"] = "import_live: alasm-table or xas-table (default: the best candidate of scan)";
    schema["properties"]["page"]["type"] = "integer";
    schema["properties"]["page"]["description"] = "import_live: the RAM page of the table (scan lists them)";
    schema["properties"]["offset"]["type"] = "integer";
    schema["properties"]["offset"]["description"] = "import_live: the table's first byte in the page";
    schema["properties"]["id"]["type"] = "string";
    schema["properties"]["id"]["description"] = "set_enable / drop: the set id (sets lists them)";
    schema["properties"]["enabled"]["type"] = "boolean";
    schema["properties"]["enabled"]["description"] = "set_enable: show the set's labels";
    schema["properties"]["priority"]["type"] = "integer";
    schema["properties"]["priority"]["description"] = "set_enable: higher wins (user 1000000, files 100 and up)";
    schema["properties"]["name"]["type"] = "string";
    schema["properties"]["name"]["description"] =
        "resolve: the label name (exactly one of name/address); export: a name pattern (* any run, ? one character); import with "
        "data: the file's name";
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

            if (action == "scan")
            {
                ResolveAndForward(args, "GET", "/symbols/scan", nullptr, caller, "Label tables in RAM", done);
                return;
            }

            if (action == "import_live")
            {
                Json::Value body(Json::objectValue);
                for (const char* name : {"scanner", "set", "policy"})
                    if (args.isMember(name) && !args[name].isNull())
                        body[name] = args[name].asString();
                for (const char* name : {"page", "offset"})
                    if (args.isMember(name) && !args[name].isNull())
                        body[name] = args[name].asString();
                ResolveAndForward(args, "POST", "/symbols/import/live", &body, caller, "Label table imported", done);
                return;
            }

            if (action == "formats" || action == "sets")
            {
                ResolveAndForward(args, "GET", "/symbols/" + action, nullptr, caller, action == "formats" ? "Symbol formats" : "Symbol sets",
                                  done);
                return;
            }

            if (action == "detect")
            {
                if (!args.isMember("path") || args["path"].asString().empty())
                {
                    done(ToolResult::Error("detect requires 'path'"));
                    return;
                }
                ResolveAndForward(args, "GET", "/symbols/detect?path=" + UrlEncodeSegment(args["path"].asString()), nullptr, caller,
                                  "Symbol format", done);
                return;
            }

            if (action == "set_enable" || action == "drop")
            {
                if (!args.isMember("id") || args["id"].asString().empty())
                {
                    done(ToolResult::Error(action + " requires 'id' (the set id from sets)"));
                    return;
                }
                if (action == "drop")
                {
                    ResolveAndForward(args, "DELETE", "/symbols/sets?id=" + UrlEncodeSegment(args["id"].asString()), nullptr, caller,
                                      "Symbol set dropped", done);
                    return;
                }
                Json::Value body;
                body["id"] = args["id"].asString();
                if (args.isMember("enabled"))
                    body["enabled"] = args["enabled"].asBool();
                if (args.isMember("priority"))
                    body["priority"] = args["priority"].asInt();
                ResolveAndForward(args, "PUT", "/symbols/sets", &body, caller, "Symbol set changed", done);
                return;
            }

            if (action == "import" || action == "export")
            {
                const bool upload = action == "import" && args.isMember("data") && !args["data"].asString().empty();
                if (!upload && (!args.isMember("path") || args["path"].asString().empty()))
                {
                    done(ToolResult::Error(action + (action == "import" ? " requires 'path' or 'data'" : " requires 'path'")));
                    return;
                }
                Json::Value body;
                if (!upload)
                    body["path"] = args["path"].asString();
                const std::vector<const char*> names = action == "import" ? std::vector<const char*>{"data", "name", "format", "set", "space", "base", "policy"}
                                                                         : std::vector<const char*>{"format", "pages", "space", "from", "to", "kinds", "name"};
                for (const char* name : names)
                    if (args.isMember(name) && !args[name].isNull())
                        body[name] = args[name].asString();
                if (action == "export" && args.isMember("sets"))
                    body["sets"] = args["sets"];
                ResolveAndForward(args, "POST", "/symbols/" + action, &body, caller,
                                  action == "import" ? "Symbols imported" : "Symbols exported", done);
                return;
            }

            done(ToolResult::Error("Unknown action '" + action +
                                   "'. Valid: load_labels, list, resolve, load_listing, source_at, step_line, run_to_line, formats, "
                                   "detect, sets, set_enable, drop, import, export, scan, import_live"));
        });
}

} // namespace

void RegisterManageSymbols(ToolRegistry& registry)
{
    RegisterManageSymbolsImpl(registry);
}

/// endregion </manage_symbols>

} // namespace mcp
