// The readable compatibility matrix (core/src/emulator/slots/slotmatrix.h) is generated from the reference data and
// the plan engine; the tables committed in docs/inprogress/2026-10-03-zx-bus-slots/compatibility-matrix.md must be
// that output, so the docs, the collection and the engine cannot drift apart.
//
// After a deliberate change to the collection or the engine, regenerate the committed tables with
//   UNREAL_SLOTS_MATRIX_UPDATE=1 tools/build/test.sh --gtest_filter='SlotMatrix_Test.MatrixMatchesDocs'
// and review the document diff.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "_helpers/testpathhelper.h"
#include "emulator/slots/slotmatrix.h"

using namespace slots;

namespace
{

constexpr const char* kMatrixDocument = "docs/inprogress/2026-10-03-zx-bus-slots/compatibility-matrix.md";

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

/// [begin, end) of the generated region of table `name`: the text after the begin marker up to the end marker
bool FindRegion(const std::string& document, std::string_view name, size_t& begin, size_t& end)
{
    const std::string beginMarker = MatrixBeginMarker(name);
    const size_t start = document.find(beginMarker);
    if (start == std::string::npos)
    {
        return false;
    }
    begin = start + beginMarker.size();
    end = document.find(MatrixEndMarker(name), begin);
    return end != std::string::npos;
}

size_t CountColumns(const std::string& line)
{
    size_t pipes = 0;
    for (char c : line)
    {
        pipes += c == '|' ? 1 : 0;
    }
    return pipes;
}

} // namespace

TEST(SlotMatrix_Test, EveryTableIsWellFormed)
{
    for (std::string_view name : MatrixTableNames())
    {
        SCOPED_TRACE(std::string(name));
        const std::string table = RenderMatrixTable(name);
        ASSERT_FALSE(table.empty());
        EXPECT_EQ(table.back(), '\n');
        std::istringstream lines(table);
        std::string line;
        std::getline(lines, line);
        const size_t columns = CountColumns(line);
        int rows = 0;
        while (std::getline(lines, line))
        {
            EXPECT_EQ(CountColumns(line), columns) << line;
            rows++;
        }
        EXPECT_GT(rows, 1);
    }
    EXPECT_TRUE(RenderMatrixTable("no-such-table").empty());
}

TEST(SlotMatrix_Test, MatrixMatchesDocs)
{
    const std::filesystem::path path = TestPathHelper::FindProjectRoot() / kMatrixDocument;
    std::string document = ReadFile(path);
    ASSERT_FALSE(document.empty()) << path;

    const bool update = std::getenv("UNREAL_SLOTS_MATRIX_UPDATE") != nullptr;
    bool changed = false;
    for (std::string_view name : MatrixTableNames())
    {
        SCOPED_TRACE(std::string(name));
        size_t begin = 0;
        size_t end = 0;
        ASSERT_TRUE(FindRegion(document, name, begin, end))
            << "markers " << MatrixBeginMarker(name) << " / " << MatrixEndMarker(name) << " missing in " << kMatrixDocument;
        const std::string generated = "\n" + RenderMatrixTable(name);
        if (update)
        {
            if (document.compare(begin, end - begin, generated) != 0)
            {
                document.replace(begin, end - begin, generated);
                changed = true;
            }
            continue;
        }
        EXPECT_EQ(document.substr(begin, end - begin), generated)
            << "the committed table differs from the reference data; regenerate it (see the header of this file)";
    }
    if (changed)
    {
        std::ofstream out(path, std::ios::binary);
        out << document;
    }
}
