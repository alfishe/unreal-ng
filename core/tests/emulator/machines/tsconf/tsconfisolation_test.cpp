// TSConf state isolation (PLAN #41 phase 0, INF-6; TSConf technical-design
// §3.3): TSConf state lives in TSConf's own files, never in shared structs or
// shared code. The scan fails on the tokens of the former half-port - the
// shared `state.ts` struct, the TS palette / sprite files, the TSU line buffers
// and DRAM budget counters, the TS cache size, the ancestor's register header -
// and on the model id `MM_TSL` outside the registration surface.
//
// Allowed everywhere under the TSConf directories below. `MM_TSL` is also
// allowed in the registration surface: the model enum, the model table row,
// the config folder, the screen factory, and the per-model ROM tables (until
// phase 1's ROM-1 moves the TSConf ROM rules next to its decoder). When phase 1
// adds the decoder factory case and the Core memory factory case, list those
// files here with the reason.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"

namespace
{
namespace fs = std::filesystem;

/// Directories (under core/src) that own TSConf code
const std::vector<std::string> kTsConfDirs = {
    "emulator/platforms/tsconf/",
    "emulator/video/tsconf/",
    "emulator/memory/tsconf/",
    "debugger/ttd/tsconf/",
};

/// Files (under core/src) that may name the model id: the registration surface
const std::vector<std::string> kModelIdFiles = {
    "emulator/platform.h",                 // the MEM_MODEL enum
    "emulator/config.h",                   // the mem_model[] row
    "emulator/config.cpp",                 // the config folder ("ts-conf")
    "emulator/video/videocontroller.cpp",  // the Screen factory (PLAN #60(e))
    "emulator/memory/rom.cpp",             // per-model ROM path / set / size rows (phase 1 ROM-1 moves the size rule)
};

/// Tokens of the former half-port; none may appear outside the TSConf directories
const std::vector<std::string> kForbidden = {
    "state.ts.", "state.ts;", ".cram[", ".sfile[", "tsline", "memdmacyc", "memvidcyc", "memcyc_lcmd",
    "TS_CACHE_SIZE", "TSPORTS_t", "platforms/tsconf/",
};

bool StartsWithAny(const std::string& path, const std::vector<std::string>& prefixes)
{
    for (const std::string& prefix : prefixes)
    {
        if (path.compare(0, prefix.size(), prefix) == 0)
            return true;
    }
    return false;
}

bool IsTsConfFile(const std::string& rel)
{
    if (StartsWithAny(rel, kTsConfDirs))
        return true;
    return rel.find("ports/models/portdecoder_tsconf.") != std::string::npos;
}

bool IsIdentifierChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/// "MM_TSL" as a whole identifier
bool NamesModelId(const std::string& text, size_t& at)
{
    static const std::string id = "MM_TSL";
    for (size_t pos = text.find(id); pos != std::string::npos; pos = text.find(id, pos + 1))
    {
        const bool before = pos > 0 && IsIdentifierChar(text[pos - 1]);
        const bool after = pos + id.size() < text.size() && IsIdentifierChar(text[pos + id.size()]);
        if (!before && !after)
        {
            at = pos;
            return true;
        }
    }
    return false;
}

size_t LineOf(const std::string& text, size_t pos)
{
    size_t line = 1;
    for (size_t i = 0; i < pos && i < text.size(); i++)
        line += text[i] == '\n';
    return line;
}
} // namespace

TEST(TsConfIsolation_Test, NoTsConfStateOrModelIdInSharedCode)
{
    const fs::path src = TestPathHelper::FindProjectRoot() / "core" / "src";
    ASSERT_TRUE(fs::is_directory(src)) << src;
    const size_t srcPrefix = src.generic_string().size() + 1;

    std::vector<std::string> violations;
    size_t scanned = 0;
    for (auto it = fs::recursive_directory_iterator(src); it != fs::recursive_directory_iterator(); ++it)
    {
        const fs::directory_entry& entry = *it;
        if (entry.is_directory() && entry.path().filename() == "3rdparty")
        {
            it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file())
            continue;
        const std::string ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".h")
            continue;
        // A plain prefix cut: fs::relative canonicalizes every path (slow)
        const std::string rel = entry.path().generic_string().substr(srcPrefix);
        if (rel.compare(0, 9, "3rdparty/") == 0 || IsTsConfFile(rel))
            continue;

        std::ifstream in(entry.path(), std::ios::binary | std::ios::ate);
        std::string text(static_cast<size_t>(in.tellg()), '\0');
        in.seekg(0);
        in.read(text.data(), static_cast<std::streamsize>(text.size()));
        scanned++;

        for (const std::string& token : kForbidden)
        {
            const size_t pos = text.find(token);
            if (pos != std::string::npos)
                violations.push_back(rel + ":" + std::to_string(LineOf(text, pos)) + ": " + token);
        }
        size_t at = 0;
        if (NamesModelId(text, at) && !StartsWithAny(rel, kModelIdFiles))
            violations.push_back(rel + ":" + std::to_string(LineOf(text, at)) + ": MM_TSL outside the registration surface");
    }

    EXPECT_GT(scanned, 100u) << "the scan must see the source tree";
    std::string report;
    for (const std::string& v : violations)
        report += "\n  " + v;
    EXPECT_TRUE(violations.empty()) << "TSConf leftovers in shared code (TSConf technical-design §3.3):" << report;
}
