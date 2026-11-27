// LuaEmulator bindings: recording, video, analyzers, section timers and audio.
// One group of registerType (lua_emulator.h): each group in a file of its own, so they compile in parallel
#include "emulator/lua_emulator.h"

void LuaEmulator::RegisterVideoAndAudio(sol::state& lua)
{
    lua.set_function("window_record_frames", [this](sol::this_state s, int frames) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return sol::make_object(s, sol::lua_nil);
        execution::script::Check(*emulator, execution::Origin::Lua);
        const WindowRecording::Result r = WindowRecording::RunFrames(*emulator, frames);
        if (!r.Ok())
            return StateNodeToLua(s, WindowRecording::ErrorNode(r));
        StateNode report = WindowRecording::Report(r.recording);
        report["ran"] = r.ran;
        return StateNodeToLua(s, report);
    });
    lua.set_function("window_record_stop", [](sol::this_state s) -> sol::object {
        const WindowRecording::Result r = WindowRecording::Stop();
        return StateNodeToLua(s, r.Ok() ? WindowRecording::Report(r.recording) : WindowRecording::ErrorNode(r));
    });
    lua.set_function("window_record_status", [](sol::this_state s) -> sol::object {
        const WindowRecording::Result r = WindowRecording::Get();
        return StateNodeToLua(s, r.Ok() ? WindowRecording::Report(r.recording) : WindowRecording::ErrorNode(r));
    });
    // The overscan frame's viewport (the machine's: answers headless too): video_viewport() reads, video_viewport_set
    // ("symmetric" | "full" | "standard" | "screen_only" | "next" | "prev") crops it; {ok = false, error} refused
    lua.set_function("video_viewport", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, VideoViewport::Report(*emulator));
    });
    lua.set_function("video_viewport_set", [this](sol::this_state s, const std::string& name) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
            return sol::make_object(s, sol::lua_nil);
        const VideoViewport::Result r = VideoViewport::SetViewport(*emulator, name);
        if (!r.Ok())
        {
            StateNode error = StateNode::Object();
            error["ok"] = false;
            error["error"] = r.error;
            return StateNodeToLua(s, error);
        }
        return StateNodeToLua(s, VideoViewport::Report(*emulator));
    });

    // Halt/active cost of the last frame plus session averages
    lua.set_function("frame_cost", [this]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }

        EmulatorContext* context = emulator->GetContext();
        if (!context) { result["error"] = "no context"; return result; }

        const CONFIG& config = context->config;
        const EmulatorState& state = context->emulatorState;

        const uint64_t frameBudget = static_cast<uint64_t>(config.frame) * state.current_z80_frequency_multiplier;
        const uint64_t lastHalted = state.tstates_halted_last;
        const uint64_t lastActive = frameBudget > lastHalted ? frameBudget - lastHalted : 0;

        sol::table last = lua_view.create_table();
        last["tstates_total"] = frameBudget;
        last["tstates_halted"] = lastHalted;
        last["tstates_active"] = lastActive;
        last["halted_percent"] = frameBudget ? lastHalted * 100.0 / frameBudget : 0.0;
        result["last"] = last;

        sol::table average = lua_view.create_table();
        average["frames"] = static_cast<uint64_t>(state.frame_cost_frames);
        average["tstates_total"] = static_cast<uint64_t>(state.tstates_frame_total);
        average["tstates_halted"] = static_cast<uint64_t>(state.tstates_halted_total);
        average["tstates_active"] = static_cast<uint64_t>(state.tstates_frame_total - state.tstates_halted_total);
        result["average"] = average;
        return result;
    });

    // Profilers, traces and captures through tracecontrol (the catalogue's prof.* actions): trace(name, op, opts)
    // with name opcode | memory | calltrace | coverage | porttrace | gsporttrace | pchist | aylog | audio | all, op
    // start | stop | toggle | pause | resume | pause_toggle | clear | status, opts {keep=, capacity=, seconds=,
    // source=, on=}. The answer {name, state, can_pause, ...}; a refusal: ok = false, error = why
    lua.set_function("trace", [this](sol::this_state ts, const std::string& name, sol::optional<std::string> op,
                                     sol::optional<sol::object> opts) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        tracecontrol::Reply reply;
        if (!emulator)
            reply.error = "Conflict", reply.message = "no emulator";
        else
            reply = tracecontrol::Execute(*emulator, name, op.value_or("status"),
                                          opts ? SymbolOptions(*opts, "") : tracecontrol::Options{});
        StateNode value = reply.body;
        if (!reply.Ok())
        {
            value["ok"] = false;
            value["error"] = reply.message;
        }
        return StateNodeToLua(ts, value);
    });
    // The analysis layers through analysis::Execute (analysiscontrol.h): analysis(action, opts) with action start |
    // stop | status | build | cancel | explain | query | export | gallery | findings | review | experiment | cover |
    // trace; opts a table ({level=, mode=, record=} /
    // {frames=, from_frame=, from_tinframe=, to_frame=, path=, wait=} (from / to: a window, one stage) /
    // {address=, page=, what=} / {kind=, range=, limit=} / {format=, path=, ranges=,
    // width=} / {address=, kind=, scale=, path=, limit=}) or, for explain and gallery, the address, for start the
    // level, for build the frames, for query the kind, for export the format. The answer: the reply's fields plus
    // text (gallery: the PNG base64 in png); a refusal: ok = false, error = why. L3: findings {op=, kind=, status=,
    // format=, path=}, review {op=, id=, verdict=, label=, title=, comment=, kind=, names=<JSON text>, reason=,
    // offset=, limit=, experiment=<JSON text>} (or the op), experiment {spec=<JSON text>, path=, finding=} (or the
    // spec's JSON text), cover {budget=, frames=, ranges=} (or the budget), trace {path=, from_frame=, from_tinframe=,
    // to_frame=, to_tinframe=, max_cycles=, kinds=, pc=, addresses=} (or the path: the CPU access trace of a window, accesstrace.h)
    lua.set_function("analysis", [this](sol::this_state ts, sol::optional<std::string> action,
                                        sol::optional<sol::object> opts) -> sol::object {
        const std::string verb = action.value_or("status");
        Emulator* emulator = effectiveEmulator();
        analysis::Reply reply;
        if (!emulator)
            reply.error = analysis::Error::NotAvailable, reply.message = "no emulator";
        else
            reply = analysis::Execute(*emulator, verb,
                                      opts ? SymbolOptions(*opts, verb == "explain" || verb == "gallery" ? "address"
                                                         : verb == "start" ? "level"
                                                         : verb == "build" ? "frames"
                                                         : verb == "query" ? "kind"
                                                         : verb == "export" ? "format"
                                                         : verb == "findings" || verb == "review" ? "op"
                                                         : verb == "experiment" ? "spec"
                                                         : verb == "cover" ? "budget"
                                                         : verb == "trace" ? "path"
                                                                           : "")
                                           : analysis::Options{});
        StateNode value = reply.ToValue();
        if (!reply.Ok())
        {
            value = StateNode::Object();
            value["ok"] = false;
            value["error"] = reply.message;
        }
        return StateNodeToLua(ts, value);
    });
    // The long verbs as jobs through jobs::Execute (emulator/jobs/jobcontrol.h, PLAN #122 S2): jobs(action, opts)
    // with action list {instance= ('this', 'all' or an id), state= (active, finished, all), limit=} | status
    // {job=, result=} (or the job id) | wait {job=, wait_ms= (default 30000, <= 60000), result=} (blocks this
    // script up to wait_ms) | cancel {job=} | start {domain= ('ttd' or 'analysis'), verb=, <the verb's options>}.
    // analysis(action, {async=true, ...}) answers {job=, state=} the same way. The answer: the reply's
    // fields plus text; a refusal: ok = false, error = why
    lua.set_function("jobs", [this](sol::this_state ts, sol::optional<std::string> action,
                                    sol::optional<sol::object> opts) -> sol::object {
        const std::string verb = action.value_or("list");
        Emulator* emulator = effectiveEmulator();
        const jobs::Reply reply =
            jobs::Execute(emulator, verb,
                          opts ? SymbolOptions(*opts, verb == "list" ? "instance" : verb == "start" ? "domain" : "job")
                               : jobs::Options{});
        StateNode value = reply.ToValue();
        if (!reply.Ok())
        {
            value = StateNode::Object();
            value["ok"] = false;
            value["error"] = reply.message;
        }
        return StateNodeToLua(ts, value);
    });
    // The memory markup through markup::Execute (markupcontrol.h): markup(action, opts) with action show
    // {range=, from=, to=, layer=, limit=} (or the range), layers, mark {range= | from=, to=, kind=, title=, cpu=}
    // (or the range), unmark {range= | from=, to=} (or the range), comment {address=, text=} (or the address),
    // clear {layer=} (or the layer), save {profile=, analysis=, confidence=, keep=} (or the profile)
    lua.set_function("markup", [this](sol::this_state ts, sol::optional<std::string> action,
                                      sol::optional<sol::object> opts) -> sol::object {
        const std::string verb = action.value_or("show");
        Emulator* emulator = effectiveEmulator();
        markup::Reply reply;
        if (!emulator)
            reply.error = markup::Error::NotAvailable, reply.message = "no emulator";
        else
            reply = markup::Execute(*emulator, verb,
                                    opts ? SymbolOptions(*opts, verb == "comment" ? "address"
                                                       : verb == "clear" ? "layer"
                                                       : verb == "save" ? "profile"
                                                       : verb == "layers" ? ""
                                                                          : "range")
                                         : markup::Options{});
        StateNode value = reply.ToValue();
        if (!reply.Ok())
        {
            value = StateNode::Object();
            value["ok"] = false;
            value["error"] = reply.message;
        }
        return StateNodeToLua(ts, value);
    });
    // The CPU time profile through profile::Execute (profilecontrol.h): cpuprofile(op, opts) with op start | stop |
    // reset | mark | status | report | frames | export; opts a table ({stack=, beam=, frames=, ring_kib=, stream=,
    // reset=} / {name=, drop=} / {since=, top=, sort=, callers=, beam=} / {last=, top=} / {format=, path=,
    // since=}) or, for mark the name, for report the top, for frames the last, for export the format. The answer:
    // the reply's fields plus text; a refusal: ok = false, error = why
    lua.set_function("cpuprofile", [this](sol::this_state ts, sol::optional<std::string> op,
                                          sol::optional<sol::object> opts) -> sol::object {
        const std::string verb = op.value_or("status");
        Emulator* emulator = effectiveEmulator();
        profile::Reply reply;
        if (!emulator)
            reply.error = profile::Error::NotAvailable, reply.message = "no emulator";
        else
            reply = profile::Execute(*emulator, verb,
                                     opts ? SymbolOptions(*opts, verb == "mark"     ? "name"
                                                                 : verb == "report" ? "top"
                                                                 : verb == "frames" ? "last"
                                                                 : verb == "export" ? "format"
                                                                                    : "")
                                          : profile::Options{});
        StateNode value = reply.ToValue();
        if (!reply.Ok())
        {
            value = StateNode::Object();
            value["ok"] = false;
            value["error"] = reply.message;
        }
        return StateNodeToLua(ts, value);
    });
    // The analyzers (GET /analyzers): analyzer_list(), analyzer_enable(name), analyzer_disable(name),
    // analyzer_active(name) (nil: no such analyzer)
    auto analyzers = [this]() -> AnalyzerManager* {
        Emulator* emulator = effectiveEmulator();
        auto* context = emulator ? emulator->GetContext() : nullptr;
        return context && context->pDebugManager ? context->pDebugManager->GetAnalyzerManager() : nullptr;
    };
    lua.set_function("analyzer_list", [this, analyzers]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        if (AnalyzerManager* manager = analyzers())
        {
            int index = 1;
            for (const std::string& id : manager->getRegisteredAnalyzers())
                result[index++] = lua_view.create_table_with("id", id, "active", manager->isActive(id));
        }
        return result;
    });
    lua.set_function("analyzer_enable", [analyzers](const std::string& name) -> bool {
        AnalyzerManager* manager = analyzers();
        return manager && manager->activate(name);
    });
    lua.set_function("analyzer_disable", [analyzers](const std::string& name) -> bool {
        AnalyzerManager* manager = analyzers();
        return manager && manager->deactivate(name);
    });
    lua.set_function("analyzer_active", [analyzers](sol::this_state ts, const std::string& name) -> sol::object {
        AnalyzerManager* manager = analyzers();
        if (!manager || !manager->hasAnalyzer(name))
            return sol::make_object(ts, sol::lua_nil);
        return sol::make_object(ts, manager->isActive(name));
    });

    // OS-aware analyzers (nedoos, dss): analyzer_events(name, limit),
    // analyzer_state(name) - tasks, page/block ownership, file handles
    lua.set_function("analyzer_events", [this, analyzers](const std::string& name,
                                                          sol::optional<unsigned> limitOpt) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        AnalyzerManager* manager = analyzers();
        IOSAnalyzer* osAnalyzer = manager ? dynamic_cast<IOSAnalyzer*>(manager->getAnalyzer(name)) : nullptr;
        if (!osAnalyzer) { result["error"] = "not an OS analyzer"; return result; }

        auto events = osAnalyzer->getOsEvents();
        const size_t limit = limitOpt.value_or(50);
        size_t index = 0;
        size_t start = events.size() > limit ? events.size() - limit : 0;
        for (size_t i = start; i < events.size(); i++)
        {
            sol::table item = lua_view.create_table();
            item["timestamp"] = events[i].timestamp;
            item["frame"] = events[i].frameNumber;
            item["kind"] = OsEventKindName(events[i].kind);
            item["severity"] = OsSeverityName(events[i].severity);
            item["formatted"] = events[i].format();
            item["pc"] = events[i].pc;
            item["task"] = events[i].taskId;
            result[index + 1] = item;
            index++;
        }
        return result;
    });
    lua.set_function("analyzer_state", [this, analyzers](const std::string& name) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        AnalyzerManager* manager = analyzers();
        IOSAnalyzer* osAnalyzer = manager ? dynamic_cast<IOSAnalyzer*>(manager->getAnalyzer(name)) : nullptr;
        if (!osAnalyzer) { result["error"] = "not an OS analyzer"; return result; }

        OsSnapshot snapshot = osAnalyzer->getOsSnapshot();
        result["os"] = snapshot.os;
        result["detected"] = snapshot.detected;
        result["profile"] = snapshot.profile;

        sol::table tasks = lua_view.create_table();
        size_t index = 0;
        for (const auto& task : snapshot.tasks)
        {
            sol::table item = lua_view.create_table();
            item["id"] = task.id;
            item["parent"] = task.parent;
            item["flags"] = task.flags;
            item["mainpg"] = task.mainpg;
            item["name"] = task.name;
            item["stdin"] = task.stdinFd;
            item["stdout"] = task.stdoutFd;
            item["stderr"] = task.stderrFd;
            item["last_time"] = task.lastTime;
            tasks[index + 1] = item;
            index++;
        }
        result["tasks"] = tasks;

        sol::table blocks = lua_view.create_table();
        index = 0;
        for (const auto& block : snapshot.blocks)
        {
            sol::table item = lua_view.create_table();
            item["index"] = block.index;
            item["owner"] = block.owner;
            blocks[index + 1] = item;
            index++;
        }
        result["blocks"] = blocks;

        sol::table handles = lua_view.create_table();
        index = 0;
        for (const auto& handle : snapshot.handles)
        {
            sol::table item = lua_view.create_table();
            item["index"] = handle.index;
            item["owner"] = handle.owner;
            item["name"] = handle.name;
            item["position"] = handle.position;
            item["size"] = handle.size;
            handles[index + 1] = item;
            index++;
        }
        result["handles"] = handles;

        sol::table pipes = lua_view.create_table();
        index = 0;
        for (const auto& pipe : snapshot.pipes)
        {
            sol::table item = lua_view.create_table();
            item["handle"] = pipe.handle;
            item["open_sides"] = pipe.openSides;
            item["reader"] = pipe.reader;
            item["lines"] = pipe.lines;
            item["queued"] = pipe.queued;
            pipes[index + 1] = item;
            index++;
        }
        result["pipes"] = pipes;
        return result;
    });

    // Code coverage control and queries
    lua.set_function("coverage_start", [this](sol::optional<bool> keepOpt) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        AnalyzerManager* manager = context && context->pDebugManager ?
                                       context->pDebugManager->GetAnalyzerManager() : nullptr;
        CoverageAnalyzer* coverage = manager ? manager->getAnalyzer<CoverageAnalyzer>("coverage") : nullptr;
        if (!coverage || !manager) { result["error"] = "coverage analyzer not available"; return result; }

        if (!keepOpt.value_or(false))
            coverage->clear();
        result["success"] = manager->activate("coverage");
        result["recording"] = coverage->isRecording();
        return result;
    });

    lua.set_function("coverage_stop", [this]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        AnalyzerManager* manager = context && context->pDebugManager ?
                                       context->pDebugManager->GetAnalyzerManager() : nullptr;
        if (!manager) { result["error"] = "analyzer manager not available"; return result; }

        result["success"] = manager->deactivate("coverage");
        return result;
    });

    lua.set_function("coverage_status", [this](sol::optional<unsigned> maxRangesOpt) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        AnalyzerManager* manager = context && context->pDebugManager ?
                                       context->pDebugManager->GetAnalyzerManager() : nullptr;
        CoverageAnalyzer* coverage = manager ? manager->getAnalyzer<CoverageAnalyzer>("coverage") : nullptr;
        if (!coverage || !manager) { result["error"] = "coverage analyzer not available"; return result; }

        const size_t executedCount = coverage->getExecutedCount();
        result["active"] = manager->isActive("coverage");
        result["recording"] = coverage->isRecording();
        result["executed_count"] = executedCount;
        result["coverage_percent"] = executedCount * 100.0 / 65536.0;
        result["instructions"] = static_cast<uint64_t>(coverage->getInstructionCount());

        sol::table ranges = lua_view.create_table();
        size_t index = 0;
        for (const auto& range : coverage->getExecutedRanges(maxRangesOpt.value_or(100)))
        {
            sol::table item = lua_view.create_table();
            item["start"] = range.first;
            item["end"] = range.second;
            ranges[index + 1] = item;
            index++;
        }
        result["ranges"] = ranges;
        return result;
    });

    // coverage_export([path], [{format=, start=, ["end"]=, merge=}]) or coverage_export({path=, ...}): the coverage
    // as SkoolKit's code execution map (coverageexport.h); without a path the map comes back in result.map
    lua.set_function("coverage_export", [this](sol::object first, sol::object second) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        coverageexport::Options options;
        const auto take = [&options](const sol::table& table) {
            for (const auto& [key, value] : table)
            {
                if (!key.is<std::string>())
                    continue;
                const std::string name = key.as<std::string>();
                if (value.is<bool>())
                    options[name] = value.as<bool>() ? "true" : "false";
                else if (value.get_type() == sol::type::number)
                    options[name] = std::to_string(value.as<int64_t>());
                else if (value.is<std::string>())
                    options[name] = value.as<std::string>();
            }
        };
        if (first.is<std::string>())
            options["path"] = first.as<std::string>();
        else if (first.is<sol::table>())
            take(first.as<sol::table>());
        if (second.is<sol::table>())
            take(second.as<sol::table>());

        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        AnalyzerManager* manager = context && context->pDebugManager ?
                                       context->pDebugManager->GetAnalyzerManager() : nullptr;
        const coverageexport::Result r =
            coverageexport::Export(manager ? manager->getAnalyzer<CoverageAnalyzer>("coverage") : nullptr, options);
        if (!r.Ok()) { result["error"] = r.message; return result; }
        result["success"] = true;
        result["format"] = r.format;
        result["start"] = r.start;
        result["end"] = r.end;
        result["count"] = r.count;
        if (r.path.empty())
            result["map"] = r.text;
        else
        {
            result["path"] = r.path;
            result["merged"] = r.merged;
        }
        result["message"] = r.message;
        return result;
    });

    lua.set_function("coverage_gaps",
                     [this](sol::optional<unsigned> startOpt, sol::optional<unsigned> endOpt,
                            sol::optional<unsigned> maxOpt) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        AnalyzerManager* manager = context && context->pDebugManager ?
                                       context->pDebugManager->GetAnalyzerManager() : nullptr;
        CoverageAnalyzer* coverage = manager ? manager->getAnalyzer<CoverageAnalyzer>("coverage") : nullptr;
        if (!coverage || !manager) { result["error"] = "coverage analyzer not available"; return result; }

        const uint16_t start = static_cast<uint16_t>(startOpt.value_or(0x4000));
        const uint16_t end = static_cast<uint16_t>(endOpt.value_or(0xFFFF));

        sol::table gaps = lua_view.create_table();
        size_t index = 0;
        for (const auto& gap : coverage->getGaps(start, end, maxOpt.value_or(100)))
        {
            sol::table item = lua_view.create_table();
            item["start"] = gap.first;
            item["end"] = gap.second;
            gaps[index + 1] = item;
            index++;
        }
        result["gaps"] = gaps;
        result["count"] = index;
        return result;
    });

    // Section timing (SectionTimerAnalyzer): section_timer_start{sections = {"Plasma", "spark=#8000:#8010"},
    // break_if = {"Frame>69888"}, extra = {...}, recursive = false, frame_t = 0, capacity = N} -> the results
    // without the passes, or nil and the reason; section_timer_stop(), section_timer_clear(),
    // section_timer_result([offset [, limit]]) -> a table, section_timer_text() -> a table as text
    auto sectionTimer = [this]() -> SectionTimerAnalyzer* {
        Emulator* emulator = effectiveEmulator();
        return SectionTimerAnalyzer::Find(emulator ? emulator->GetContext() : nullptr);
    };
    auto sectionTimerTable = [](sol::state_view view, const SectionTimerAnalyzer::Result& r) -> sol::table {
        sol::table t = view.create_table();
        t["active"] = r.active;
        t["frame_t"] = r.frameT;
        sol::table sections = view.create_table();
        for (size_t i = 0; i < r.sections.size(); i++)
        {
            const auto& section = r.sections[i];
            const auto x = i < r.stats.size() ? r.stats[i] : SectionTimerAnalyzer::Stats{};
            sol::table one = view.create_table();
            one["name"] = section.name;
            one["entry"] = section.entry;
            sol::table exits = view.create_table();
            for (size_t e = 0; e < section.exits.size(); e++)
                exits[e + 1] = section.exits[e];
            one["exits"] = exits;
            one["count"] = x.count;
            one["min"] = x.min;
            one["max"] = x.max;
            one["avg"] = x.count ? x.sum / x.count : 0;
            one["net_min"] = x.netMin;
            one["net_max"] = x.netMax;
            one["net_avg"] = x.count ? x.netSum / x.count : 0;
            one["interrupted"] = x.interrupted;
            one["last"] = x.last;
            one["max_frame"] = x.maxFrame;
            one["max_t"] = x.maxT;
            sections[i + 1] = one;
        }
        t["sections"] = sections;
        auto name = [&](uint16_t section) {
            return section < r.sections.size() ? r.sections[section].name : std::string();
        };
        sol::table passes = view.create_table();
        for (size_t i = 0; i < r.passes.size(); i++)
        {
            const auto& p = r.passes[i];
            passes[i + 1] = view.create_table_with("name", name(p.section), "frame", p.frame, "t", p.t, "elapsed",
                                                   p.elapsed, "net", p.net, "interrupts", p.interrupts);
        }
        t["passes"] = passes;
        t["passes_total"] = r.passesTotal;
        t["passes_kept"] = r.passesKept;
        t["hits"] = r.hits;
        t["orphan"] = r.orphans;
        t["dropped"] = r.dropped;
        t["open"] = r.open;
        if (r.stop.hit)
        {
            const auto& p = r.stop.pass;
            t["stopped"] = view.create_table_with("condition", r.stop.condition, "section", name(p.section), "frame",
                                                  p.frame, "t", p.t, "elapsed", p.elapsed, "net", p.net,
                                                  "interrupts", p.interrupts);
        }
        return t;
    };
    auto strings = [](const sol::table& spec, const char* key) {
        std::vector<std::string> out;
        sol::object value = spec[key];
        if (value.is<std::string>())
            out.push_back(value.as<std::string>());
        else if (value.get_type() == sol::type::number)
            out.push_back(std::to_string(value.as<uint32_t>()));
        else if (value.is<sol::table>())
        {
            sol::table list = value.as<sol::table>();
            for (size_t i = 1; i <= list.size(); i++)
            {
                sol::object one = list[i];
                out.push_back(one.get_type() == sol::type::number ? std::to_string(one.as<uint32_t>())
                                                                   : one.as<std::string>());
            }
        }
        return out;
    };
    lua.set_function("section_timer_start", [sectionTimer, sectionTimerTable, strings](
                                                 sol::optional<sol::table> specOpt, sol::this_state ts)
                                                 -> std::tuple<sol::object, sol::object> {
        sol::state_view view(ts);
        SectionTimerAnalyzer* timer = sectionTimer();
        if (!timer)
            return {sol::lua_nil, sol::make_object(view, std::string("section timer not available"))};
        SectionTimerAnalyzer::Spec spec;
        if (specOpt)
        {
            const sol::table& t = *specOpt;
            spec.sections = strings(t, "sections");
            spec.breakIf = strings(t, "break_if");
            spec.extra = strings(t, "extra");
            spec.recursive = t.get_or("recursive", false);
            spec.frameT = t.get_or("frame_t", 0u);
            spec.capacity = t.get_or("capacity", SectionTimerAnalyzer::kDefaultCapacity);
        }
        std::string error;
        if (!timer->Start(spec, error))
            return {sol::lua_nil, sol::make_object(view, error)};
        return {sectionTimerTable(view, timer->GetResult(0, 0)), sol::lua_nil};
    });
    lua.set_function("section_timer_stop", [sectionTimer]() -> bool {
        SectionTimerAnalyzer* timer = sectionTimer();
        return timer && timer->Stop();
    });
    lua.set_function("section_timer_clear", [sectionTimer]() {
        if (SectionTimerAnalyzer* timer = sectionTimer())
            timer->Clear();
    });
    lua.set_function("section_timer_result", [sectionTimer, sectionTimerTable](sol::optional<size_t> offset,
                                                                               sol::optional<size_t> limit,
                                                                               sol::this_state ts) -> sol::object {
        SectionTimerAnalyzer* timer = sectionTimer();
        if (!timer)
            return sol::lua_nil;
        return sectionTimerTable(sol::state_view(ts), timer->GetResult(offset.value_or(0), limit.value_or(1000)));
    });
    lua.set_function("section_timer_text", [sectionTimer]() -> std::string {
        SectionTimerAnalyzer* timer = sectionTimer();
        return timer ? SectionTimerAnalyzer::ToText(timer->GetResult(0, 0)) : std::string();
    });
    lua.set_function("section_timer_json", [sectionTimer](sol::optional<size_t> offset,
                                                          sol::optional<size_t> limit) -> std::string {
        SectionTimerAnalyzer* timer = sectionTimer();
        return timer ? SectionTimerAnalyzer::ToJson(timer->GetResult(offset.value_or(0), limit.value_or(SIZE_MAX)))
                     : std::string();
    });

    // AY register-write logging
    lua.set_function("ay_log_start", [this](sol::optional<unsigned> capacityOpt) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        AnalyzerManager* manager = context && context->pDebugManager ?
                                       context->pDebugManager->GetAnalyzerManager() : nullptr;
        AYLogAnalyzer* aylog = manager ? manager->getAnalyzer<AYLogAnalyzer>("aylog") : nullptr;
        if (!aylog || !manager) { result["error"] = "AY log analyzer not available"; return result; }

        result["success"] = manager->activate("aylog");
        aylog->setCapacity(capacityOpt.value_or(4096));
        result["capacity"] = aylog->getCapacity();
        return result;
    });

    lua.set_function("ay_log_stop", [this]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        AnalyzerManager* manager = context && context->pDebugManager ?
                                       context->pDebugManager->GetAnalyzerManager() : nullptr;
        if (!manager) { result["error"] = "analyzer manager not available"; return result; }

        result["success"] = manager->deactivate("aylog");
        return result;
    });

    lua.set_function("ay_log_status", [this]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        AnalyzerManager* manager = context && context->pDebugManager ?
                                       context->pDebugManager->GetAnalyzerManager() : nullptr;
        AYLogAnalyzer* aylog = manager ? manager->getAnalyzer<AYLogAnalyzer>("aylog") : nullptr;
        if (!aylog || !manager) { result["error"] = "AY log analyzer not available"; return result; }

        result["active"] = manager->isActive("aylog");
        result["recording"] = aylog->isRecording();
        result["entry_count"] = static_cast<uint64_t>(aylog->getEntryCount());
        result["capacity"] = static_cast<uint64_t>(aylog->getCapacity());
        result["dropped"] = static_cast<uint64_t>(aylog->getDroppedCount());
        return result;
    });

    lua.set_function("ay_log_dump", [this](sol::optional<unsigned> countOpt,
                                            sol::optional<unsigned> offsetOpt) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        AnalyzerManager* manager = context && context->pDebugManager ?
                                       context->pDebugManager->GetAnalyzerManager() : nullptr;
        AYLogAnalyzer* aylog = manager ? manager->getAnalyzer<AYLogAnalyzer>("aylog") : nullptr;
        if (!aylog || !manager) { result["error"] = "AY log analyzer not available"; return result; }

        const size_t total = aylog->getEntryCount();
        const size_t count = countOpt.value_or(20);
        const size_t offset = offsetOpt.value_or(total > count ? static_cast<unsigned>(total - count) : 0u);

        sol::table records = lua_view.create_table();
        size_t index = 0;
        for (const auto& record : aylog->getEntries(offset, count))
        {
            sol::table item = lua_view.create_table();
            item["frame"] = static_cast<uint64_t>(record.frame);
            item["tacts"] = record.tacts;
            item["pc"] = record.pc;
            item["port"] = record.port;
            item["chip"] = record.chip;
            item["reg"] = record.reg;
            item["value"] = record.value;
            item["type"] = record.port == 0xFFFD ? (record.value > 0x0F ? "switch" : "select") : "write";
            records[index + 1] = item;
            index++;
        }
        result["records"] = records;
        result["total"] = static_cast<uint64_t>(total);
        return result;
    });

    // Buffered stereo audio capture (records into analyzer RAM, then export)
    // audio_mixer(): the per-device mixer (DeviceState::AudioMixer); audio_mixer_set(source, {muted=, solo=,
    // volume=, gain_db=}) -> the mixer, or nil, error (AudioMixer::Apply; source "master" takes muted only; a
    // channel is "<source>.<channel>": "ay1.a", "gs.3", "moonsound_fm.7")
    lua.set_function("audio_mixer", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, DeviceState::AudioMixer(emulator->GetContext()));
    });
    lua.set_function("audio_mixer_set", [this](sol::this_state s, const std::string& source, sol::table options) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        auto text = [&](const char* key) -> std::string {
            sol::object value = options[key];
            if (value.get_type() == sol::type::boolean)
                return value.as<bool>() ? "1" : "0";
            if (value.get_type() == sol::type::number)
                return std::to_string(value.as<double>());
            if (value.get_type() == sol::type::string)
                return value.as<std::string>();
            return std::string();
        };
        AudioMixer::Change change;
        std::string error;
        if (!AudioMixer::ChangeFromStrings(text("muted"), text("solo"), text("volume"), text("gain_db"), change, error) ||
            !AudioMixer::Apply(emulator->GetContext(), source, change, error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, DeviceState::AudioMixer(emulator->GetContext())));
        return out;
    });
    // audio_mixer_get(source) -> one row ("ay1") or channel ("ay1.a"), or nil, error; audio_levels() -> every
    // row's and channel's levels and mute / solo (the WebSocket topic audio.mixer)
    lua.set_function("audio_mixer_get", [this](sol::this_state s, const std::string& source) -> sol::variadic_results {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return mouseError(s, "No emulator selected");
        StateNode node;
        std::string error;
        if (!AudioMixer::Read(emulator->GetContext(), source, node, error))
            return mouseError(s, error);
        sol::variadic_results out;
        out.push_back(StateNodeToLua(s, node));
        return out;
    });
    lua.set_function("audio_levels", [this](sol::this_state s) -> sol::object {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return sol::make_object(s, sol::lua_nil);
        return StateNodeToLua(s, AudioMixer::Levels(emulator->GetContext()));
    });

    // audio_capture_start(seconds [, source [, file]]): source = a mixer key (beeper, ay1, covox, gs ...) records
    // that device's own buffer; default the master mix. file = a WAV path: the samples are written there while
    // they come (seconds up to 86400, no memory ceiling); it closes by itself at the target
    lua.set_function("audio_capture_start", [this](double seconds, sol::optional<std::string> sourceKey,
                                                   sol::optional<std::string> fileOpt) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        if (!context || !context->pDebugManager) { result["error"] = "debug manager not available"; return result; }

        const std::string file = fileOpt.value_or("");
        const double maxSeconds = file.empty() ? 30.0 : AudioCaptureAnalyzer::MAX_STREAM_SECONDS;
        if (seconds < 0.01 || seconds > maxSeconds)
        {
            result["error"] = file.empty() ? "seconds must be within [0.01, 30.0] (give a file for longer)"
                                           : "seconds must be within [0.01, 86400]";
            return result;
        }

        AnalyzerManager* manager = context->pDebugManager->GetAnalyzerManager();
        AudioCaptureAnalyzer* capture =
            manager ? manager->getAnalyzer<AudioCaptureAnalyzer>("audiocapture") : nullptr;
        if (!capture || !manager) { result["error"] = "audio capture analyzer not available"; return result; }

        AudioSourceType source = AudioSourceType::MasterMix;
        std::string sourceError;
        if (!AudioMixer::Capturable(context, sourceKey.value_or(""), source, sourceError))
        {
            result["error"] = sourceError;
            return result;
        }

        const size_t rate = context->pSoundManager ? context->pSoundManager->getCoreRate() : 44100;
        const size_t target = static_cast<size_t>(seconds * static_cast<double>(rate)) * 2;

        manager->activate("audiocapture");
        if (!file.empty())
        {
            std::string streamError;
            if (!capture->startStreamCapture(file, static_cast<uint32_t>(rate), target, source, streamError))
            {
                result["error"] = streamError;
                return result;
            }
            result["wav_path"] = file;
        }
        else
            capture->startCapture(target, source);

        result["armed"] = capture->isCaptureArmed();
        result["source"] = AudioMixer::Key(source);
        result["target_samples"] = static_cast<uint64_t>(target);
        result["sample_rate"] = static_cast<uint64_t>(rate);
        return result;
    });

    lua.set_function("audio_capture_status", [this]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        AnalyzerManager* manager = context && context->pDebugManager ?
                                       context->pDebugManager->GetAnalyzerManager() : nullptr;
        AudioCaptureAnalyzer* capture =
            manager ? manager->getAnalyzer<AudioCaptureAnalyzer>("audiocapture") : nullptr;
        if (!capture || !manager) { result["error"] = "audio capture analyzer not available"; return result; }

        result["armed"] = capture->isCaptureArmed();
        result["complete"] = capture->isCaptureComplete();
        result["captured_samples"] = static_cast<uint64_t>(capture->getCapturedSamples());
        result["target_samples"] = static_cast<uint64_t>(capture->getTargetSamples());
        result["to_file"] = capture->isStreaming();
        if (capture->isStreaming())
        {
            result["wav_path"] = capture->getStreamPath();
            result["file_closed"] = capture->isStreamClosed();
            result["file_failed"] = capture->hasStreamFailed();
        }
        return result;
    });
}
