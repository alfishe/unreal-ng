#pragma once

/// @file yaml.h
/// @brief The profile files are YAML (mapping-and-profiles.md §7, §7.5): rapidyaml reads them into the JsonValue tree
/// the validation, the merge and the WebAPI / MCP JSON work on. rapidyaml is a library of its own (the core's symbols
/// are not used here) and stays behind this header.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "profiles/json.h"

namespace controllers
{

/// One YAML document -> JsonValue. A quoted scalar is a string; a plain one is null (`~`, `null`, empty), a bool
/// (`true`, `false`), a number (YAML 1.2 core: decimal, `0x`, `0o`) or a string. Anchors resolve; a duplicate key is an
/// error. False with "line L, column C: reason"
bool ParseYaml(const std::string& text, JsonValue& out, std::string& error);

/// A bundle's documents: [offset, length) slices of @p text, split on the `---` lines (a document starts after its
/// marker; a text with no marker is one document). Empty documents (blank lines and comments only) are skipped
std::vector<std::pair<size_t, size_t>> SplitYamlDocuments(const std::string& text);

/// Block style for objects and arrays down to @p flowDepth, flow style (`{ key: ENTER, label: Fire }`) below it; a
/// string is plain where YAML reads it back as the same string, else double-quoted
std::string DumpYaml(const JsonValue& value, int flowDepth = 3);

}  // namespace controllers
