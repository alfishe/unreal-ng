#pragma once

/// @file actionparams.h
/// @brief Reading an action's params (ActionValue::params) the same way from every plane: the WebAPI and MCP give JSON
/// types, the CLI's `name=value` words and Lua / Python give strings, numbers and bools. A number may come as a string
/// ("10", "0x8000"), a bool as "true" / "on" / "1"; a list as an array or as one string joined with '+' or ','.

#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "emulator/actions/actionregistry.h"
#include "emulator/state/statenode.h"

namespace actions
{

/// A scalar as text: a string as it is, an integer in decimal, a bool "true" / "false"; nullopt when absent or not a
/// scalar
std::optional<std::string> TextParam(const ActionValue& value, const char* name);
/// An integer (an Int, a whole Double, or a decimal / 0x hex string); nullopt when absent. Present but not an integer:
/// nullopt and `error` names it
std::optional<int64_t> IntParam(const ActionValue& value, const char* name, std::string& error);
/// A number (Int, Double or a numeric string); nullopt when absent, `error` when present but not a number
std::optional<double> NumberParam(const ActionValue& value, const char* name, std::string& error);
/// A bool (Bool, an Int 0 / 1, "true" / "false" / "on" / "off" / "yes" / "no" / "1" / "0"); nullopt when absent,
/// `error` when present but not a bool
std::optional<bool> BoolParam(const ActionValue& value, const char* name, std::string& error);
/// A list: an array of scalars, or one string split at '+' and ',' (spaces trimmed); empty when absent
std::vector<std::string> ListParam(const ActionValue& value, const char* name);

/// Every scalar param as text, for the verbs that take string options (TTDControl, MediaControl); `skip` names the
/// params the caller reads itself
std::map<std::string, std::string> ParamOptions(const StateNode& params, std::initializer_list<const char*> skip = {});

/// The action's error code for an HTTP status a core control answers with (400 "Bad Request", 404 "Not Found",
/// 409 / 422 "Conflict", 501 "Not Implemented", 5xx "Internal")
std::string ErrorForStatus(int status);

}  // namespace actions
