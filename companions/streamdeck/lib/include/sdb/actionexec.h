// The `action` kind's executor (scenarios.md §2): a scenario step `{kind: action, id, value?, params?, hold?}` becomes
// a call of the host's action registry. Behind an interface: the surface client of the daemon (SD5, over the `surface`
// topic of /api/v1/ws) implements it later; until then the REST fallback posts the call to
// POST /api/v1/controllers/actions/{id}, which works against any running unreal-qt.
#pragma once
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "sdb/http.h"

namespace sdb::scn
{

using json = nlohmann::json;
struct ActionResult;

// One call of a registry action, as the routes take it (POST body {value?, params?}):
//   value  - press: null (a press) or true / false (press / release of a held action); delta: {delta, rate?};
//            text: a string; level: a number
//   params - an object or null
struct ActionCall
{
    std::string id;
    json value;
    json params;
    [[nodiscard]] json Body() const;  // {value?, params?}
};

// What a key press or a dial event of a step asks, from the step's fields after "$ticks" / "$rate" are filled in:
//   "value" omitted          -> a press (hold: value true on the press, false on the release)
//   "value": {d, rate?, ...} -> a delta {delta: d, rate}; the other fields are params (ttd.seek_by: unit, min, max)
//   "value": "text"          -> the text (ctx.set, input.type); {text, ...}: the text, the rest params
//   "value": {...} otherwise -> params of a press (mach.start {model, ram_kb, replace}); hold as above
//   "value": a number / bool -> as it is (level, a press flag)
//   "params": {...}          -> params too (merged over the ones the value carried)
// phase: 0 = once, +1 = the press of a held step, -1 its release.
[[nodiscard]] ActionCall MakeActionCall(const json& step, int phase = 0);

class ActionExecutor
{
public:
    virtual ~ActionExecutor() = default;
    // Runs the call on the host's target instance; blocks until the host answers (the runtime's action worker)
    virtual ActionResult Run(const ActionCall& call) = 0;
};

// POST {path} with a JSON body, the caller's headers (companion token) and base URL: the REST fallback's transport.
using RestPost = std::function<HttpResponse(const std::string& path, const json& body)>;
// The REST fallback: POST /api/v1/controllers/actions/{id} {instance?, value?, params?}. `instance` = the id to send
// ("" = none: the host picks its followed / selected / sole instance).
[[nodiscard]] std::unique_ptr<ActionExecutor> MakeRestActionExecutor(RestPost post,
                                                                     std::function<std::string()> instance = {});

}  // namespace sdb::scn
