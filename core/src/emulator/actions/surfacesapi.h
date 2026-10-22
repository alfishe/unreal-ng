#pragma once

/// @file surfacesapi.h
/// @brief The surface verbs of every automation plane, in one place (automation planes guidelines §1): the views and
/// the followed instance, the surface sessions and their pins, a target, the readouts, the activity feed. The WebAPI
/// routes (`/api/v1/surfaces/...`, and so the WebSocket `call`), MCP `actions`, the CLI `surface`, Lua and Python
/// `surfaces()` all call Call() and serialise its StateNode: the same fields, errors and statuses everywhere.
///
/// | Verb | Args | Does |
/// |:--|:--|:--|
/// | views | | the views in focus order, the followed instance (`followed`, the §2 target object) |
/// | view_add | name, active? | a view owned by the automation plane (an out-of-process view, a test) |
/// | view_update | view, active?, activate? | `active` (only a view the plane owns), `activate`: to the front |
/// | view_remove | view | removes a view the plane owns |
/// | target | session? | the followed instance's target object, or that session's |
/// | sessions | | every surface session and its target |
/// | session | session | one |
/// | session_open | name, kind? (api), serial?, target? | a surface session owned by the plane (a REST surface) |
/// | session_close | session | closes one the plane owns |
/// | pin | session, target | pin or unpin (target "follow") a session |
/// | readouts | instance?, names? | the readouts' values now (names or prefixes `media.*`; default all) |
/// | activity | since?, instance?, limit? | the activity feed after `since` |
///
/// Targets are given as "follow", "view:<id>", "instance:<id|index>", or {"view": id} / {"instance": id}.

#include <string>
#include <string_view>
#include <vector>

#include "emulator/actions/targetstate.h"
#include "emulator/state/statenode.h"

namespace actions
{
namespace surfaces
{

struct Reply
{
    int status = 200;  ///< 200, 400 Bad Request, 404 Not Found, 409 Conflict / not_now
    StateNode body = StateNode::Object();  ///< {ok: true, ...} or {ok: false, error, message}
    bool Ok() const { return status == 200; }
};

/// Every verb, in the table's order
const std::vector<std::string>& Verbs();

/// Run a verb with its args (an object; text values are accepted for numbers and bools, as the CLI gives them)
Reply Call(std::string_view verb, const StateNode& args);

/// The protocol's target object (protocol.md §2): {instance, index, model, ram_kb, view: {id, name} | null, mode,
/// pin, source}; instance null when there is no machine
StateNode TargetNode(const TargetInfo& target);

/// Rule T-1 (targeting.md §9): the instance a WebAPI request without an id acts on - the followed instance when the
/// host has views, else the selected one, else the sole one. "" with `error` ("no machine", "several instances: name
/// one (instance)") when none fits
std::string DefaultInstance(std::string& error);

/// A target from text or an object; an instance named by index becomes its id. False with `error`
bool ParseTarget(const StateNode& node, Pin& pin, std::string& error);

}  // namespace surfaces
}  // namespace actions
