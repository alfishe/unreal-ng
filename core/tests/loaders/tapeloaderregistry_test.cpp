#include "pch.h"

#include <gtest/gtest.h>

#include "loaders/tape/loader_tap.h"
#include "loaders/tape/loader_tape.h"

/// region <Test fixture>

/// Registry + probe tests are pure: no EmulatorContext, no filesystem — the
/// contract is buffer-in, TapeImage-out (design §5.3).
class TapeLoaderRegistry_Test : public ::testing::Test
{
protected:
    /// A minimal clean TAP image in memory: one valid header block
    static std::vector<uint8_t> MakeCleanTap()
    {
        std::vector<uint8_t> header(19, 0x00);
        header[0] = 0x00;   // flag: header
        header[1] = 0x00;   // type: Program
        header[18] = 0x00;  // checksum: XOR of the zeros above is zero

        std::vector<uint8_t> bytes;
        AppendBlock(bytes, header);
        return bytes;
    }

    static void AppendBlock(std::vector<uint8_t>& out, const std::vector<uint8_t>& block)
    {
        out.push_back(static_cast<uint8_t>(block.size() & 0xFF));
        out.push_back(static_cast<uint8_t>(block.size() >> 8));
        out.insert(out.end(), block.begin(), block.end());
    }
};

/// endregion </Test fixture>

/// region <Probe>

TEST_F(TapeLoaderRegistry_Test, ProbeCleanTapScores100)
{
    EXPECT_EQ(LoaderTAP::Probe(TapeLoaderRegistry_Test::MakeCleanTap()), 100);
}

TEST_F(TapeLoaderRegistry_Test, ProbeZeroLengthBlockStillLandsClean)
{
    // [02 00][AA BB] [00 00] [02 00][CC DD] — a zero-length block is legal framing
    std::vector<uint8_t> bytes = { 0x02, 0x00, 0xAA, 0xBB, 0x00, 0x00, 0x02, 0x00, 0xCC, 0xDD };
    EXPECT_EQ(LoaderTAP::Probe(bytes), 100);
}

TEST_F(TapeLoaderRegistry_Test, ProbeTruncatedTailScoresPartial)
{
    // One complete block, then a length prefix that promises 5 bytes with none left
    std::vector<uint8_t> bytes = { 0x03, 0x00, 0xAA, 0xBB, 0xCC, 0x05, 0x00 };
    EXPECT_EQ(LoaderTAP::Probe(bytes), 25);
}

TEST_F(TapeLoaderRegistry_Test, ProbeTzxMagicScoresZero)
{
    // "ZXTape!" lead bytes read as u16 length 0x585A (22622) — never lands
    std::vector<uint8_t> bytes = { 'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 0x01, 0x14 };
    EXPECT_EQ(LoaderTAP::Probe(bytes), 0);
}

TEST_F(TapeLoaderRegistry_Test, ProbeGarbageScoresZero)
{
    std::vector<uint8_t> bytes = { 0xFF, 0xFF, 0xFF, 0xFF };
    EXPECT_EQ(LoaderTAP::Probe(bytes), 0);
}

TEST_F(TapeLoaderRegistry_Test, ProbeEmptyScoresZero)
{
    EXPECT_EQ(LoaderTAP::Probe({}), 0);
}

/// endregion </Probe>

/// region <Registry selection>

TEST_F(TapeLoaderRegistry_Test, SelectReturnsTapLoaderForTapContent)
{
    TapeLoaderRegistry& registry = TapeLoaderRegistry::Instance();

    LoaderTapeBase* loader = registry.Select(MakeCleanTap(), "anything.tap");
    ASSERT_NE(loader, nullptr);
    EXPECT_EQ(loader->Format().id, "tap");
}

TEST_F(TapeLoaderRegistry_Test, SelectIgnoresMisleadingExtension)
{
    // TAP content in a file named .tzx: the content probe wins (design §5.3)
    TapeLoaderRegistry& registry = TapeLoaderRegistry::Instance();

    LoaderTapeBase* loader = registry.Select(MakeCleanTap(), "renamed.tzx");
    ASSERT_NE(loader, nullptr);
    EXPECT_EQ(loader->Format().id, "tap");
}

TEST_F(TapeLoaderRegistry_Test, SelectReturnsTzxLoaderForTzxMagic)
{
    // LoaderTZX self-registers (P2): the magic probe answers 100 and the
    // registry hands back the tzx format — never a garbage TAP parse
    std::vector<uint8_t> bytes = { 'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 0x01, 0x14 };
    LoaderTapeBase* loader = TapeLoaderRegistry::Instance().Select(bytes, "real.tzx");
    ASSERT_NE(loader, nullptr);
    EXPECT_EQ(loader->Format().id, "tzx");
}

TEST_F(TapeLoaderRegistry_Test, SelectReturnsNullptrForEmptyBuffer)
{
    EXPECT_EQ(TapeLoaderRegistry::Instance().Select({}, "x.tap"), nullptr);
}

TEST_F(TapeLoaderRegistry_Test, SupportedExtensionsContainTapFamily)
{
    std::vector<std::string> extensions = TapeLoaderRegistry::Instance().SupportedExtensions();
    EXPECT_NE(std::find(extensions.begin(), extensions.end(), "tap"), extensions.end());
    EXPECT_NE(std::find(extensions.begin(), extensions.end(), "tzx"), extensions.end());
}

/// endregion </Registry selection>

/// region <Contract load>

TEST_F(TapeLoaderRegistry_Test, LoadProducesUsableImage)
{
    LoaderTAP loader;
    TapeImage image = loader.Load(MakeCleanTap(), "memory.tap");

    EXPECT_TRUE(image.IsUsable());
    EXPECT_EQ(image.status, TapeLoadStatus::Ok);
    EXPECT_EQ(image.formatId, "tap");
    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_EQ(image.blocks[0].data.size(), 19u);
    EXPECT_EQ(image.blocks[0].type, TAP_BLOCK_FLAG_HEADER);
    EXPECT_TRUE(image.blocks[0].timing == std::nullopt) << "TAP blocks stay ROM-standard (representation 1)";
}

TEST_F(TapeLoaderRegistry_Test, LoadMarksTruncatedTailAsWarningsWithUsablePrefix)
{
    std::vector<uint8_t> bytes = TapeLoaderRegistry_Test::MakeCleanTap();
    bytes.push_back(0x40);  // dangling length-prefix byte: one more block promised, never delivered

    LoaderTAP loader;
    TapeImage image = loader.Load(bytes, "truncated.tap");

    // Contract v2 (design §5.3): a truncated tail over a usable prefix is
    // degraded-but-usable — Warnings + parseWarnings, never Malformed.
    // Malformed (IsUsable() == false) is reserved for framing broken beyond
    // recovery, and errorText only accompanies Unsupported/Malformed/IoError.
    EXPECT_EQ(image.status, TapeLoadStatus::Warnings);
    EXPECT_TRUE(image.errorText.empty());
    ASSERT_FALSE(image.parseWarnings.empty());
    EXPECT_TRUE(image.IsUsable()) << "The complete prefix stays loadable";
    EXPECT_EQ(image.blocks.size(), 1u);
}

/// endregion </Contract load>
