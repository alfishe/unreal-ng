#pragma once

/// @file statenodejson.h
/// @brief StateNode <-> compact JSON text, for the surfaces that print JSON
/// without a JSON library of their own (the CLI's --json), and for reading a
/// companion's answers (the Stream Deck daemon's automation). The WebAPI keeps
/// its Json::Value converter (webapi/src/common/statenode_json.h); both give
/// the same document.

#include <string>

#include "emulator/state/statenode.h"

/// Compact JSON; strings escaped per RFC 8259 (UTF-8 passes through)
std::string StateNodeToJsonText(const StateNode& node);

/// JSON text (RFC 8259) -> StateNode: integers stay Int, other numbers Double,
/// `\uXXXX` escapes (surrogate pairs too) become UTF-8. False with `error` (and
/// `out` null) for anything that is not one JSON value
bool StateNodeFromJsonText(const std::string& text, StateNode& out, std::string* error = nullptr);
