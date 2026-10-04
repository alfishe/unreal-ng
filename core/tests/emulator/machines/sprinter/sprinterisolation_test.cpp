// Sprinter state isolation (Sprinter technical-design §5, the TSConf
// precedent): Sprinter state lives in Sprinter files. The model id
// `MM_SPRINTER` and the Sprinter type names may appear only in the Sprinter
// directories and in the registration surface listed below.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"

namespace
{
namespace fs = std::filesystem;

/// Directories / files (under core/src) that own Sprinter code
const std::vector<std::string> kSprinterPaths = {
    "emulator/ports/models/sprinter/",
    "emulator/ports/models/portdecoder_sprinter.",
    "emulator/memory/sprinter/",
    "emulator/video/sprinter/",
    "emulator/sound/sprinter/",  // the Covox / Covox-Blaster (phase S6)
    "emulator/io/z84c15/",  // the Z84C15 engine adapter (machine neutral): names no Sprinter type
    "emulator/io/sprinter/",  // the ISA slots (ISA phase I1) and the slot wrappers of shared cards
    "debugger/ttd/sprinter/",  // the TTD serializers (phase S7)
};

/// Files that may name the model id or construct a Sprinter type: the registration surface
const std::vector<std::string> kRegistrationFiles = {
    "emulator/platform.h",              // the MEM_MODEL enum, the [SPRINTER] config struct
    "emulator/config.h",                // the mem_model[] row
    "emulator/config.cpp",              // the [SPRINTER] keys, the frame geometry
    "emulator/memory/rom.cpp",          // the ROM path / size rows
    "emulator/ports/portdecoder.cpp",   // the decoder factory + IsModelSupported
    "emulator/cpu/core.cpp",            // the memory factory (SprinterMemory)
    "emulator/video/videocontroller.cpp",  // the screen factory (ScreenSprinter, PLAN #60(e))
    "emulator/io/ide/idecontroller.cpp",   // [HDD] Scheme=SPRINTER fits MM_SPRINTER only (IdeController::SchemeFits, S3b)
    "debugger/ttd/ttdserializable.h",   // the PeripheralId Sprinter rows (serializers in debugger/ttd/sprinter/)
    "debugger/ttd/ttdfileinfo.cpp",     // its name
};

/// Identifiers no shared file may use
const std::vector<std::string> kTokens = {
    "MM_SPRINTER", "PortDecoder_Sprinter", "SprinterMemory", "SprinterPld", "SprinterVideoRam", "SprinterIntSource",
    "SprinterWaits", "config.sprinter.",
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

bool IsIdentifierChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/// The token as a whole identifier (prefix match allowed for the dotted config path)
bool Names(const std::string& text, const std::string& token, size_t& at)
{
    for (size_t pos = text.find(token); pos != std::string::npos; pos = text.find(token, pos + 1))
    {
        const bool before = pos > 0 && IsIdentifierChar(text[pos - 1]);
        if (!before)
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

TEST(SprinterIsolation_Test, NoSprinterStateOrModelIdInSharedCode)
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
        const std::string rel = entry.path().generic_string().substr(srcPrefix);
        if (rel.compare(0, 9, "3rdparty/") == 0 || StartsWithAny(rel, kSprinterPaths) ||
            StartsWithAny(rel, kRegistrationFiles))
            continue;

        std::ifstream in(entry.path(), std::ios::binary | std::ios::ate);
        std::string text(static_cast<size_t>(in.tellg()), '\0');
        in.seekg(0);
        in.read(text.data(), static_cast<std::streamsize>(text.size()));
        scanned++;

        for (const std::string& token : kTokens)
        {
            size_t at = 0;
            if (Names(text, token, at))
                violations.push_back(rel + ":" + std::to_string(LineOf(text, at)) + ": " + token);
        }
    }

    EXPECT_GT(scanned, 100u) << "the scan must see the source tree";
    std::string report;
    for (const std::string& v : violations)
        report += "\n  " + v;
    EXPECT_TRUE(violations.empty()) << "Sprinter code outside its files (technical-design §5):" << report;
}

// The Z84C15 engine adapter is reusable: it never names the Sprinter
TEST(SprinterIsolation_Test, Z84C15PackageIsMachineNeutral)
{
    const fs::path dir = TestPathHelper::FindProjectRoot() / "core" / "src" / "emulator" / "io" / "z84c15";
    ASSERT_TRUE(fs::is_directory(dir));
    for (const auto& entry : fs::directory_iterator(dir))
    {
        std::ifstream in(entry.path(), std::ios::binary | std::ios::ate);
        std::string text(static_cast<size_t>(in.tellg()), '\0');
        in.seekg(0);
        in.read(text.data(), static_cast<std::streamsize>(text.size()));
        for (const std::string& token : kTokens)
            EXPECT_EQ(text.find(token), std::string::npos) << entry.path().filename() << ": " << token;
    }
}
