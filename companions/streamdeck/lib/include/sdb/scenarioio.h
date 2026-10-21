// Scenario documents in JSON, YAML, CSV and TSV (vendored nlohmann/json, fkYAML, rapidcsv).
//
// JSON and YAML hold the document as is. CSV / TSV hold one row per node, depth first, parents before children:
//   id, parent, type, title, row, col, label, icon, bg, fg, kind, method, arg, then, tags, extra
// The first data row (type "scenario") carries the name (id / title), the model (kind) and the settings (extra).
// `arg` is the main argument of the action kind (webapi path, http url, shell command, open target, keys combo,
// nav to, delay ms, brightness value); `tags` holds the node's tags joined with ';' (',' is read too); `extra` is
// a JSON object with everything else (nav, strip, dials, the rest of "face" and "do", face_on), so a spreadsheet
// edit of the simple columns round-trips without touching JSON.
#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sdb::scn::io
{

using json = nlohmann::json;

enum class Format
{
    Json,
    Yaml,
    Csv,
    Tsv
};

[[nodiscard]] std::optional<Format> FormatByName(std::string_view name);          // json yaml yml csv tsv
[[nodiscard]] std::optional<Format> FormatOf(const std::filesystem::path& file);  // by extension
[[nodiscard]] const char* FormatName(Format f);

[[nodiscard]] std::string Serialize(const json& doc, Format f);
[[nodiscard]] std::optional<json> Deserialize(const std::string& text, Format f, std::string* error = nullptr);

// Format from the extension (unknown -> JSON).
[[nodiscard]] std::optional<json> LoadFile(const std::filesystem::path& file, std::string* error = nullptr);
bool SaveFile(const std::filesystem::path& file, const json& doc, std::string* error = nullptr);

// ---- includes: {"type": "include", "file": "debug.yaml", "node"?: "id", "title"? / "face"? overrides}
// Replaces every include with the subtree it names (the file's root by default), recursively, files relative to the
// including file. The included root takes the include's id and slot; its descendants are prefixed "<includeId>."
// (so one file can be included twice), nav targets inside follow. Every resolved node carries "included_from".
// Cycles, missing files / nodes and nesting deeper than 8 are errors. `files` gets every file read.
[[nodiscard]] std::optional<json> ResolveIncludes(const json& doc, const std::filesystem::path& baseDir,
                                                  std::string* error = nullptr,
                                                  std::vector<std::string>* files = nullptr);
// loadFile + resolveIncludes (relative to the file's folder).
[[nodiscard]] std::optional<json> LoadResolved(const std::filesystem::path& file, std::string* error = nullptr,
                                               std::vector<std::string>* files = nullptr);
// Removes the "included_from" marks from a node and its items (a whole document: pass doc["root"]). The marks are
// the editor's (read-only, italics); a copy, an export, `scenario resolve` and the run file are documents of their
// own and must not carry the absolute paths of the files they came from.
void StripIncludeMarks(json& node);

// YAML <-> JSON values (exposed for tests and tools).
[[nodiscard]] std::string ToYaml(const json& j);
[[nodiscard]] std::optional<json> FromYaml(const std::string& text, std::string* error = nullptr);

}  // namespace sdb::scn::io
