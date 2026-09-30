#pragma once

/// @file statenodejson.h
/// @brief StateNode -> compact JSON text, for the surfaces that print JSON
/// without a JSON library of their own (the CLI's --json). The WebAPI keeps
/// its Json::Value converter (webapi/src/common/statenode_json.h); both give
/// the same document.

#include <string>

#include "emulator/state/statenode.h"

/// Compact JSON; strings escaped per RFC 8259 (UTF-8 passes through)
std::string StateNodeToJsonText(const StateNode& node);
