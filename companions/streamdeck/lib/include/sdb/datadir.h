// The companion's data directory: `$data` in scenario paths (docs/inprogress/2026-10-19-streamdeck-integration/
// scenarios.md §3), laid out as data/ of the repository (streamdeck/ with themes and scenarios, icons/). Found as
// unreal-qt finds its resources (FileHelper::GetResourcesPath): Contents/Resources of the app bundle on macOS, beside
// the executable on Windows and Linux (the DEB's lib/unreal-ng), then the source tree for a build that was not
// installed. Lookup order and rationale: docs/companions/streamdeck/daemon.md, "Data directory".
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace sdb::data
{

// A folder holding streamdeck/: taken before every other place
inline constexpr const char* kEnv = "UNREAL_STREAMDECK_DATA";

// Where to look, in order, for an executable in `exeDir`: `env` (when set), Contents/Resources of every app bundle
// round the executable (innermost first: the nested Stream Deck Studio.app, then UnrealNG.app), the executable's
// folder, the folder holding the outermost bundle (a build tree: bin/ beside bin/*.app), ../share/unreal-ng, data/ in
// the executable's folder and each folder above it (a build inside the source tree), `sourceData` (the source tree
// the binary was built from).
[[nodiscard]] std::vector<std::filesystem::path> Candidates(const std::filesystem::path& exeDir,
                                                            const std::string& env,
                                                            const std::filesystem::path& sourceData);
// The first candidate that has a streamdeck/ folder; empty when none has.
[[nodiscard]] std::filesystem::path Find(const std::vector<std::filesystem::path>& candidates);

// The running executable's folder, symlinks resolved (empty when unknown).
[[nodiscard]] std::filesystem::path ExecutableDir();

// This process's data directory: looked up once (environment, executable, the source tree); empty = not found.
[[nodiscard]] std::filesystem::path Root();
// Replaces the lookup (tests, tools); an empty path restores it.
void SetRoot(const std::filesystem::path& root);

// "$data/x" -> Root()/x, "$icons/x" -> Root()/icons/x; anything else as it is. Without a data directory the path
// stays as written (and does not load).
[[nodiscard]] std::string Resolve(const std::string& path);

}  // namespace sdb::data
