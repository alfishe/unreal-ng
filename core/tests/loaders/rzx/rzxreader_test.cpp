// RzxReader: every block type (creator, security, snapshot embedded /
// compressed / external, input compressed or not), repeat frames, the bounds
// checks on block and frame lengths, inflate limits, the real recordings in
// testdata/loaders/rzx and a fuzz pass over them.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "_helpers/rzxtestbuilder.h"
#include "_helpers/testpathhelper.h"
#include "loaders/rzx/rzxreader.h"

using namespace rzx;

namespace
{
    std::filesystem::path Folder(const std::string& name)
    {
        return TestPathHelper::FindProjectRoot() / "testdata" / "loaders" / "rzx" / name;
    }

    std::vector<uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    std::vector<std::filesystem::path> Corpus()
    {
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(Folder("archive")))
        {
            if (entry.path().extension() == ".rzx")
                files.push_back(entry.path());
        }
        return files;
    }

    bool Parse(const std::vector<uint8_t>& bytes, File& file, std::string& error)
    {
        return RzxReader::Parse(bytes.data(), bytes.size(), file, error);
    }

    const std::vector<uint8_t> kImage = {'Z', 'X', 'S', 'T', 1, 5, 1, 0};
}  // namespace

TEST(RzxReader_Test, ParsesEveryBlockType)
{
    RzxTestBuilder rzx;
    rzx.Creator("Fuse", 1, 6)
        .Block(kBlockSecurityInfo, {0x78, 0x56, 0x34, 0x12, 0x07, 0x00, 0x00, 0x00})
        .Snapshot("SZX", kImage)
        .Input({{100, {0xBF, 0xFE}}, {90, {}}, {80, {0x1F}}}, 1234)
        .Block(kBlockSecuritySignature, {0, 1, 2, 3});
    File file;
    std::string error;
    ASSERT_TRUE(Parse(rzx.Build(), file, error)) << error;

    EXPECT_EQ(file.VersionText(), "0.13");
    ASSERT_TRUE(file.hasCreator);
    EXPECT_EQ(file.creator.name, "Fuse");
    EXPECT_EQ(file.creator.major, 1);
    EXPECT_EQ(file.creator.minor, 6);
    EXPECT_TRUE(file.hasSecurityInfo);
    EXPECT_EQ(file.keyId, 0x12345678u);
    EXPECT_EQ(file.weekCode, 7u);
    EXPECT_TRUE(file.hasSignature);

    ASSERT_EQ(file.order.size(), 2u);
    EXPECT_EQ(file.order[0].type, BlockType::Snapshot);
    EXPECT_EQ(file.order[1].type, BlockType::Input);
    EXPECT_EQ(file.snapshots[0].extension, "szx") << "lower case";
    EXPECT_EQ(file.snapshots[0].data, kImage);

    const InputBlock& input = file.inputs[0];
    EXPECT_EQ(input.tstates, 1234u);
    ASSERT_EQ(input.frames.size(), 3u);
    EXPECT_EQ(input.frames[0].fetchCount, 100);
    EXPECT_EQ(input.frames[0].inCount, 2);
    EXPECT_EQ(input.frames[1].inCount, 0);
    EXPECT_EQ(input.frames[2].inOffset, 2u);
    EXPECT_EQ(input.inValues, (std::vector<uint8_t>{0xBF, 0xFE, 0x1F}));
    EXPECT_EQ(file.TotalFrames(), 3u);
    EXPECT_TRUE(file.warnings.empty());
}

TEST(RzxReader_Test, CompressedSnapshotAndInput)
{
    std::vector<uint8_t> image(49182);
    for (size_t i = 0; i < image.size(); i++)
        image[i] = static_cast<uint8_t>(i * 7);
    std::vector<RzxTestFrame> frames;
    for (int i = 0; i < 2000; i++)
        frames.push_back({static_cast<uint16_t>(1000 + i), {static_cast<uint8_t>(i), 0xFF}});

    RzxTestBuilder rzx;
    rzx.Creator("Test", 0, 1).Snapshot("z80", image, true).Input(frames, 0, true);
    File file;
    std::string error;
    ASSERT_TRUE(Parse(rzx.Build(), file, error)) << error;
    EXPECT_TRUE(file.snapshots[0].compressed);
    EXPECT_EQ(file.snapshots[0].data, image);
    EXPECT_TRUE(file.inputs[0].compressed);
    ASSERT_EQ(file.inputs[0].frames.size(), 2000u);
    EXPECT_EQ(file.inputs[0].frames[1999].fetchCount, 2999);
    EXPECT_EQ(file.inputs[0].inValues[2 * 1999], static_cast<uint8_t>(1999));
}

/// A repeat frame (IN counter 65535) points at the previous stored frame's
/// values; at the block start it has none
TEST(RzxReader_Test, RepeatFrames)
{
    RzxTestFrame repeat;
    repeat.fetchCount = 50;
    repeat.repeat = true;
    RzxTestBuilder rzx;
    rzx.Snapshot("z80", kImage).Input({repeat, {10, {0x01, 0x02}}, repeat, repeat});
    File file;
    std::string error;
    ASSERT_TRUE(Parse(rzx.Build(), file, error)) << error;
    const std::vector<Frame>& frames = file.inputs[0].frames;
    EXPECT_TRUE(frames[0].repeated);
    EXPECT_EQ(frames[0].inCount, 0);
    EXPECT_EQ(frames[2].inCount, 2);
    EXPECT_EQ(frames[2].inOffset, 0u);
    EXPECT_EQ(frames[3].inOffset, 0u);
    EXPECT_EQ(file.inputs[0].inValues.size(), 2u) << "repeats store nothing";
}

TEST(RzxReader_Test, ExternalSnapshotDescriptor)
{
    RzxTestBuilder rzx;
    rzx.ExternalSnapshot("z80", "games/manic.z80", 0xCAFEBABE).Input({{10, {}}});
    File file;
    std::string error;
    ASSERT_TRUE(Parse(rzx.Build(), file, error)) << error;
    EXPECT_TRUE(file.snapshots[0].external);
    EXPECT_EQ(file.snapshots[0].externalName, "games/manic.z80");
    EXPECT_EQ(file.snapshots[0].externalChecksum, 0xCAFEBABEu);
    EXPECT_TRUE(file.snapshots[0].data.empty());
}

/// Multiload: snapshot, input, snapshot, input - kept in file order
TEST(RzxReader_Test, BlocksKeepFileOrder)
{
    RzxTestBuilder rzx;
    rzx.Snapshot("z80", kImage).Input({{10, {}}}).Snapshot("sna", kImage).Input({{20, {}}, {30, {}}});
    File file;
    std::string error;
    ASSERT_TRUE(Parse(rzx.Build(), file, error)) << error;
    ASSERT_EQ(file.order.size(), 4u);
    EXPECT_EQ(file.order[2].type, BlockType::Snapshot);
    EXPECT_EQ(file.order[2].index, 1u);
    EXPECT_EQ(file.order[3].index, 1u);
    EXPECT_EQ(file.TotalFrames(), 3u);
}

TEST(RzxReader_Test, UnknownBlocksAreSkippedWithAWarning)
{
    RzxTestBuilder rzx;
    rzx.Snapshot("z80", kImage).Block(0x55, {1, 2, 3}).Input({{10, {}}});
    File file;
    std::string error;
    ASSERT_TRUE(Parse(rzx.Build(), file, error)) << error;
    ASSERT_EQ(file.warnings.size(), 1u);
    EXPECT_NE(file.warnings[0].find("#55"), std::string::npos);
}

TEST(RzxReader_Test, ProtectedFramesAreKeptUnparsed)
{
    RzxTestBuilder rzx;
    rzx.Snapshot("z80", kImage).Input({{10, {1}}}, 0, false, kInputFlagProtected);
    File file;
    std::string error;
    ASSERT_TRUE(Parse(rzx.Build(), file, error)) << error;
    EXPECT_TRUE(file.inputs[0].protectedFrames);
    EXPECT_TRUE(file.inputs[0].frames.empty());
}

struct BadCase
{
    const char* name;
    std::vector<uint8_t> bytes;
    const char* errorPart;
};

class RzxReaderBad_Test : public ::testing::TestWithParam<BadCase>
{
};

TEST_P(RzxReaderBad_Test, RefusesWithAReason)
{
    File file;
    std::string error;
    EXPECT_FALSE(Parse(GetParam().bytes, file, error));
    EXPECT_NE(error.find(GetParam().errorPart), std::string::npos) << error;
}

namespace
{
    std::vector<uint8_t> WithInputs(std::vector<RzxTestFrame> frames)
    {
        RzxTestBuilder rzx;
        rzx.Snapshot("z80", kImage).Input(frames);
        return rzx.Build();
    }

    /// A valid file whose byte at `offset` from the end is replaced
    std::vector<uint8_t> Patched(std::vector<uint8_t> bytes, size_t offset, uint8_t value)
    {
        bytes[offset] = value;
        return bytes;
    }

    std::vector<uint8_t> Truncated(std::vector<uint8_t> bytes, size_t drop)
    {
        bytes.resize(bytes.size() - drop);
        return bytes;
    }

    std::vector<uint8_t> NoInput()
    {
        RzxTestBuilder rzx;
        rzx.Snapshot("z80", kImage);
        return rzx.Build();
    }

    std::vector<uint8_t> CorruptZlib()
    {
        RzxTestBuilder rzx;
        rzx.Snapshot("z80", std::vector<uint8_t>(1000, 0x11), true).Input({{10, {}}});
        std::vector<uint8_t> bytes = rzx.Build();
        bytes[10 + 17 + 5] ^= 0xFF;  // inside the snapshot's zlib stream
        return bytes;
    }

    std::vector<uint8_t> TooManyFrames()
    {
        // Frame count 1000 in a block holding 2 frames
        std::vector<uint8_t> bytes = WithInputs({{10, {}}, {20, {}}});
        const size_t input = bytes.size() - 8 - 13;
        RzxTestBuilder::Put32(bytes, input + 0, 1000);
        return bytes;
    }
}  // namespace

INSTANTIATE_TEST_SUITE_P(
    Malformed, RzxReaderBad_Test,
    ::testing::Values(BadCase{"empty", {}, "RZX!"}, BadCase{"signature", {'R', 'Z', 'Y', '!', 0, 13, 0, 0, 0, 0}, "RZX!"},
                      BadCase{"major", {'R', 'Z', 'X', '!', 1, 0, 0, 0, 0, 0}, "version"},
                      BadCase{"no_input", NoInput(), "no input"},
                      BadCase{"block_length_past_end", Patched(WithInputs({{10, {1, 2}}}), 11, 0xFF), "length"},
                      BadCase{"block_length_zero", Patched(Patched(WithInputs({{10, {}}}), 11, 0), 12, 0), "length"},
                      BadCase{"truncated_block", Truncated(WithInputs({{10, {1, 2, 3}}}), 2), "length"},
                      BadCase{"frame_count_over_data", TooManyFrames(), "cannot fit"},
                      BadCase{"corrupt_zlib", CorruptZlib(), "inflate"}),
    [](const ::testing::TestParamInfo<BadCase>& info) { return std::string(info.param.name); });

/// The IN counter of a frame says more values than the block holds
TEST(RzxReader_Test, FrameWithMoreInsThanTheBlockHolds)
{
    std::vector<uint8_t> bytes = WithInputs({{10, {1, 2}}});
    // The last frame's IN counter (2) becomes 200
    RzxTestBuilder::Put16(bytes, bytes.size() - 4, 200);
    File file;
    std::string error;
    EXPECT_FALSE(Parse(bytes, file, error));
    EXPECT_NE(error.find("IN values"), std::string::npos) << error;
}

/// A compressed input block that inflates past the bound is refused, not truncated
TEST(RzxReader_Test, InflateRespectsItsLimit)
{
    std::vector<uint8_t> out;
    const std::vector<uint8_t> big(1 << 20, 0);
    const std::vector<uint8_t> stream = Deflate(big.data(), big.size());
    EXPECT_TRUE(Inflate(stream.data(), stream.size(), 0, 1 << 20, out));
    EXPECT_EQ(out.size(), big.size());
    EXPECT_FALSE(Inflate(stream.data(), stream.size(), 0, (1 << 20) - 1, out));
    EXPECT_FALSE(Inflate(stream.data(), stream.size(), 1000, 1 << 20, out)) << "longer than expected";
    EXPECT_FALSE(Inflate(stream.data(), stream.size() - 4, 0, 1 << 20, out)) << "stream cut short";
}

/// The real recordings parse with the counts rzxinfo.py (SkoolKit) reports
TEST(RzxReader_Test, RealRecordings)
{
    struct Expected
    {
        const char* file;
        const char* creator;
        const char* extension;
        uint64_t frames;
        uint32_t tstates;
    };
    const Expected cases[] = {
        {"ericfloaters.rzx", "Spectaculator", "z80", 32315, 20},
        {"garfield.rzx", "SPIN 0.5", "z80", 23234, 0},
        {"greenberet.rzx", "Spectaculator", "z80", 39041, 21},
        {"thundercats.rzx", "SPIN 0.5", "z80", 61282, 0},
    };
    for (const Expected& expected : cases)
    {
        File file;
        std::string error;
        ASSERT_TRUE(RzxReader::ParseFile(Folder("archive/" + std::string(expected.file)).string(), file, error))
            << expected.file << ": " << error;
        EXPECT_EQ(file.VersionText(), "0.12") << expected.file;
        EXPECT_EQ(file.creator.name.rfind(expected.creator, 0), 0u) << file.creator.name;
        EXPECT_EQ(file.snapshots[0].extension, expected.extension) << expected.file;
        EXPECT_EQ(file.TotalFrames(), expected.frames) << expected.file;
        EXPECT_EQ(file.inputs[0].tstates, expected.tstates) << expected.file;
    }
}

TEST(RzxReader_Test, FuzzedCorpusNeverCrashes)
{
    std::mt19937 random(20260929);
    for (const auto& path : Corpus())
    {
        const std::vector<uint8_t> original = ReadBytes(path);
        File file;
        std::string error;
        for (size_t cut = 0; cut < original.size(); cut += 1 + original.size() / 16)
            RzxReader::Parse(original.data(), cut, file, error);
        for (int round = 0; round < 6; round++)
        {
            std::vector<uint8_t> bytes = original;
            for (int flips = 0; flips < 4; flips++)
                bytes[random() % bytes.size()] = static_cast<uint8_t>(random());
            RzxReader::Parse(bytes.data(), bytes.size(), file, error);
        }
    }
    // Headers only: every byte of a small valid file flipped in turn
    const std::vector<uint8_t> small = WithInputs({{10, {1, 2}}, {20, {3}}});
    for (size_t i = 0; i < small.size(); i++)
    {
        for (uint8_t value : {uint8_t(0x00), uint8_t(0xFF), uint8_t(0x80)})
        {
            std::vector<uint8_t> bytes = small;
            bytes[i] = value;
            File file;
            std::string error;
            RzxReader::Parse(bytes.data(), bytes.size(), file, error);
        }
    }
}
