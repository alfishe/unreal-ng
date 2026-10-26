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
//   coverage_export                     → POST /coverage/export {format?, start?, end?, path?, merge?}
//   frame_cost                          → GET /frame_cost
//   vdac2_line_budget                   → GET /vdac2/metrics[?lines=1&in_flight=1] (FT812 line metrics)
//   vdac2_line_budget_set               → PUT /vdac2/metrics {margin?, measure_always?}
//   profile_start/stop/status           → unified profiler control
//   profile_report                      → fan-out: opcode counters + memory status
//                                        + calltrace entries + unified status
//   porttrace                           → start → run_frames → stop → events
//   gs_porttrace                        → General Sound coprocessor triage:
//                                        start → run_frames → stop → counters+events
//   profiler {profiler, op}             → POST /profiler/{opcode|memory|calltrace|porttrace}/{op}, /profiler/{op}
//   gs_trace {op}                       → POST /control/audio/gs/porttrace {action}, GET /state/audio/gs/porttrace
//   aylog {op}                          → POST /ay/log {action, capacity?}, GET /ay/log
//   pchist {op}                         → POST /debug/pchist {enabled}, GET /debug/pchist?depth=
//   analyzers {op, name}                → GET /analyzers, PUT /analyzer/{name}, GET|DELETE /analyzer/{name}/events
//
// analysis actions (the analysis layers, core analysis::Execute behind the routes):
//   start {level?, mode?, record?} → POST /analysis/start
//   stop                  → POST /analysis/stop
//   status                → GET  /analysis/status
//   build {frames?, path?, wait?}  → POST /analysis/build (wait default 20000 ms: one call answers the result)
//   cancel                → POST /analysis/cancel
//   explain {address, page?, what?} → GET /analysis/explain?address=&page=&what=
//   query {kind?, range?, limit?}   → GET /analysis/query?kind=&range=&limit=      (L2 claims, P3)
//   export {format, path?, ranges?, width?} → POST /analysis/export                (the L2 draft)
//   gallery {address?, kind?, scale?, path?, limit?} → POST /analysis/gallery     (P4: graphics, one as a PNG:
//                                                    an MCP image content block)
//   findings {op?, kind?, status?, range?, limit?, format?, path?, claim?, evidence?, by?, title?}
//                         → POST /analysis/findings  (P5: the L3 findings register; op add: a note)
//   review {op?, id?, verdict?, label?, title?, comment?, by?, experiment?, ranges?, limit?}
//                         → POST /analysis/review    (P5: the review queue; next, list, show, resolve)
//   experiment {spec | path, finding?, parallel?} → POST /analysis/experiment (P5: spec an object or an array)
//   cover {budget?, per_branch?, frames?, parallel?, limit?, ranges?} → POST /analysis/cover (P5: directed coverage)
// The answer's "text" (shared with the CLI) is the tool's text.
//
// Drogon-free; all calls go through the loopback IApiCaller.

#include "mcp-analysis.h"

#include "mcp-tool-utils.h"

#include <map>
#include <string>
#include <vector>

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
    schema["properties"]["pattern"]["type"] = "string";
    schema["properties"]["pattern"]["description"] = "find_bytes: the same as pattern_hex";
    schema["properties"]["space"]["type"] = "string";
    schema["properties"]["space"]["description"] =
        "find_bytes address space: \"cpu\" (default, what is paged in now), \"ram\" (every RAM page; matches as page + "
        "offset), one page (\"ram5\", \"rom2\", \"cache0\"; start / end are offsets in it) or a device memory region by "
        "name or alias (\"vram\", \"neogs.ram\", \"tsconf.cram\"; memory_access regions lists them)";
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
                // The listing goes in the text itself: clients that pass only content[] to the model get the code
                TargetResolver::ResolveFromArgs(args, caller, [suffix, &caller, done](bool ok, const std::string& idOrError) {
                    if (!ok)
                    {
                        done(ToolResult::Error(idOrError));
                        return;
                    }
                    const std::string id = idOrError;
                    ForwardCall("GET", Endpoint(id, suffix), nullptr, caller, "", [id, done](ToolResult result) {
                        if (result.isError)
                        {
                            done(std::move(result));
                            return;
                        }
                        const Json::Value& body = result.structured;
                        result.text = "Disassembly [target " + id + "]: " + std::to_string(body["instructions"].size()) +
                                      " instruction(s) at " + Hex16(body["address"].asUInt()) + "\n" +
                                      FormatDisasmListing(body);
                        done(std::move(result));
                    });
                });
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
                            // One line per control-flow event (Z80CFType order): "0x02D6 jr -> 0x02C6 x7 sp 0x5BDF"
                            static const char* const kTypes[] = {"jp", "jr", "call", "rst", "ret", "reti", "djnz"};
                            if (acc["entries"].isObject() && acc["entries"]["entries"].isArray())
                                for (const Json::Value& e : acc["entries"]["entries"])
                                {
                                    const unsigned type = e["type"].asUInt();
                                    out << "\n" << Hex16(e["from_address"].asUInt()) << " "
                                        << (type < 7 ? kTypes[type] : "?") << " -> " << Hex16(e["to_address"].asUInt());
                                    if (e["loop_count"].asUInt() > 1)
                                        out << " x" << e["loop_count"].asUInt();
                                    out << " sp " << Hex16(e["sp"].asUInt());
                                }
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
    for (const char* action : {"coverage_start", "coverage_stop", "coverage_read", "coverage_gaps", "coverage_clear", "coverage_export", "frame_cost",
                               "profile_start", "profile_stop", "profile_status", "profile_report", "porttrace", "gs_porttrace",
                               "vdac2_line_budget", "vdac2_line_budget_set", "profiler", "gs_trace", "aylog", "pchist",
                               "analyzers"})
    {
        schema["properties"]["action"]["enum"].append(action);
    }
    schema["properties"]["action"]["description"] =
        "Analysis operation: executed-address coverage (coverage_export writes it as SkoolKit's code execution map for "
        "sna2ctl.py / sna2skool.py -m: one $HHHH line per executed instruction start), per-frame CPU cost, profiler sessions, a one-shot main-Z80 port I/O "
        "trace (on decoders with internal port codes - ZX-Evo decode arms - each event also carries code / code_name, "
        "and session.codes lists them), or gs_porttrace (General Sound coprocessor triage - CPU steps/interrupts/DAC "
        "fetches + event trace), or the TS-Conf VDAC2 card's FT812 line budget: vdac2_line_budget (the last finished "
        "frame's per-line cost against the line period; lines / in_flight add the per-line costs and the frame in "
        "flight, the latter on a paused machine) and vdac2_line_budget_set (margin = soft budget percent 0..50, "
        "measure_always = draw and measure every frame, also not shown). The metrics are part of the chip state: "
        "after a time_travel seek they are those of that moment. "
        "profiler drives one profiler or trace (profiler: opcode, memory, calltrace, porttrace, or all; op: start, stop, "
        "pause, resume, clear, status), gs_trace the General Sound port trace step by step (op: start, stop, pause, "
        "resume, clear, events), aylog the AY register log (op: start + capacity?, stop, clear, status, read + limit?), "
        "pchist the PC history (op: on, off, read + depth?), analyzers the analyzers (op: list, enable, disable, events, "
        "clear; name: trdos, editor-input, coverage, aylog, audiocapture).";
    schema["properties"]["profiler"]["type"] = "string";
    schema["properties"]["profiler"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* name : {"opcode", "memory", "calltrace", "porttrace", "all"})
        schema["properties"]["profiler"]["enum"].append(name);
    schema["properties"]["profiler"]["description"] = "profiler: which one (all: memory + calltrace + opcode together)";
    schema["properties"]["op"]["type"] = "string";
    schema["properties"]["op"]["description"] = "profiler / gs_trace / aylog / pchist / analyzers: the operation";
    schema["properties"]["name"]["type"] = "string";
    schema["properties"]["name"]["description"] = "analyzers: the analyzer id (trdos, editor-input, coverage, aylog, audiocapture)";
    schema["properties"]["depth"]["type"] = "integer";
    schema["properties"]["depth"]["description"] = "pchist read: entries, newest first (default 64)";
    schema["properties"]["capacity"]["type"] = "integer";
    schema["properties"]["capacity"]["description"] = "aylog start: the ring's capacity in register writes";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["properties"]["clear"]["type"] = "boolean";
    schema["properties"]["clear"]["default"] = true;
    schema["properties"]["clear"]["description"] = "Reset coverage data on coverage_start (default true)";
    schema["properties"]["max_ranges"]["type"] = "integer";
    schema["properties"]["max_ranges"]["description"] = "Maximum executed ranges for coverage_read (default 512, 0 = unlimited)";
    schema["properties"]["start"]["type"] = "string";
    schema["properties"]["start"]["description"] =
        "coverage_gaps / coverage_export window start — integer or \"0x…\" hex string (default 0x4000 for gaps, 0 for export)";
    schema["properties"]["end"]["type"] = "string";
    schema["properties"]["end"]["description"] = "coverage_gaps / coverage_export window end (default 0xFFFF)";
    schema["properties"]["format"]["type"] = "string";
    schema["properties"]["format"]["enum"] = Json::Value(Json::arrayValue);
    schema["properties"]["format"]["enum"].append("skoolkit-map");
    schema["properties"]["format"]["description"] = "coverage_export: the file format (default skoolkit-map)";
    schema["properties"]["path"]["type"] = "string";
    schema["properties"]["path"]["description"] =
        "coverage_export: the file to write (on the emulator's host); without it the map comes back in the answer";
    schema["properties"]["merge"]["type"] = "boolean";
    schema["properties"]["merge"]["description"] =
        "coverage_export: add the addresses of the map file already at path (several runs into one map)";
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
        "Performance and coverage analysis: executed-address coverage (start/stop/read/gaps/clear, export as a SkoolKit "
        "code execution map), frame cost (active vs "
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
            if (action == "coverage_export")
            {
                Json::Value body(Json::objectValue);
                for (const char* name : {"format", "path"})
                    if (args.isMember(name))
                        body[name] = args[name].asString();
                for (const char* name : {"start", "end"})
                    if (args.isMember(name) && !AddressArg(args[name]).empty())
                        body[name] = AddressArg(args[name]);
                if (args.isMember("merge"))
                    body["merge"] = args["merge"].asBool();
                // An agent may see only the text: the message, then the map itself (or where it was written)
                ResolveAndForward(args, "POST", "/coverage/export", &body, caller, "Coverage map", [done](ToolResult result) {
                    if (!result.isError && result.structured.isObject())
                    {
                        const Json::Value& r = result.structured;
                        result.text = r.get("message", "Coverage map").asString() + " [" + r.get("start", "").asString() +
                                      "-" + r.get("end", "").asString() + ", format " + r.get("format", "").asString() + "]";
                        if (r.isMember("map"))
                            result.text += "\n" + r["map"].asString();
                    }
                    done(std::move(result));
                });
                return;
            }
            if (action == "frame_cost")
            {
                ResolveAndForward(args, "GET", "/frame_cost", nullptr, caller, "Frame cost", AppendFields(done));
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
                ResolveAndForward(args, "GET", "/profiler/status", nullptr, caller, "Profiler status", AppendFields(done));
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
                            caller.Call("POST", Endpoint(id, "/control/audio/gs/porttrace"), body.get(),
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
                            caller.Call("POST", Endpoint(id, "/control/audio/gs/porttrace"), body.get(),
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

            if (action == "profiler" || action == "gs_trace" || action == "aylog" || action == "pchist" || action == "analyzers")
            {
                const std::string op = args.get("op", action == "analyzers" ? "list" : "status").asString();
                std::string method = "POST";
                std::string path;
                Json::Value body(Json::objectValue);
                bool hasBody = false;
                std::string text;
                if (action == "profiler")
                {
                    const std::string which = args.get("profiler", "all").asString();
                    if (which != "opcode" && which != "memory" && which != "calltrace" && which != "porttrace" && which != "all")
                    {
                        done(ToolResult::Error("profiler: opcode, memory, calltrace, porttrace or all"));
                        return;
                    }
                    if (op != "start" && op != "stop" && op != "pause" && op != "resume" && op != "clear" && op != "status")
                    {
                        done(ToolResult::Error("op: start, stop, pause, resume, clear or status"));
                        return;
                    }
                    path = (which == "all" ? std::string("/profiler/") : "/profiler/" + which + "/") + op;
                    if (op == "status")
                        method = "GET";
                    text = "Profiler " + which + ": " + op;
                }
                else if (action == "gs_trace")
                {
                    if (op == "events" || op == "status")
                    {
                        method = "GET";
                        path = "/state/audio/gs/porttrace?events=" + std::to_string(args.get("limit", 32).asUInt());
                    }
                    else if (op == "start" || op == "stop" || op == "pause" || op == "resume" || op == "clear")
                    {
                        path = "/control/audio/gs/porttrace";
                        body["action"] = op;
                        hasBody = true;
                    }
                    else
                    {
                        done(ToolResult::Error("op: start, stop, pause, resume, clear or events"));
                        return;
                    }
                    text = "GS port trace: " + op;
                }
                else if (action == "aylog")
                {
                    if (op == "status" || op == "read")
                    {
                        method = "GET";
                        path = "/ay/log?limit=" + std::to_string(op == "status" ? 0u : args.get("limit", 32).asUInt()) +
                               "&tail=true";
                    }
                    else if (op == "start" || op == "stop" || op == "clear")
                    {
                        path = "/ay/log";
                        body["action"] = op;
                        if (args.isMember("capacity"))
                            body["capacity"] = args["capacity"];
                        hasBody = true;
                    }
                    else
                    {
                        done(ToolResult::Error("op: start, stop, clear, status or read"));
                        return;
                    }
                    text = "AY log: " + op;
                }
                else if (action == "pchist")
                {
                    if (op == "read" || op == "status")
                    {
                        method = "GET";
                        path = "/debug/pchist?depth=" + std::to_string(args.get("depth", 64).asUInt());
                    }
                    else if (op == "on" || op == "off")
                    {
                        path = "/debug/pchist";
                        body["enabled"] = op == "on";
                        hasBody = true;
                    }
                    else
                    {
                        done(ToolResult::Error("op: on, off or read"));
                        return;
                    }
                    text = "PC history: " + op;
                }
                else
                {
                    const std::string name = args.get("name", "").asString();
                    if (op == "list")
                    {
                        method = "GET";
                        path = "/analyzers";
                    }
                    else if (name.empty())
                    {
                        done(ToolResult::Error("analyzers " + op + " requires 'name' (list shows them)"));
                        return;
                    }
                    else if (op == "enable" || op == "disable")
                    {
                        method = "PUT";
                        path = "/analyzer/" + name;
                        body["enabled"] = op == "enable";
                        hasBody = true;
                    }
                    else if (op == "events" || op == "clear")
                    {
                        method = op == "events" ? "GET" : "DELETE";
                        path = "/analyzer/" + name + "/events";
                    }
                    else
                    {
                        done(ToolResult::Error("op: list, enable, disable, events or clear"));
                        return;
                    }
                    text = "Analyzers: " + op + (name.empty() ? std::string() : " " + name);
                }
                ResolveAndForward(args, method, path, hasBody ? &body : nullptr, caller, text, done);
                return;
            }

            done(ToolResult::Error("Unknown action '" + action +
                                            "'. Valid: coverage_start, coverage_stop, coverage_read, coverage_gaps, coverage_clear, "
                                            "frame_cost, profile_start, profile_stop, profile_status, profile_report, porttrace, "
                                            "gs_porttrace, vdac2_line_budget, vdac2_line_budget_set, profiler, gs_trace, aylog, "
                                            "pchist, analyzers"));
        });
}

} // namespace

void RegisterAnalyzePerformance(ToolRegistry& registry)
{
    RegisterAnalyzePerformanceImpl(registry);
}

/// endregion </analyze_performance>

/// region <analysis>

void RegisterAnalysis(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["action"]["type"] = "string";
    schema["properties"]["action"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* action : {"start", "stop", "status", "build", "cancel", "explain", "query", "export",
                               "gallery", "findings", "review", "experiment", "cover"})
        schema["properties"]["action"]["enum"].append(action);
    schema["properties"]["action"]["description"] =
        "start: switch on memory tracking with per-byte access facts and start a fresh session (level, mode; "
        "record=true also starts the recording build needs); stop: end it, the facts stay readable; status: what the "
        "session holds and the L1 build; build: replay the recording on a muted analysis instance into L1 (routines, "
        "call graph, loops, fields, data flow; waits up to 'wait' ms, default 20000, then answers the job state); "
        "cancel: stop a running build; explain: a routine's fact sheet (profile, loops, fields, callers, registers, a "
        "disassembly excerpt) when L1 has a routine at the address, otherwise what is known of one byte - how it was "
        "accessed (opcode or operand fetch, data or stack read / write, block instruction, written while code), the "
        "last instruction that read and wrote it, the frames, the values written, the counters and the labels; "
        "query: L2 regions (what the bytes are: code, text, records with fields, tables, variables, unused source, "
        "each with geometry, users, evidence and a confidence 0..1000) as a compact table; export: the L2 draft as a "
        "SkoolKit ctl (sna2skool.py -c) or a relocatable skool, the record fields / geometry JSON, the relocation "
        "layout, a symbol file or the region map; gallery: the graphics L2 found (fonts, sprites, tiles, screens and "
        "back buffers, level maps) with their geometry, or with an address the one there drawn as a PNG (an image "
        "block: check the width, height, frames and mask by eye); findings: the L3 register, every L2 claim, conflict, "
        "candidate bug, unnamed hot routine, unknown area, note and experiment with evidence and a status (observed, "
        "inferred, refuted, confirmed), ids F1.. in a fixed order (op add: a note of yours); review: the queue of "
        "findings that need a decision (op next, the default: the next item with a fact sheet under 1 KB; list; show "
        "id; resolve id with verdict ok / wrong / skip and a label, title or comment that go into the draft export, "
        "optionally with an experiment); experiment: poke bytes / set registers / press keys on a muted clone from "
        "the recording, run N frames, check memory, the screen, watched addresses or new code (the source machine is "
        "never touched; the result marks the finding observed or refuted); cover: flip branches the session took one "
        "way only by holding the byte they test or pressing the keys they read, and report the new code";
    schema["properties"]["level"]["type"] = "string";
    schema["properties"]["level"]["enum"] = Json::Value(Json::arrayValue);
    schema["properties"]["level"]["enum"].append("facts");
    schema["properties"]["level"]["enum"].append("counters");
    schema["properties"]["level"]["description"] = "start: facts (default) or counters only";
    schema["properties"]["mode"]["type"] = "string";
    schema["properties"]["mode"]["enum"] = Json::Value(Json::arrayValue);
    schema["properties"]["mode"]["enum"].append("z80");
    schema["properties"]["mode"]["enum"].append("physical");
    schema["properties"]["mode"]["description"] =
        "start: z80 (default, the 64K address space) or physical (per RAM / ROM page: banked code apart)";
    schema["properties"]["record"]["type"] = "boolean";
    schema["properties"]["record"]["description"] = "start: also start the TTD recording that build replays";
    schema["properties"]["frames"]["type"] = "integer";
    schema["properties"]["frames"]["description"] =
        "build: replay at most this many frames (default: the whole recording); cover: frames per attempt (default 50)";
    schema["properties"]["path"]["type"] = "string";
    schema["properties"]["path"]["description"] =
        "build: write the L1 JSON to this file; export: write the draft there; gallery: the PNG (none: the answer "
        "carries it)";
    schema["properties"]["kind"]["type"] = "string";
    schema["properties"]["kind"]["description"] =
        "query: all (default), conflicts, or a region kind (code, data, text, font, sprite, tile, screen, level_map, "
        "buffer, init_table, records, variable, pointer_table, handler_table, rng_state, music_data, packed, unused, "
        "untouched, ...); gallery: all, font, sprite, tile, screen or level_map";
    schema["properties"]["range"]["type"] = "string";
    schema["properties"]["range"]["description"] = "query: hex start-end, e.g. 8000-83FF (default the whole 64K)";
    schema["properties"]["limit"]["type"] = "integer";
    schema["properties"]["limit"]["description"] =
        "query, gallery, findings: the most rows (default 200); review: the queue's cap (default 40); cover: branches";
    schema["properties"]["scale"]["type"] = "integer";
    schema["properties"]["scale"]["description"] = "gallery: integer zoom of the PNG, 1..8 (default 2)";
    schema["properties"]["format"]["type"] = "string";
    schema["properties"]["format"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* format : {"ctl", "skool", "fields", "geometry", "layout", "symbols", "map", "table", "markdown",
                               "jsonl"})
        schema["properties"]["format"]["enum"].append(format);
    schema["properties"]["format"]["description"] =
        "export: the file to write (ctl ... map); findings: table (default), markdown (zx-reaper re/findings.md) or "
        "jsonl";
    schema["properties"]["ranges"]["type"] = "string";
    schema["properties"]["ranges"]["description"] =
        "export, review, cover: hex Z80 ranges, comma separated (default 4000-FFFF), e.g. 5900-59FF,8000-FFFF";
    schema["properties"]["width"]["type"] = "integer";
    schema["properties"]["width"]["description"] = "export: bytes per DEFB line (default 8)";
    schema["properties"]["wait"]["type"] = "integer";
    schema["properties"]["wait"]["description"] = "build: milliseconds to wait for the result (0..30000, default 20000)";
    schema["properties"]["what"]["type"] = "string";
    schema["properties"]["what"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* what : {"auto", "routine", "byte"})
        schema["properties"]["what"]["enum"].append(what);
    schema["properties"]["what"]["description"] =
        "explain: auto (default; the routine when L1 has one at the address), routine (L1 fact sheet) or byte (L0)";
    schema["properties"]["address"]["type"] = "string";  // an integer works too (AddressArg)
    schema["properties"]["address"]["description"] =
        "explain, gallery: $80BC, #80BC, 0x80BC, 80BCh, decimal or a label name (gallery: the region to draw)";
    schema["properties"]["page"]["type"] = "string";
    schema["properties"]["page"]["description"] =
        "explain: the page (absolute index, ram:N or rom:N); L0 in physical mode (default the page mapped at the "
        "address now), L1 picks the routine of that page";
    schema["properties"]["op"]["type"] = "string";
    schema["properties"]["op"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* op : {"list", "add", "next", "show", "resolve"})
        schema["properties"]["op"]["enum"].append(op);
    schema["properties"]["op"]["description"] =
        "findings: list (default) or add (a note: claim, range, status, evidence, by); review: next (default), list, "
        "show (id) or resolve (id + verdict / label / title / comment)";
    schema["properties"]["id"]["type"] = "string";
    schema["properties"]["id"]["description"] = "review show / resolve: the finding, e.g. F12";
    schema["properties"]["verdict"]["type"] = "string";
    schema["properties"]["verdict"]["enum"] = Json::Value(Json::arrayValue);
    for (const char* verdict : {"ok", "wrong", "skip"})
        schema["properties"]["verdict"]["enum"].append(verdict);
    schema["properties"]["verdict"]["description"] =
        "review resolve: ok (the finding is confirmed), wrong (refuted) or skip (out of the queue, undecided)";
    schema["properties"]["label"]["type"] = "string";
    schema["properties"]["label"]["description"] = "review resolve: the routine's / entry's label in the draft (an identifier)";
    schema["properties"]["title"]["type"] = "string";
    schema["properties"]["title"]["description"] = "review resolve: the entry title in the draft; findings add: the note's title";
    schema["properties"]["comment"]["type"] = "string";
    schema["properties"]["comment"]["description"] = "review resolve: a comment the draft puts in the entry";
    schema["properties"]["by"]["type"] = "string";
    schema["properties"]["by"]["description"] = "findings add, review resolve: who decided (default agent)";
    schema["properties"]["status"]["type"] = "string";
    schema["properties"]["status"]["description"] =
        "findings: list only this status, or the status of a note added (observed, inferred, refuted, confirmed)";
    schema["properties"]["claim"]["type"] = "string";
    schema["properties"]["claim"]["description"] = "findings add: the note's sentence";
    schema["properties"]["evidence"]["type"] = "string";
    schema["properties"]["evidence"]["description"] = "findings add: evidence, ';' separated";
    for (const char* name : {"spec", "experiment"})
        for (const char* type : {"object", "array", "string"})
            schema["properties"][name]["type"].append(type);
    schema["properties"]["spec"]["description"] =
        "experiment: one spec or an array of them (or their JSON text): {\"poke\": {\"$8100\": 5}, \"regs\": "
        "{\"a\": 1}, \"input\": [{\"frame\": 0, \"key\": \"q\"}], \"from\": \"start\" | \"end\" | "
        "\"frame:N\", \"frames\": 50, \"watch\": [{\"pc\": \"$8130\", \"as\": \"rare\"}, {\"write\": "
        "\"$8101\"}], \"expect\": {\"rare\": \"reached\", \"mem:$8101\": \"$AA\", \"screen\": "
        "\"changed\", \"new_code\": \">0\"}, \"finding\": \"F12\"}";
    schema["properties"]["experiment"]["description"] =
        "review resolve: an experiment spec (as for spec) whose result marks the finding";
    schema["properties"]["finding"]["type"] = "string";
    schema["properties"]["finding"]["description"] = "experiment: the finding the result marks (none: a new experiment finding)";
    schema["properties"]["parallel"]["type"] = "integer";
    schema["properties"]["parallel"]["description"] = "experiment, cover: clones run at once (1..8, default by the cores)";
    schema["properties"]["budget"]["type"] = "integer";
    schema["properties"]["budget"]["description"] = "cover: experiments in all (default 64)";
    schema["properties"]["per_branch"]["type"] = "integer";
    schema["properties"]["per_branch"]["description"] = "cover: attempts per branch (default 8)";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["default"] = "auto";
    schema["required"].append("action");

    registry.Register(
        "analysis",
        "Program analysis from what the emulated CPU does: per-byte access facts (code vs operand vs data vs stack, "
        "who reads and writes a byte, which values, when, self-modified code) and L1 code behaviour built from a "
        "recording (routines with their data ranges, ports, time share and registers; call graph; loops; record "
        "fields); L2 structure from L1 (regions with kinds, fields and confidence) to query and export as a SkoolKit "
        "draft, see the graphics it found (gallery); start (record=true for L1), run the program, build, then explain "
        "addresses, query regions, export the draft; status shows what was recorded. L3 review: findings (the "
        "register), review (the queue of decisions; resolve writes into the draft), experiment (poke / keys / regs "
        "on a muted clone, then checks), cover (flip one-sided branches).",
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();
            // The core answer's text is the readable answer; the target goes in front
            auto withText = [done](ToolResult result) {
                if (!result.isError && result.structured.isObject() && result.structured.isMember("text"))
                    result.text = result.text + "\n" + result.structured["text"].asString();
                done(std::move(result));
            };
            if (action == "start")
            {
                Json::Value body(Json::objectValue);
                for (const char* name : {"level", "mode"})
                    if (args.isMember(name))
                        body[name] = args[name].asString();
                if (args.isMember("record"))
                    body["record"] = args["record"];
                ResolveAndForward(args, "POST", "/analysis/start", &body, caller, "Analysis started", withText);
                return;
            }
            if (action == "build")
            {
                Json::Value body(Json::objectValue);
                for (const char* name : {"frames", "wait"})
                    if (args.isMember(name))
                        body[name] = args[name];  // a number or its text: the core checks it
                if (!args.isMember("wait"))
                    body["wait"] = 20000;
                if (args.isMember("path"))
                    body["path"] = args["path"].asString();
                ResolveAndForward(args, "POST", "/analysis/build", &body, caller, "Analysis build", withText);
                return;
            }
            if (action == "cancel")
            {
                ResolveAndForward(args, "POST", "/analysis/cancel", nullptr, caller, "Analysis build cancelled", withText);
                return;
            }
            if (action == "stop")
            {
                ResolveAndForward(args, "POST", "/analysis/stop", nullptr, caller, "Analysis stopped", withText);
                return;
            }
            if (action == "status")
            {
                ResolveAndForward(args, "GET", "/analysis/status", nullptr, caller, "Analysis status", withText);
                return;
            }
            if (action == "explain")
            {
                if (!args.isMember("address") || AddressArg(args["address"]).empty())
                {
                    done(ToolResult::Error("explain needs address ($80BC, 0x80BC, decimal or a label)"));
                    return;
                }
                std::string suffix = "/analysis/explain?address=" + UrlEncodeSegment(AddressArg(args["address"]));
                if (args.isMember("page") && !AddressArg(args["page"]).empty())
                    suffix += "&page=" + UrlEncodeSegment(AddressArg(args["page"]));
                if (args.isMember("what") && !args["what"].asString().empty())
                    suffix += "&what=" + UrlEncodeSegment(args["what"].asString());
                ResolveAndForward(args, "GET", suffix, nullptr, caller, "Analysis explain", withText);
                return;
            }
            if (action == "query")
            {
                std::string suffix = "/analysis/query";
                char sep = '?';
                for (const char* name : {"kind", "range", "limit"})
                    if (args.isMember(name) && !AddressArg(args[name]).empty())
                    {
                        suffix += sep + std::string(name) + "=" + UrlEncodeSegment(AddressArg(args[name]));
                        sep = '&';
                    }
                ResolveAndForward(args, "GET", suffix, nullptr, caller, "Analysis query", withText);
                return;
            }
            if (action == "export")
            {
                if (!args.isMember("format") || args["format"].asString().empty())
                {
                    done(ToolResult::Error("export needs format (ctl, skool, fields, geometry, layout, symbols, map)"));
                    return;
                }
                Json::Value body(Json::objectValue);
                for (const char* name : {"format", "path", "ranges"})
                    if (args.isMember(name))
                        body[name] = args[name].asString();
                if (args.isMember("width"))
                    body["width"] = args["width"];
                ResolveAndForward(args, "POST", "/analysis/export", &body, caller, "Analysis export", withText);
                return;
            }
            if (action == "gallery")
            {
                Json::Value body(Json::objectValue);
                for (const char* name : {"kind", "path"})
                    if (args.isMember(name))
                        body[name] = args[name].asString();
                if (args.isMember("address") && !AddressArg(args["address"]).empty())
                    body["address"] = AddressArg(args["address"]);
                for (const char* name : {"scale", "limit"})
                    if (args.isMember(name))
                        body[name] = args[name];
                // The PNG goes out as an image content block (the model sees it); structuredContent keeps the rest
                auto withImage = [withText](ToolResult result) {
                    if (!result.isError && result.structured.isObject() && !result.structured["png"].asString().empty())
                    {
                        result.imageData = result.structured["png"].asString();
                        result.imageMimeType = "image/png";
                        result.structured["png"] = "[base64 picture: see the image content block]";
                    }
                    withText(std::move(result));
                };
                ResolveAndForward(args, "POST", "/analysis/gallery", &body, caller, "Analysis gallery", withImage);
                return;
            }
            if (action == "findings" || action == "review" || action == "experiment" || action == "cover")
            {
                // Every option goes through as given (numbers, strings, booleans; spec / experiment objects or arrays):
                // the core checks them
                static const std::map<std::string, std::vector<const char*>> kOptions = {
                    {"findings", {"op", "kind", "status", "range", "limit", "format", "path", "claim", "evidence", "by",
                                  "title"}},
                    {"review", {"op", "id", "verdict", "label", "title", "comment", "by", "experiment", "ranges", "limit"}},
                    {"experiment", {"spec", "path", "parallel", "finding"}},
                    {"cover", {"budget", "per_branch", "frames", "parallel", "limit", "ranges"}},
                };
                Json::Value body(Json::objectValue);
                for (const char* name : kOptions.at(action))
                    if (args.isMember(name) && !args[name].isNull())
                        body[name] = args[name];
                if (action == "experiment" && !body.isMember("spec") && !body.isMember("path"))
                {
                    done(ToolResult::Error("experiment needs spec (an object, an array or their JSON text) or path"));
                    return;
                }
                static const std::map<std::string, std::string> kTitles = {
                    {"findings", "Analysis findings"}, {"review", "Analysis review"},
                    {"experiment", "Analysis experiment"}, {"cover", "Analysis coverage"}};
                ResolveAndForward(args, "POST", "/analysis/" + action, &body, caller, kTitles.at(action), withText);
                return;
            }
            done(ToolResult::Error("unknown action '" + action +
                                   "': start, stop, status, build, cancel, explain, query, export, gallery, findings, "
                                   "review, experiment or cover"));
        });
}

/// endregion </analysis>

} // namespace mcp
