// SzxReader against the fixture corpus (testdata/loaders/szx, see its README):
// every file parses, and every field agrees with what libspectrum (Fuse's
// library) read from the same file, recorded next to it as
// <name>.libspectrum.txt by tools/verification/szx/szxtool. Plus the version
// rules, the libspectrum A/F quirk, inflate limits and a fuzz pass.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "3rdparty/miniz/miniz.h"
#include "_helpers/testpathhelper.h"
#include "loaders/snapshot/szx/szxreader.h"
#include "loaders/snapshot/szx/szxwriter.h"

using namespace szx;

namespace
{
    std::vector<uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    /// The oracle: "key=value" pairs, several per line
    std::map<std::string, std::string> ReadOracle(const std::filesystem::path& path)
    {
        std::map<std::string, std::string> values;
        std::ifstream file(path);
        std::string line;
        while (std::getline(file, line))
        {
            std::istringstream words(line);
            std::string word;
            while (words >> word)
            {
                const size_t eq = word.find('=');
                if (eq != std::string::npos)
                    values[word.substr(0, eq)] = word.substr(eq + 1);
            }
        }
        return values;
    }

    int Int(const std::map<std::string, std::string>& oracle, const std::string& key)
    {
        return std::stoi(oracle.at(key));
    }

    std::vector<std::filesystem::path> Corpus()
    {
        std::vector<std::filesystem::path> files;
        const std::filesystem::path root = TestPathHelper::FindProjectRoot() / "testdata" / "loaders" / "szx";
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
            if (entry.path().extension() == ".szx")
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        return files;
    }

    /// A minimal valid file: header, Z80R, SPCR, 48K pages
    std::vector<uint8_t> MinimalFile(uint8_t minor, const std::vector<uint8_t>& z80Tail)
    {
        Stage stage;
        stage.machineId = Mid48K;
        stage.z80 = Z80Regs{};
        stage.spec = SpecRegs{};
        for (uint8_t page : {5, 2, 0})
            stage.pages[page] = std::vector<uint8_t>(kPageSize, page);
        std::vector<uint8_t> bytes = SzxWriter::Write(stage);
        bytes[5] = minor;
        // Z80R body starts after the file header and its block header
        const size_t z80Body = kHeaderSize + kBlockHeaderSize;
        for (size_t i = 0; i < z80Tail.size(); i++)
            bytes[z80Body + 33 + i] = z80Tail[i];
        return bytes;
    }
}  // namespace

TEST(SzxReader_Test, CorpusAgreesWithLibspectrum)
{
    const std::vector<std::filesystem::path> files = Corpus();
    ASSERT_GE(files.size(), 10u) << "testdata/loaders/szx is missing";
    for (const auto& path : files)
    {
        SCOPED_TRACE(path.filename().string());
        const std::vector<uint8_t> bytes = ReadBytes(path);
        Stage stage;
        std::string error;
        ASSERT_TRUE(SzxReader::Parse(bytes.data(), bytes.size(), stage, error)) << error;

        std::filesystem::path oraclePath = path;
        oraclePath.replace_extension(".libspectrum.txt");
        const auto oracle = ReadOracle(oraclePath);
        ASSERT_FALSE(oracle.empty());

        const Z80Regs& z = *stage.z80;
        EXPECT_EQ(z.af >> 8, Int(oracle, "a"));
        EXPECT_EQ(z.af & 0xFF, Int(oracle, "f"));
        EXPECT_EQ(z.bc, Int(oracle, "bc"));
        EXPECT_EQ(z.de, Int(oracle, "de"));
        EXPECT_EQ(z.hl, Int(oracle, "hl"));
        EXPECT_EQ(z.af1 >> 8, Int(oracle, "a_"));
        EXPECT_EQ(z.af1 & 0xFF, Int(oracle, "f_"));
        EXPECT_EQ(z.bc1, Int(oracle, "bc_"));
        EXPECT_EQ(z.de1, Int(oracle, "de_"));
        EXPECT_EQ(z.hl1, Int(oracle, "hl_"));
        EXPECT_EQ(z.ix, Int(oracle, "ix"));
        EXPECT_EQ(z.iy, Int(oracle, "iy"));
        EXPECT_EQ(z.sp, Int(oracle, "sp"));
        EXPECT_EQ(z.pc, Int(oracle, "pc"));
        EXPECT_EQ(z.i, Int(oracle, "i"));
        EXPECT_EQ(z.r, Int(oracle, "r"));
        EXPECT_EQ(z.iff1, Int(oracle, "iff1"));
        EXPECT_EQ(z.iff2, Int(oracle, "iff2"));
        EXPECT_EQ(z.im, Int(oracle, "im"));
        EXPECT_EQ(z.cyclesStart, static_cast<uint32_t>(std::stoul(oracle.at("tstates"))));
        EXPECT_EQ((z.flags & kHalted) != 0, Int(oracle, "halted") != 0);
        EXPECT_EQ((z.flags & kSuppressInts) != 0, Int(oracle, "ei") != 0);
        EXPECT_EQ((z.flags & kFset) != 0, Int(oracle, "setf") != 0);
        const unsigned version = (stage.versionMajor << 8) | stage.versionMinor;
        if (version >= 0x0104)
            EXPECT_EQ(z.memptr, Int(oracle, "memptr")) << "libspectrum reads MEMPTR from 1.4 only";

        const SpecRegs& s = *stage.spec;
        EXPECT_EQ(s.border, Int(oracle, "ula") & 0x07);
        if (HasAy(stage.machineId))
            EXPECT_EQ(s.port7FFD, Int(oracle, "p7ffd"));
        if (HasPort1FFD(stage.machineId) || HasPortEFF7(stage.machineId))
            EXPECT_EQ(s.port1FFDorEFF7, Int(oracle, "p1ffd"));

        if (stage.ay)
        {
            EXPECT_EQ(stage.ay->currentRegister, Int(oracle, "ay_port"));
            std::istringstream list(oracle.at("ay"));
            std::string value;
            for (int reg = 0; std::getline(list, value, ','); reg++)
                EXPECT_EQ(stage.ay->registers[reg], std::stoi(value)) << "AY register " << reg;
        }
        // libspectrum treats a Pentagon / Scorpion as Beta-equipped even when the
        // writer left CONNECTED out (ZXMAK2 does), so only "connected => active"
        if (stage.beta && (stage.beta->flags & kBetaConnected))
            EXPECT_NE(Int(oracle, "beta_active"), 0);
        EXPECT_EQ(stage.beta.has_value() && (stage.beta->flags & kBetaPaged), Int(oracle, "beta_paged") != 0);
        if (stage.beta)
            EXPECT_EQ(stage.beta->system, Int(oracle, "beta_system"));

        // Every page libspectrum saw, with the same bytes
        size_t pages = 0;
        for (const auto& [key, crcText] : oracle)
        {
            if (key.rfind("page", 0) != 0)
                continue;
            const uint8_t page = static_cast<uint8_t>(std::stoi(key.substr(4)));
            ASSERT_TRUE(stage.pages.count(page)) << "page " << int(page);
            const std::vector<uint8_t>& data = stage.pages.at(page);
            const uint32_t crc = static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, data.data(), data.size()));
            EXPECT_EQ(crc, static_cast<uint32_t>(std::stoul(crcText, nullptr, 16))) << "page " << int(page);
            pages++;
        }
        EXPECT_EQ(pages, stage.pages.size());
    }
}

TEST(SzxReader_Test, OtherWritersBlocksAreListed)
{
    const std::filesystem::path path =
        TestPathHelper::FindProjectRoot() / "testdata/loaders/szx/other/spectaculator-pentagon-crazylove.szx";
    const std::vector<uint8_t> bytes = ReadBytes(path);
    Stage stage;
    std::string error;
    ASSERT_TRUE(SzxReader::Parse(bytes.data(), bytes.size(), stage, error)) << error;
    EXPECT_EQ(stage.versionMinor, 1);
    ASSERT_TRUE(stage.creator);
    EXPECT_EQ(stage.creator->name, "Spectaculator");
    std::vector<std::string> names;
    for (const auto& [name, size] : stage.otherBlocks)
        names.push_back(name);
    EXPECT_TRUE(stage.beta) << "B128 is parsed";
    for (const char* expected : {"BDSK", "IF1", "MFCE", "ZXPR", "KEYB", "JOY", "AMXM"})
        EXPECT_NE(std::find(names.begin(), names.end(), expected), names.end()) << expected;
}

TEST(SzxReader_Test, VersionRulesForTheZ80RTail)
{
    // Bytes 33-36: hold 7, flags SUPPRESS_INTS | FSET, then #AB #CD
    const std::vector<uint8_t> tail = {7, kSuppressInts | kFset, 0xAB, 0xCD};
    Stage stage;
    std::string error;

    std::vector<uint8_t> v10 = MinimalFile(0, tail);
    ASSERT_TRUE(SzxReader::Parse(v10.data(), v10.size(), stage, error)) << error;
    EXPECT_EQ(stage.z80->holdIntReqCycles, 0) << "1.0: reserved bytes";
    EXPECT_EQ(stage.z80->flags, 0);
    EXPECT_EQ(stage.z80->memptr, 0);

    std::vector<uint8_t> v13 = MinimalFile(3, tail);
    ASSERT_TRUE(SzxReader::Parse(v13.data(), v13.size(), stage, error)) << error;
    EXPECT_EQ(stage.z80->holdIntReqCycles, 7);
    EXPECT_EQ(stage.z80->flags, kSuppressInts) << "FSET means nothing before 1.5";
    EXPECT_EQ(stage.z80->memptr, 0xAB00) << "chBitReg is MEMPTR's high byte";

    std::vector<uint8_t> v15 = MinimalFile(5, tail);
    ASSERT_TRUE(SzxReader::Parse(v15.data(), v15.size(), stage, error)) << error;
    EXPECT_EQ(stage.z80->flags, kSuppressInts | kFset);
    EXPECT_EQ(stage.z80->memptr, 0xCDAB);
}

TEST(SzxReader_Test, OldLibspectrumSwappedAF)
{
    Stage stage;
    stage.machineId = Mid48K;
    stage.creator = Creator{"Fuse", 0, 7, {}};
    const std::string tag = "libspectrum: 0.5.0";
    stage.creator->data.assign(tag.begin(), tag.end());
    Z80Regs z;
    z.af = 0x1234;  // written swapped: A = #34, F = #12 in truth
    z.af1 = 0xABCD;
    stage.z80 = z;
    stage.spec = SpecRegs{};
    const std::vector<uint8_t> bytes = SzxWriter::Write(stage);
    Stage parsed;
    std::string error;
    ASSERT_TRUE(SzxReader::Parse(bytes.data(), bytes.size(), parsed, error)) << error;
    EXPECT_EQ(parsed.z80->af, 0x3412);
    EXPECT_EQ(parsed.z80->af1, 0xCDAB);

    parsed.creator->data.clear();
    stage.creator->data.assign({'l', 'i', 'b', 's', 'p', 'e', 'c', 't', 'r', 'u', 'm', ':', ' ', '1', '.', '5', '.', '0'});
    const std::vector<uint8_t> modern = SzxWriter::Write(stage);
    ASSERT_TRUE(SzxReader::Parse(modern.data(), modern.size(), parsed, error)) << error;
    EXPECT_EQ(parsed.z80->af, 0x1234) << "libspectrum 1.5.0 writes A and F right";
}

TEST(SzxReader_Test, CompressedPagesInflateToExactlyOnePage)
{
    auto withPage = [](const std::vector<uint8_t>& payload, uint16_t flags) {
        std::vector<uint8_t> file = MinimalFile(5, {});
        Put32(file, kRamPage);
        Put32(file, static_cast<uint32_t>(3 + payload.size()));
        Put16(file, flags);
        file.push_back(1);
        file.insert(file.end(), payload.begin(), payload.end());
        return file;
    };
    Stage stage;
    std::string error;

    const std::vector<uint8_t> page(kPageSize, 0x5A);
    const std::vector<uint8_t> good = withPage(Deflate(page.data(), page.size()), kPageCompressed);
    ASSERT_TRUE(SzxReader::Parse(good.data(), good.size(), stage, error)) << error;
    EXPECT_EQ(stage.pages.at(1), page);

    const std::vector<uint8_t> big(kPageSize * 4, 0);
    const std::vector<uint8_t> bomb = withPage(Deflate(big.data(), big.size()), kPageCompressed);
    EXPECT_FALSE(SzxReader::Parse(bomb.data(), bomb.size(), stage, error)) << "a stream larger than 16 KB";

    const std::vector<uint8_t> small(100, 0);
    const std::vector<uint8_t> shortPage = withPage(Deflate(small.data(), small.size()), kPageCompressed);
    EXPECT_FALSE(SzxReader::Parse(shortPage.data(), shortPage.size(), stage, error)) << "a stream shorter than 16 KB";

    const std::vector<uint8_t> raw = withPage(std::vector<uint8_t>(100, 0), 0);
    EXPECT_FALSE(SzxReader::Parse(raw.data(), raw.size(), stage, error)) << "an uncompressed page must be 16 KB";
}

TEST(SzxReader_Test, RefusesWhatIsNotAnSzxFile)
{
    Stage stage;
    std::string error;
    const std::vector<uint8_t> empty;
    EXPECT_FALSE(SzxReader::Parse(empty.data(), 0, stage, error));
    std::vector<uint8_t> file = MinimalFile(5, {});
    file[4] = 2;
    EXPECT_FALSE(SzxReader::Parse(file.data(), file.size(), stage, error)) << "major version 2";
    file = MinimalFile(5, {});
    file.resize(file.size() - 1);
    EXPECT_FALSE(SzxReader::Parse(file.data(), file.size(), stage, error)) << "a block past the end";
    file = MinimalFile(5, {});
    Put32(file, BlockId('Z', 'Z', 'Z', 'Z'));
    Put32(file, 3);
    file.insert(file.end(), {1, 2, 3});
    ASSERT_TRUE(SzxReader::Parse(file.data(), file.size(), stage, error)) << "unknown blocks are skipped";
    ASSERT_EQ(stage.otherBlocks.size(), 1u);
    EXPECT_EQ(stage.otherBlocks[0].first, "ZZZZ");
}

/// Truncated and corrupted corpus files: never crash, never read out of bounds
/// (the sanitizer builds catch the latter)
TEST(SzxReader_Test, FuzzedCorpusNeverCrashes)
{
    std::mt19937 random(20260929);
    for (const auto& path : Corpus())
    {
        const std::vector<uint8_t> original = ReadBytes(path);
        Stage stage;
        std::string error;
        for (size_t cut = 0; cut < original.size(); cut += 1 + original.size() / 16)
            SzxReader::Parse(original.data(), cut, stage, error);
        for (int round = 0; round < 6; round++)
        {
            std::vector<uint8_t> bytes = original;
            for (int flips = 0; flips < 4; flips++)
                bytes[random() % bytes.size()] = static_cast<uint8_t>(random());
            SzxReader::Parse(bytes.data(), bytes.size(), stage, error);
        }
    }
    SUCCEED();
}
