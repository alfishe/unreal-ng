#pragma once

/// @file streamdeckapi.h
/// @brief The Stream Deck daemon's verbs on every automation plane, in one place (automation planes guidelines §1):
/// its supervision by unreal-qt, its settings, its pairing, the brightness of its decks. WebAPI
/// `/api/v1/streamdeck/...` (and so the WebSocket `call`), MCP `streamdeck`, the CLI `streamdeck`, Lua and Python
/// `streamdeck()` call Call() and serialise its StateNode: the same fields, errors and statuses everywhere. The
/// settings page in unreal-qt (Tools > Controllers > Stream Deck) shows and changes the same.
///
/// | Verb | Args | Does |
/// |:--|:--|:--|
/// | status | | the supervisor and the daemon: state, pid, own / adopted, version, decks, viewers, pairing, settings |
/// | start | | start it (or adopt the one running), also in mode off |
/// | stop | | stop it (POST /api/v1/quit, then terminate); nothing starts again until start |
/// | restart | | stop, then start |
/// | settings | mode?, activity?, api_port?, viewers_port?, binary? | read, or change and read |
/// | pairing | | the daemon's pairing state: idle, pending (code), paired, denied, expired, refused; companion_id |
/// | pair | | the daemon asks unreal-ng for a pairing; the user approves it at the GUI (never a plane) |
/// | pair_cancel | | a pending pairing is cancelled |
/// | revoke | id? | revoke the daemon's companion (its token stops working; the deck offers to pair again) |
/// | brightness | deck? (first), value? | a deck's brightness (0..100): read, or set and keep for that deck |
/// | udev | | the Linux udev rule text (an AppImage cannot install it) |
///
/// Statuses: 200, 400 Bad Request, 404 Not Found, 409 Conflict (not running), 502 the daemon refused, 503 no
/// supervisor (headless, the video wall: it lives in unreal-qt).

#include <string>
#include <string_view>
#include <vector>

#include "emulator/state/statenode.h"

namespace companion
{
namespace streamdeck
{

struct Reply
{
    int status = 200;
    StateNode body = StateNode::Object();  ///< {ok: true, ...} or {ok: false, error, message}
    bool Ok() const { return status == 200; }
};

/// Every verb, in the table's order
const std::vector<std::string>& Verbs();

/// Run a verb with its args (an object; text values are accepted for numbers, as the CLI gives them)
Reply Call(std::string_view verb, const StateNode& args);

} // namespace streamdeck
} // namespace companion
