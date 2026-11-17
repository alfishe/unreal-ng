// The rasm engine against rasm v3.3.3 itself: rasm.c's AUTOTEST_* snippets, assembled by the rasm binary
// (tools/verification/unreal-asm/checks/rasmautotests.py writes testdata/rasm/autotests/: NAME.asm, NAME.bin when rasm
// wrote a binary, index.tsv with rasm's exit code). The engine must fail what rasm fails and write the same bytes.

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "dialects/rasm/engine/rasmassembly.h"
#include "testdata.h"

namespace
{
using namespace unrealasm;
using namespace unrealasm::rasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::TestDataPath;

struct Case
{
    std::string name;
    int exitCode = 0;
};

std::vector<Case> Cases()
{
    std::vector<Case> cases;
    std::ifstream in(TestDataPath("rasm/autotests/index.tsv"));
    std::string line;
    while (std::getline(in, line))
    {
        std::istringstream fields(line);
        Case c;
        std::string code;
        std::getline(fields, c.name, '\t');
        std::getline(fields, code, '\t');
        if (c.name.empty())
            continue;
        c.exitCode = std::atoi(code.c_str());
        cases.push_back(c);
    }
    return cases;
}

/// EDSK reads and writes disk images other snippets of rasm's run left behind: CPC media, not converted
bool IsDiskImageCase(const std::string& name)
{
    return name.rfind("AUTOTEST_EDSK_", 0) == 0;
}

std::string Messages(const RasmAssembly& r)
{
    std::string s;
    for (const auto& d : r.diagnostics)
        s += d.message + "\n";
    return s;
}

RasmAssembly Assemble(const std::string& name)
{
    FileSystem fs;
    fs.read = [](const std::string& file) -> std::optional<std::vector<uint8_t>> {
        std::ifstream f(TestDataPath("rasm/autotests/" + file), std::ios::binary);
        if (!f)
            return std::nullopt;
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
    };
    EngineOptions options;
    options.timestamp = 0;
    return AssembleRasm(name + ".asm", ReadTestData("rasm/autotests/" + name + ".asm"), fs, options);
}
}  // namespace

TEST(RasmEngine_Test, AutotestsMatchRasm)
{
    const std::vector<Case> cases = Cases();
    ASSERT_GT(cases.size(), 300u);
    int compared = 0;
    for (const Case& c : cases)
    {
        if (IsDiskImageCase(c.name))
            continue;
        const RasmAssembly r = Assemble(c.name);
        if (c.exitCode != 0)
        {
            EXPECT_FALSE(r.ok) << c.name << ": rasm reports an error, the engine does not";
            continue;
        }
        ASSERT_TRUE(r.ok) << c.name << ":\n" << Messages(r);
        const std::vector<uint8_t> want = ReadTestData("rasm/autotests/" + c.name + ".bin");
        if (want.empty())
            continue;
        EXPECT_TRUE(r.hasBinary) << c.name;
        EXPECT_EQ(r.binary, want) << c.name;
        compared++;
    }
    EXPECT_GT(compared, 140);
}

TEST(RasmEngine_Test, CpcMediaReportedNotConverted)
{
    FileSystem fs;
    EngineOptions options;
    const std::string src = "buildsna : bank 0 : nop";
    const RasmAssembly r = AssembleRasm("a.asm", std::vector<uint8_t>(src.begin(), src.end()), fs, options);
    EXPECT_TRUE(r.ok) << Messages(r);
    EXPECT_FALSE(r.hasBinary);
    ASSERT_FALSE(r.notConverted.empty());
}

TEST(RasmEngine_Test, BuildZxWritesSnapshot)
{
    FileSystem fs;
    EngineOptions options;
    const std::string src = "buildzx : bank 0 : org #8000 : start: jr start : run start,#ff00";
    const RasmAssembly r = AssembleRasm("a.asm", std::vector<uint8_t>(src.begin(), src.end()), fs, options);
    ASSERT_TRUE(r.ok) << Messages(r);
    ASSERT_EQ(r.outputs.size(), 1u);
    EXPECT_TRUE(r.outputs[0].name.empty());
    EXPECT_EQ(r.outputs[0].bytes.size(), 49179u);   // 27-byte header + 48K
}
