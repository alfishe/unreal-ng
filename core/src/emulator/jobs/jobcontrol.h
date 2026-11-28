#pragma once

/// @file jobcontrol.h
/// @brief The `jobs` verbs for every surface (the jobregistry.h jobs): CLI `jobs`, Lua jobs(), Python emu.jobs(),
/// the WebAPI /api/v1/jobs routes (and the WebSocket `call` of them), MCP `jobs`. Options, defaults, refusals, the
/// answer's fields and its text live here, so the surfaces cannot drift apart (the analysiscontrol.h pattern).
/// Design: docs/emulator/design/control-interfaces/jobs.md.
///
/// Actions:
///   list   [instance state limit]  the jobs, oldest first: instance this (the selected one, the default when there
///                                  is one) | all | <id>; state active (queued, running) | finished | all (default)
///   status job [result]           one job: state, progress, the result when it ended (result=false: without it)
///   wait   job [wait_ms result]   block up to wait_ms (default 30000, at most kMaxWaitMs) for it to end, then as
///                                 status (a script's thread; the WebAPI long-polls without one)
///   cancel job                    a queued job ends cancelled; a running one is asked to stop
///   start  domain verb [...]      run a long verb as a job: domain ttd (verb dump, load, probe-frames, ...) or
///                                 analysis (verb build, trace, experiment, cover), the verb's own options beside;
///                                 the same as the verb with async=true
///
/// @code
///   jobs::Reply reply = jobs::Execute(emulator, "start", {{"domain", "ttd"}, {"verb", "dump"}, {"path", "/x.ttd"}});
///   jobs::Execute(emulator, "wait", {{"job", reply.body["job"]...}, {"wait_ms", "60000"}});
/// @endcode

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "emulator/jobs/jobregistry.h"
#include "emulator/state/statenode.h"

class Emulator;

namespace jobs
{

using Options = std::map<std::string, std::string>;

enum class Error : uint8_t
{
    None,
    BadRequest,   ///< unknown action or option, a value that does not parse (HTTP 400)
    NotFound,     ///< no such job (HTTP 404), no instance (start)
    Conflict,     ///< not possible now: the job cannot stop, it ended already (HTTP 409)
    Unavailable,  ///< the queue is full, the registry stops (HTTP 503)
};

int ErrorHttpStatus(Error error);
const char* ErrorPhrase(Error error);

struct Reply
{
    Error error = Error::None;
    std::string message;  ///< the refusal, one sentence a user can act on
    std::string text;     ///< the readable answer (CLI, MCP content[])
    StateNode body = StateNode::Object();
    bool accepted = false;  ///< start: a job was queued (HTTP 202)

    bool Ok() const { return error == Error::None; }
    /// Success: the body plus "text". Failure: {"error": <phrase>, "message": ...}
    StateNode ToValue() const;
    int HttpStatus() const { return Ok() ? (accepted ? 202 : 200) : ErrorHttpStatus(error); }
};

const std::vector<std::string>& Actions();
/// The option names an action takes ({"*"}: start passes the verb's own on)
const std::vector<std::string>& OptionsFor(const std::string& action);

/// Check the action and its options, then run it. @p emulator: the selected instance (list's default filter, start's
/// target); may be null
Reply Execute(Emulator* emulator, const std::string& action, const Options& options = {});

/// "async" given as true / false / 1 / 0 / yes / no / "" (a bare flag: true); false for anything else
bool ParseAsync(const std::string& text, bool& async);

/// Submit @p call as a job of the instance @p emulatorId: the instance is looked up and leased (its context held)
/// when the job runs, so a removal waits for it or its cancel. Lane: one at a time per lane ("" : the kind and the
/// instance). 0 with @p error when the registry refuses
uint64_t SubmitOnInstance(const std::string& emulatorId, const std::string& kind, const std::string& lane,
                          const std::string& summary, bool cancellable,
                          std::function<Outcome(Emulator&, Context&)> call, std::function<void(Emulator&)> onCancel,
                          std::string& error);

/// The answer of a verb started as a job: {job, state, kind, emulator_id, async: true, status_url, wait_url, text}
StateNode AcceptedBody(uint64_t id);
/// "Job 12 (ttd.dump) queued on <id>: GET /api/v1/jobs/12?wait_ms=30000 (MCP jobs action=wait job=12) for its end"
std::string AcceptedText(uint64_t id);
/// "dump path=/tmp/a.ttd lineage=true" (options in name order, long values cut)
std::string Summary(const std::string& verb, const std::map<std::string, std::string>& options);
/// The WebSocket topic `jobs.progress` of an instance: {active: [{job, kind, summary, state, done, total, unit,
/// percent, hits?, cancel_requested}], count}
StateNode ProgressTopic(const std::string& emulatorId);
/// The topic `jobs.done`: {recent: [{job, kind, summary, state, status, error, ended_ms}] (the last 8 that ended,
/// newest first), last_job}
StateNode DoneTopic(const std::string& emulatorId);

/// The outcome's text for a reply body without one: "HTTP 200: {compact JSON, cut at @p maxChars}"
std::string ResultText(int status, const StateNode& value, size_t maxChars = 1500);

}  // namespace jobs
