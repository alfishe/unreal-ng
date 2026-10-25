#pragma once

/// @file coverageexport.h
/// @brief The executed-address coverage as a file another tool reads: the one implementation behind the coverage
/// export verb of every surface (CLI `coverage export`, Lua / Python `coverage_export`, MCP `coverage_export`,
/// REST and WebSocket `POST /coverage/export`). Options by name as strings, so the surfaces cannot drift apart.
///
/// Formats:
///   skoolkit-map   SkoolKit's code execution map (sna2ctl.py -m, sna2skool.py -m; the map rzxplay.py --map and
///                  zx-reaper's zxrun.py --map write): one "$HHHH" line per executed instruction start, ascending
///
/// The coverage analyzer records the PC at the start of each instruction (the instruction-start hook, once per
/// instruction: a DD / FD / CB / ED prefix is not counted apart from its instruction), so every address in the map is
/// an instruction start, the address the CPU saw (not a physical page).
///
/// @code
///   coverageexport::Result r = coverageexport::Export(coverage, {{"path", "cov.map"}, {"start", "0x6000"}});
/// @endcode

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class CoverageAnalyzer;

namespace coverageexport
{

using Options = std::map<std::string, std::string>;

enum class Error : uint8_t
{
    None,
    NotAvailable,  ///< no coverage analyzer (HTTP 500, as the other coverage routes)
    BadRequest,    ///< an unknown format or option, an address that does not parse, a file that cannot be written (400)
};

struct Result
{
    Error error = Error::None;
    std::string message;    ///< one sentence a user can act on
    std::string format;
    uint16_t start = 0;
    uint16_t end = 0xFFFF;
    size_t count = 0;       ///< addresses in the map (the merged ones included)
    size_t merged = 0;      ///< addresses taken from the existing file (merge)
    std::string path;       ///< the file written ("" = the text is in `text`)
    std::string text;       ///< the map, when no path was given

    bool Ok() const { return error == Error::None; }
};

/// The formats: "skoolkit-map"
const std::vector<std::string>& Formats();
/// The option names: format, start, end, path, merge
const std::vector<std::string>& OptionNames();

/// The map of the addresses executed in [start, end]: one "$HHHH" line each, ascending
std::string SkoolkitMap(const CoverageAnalyzer& coverage, uint16_t start, uint16_t end, size_t& count);

/// Export the coverage. Options: format (default skoolkit-map), start / end (decimal, 0x / $ / # hex; default the
/// whole 64K), path (write the file; else the map comes back in `text`), merge (true: add the addresses of the
/// existing file at path, as zxrun.py --map does)
Result Export(const CoverageAnalyzer* coverage, const Options& options);

}  // namespace coverageexport
