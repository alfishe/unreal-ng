// SourceDocument helpers and diagnostics

#include <gtest/gtest.h>

#include "unrealasm/document.h"
#include "unrealasm/diagnostics.h"

using namespace unrealasm;

TEST(Document_Test, FromTextAndBack)
{
    const SourceDocument document = SourceDocument::FromText("\tLD A,1\n\tRET\n", "sjasmplus");
    ASSERT_EQ(document.lines.size(), 2u);
    EXPECT_EQ(document.lines[1].text, "\tRET");
    EXPECT_EQ(document.dialect, "sjasmplus");
    EXPECT_EQ(document.Text(), "\tLD A,1\n\tRET");
    EXPECT_TRUE(SourceDocument::FromText("").lines.empty());
    EXPECT_EQ(SourceDocument::FromText("a\n\nb").lines.size(), 3u);
}

TEST(Document_Test, HasErrors)
{
    Diagnostics diagnostics = {{Severity::Warning, 1, 0, "w"}};
    EXPECT_FALSE(HasErrors(diagnostics));
    diagnostics.push_back({Severity::Error, 2, 0, "e"});
    EXPECT_TRUE(HasErrors(diagnostics));
}
