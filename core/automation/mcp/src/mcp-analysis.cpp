// MCP smart tools: debug_code + analyze_performance
//
// debug_code actions and their WebAPI mappings:
//   disassemble → GET  /disasm?address=&count=
//   assemble    → POST /assemble {code, address?, write}
//   find_bytes  → POST /memory/find {pattern_hex, space?, mask_hex?, start?, end?, max?, alignment?}
//   trace       → POST /profiler/calltrace/start → run_frames → stop (finalizes hot→cold) → GET entries
//
// analyze_performance actions:
//   coverage_start/stop/read/gaps/clear → /coverage* endpoints
//   frame_cost                          → GET /frame_cost
//   vdac2_line_budget                   → GET /vdac2/metrics[?lines=1&in_flight=1] (FT812 line metrics)
//   vdac2_line_budget_set               → PUT /vdac2/metrics {margin?, measure_always?}
//   profile_start/stop/status           → unified profiler control
//   profile_report                      → fan-out: opcode counters + memory status
//                                        + calltrace entries + unified status
//   porttrace                           → start → run_frames → stop → events
//   gs_porttrace                        → General Sound coprocessor triage:
//                                        start → run_frames → stop → counters+events
//
// Drogon-free; all calls go through the loopback IApiCaller.

#include "mcp-analysis.h"

#include "mcp-tool-utils.h"

namespace mcp
{

/// region <debug_code>

namespace
{

void RegisterDebugCodeImpl(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"disassemble", "assemble", "find_bytes", "trace"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "Code operation: read disassembly, assemble Z80 source into memory, search for byte patterns, capture a call trace.";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["address"]["type"] = "string";
    schema["properties"]["address"]["description"] =
        "Start address for disassemble/assemble and window start for find_bytes — integer or \"0x…\" hex string "
        "(disassemble defaults to the current PC)";
    schema["properties"]["count"]["type"] = "integer";
    schema["properties"]["count"]["default"] = 16;
    schema["properties"]["count"]["description"] = "Instruction count for disassemble (1-256)";
    schema["properties"]["code"]["type"] = "string";
    schema["properties"]["code"]["description"] = "Z80 assembly source for assemble (two-pass, labels, $ and 0x literals)";
    schema["properties"]["write"]["type"] = "boolean";
    schema["properties"]["write"]["default"] = true;
    schema["properties"]["write"]["description"] = "Write assembled bytes into emulator memory at 'address'";
    schema["properties"]["pattern_hex"]["type"] = "string";
    schema["properties"]["pattern_hex"]["description"] =
        "Hex byte pattern for find_bytes, e.g. \"CD 16 00\"; \"??\" = any byte, \"A?\" = any low nibble (alias: pattern)";
    schema["properties"]["space"]["type"] = "string";
    schema["properties"]["space"]["description"] =
        "find_bytes address space: \"cpu\" (default, what is paged in now), \"ram\" (every RAM page; matches as page + "
        "offset) or one page (\"ram5\", \"rom2\", \"cache0\"; start / end are offsets in it)";
    schema["properties"]["mask_hex"]["type"] = "string";
    schema["properties"]["mask_hex"]["description"] =
        "find_bytes mask as hex bytes, as long as the pattern: 1 bits must match (replaces the ?? wildcards)";
    schema["properties"]["start"]["type"] = "string";
    schema["properties"]["start"]["description"] = "find_bytes window start (integer or hex string; default 0)";
    schema["properties"]["end"]["type"] = "string";
    schema["properties"]["end"]["description"] = "find_bytes window end (integer or hex string; default 0xFFFF)";
    schema["properties"]["max"]["type"] = "integer";
    schema["properties"]["max"]["description"] = "Maximum matches for find_bytes";
    schema["properties"]["alignment"]["type"] = "integer";
    schema["properties"]["alignment"]["description"] = "Match alignment for find_bytes (default 1)";
    schema["properties"]["frames"]["type"] = "integer";
    schema["properties"]["frames"]["default"] = 10;
    schema["properties"]["frames"]["description"] = "Emulated frames to run while tracing (1-1000)";
    schema["properties"]["limit"]["type"] = "integer";
    schema["properties"]["limit"]["default"] = 64;
    schema["properties"]["limit"]["description"] = "Maximum trace entries to return";
    schema["required"].append("action");

    registry.Register(
        "debug_code",
        "Low-level code tools: disassemble memory at an address, assemble Z80 source directly into memory (two-pass with "
        "labels), scan a memory window for a byte pattern, and capture an execution call trace over N frames.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn& progress) {
            std::string action = args["action"].asString();

            if (action == "disassemble")
            {
                unsigned count = args.isMember("count") ? args["count"].asUInt() : 16u;
                if (count < 1) count = 1;
                if (count > 256) count = 256;
                std::string suffix = "/disasm?count=" + std::to_string(count);
                if (args.isMember("address") && !AddressArg(args["address"]).empty())
                {
                    suffix += "&address=" + AddressArg(args["address"]);
                }
                ResolveAndForward(args, "GET", suffix, nullptr, caller, "Disassembly", done);
                return;
            }

            if (action == "assemble")
            {
                if (!args.isMember("code") || args["code"].asString().empty())
                {
                    done(ToolResult::Error("assemble requires 'code'"));
                    return;
                }
                bool hasAddress = args.isMember("address") && !AddressArg(args["address"]).empty();

                TargetResolver::ResolveFromArgs(args, caller, [&args, hasAddress, &caller, done](bool ok, const std::string& idOrError) {
                    if (!ok)
                    {
                        done(ToolResult::Error(idOrError));
                        return;
                    }
                    auto body = std::make_shared<Json::Value>();
                    (*body)["code"] = args["code"].asString();
                    if (hasAddress)
                    {
                        (*body)["address"] = args["address"];
                    }
                    (*body)["write"] = args.isMember("write") ? args["write"].asBool() : true;

                    caller.Call("POST", Endpoint(idOrError, "/assemble"), body.get(),
                                [body, idOrError, done](int status, Json::Value response) {
                        if (status < 200 || status >= 300)
                        {
                            done(ToolResult::Error("Assembly request failed (HTTP " + std::to_string(status) + "): " +
                                                   DescribeErrorBody(response)));
                            return;
                        }
                        if (response.get("status", "").asString() == "error")
                        {
                            done(ToolResult::Error("Assembly failed: " + response.get("message", Json::Value("")).asString() +
                                                   " (line " + std::to_string(response.get("line", Json::Value(0)).asUInt()) + ")"));
                            return;
                        }
                        unsigned size = response.get("size", 0).asUInt();
                        std::ostringstream out;
                        out << "Assembled " << size << " byte(s)";
                        if (response.isMember("address"))
                        {
                            out << " at " << (response["address"].isString() ? response["address"].asString()
                                                                              : Hex16(response["address"].asUInt()));
                        }
                        if (response.get("written", false).asBool())
                        {
                            out << " (written to memory)";
                        }
                        out << " [target " << idOrError << "]";
                        done(ToolResult::Ok(out.str(), std::move(response)));
                    });
                });
                return;
            }

            if (action == "find_bytes")
            {
                std::string pattern = args.isMember("pattern_hex") && args["pattern_hex"].isString()
                                          ? args["pattern_hex"].asString()
                                          : (args.isMember("pattern") && args["pattern"].isString() ? args["pattern"].asString() : "");
                if (pattern.empty())
                {
                    done(ToolResult::Error("find_bytes requires 'pattern_hex' (hex bytes, e.g. \"CD 16 00\")"));
                    return;
                }

                TargetResolver::ResolveFromArgs(args, caller, [&args, pattern, &caller, done](bool ok, const std::string& idOrError) {
                    if (!ok)
                    {
                        done(ToolResult::Error(idOrError));
                        return;
                    }
                    auto body = std::make_shared<Json::Value>();
                    (*body)["pattern_hex"] = pattern;
                    for (const char* field : {"space", "mask_hex"})
                    {
                        if (args.isMember(field) && args[field].isString())
                            (*body)[field] = args[field];
                    }
                    for (const char* field : {"start", "end", "max", "alignment"})
                    {
                        if (args.isMember(field))
                        {
                            Json::Value converted = NumericOrHex(args[field]);
                            if (converted.isNull())
                            {
                                done(ToolResult::Error(std::string("'") + field +
                                                        "' must be an integer or a \"0x…\" hex string"));
                                return;
                            }
                            (*body)[field] = converted;
                        }
                    }

                    caller.Call("POST", Endpoint(idOrError, "/memory/find"), body.get(), [body, done](int status, Json::Value response) {
                        if (status < 200 || status >= 300)
                        {
                            done(ToolResult::Error("Memory search failed (HTTP " + std::to_string(status) + "): " +
                                                   DescribeErrorBody(response)));
                            return;
                        }
                        std::ostringstream out;
                        out << "Found " << response.get("count", 0).asUInt() << " match(es)";
                        if (response.get("truncated", false).asBool())
                        {
                            out << " (truncated)";
                        }
                        const Json::Value& matches = response["matches"];
                        if (matches.isArray())
                        {
                            for (Json::ArrayIndex i = 0; i < matches.size() && i < 8; ++i)
                            {
                                const Json::Value& m = matches[i];
                                out << (i == 0 ? ": " : ", ");
                                if (m.isMember("page"))
                                    out << m["page"].get("kind", "").asString() << m["page"].get("page", 0).asUInt() << ":"
                                        << m.get("offset", "").asString();
                                else
                                    out << m.get("address", Json::Value("")).asString();
                            }
                            if (matches.size() > 8)
                            {
                                out << ", …";
                            }
                        }
                        done(ToolResult::Ok(out.str(), std::move(response)));
                    });
                });
                return;
            }

            if (action == "trace")
            {
                unsigned frames = args.isMember("frames") ? args["frames"].asUInt() : 10u;
                if (frames < 1) frames = 1;
                if (frames > 1000) frames = 1000;
                unsigned limit = args.isMember("limit") ? args["limit"].asUInt() : 64u;
                if (limit < 1) limit = 1;
                if (limit > 1024) limit = 1024;

                TargetResolver::ResolveFromArgs(
                    args, caller, [frames, limit, &caller, done, progress](bool ok, const std::string& idOrError) {
                        if (!ok)
                        {
                            done(ToolResult::Error(idOrError));
                            return;
                        }
                        const std::string& id = idOrError;

                        // start → run frames → stop (finalizes hot→cold) → read entries.
                        // Stopping before the read matters: the stop endpoint flushes
                        // pinned hot (tight-loop) events into the cold buffer that
                        // the entries endpoint reads
                        std::vector<SeriesStep> steps;
                        steps.push_back([&caller, id](Json::Value&, std::function<void(bool)> next) {
                            caller.Call("POST", Endpoint(id, "/profiler/calltrace/start"), nullptr,
                                        [next](int, Json::Value) { next(true); });
                        });
                        steps.push_back([&caller, id, frames](Json::Value& acc, std::function<void(bool)> next) {
                            RunFrames(caller, id, frames, [frames, &acc, next](bool ok, Json::Value) mutable {
                                acc["frames_run"] = frames;
                                next(ok);
                            });
                        });
                        steps.push_back([&caller, id](Json::Value&, std::function<void(bool)> next) {
                            caller.Call("POST", Endpoint(id, "/profiler/calltrace/stop"), nullptr,
                                        [next](int, Json::Value) { next(true); });
                        });
                        steps.push_back([&caller, id, limit](Json::Value& acc, std::function<void(bool)> next) {
                            caller.Call("GET", Endpoint(id, "/profiler/calltrace/entries") + "?limit=" + std::to_string(limit),
                                        nullptr, [&acc, next](int status, Json::Value body) mutable {
                                            if (status == 200)
                                            {
                                                acc["entries"] = std::move(body);
                                            }
                                            next(true);
                                        });
                        });

                        RunSeries(ReportSeriesProgress(std::move(steps), progress,
                                                       {"start calltrace", "run frames", "stop calltrace", "read entries"}),
                                  [id, done](Json::Value acc) {
                            unsigned entryCount = 0;
                            if (acc.isMember("entries") && acc["entries"].isObject() && acc["entries"]["entries"].isArray())
                            {
                                entryCount = static_cast<unsigned>(acc["entries"]["entries"].size());
                            }
                            else if (acc.isMember("entries") && acc["entries"].isArray())
                            {
                                entryCount = static_cast<unsigned>(acc["entries"].size());
                            }
                            std::ostringstream out;
                            out << "Call trace: " << entryCount << " entries over " << acc.get("frames_run", 0).asUInt()
                                << " frames [target " << id << "]";
                            done(ToolResult::Ok(out.str(), std::move(acc)));
                        });
                    });
                return;
            }

            done(ToolResult::Error("Unknown action '" + action + "'. Valid: disassemble, assemble, find_bytes, trace"));
        });
}

} // namespace

void RegisterDebugCode(ToolRegistry& registry)
{
    RegisterDebugCodeImpl(registry);
}

/// endregion </debug_code>

/// region <analyze_performance>

namespace
{

/// One line a person reads: the FT812 line budget of the last finished frame
std::string SummarizeLineBudget(const Json::Value& m)
{
    std::ostringstream out;
    out << "VDAC2 line budget, FT812 frame " << m["frame"].asUInt64() << ": ";
    if (!m["valid"].asBool())
        out << "not measured (the frame was not drawn: the monitor showed the Evo or turbo skipped it; "
               "measure_always draws every frame)";
    else
        out << "worst line " << m["worst_line"].asUInt() << " = " << m["worst_clocks"].asUInt() << " of "
            << m["hard_budget"].asUInt() << " clocks (soft " << m["soft_budget"].asUInt() << ", margin "
            << m["margin"].asUInt() << " %), " << m["lines_over_soft"].asUInt() << " line(s) over soft, "
            << m["lines_over_hard"].asUInt() << " over hard (broken on a real card), " << m["lines"].asUInt()
            << " lines";
    out << "; measure_always " << (m["measure_always"].asBool() ? "on" : "off");
    if (m.isMember("in_flight"))
    {
        const Json::Value& f = m["in_flight"];
        if (f["known"].asBool())
            out << "; frame in flight: " << f["lines_passed"].asUInt() << " line(s) passed";
        else
            out << "; frame in flight: " << f["reason"].asString();
    }
    return out.str();
}

void RegisterAnalyzePerformanceImpl(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"coverage_start", "coverage_stop", "coverage_read", "coverage_gaps", "coverage_clear", "frame_cost",
                               "profile_start", "profile_stop", "profile_status", "profile_report", "porttrace", "gs_porttrace",
                               "vdac2_line_budget", "vdac2_line_budget_set"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "Analysis operation: executed-address coverage, per-frame CPU cost, profiler sessions, a one-shot main-Z80 port I/O "
        "trace (on decoders with internal port codes - ZX-Evo decode arms - each event also carries code / code_name, "
        "and session.codes lists them), or gs_porttrace (General Sound coprocessor triage - CPU steps/interrupts/DAC "
        "fetches + event trace), or the TS-Conf VDAC2 card's FT812 line budget: vdac2_line_budget (the last finished "
        "frame's per-line cost against the line period; lines / in_flight add the per-line costs and the frame in "
        "flight, the latter on a paused machine) and vdac2_line_budget_set (margin = soft budget percent 0..50, "
        "measure_always = draw and measure every frame, also not shown). The metrics are part of the chip state: "
        "after a time_travel seek they are those of that moment.";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["clear"]["type"] = "boolean";
    schema["properties"]["clear"]["default"] = true;
    schema["properties"]["clear"]["description"] = "Reset coverage data on coverage_start (default true)";
    schema["properties"]["max_ranges"]["type"] = "integer";
    schema["properties"]["max_ranges"]["description"] = "Maximum executed ranges for coverage_read (default 512, 0 = unlimited)";
    schema["properties"]["start"]["type"] = "string";
    schema["properties"]["start"]["description"] = "coverage_gaps window start — integer or \"0x…\" hex string (default 0x4000)";
    schema["properties"]["end"]["type"] = "string";
    schema["properties"]["end"]["description"] = "coverage_gaps window end (default 0xFFFF)";
    schema["properties"]["max_gaps"]["type"] = "integer";
    schema["properties"]["max_gaps"]["description"] = "Maximum gap ranges for coverage_gaps (default 256)";
    schema["properties"]["limit"]["type"] = "integer";
    schema["properties"]["limit"]["default"] = 32;
    schema["properties"]["limit"]["description"] = "Entry cap for profile_report / porttrace reads";
    schema["properties"]["frames"]["type"] = "integer";
    schema["properties"]["frames"]["default"] = 10;
    schema["properties"]["frames"]["description"] = "Emulated frames to run for porttrace (1-1000)";
    schema["properties"]["lines"]["type"] = "boolean";
    schema["properties"]["lines"]["description"] = "vdac2_line_budget: include the cost of every line (line_clocks)";
    schema["properties"]["in_flight"]["type"] = "boolean";
    schema["properties"]["in_flight"]["description"] =
        "vdac2_line_budget: include the frame in flight (lines passed so far; paused machine only)";
    schema["properties"]["margin"]["type"] = "integer";
    schema["properties"]["margin"]["minimum"] = 0;
    schema["properties"]["margin"]["maximum"] = 50;
    schema["properties"]["margin"]["description"] = "vdac2_line_budget_set: soft budget, percent below the line period";
    schema["properties"]["measure_always"]["type"] = "boolean";
    schema["properties"]["measure_always"]["description"] =
        "vdac2_line_budget_set: draw and measure every FT812 frame, also while the monitor shows the Evo or turbo skips";
    schema["required"].append("action");

    registry.Register(
        "analyze_performance",
        "Performance and coverage analysis: executed-address coverage (start/stop/read/gaps/clear), frame cost (active vs "
        "halted t-states per frame), unified profiler control (opcode/memory/call-trace) with a combined report, and a "
        "one-shot port I/O trace over N frames.",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn& progress) {
            std::string action = args["action"].asString();

            if (action == "coverage_start")
            {
                Json::Value body;
                if (args.isMember("clear"))
                {
                    body["clear"] = args["clear"].asBool();
                }
                ResolveAndForward(args, "POST", "/coverage/start", body.isNull() ? nullptr : &body, caller,
                                  "Coverage recording started", done);
                return;
            }
            if (action == "coverage_stop")
            {
                ResolveAndForward(args, "POST", "/coverage/stop", nullptr, caller, "Coverage recording stopped (data kept)", done);
                return;
            }
            if (action == "coverage_clear")
            {
                ResolveAndForward(args, "POST", "/coverage/clear", nullptr, caller, "Coverage data cleared", done);
                return;
            }
            if (action == "coverage_read")
            {
                std::string suffix = "/coverage";
                if (args.isMember("max_ranges"))
                {
                    suffix += "?max_ranges=" + std::to_string(args["max_ranges"].asUInt());
                }
                ResolveAndForward(args, "GET", suffix, nullptr, caller, "Coverage report", done);
                return;
            }
            if (action == "coverage_gaps")
            {
                std::string suffix = "/coverage/gaps";
                std::string query;
                if (args.isMember("start") && !AddressArg(args["start"]).empty())
                {
                    query += "start=" + AddressArg(args["start"]) + "&";
                }
                if (args.isMember("end") && !AddressArg(args["end"]).empty())
                {
                    query += "end=" + AddressArg(args["end"]) + "&";
                }
                if (args.isMember("max_gaps"))
                {
                    Json::Value maxGaps = NumericOrHex(args["max_gaps"]);
                    query += "max_gaps=" + std::to_string(maxGaps.isNull() ? 0u : maxGaps.asUInt()) + "&";
                }
                if (!query.empty())
                {
                    query.pop_back();  // trailing '&'
                    suffix += "?" + query;
                }
                ResolveAndForward(args, "GET", suffix, nullptr, caller, "Coverage gaps", done);
                return;
            }
            if (action == "frame_cost")
            {
                ResolveAndForward(args, "GET", "/frame_cost", nullptr, caller, "Frame cost", done);
                return;
            }
            if (action == "vdac2_line_budget" || action == "vdac2_line_budget_set")
            {
                std::string suffix = "/vdac2/metrics";
                auto body = std::make_shared<Json::Value>(Json::objectValue);
                const bool set = action == "vdac2_line_budget_set";
                if (set)
                {
                    if (!args.isMember("margin") && !args.isMember("measure_always"))
                    {
                        done(ToolResult::Error("vdac2_line_budget_set needs margin (0..50) and/or measure_always"));
                        return;
                    }
                    if (args.isMember("margin"))
                    {
                        if (!args["margin"].isUInt() || args["margin"].asUInt() > 50)
                        {
                            done(ToolResult::Error("margin is a percent from 0 to 50"));
                            return;
                        }
                        (*body)["margin"] = args["margin"].asUInt();
                    }
                    if (args.isMember("measure_always"))
                        (*body)["measure_always"] = args["measure_always"].asBool();
                }
                else
                {
                    std::string query;
                    if (args["lines"].asBool())
                        query += "lines=1&";
                    if (args["in_flight"].asBool())
                        query += "in_flight=1&";
                    if (!query.empty())
                    {
                        query.pop_back();
                        suffix += "?" + query;
                    }
                }
                TargetResolver::ResolveFromArgs(args, caller, [set, suffix, body, &caller, done](bool ok, const std::string& id) {
                    if (!ok)
                    {
                        done(ToolResult::Error(id));
                        return;
                    }
                    caller.Call(set ? "PUT" : "GET", Endpoint(id, suffix), set ? body.get() : nullptr,
                                [body, done](int status, Json::Value response) {
                                    if (status >= 200 && status < 300)
                                    {
                                        const std::string text = SummarizeLineBudget(response);
                                        done(ToolResult::Ok(text, std::move(response)));
                                        return;
                                    }
                                    const std::string details = DescribeErrorBody(response);
                                    done(ToolResult::Error("WebAPI returned HTTP " + std::to_string(status) +
                                                           (details.empty() ? "" : ": " + details)));
                                });
                });
                return;
            }
            if (action == "profile_start")
            {
                ResolveAndForward(args, "POST", "/profiler/start", nullptr, caller, "Profilers started", done);
                return;
            }
            if (action == "profile_stop")
            {
                ResolveAndForward(args, "POST", "/profiler/stop", nullptr, caller, "Profilers stopped", done);
                return;
            }
            if (action == "profile_status")
            {
                ResolveAndForward(args, "GET", "/profiler/status", nullptr, caller, "Profiler status", done);
                return;
            }

            if (action == "profile_report")
            {
                unsigned limit = args.isMember("limit") ? args["limit"].asUInt() : 32u;
                if (limit < 1) limit = 1;
                if (limit > 1024) limit = 1024;

                TargetResolver::ResolveFromArgs(
                    args, caller, [limit, &caller, done, progress](bool ok, const std::string& idOrError) {
                        if (!ok)
                        {
                            done(ToolResult::Error(idOrError));
                            return;
                        }
                        const std::string& id = idOrError;
                        std::string limitQuery = "?limit=" + std::to_string(limit);

                        std::vector<SeriesStep> steps;
                        auto fetch = [&caller, id](const std::string& suffix, const std::string& key) {
                            return [&caller, id, suffix, key](Json::Value& acc, std::function<void(bool)> next) {
                                caller.Call("GET", Endpoint(id, suffix), nullptr, [key, &acc, next](int status, Json::Value body) mutable {
                                    if (status == 200)
                                    {
                                        acc[key] = std::move(body);
                                    }
                                    next(true);
                                });
                            };
                        };
                        steps.push_back(fetch("/profiler/opcode/counters" + limitQuery, "opcode_counters"));
                        steps.push_back(fetch("/profiler/memory/status", "memory_status"));
                        steps.push_back(fetch("/profiler/calltrace/entries" + limitQuery, "calltrace"));
                        steps.push_back(fetch("/profiler/status", "status"));

                        RunSeries(ReportSeriesProgress(std::move(steps), progress,
                                                       {"opcode counters", "memory status", "calltrace entries", "profiler status"}),
                                  [id, done](Json::Value acc) {
                            std::ostringstream out;
                            out << "Performance report for " << id << ":";
                            out << "\n[opcode_counters] " << (acc.isMember("opcode_counters") ? "see structuredContent" : "unavailable");
                            out << "\n[memory_status] " << (acc.isMember("memory_status") ? "see structuredContent" : "unavailable");
                            out << "\n[calltrace] "
                                << (acc.isMember("calltrace") && acc["calltrace"].isObject() &&
                                            acc["calltrace"]["entries"].isArray()
                                        ? std::to_string(acc["calltrace"]["entries"].size()) + " entries"
                                        : std::string("unavailable"));
                            out << "\n[status] " << (acc.isMember("status") ? "see structuredContent" : "unavailable");
                            done(ToolResult::Ok(out.str(), std::move(acc)));
                        });
                    });
                return;
            }

            if (action == "porttrace")
            {
                unsigned frames = args.isMember("frames") ? args["frames"].asUInt() : 10u;
                if (frames < 1) frames = 1;
                if (frames > 1000) frames = 1000;
                unsigned limit = args.isMember("limit") ? args["limit"].asUInt() : 32u;
                if (limit < 1) limit = 1;
                if (limit > 1024) limit = 1024;

                TargetResolver::ResolveFromArgs(
                    args, caller, [frames, limit, &caller, done, progress](bool ok, const std::string& idOrError) {
                        if (!ok)
                        {
                            done(ToolResult::Error(idOrError));
                            return;
                        }
                        const std::string& id = idOrError;

                        // enable feature → start → run frames → stop → read events
                        std::vector<SeriesStep> steps;
                        steps.push_back([&caller, id](Json::Value&, std::function<void(bool)> next) {
                            // The porttrace endpoints 409 unless the feature is on — enable it
                            // (idempotent; the start step surfaces any real failure)
                            auto body = std::make_shared<Json::Value>();
                            (*body)["enabled"] = true;
                            caller.Call("PUT", Endpoint(id, "/feature/porttrace"), body.get(),
                                        [body, next](int, Json::Value) { next(true); });
                        });
                        steps.push_back([&caller, id](Json::Value& acc, std::function<void(bool)> next) {
                            caller.Call("POST", Endpoint(id, "/profiler/porttrace/start"), nullptr,
                                        [&acc, next](int status, Json::Value body) mutable {
                                            if (status < 200 || status >= 300)
                                            {
                                                acc["error"] = "Port trace start failed (HTTP " + std::to_string(status) +
                                                               "): " + DescribeErrorBody(body);
                                                next(false);
                                                return;
                                            }
                                            next(true);
                                        });
                        });
                        steps.push_back([&caller, id, frames](Json::Value& acc, std::function<void(bool)> next) {
                            RunFrames(caller, id, frames, [frames, &acc, next](bool ok, Json::Value) mutable {
                                acc["frames_run"] = frames;
                                if (!ok)
                                {
                                    acc["error"] = "Emulator did not run the requested frames";
                                }
                                next(ok);
                            });
                        });
                        steps.push_back([&caller, id](Json::Value&, std::function<void(bool)> next) {
                            caller.Call("POST", Endpoint(id, "/profiler/porttrace/stop"), nullptr,
                                        [next](int, Json::Value) { next(true); });
                        });
                        steps.push_back([&caller, id, limit](Json::Value& acc, std::function<void(bool)> next) {
                            caller.Call("GET", Endpoint(id, "/profiler/porttrace/events") + "?limit=" + std::to_string(limit),
                                        nullptr, [&acc, next](int status, Json::Value body) mutable {
                                            if (status == 200)
                                            {
                                                acc["events"] = std::move(body);
                                            }
                                            next(true);
                                        });
                        });

                        RunSeries(ReportSeriesProgress(std::move(steps), progress,
                                                       {"enable porttrace", "start capture", "run frames", "stop capture", "read events"}),
                                  [id, done](Json::Value acc) {
                            if (acc.isMember("error") && acc["error"].isString())
                            {
                                done(ToolResult::Error(acc["error"].asString()));
                                return;
                            }
                            unsigned eventCount = 0;
                            if (acc.isMember("events") && acc["events"].isObject() && acc["events"]["events"].isArray())
                            {
                                eventCount = static_cast<unsigned>(acc["events"]["events"].size());
                            }
                            std::ostringstream out;
                            out << "Port trace: " << eventCount << " event(s) over " << acc.get("frames_run", 0).asUInt()
                                << " frames [target " << id << "]";
                            done(ToolResult::Ok(out.str(), std::move(acc)));
                        });
                    });
                return;
            }

            if (action == "gs_porttrace")
            {
                // General Sound coprocessor triage: is the GS Z80 alive and
                // doing DAC pushes? Start capture -> run frames -> stop ->
                // read counters+events in one call (no feature flag needed,
                // the GS tracer is always instantiated when the card is fitted).
                unsigned frames = args.isMember("frames") ? args["frames"].asUInt() : 10u;
                if (frames < 1) frames = 1;
                if (frames > 1000) frames = 1000;
                unsigned limit = args.isMember("limit") ? args["limit"].asUInt() : 32u;
                if (limit < 1) limit = 1;
                if (limit > 1024) limit = 1024;

                TargetResolver::ResolveFromArgs(
                    args, caller, [frames, limit, &caller, done, progress](bool ok, const std::string& idOrError) {
                        if (!ok)
                        {
                            done(ToolResult::Error(idOrError));
                            return;
                        }
                        const std::string& id = idOrError;

                        std::vector<SeriesStep> steps;
                        steps.push_back([&caller, id](Json::Value& acc, std::function<void(bool)> next) {
                            auto body = std::make_shared<Json::Value>();
                            (*body)["action"] = "start";
                            caller.Call("POST", Endpoint(id, "/state/audio/gs/porttrace"), body.get(),
                                        [body, &acc, next](int status, Json::Value respBody) mutable {
                                            if (status < 200 || status >= 300)
                                            {
                                                acc["error"] = "GS port trace start failed (HTTP " + std::to_string(status) +
                                                               "): " + DescribeErrorBody(respBody);
                                                next(false);
                                                return;
                                            }
                                            next(true);
                                        });
                        });
                        steps.push_back([&caller, id, frames](Json::Value& acc, std::function<void(bool)> next) {
                            RunFrames(caller, id, frames, [frames, &acc, next](bool ok, Json::Value) mutable {
                                acc["frames_run"] = frames;
                                if (!ok)
                                {
                                    acc["error"] = "Emulator did not run the requested frames";
                                }
                                next(ok);
                            });
                        });
                        steps.push_back([&caller, id](Json::Value&, std::function<void(bool)> next) {
                            auto body = std::make_shared<Json::Value>();
                            (*body)["action"] = "stop";
                            caller.Call("POST", Endpoint(id, "/state/audio/gs/porttrace"), body.get(),
                                        [body, next](int, Json::Value) { next(true); });
                        });
                        steps.push_back([&caller, id, limit](Json::Value& acc, std::function<void(bool)> next) {
                            caller.Call("GET", Endpoint(id, "/state/audio/gs/porttrace") + "?events=" + std::to_string(limit),
                                        nullptr, [&acc, next](int status, Json::Value body) mutable {
                                            if (status == 200)
                                            {
                                                acc["gs"] = std::move(body);
                                            }
                                            else
                                            {
                                                acc["error"] = "GS state not available (HTTP " + std::to_string(status) +
                                                               "): " + DescribeErrorBody(body);
                                            }
                                            next(true);
                                        });
                        });

                        RunSeries(ReportSeriesProgress(std::move(steps), progress,
                                                       {"start capture", "run frames", "stop capture", "read counters+events"}),
                                  [id, done](Json::Value acc) {
                            if (acc.isMember("error") && acc["error"].isString())
                            {
                                done(ToolResult::Error(acc["error"].asString()));
                                return;
                            }
                            std::ostringstream out;
                            out << "GS port trace [target " << id << "]: ran " << acc.get("frames_run", 0).asUInt() << " frames";
                            if (acc.isMember("gs") && acc["gs"].isObject() && acc["gs"]["counters"].isObject())
                            {
                                const Json::Value& c = acc["gs"]["counters"];
                                out << " - cpuSteps=" << c.get("cpu_steps", 0).asUInt64()
                                    << " interrupts=" << c.get("interrupts_accepted", 0).asUInt64()
                                    << " intPeriods=" << c.get("interrupt_periods", 0).asUInt64()
                                    << " intCoalesced=" << c.get("interrupts_coalesced", 0).asUInt64()
                                    << " dacFetches=" << c.get("dac_fetches", 0).asUInt64();
                            }
                            done(ToolResult::Ok(out.str(), std::move(acc)));
                        });
                    });
                return;
            }

            done(ToolResult::Error("Unknown action '" + action +
                                            "'. Valid: coverage_start, coverage_stop, coverage_read, coverage_gaps, coverage_clear, "
                                            "frame_cost, profile_start, profile_stop, profile_status, profile_report, porttrace, "
                                            "gs_porttrace, vdac2_line_budget, vdac2_line_budget_set"));
        });
}

} // namespace

void RegisterAnalyzePerformance(ToolRegistry& registry)
{
    RegisterAnalyzePerformanceImpl(registry);
}

/// endregion </analyze_performance>

} // namespace mcp
