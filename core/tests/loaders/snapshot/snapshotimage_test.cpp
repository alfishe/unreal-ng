/// @file snapshotimage_test.cpp
/// @brief SnapshotImage built by each format reader (snapshot pipeline P1, PLAN #84, test SP-2). The oracle is the
/// file itself: the test slices the raw bytes by the format's published layout, independently of the loader, and the
/// image must agree (banks, registers, paging). The plan step's report is checked here too.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "3rdparty/miniz/miniz.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulatorcontext.h"
#include "loaders/snapshot/loader_sna.h"
#include "loaders/snapshot/loader_z80.h"
#include "loaders/snapshot/loaderspg.h"
#include "loaders/snapshot/loaderzxp.h"
#include "loaders/snapshot/snapshotimage.h"
#include "loaders/snapshot/snapshotpipeline.h"
#include "loaders/snapshot/szx/loaderszx.h"
#include "loaders/snapshot/szx/szxreader.h"

namespace fs = std::filesystem;

namespace
{
std::vector<uint8_t> ReadFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<fs::path> Fixtures(const char* folder, const char* extension)
{
    std::vector<fs::path> out;
    for (const auto& entry : fs::directory_iterator(TestPathHelper::GetTestDataPath(folder)))
    {
        if (entry.is_regular_file() && entry.path().extension() == extension)
            out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<uint8_t> Slice(const std::vector<uint8_t>& file, size_t at, size_t size)
{
    return std::vector<uint8_t>(file.begin() + static_cast<std::ptrdiff_t>(at),
                                file.begin() + static_cast<std::ptrdiff_t>(at + size));
}

uint16_t Word(uint8_t high, uint8_t low) { return static_cast<uint16_t>(high << 8 | low); }
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// SNA
// ---------------------------------------------------------------------------------------------------------------------

class SnapshotImageSna_Test : public ::testing::Test
{
protected:
    EmulatorContext _context{LoggerLevel::LogError};

    /// The image the loader builds from its staging
    snapshot::Image ImageOf(const fs::path& path)
    {
        LoaderSNACUT loader(&_context, path.string());
        EXPECT_TRUE(loader.validate()) << path;
        EXPECT_TRUE(loader.loadToStaging()) << path;
        return loader.BuildImage();
    }
};

TEST_F(SnapshotImageSna_Test, EveryFixtureAgreesWithItsOwnBytes)
{
    const auto fixtures = Fixtures("loaders/sna", ".sna");
    ASSERT_GE(fixtures.size(), 20u);
    for (const fs::path& path : fixtures)
    {
        SCOPED_TRACE(path.filename().string());
        const std::vector<uint8_t> f = ReadFile(path);
        const snapshot::Image image = ImageOf(path);
        const bool is128 = f.size() > 49179;

        EXPECT_EQ(image.format, "sna");
        EXPECT_EQ(image.memoryModel, is128 ? snapshot::MemoryModel::Mem128k : snapshot::MemoryModel::Mem48k);
        EXPECT_EQ(image.machineHint, is128 ? "128k-family" : "48k");

        // Registers, by the published header layout (WoS formats)
        EXPECT_EQ(image.cpu.i, f[0]);
        EXPECT_EQ(image.cpu.hl2, Word(f[2], f[1]));
        EXPECT_EQ(image.cpu.de2, Word(f[4], f[3]));
        EXPECT_EQ(image.cpu.bc2, Word(f[6], f[5]));
        EXPECT_EQ(image.cpu.af2, Word(f[8], f[7]));
        EXPECT_EQ(image.cpu.hl, Word(f[10], f[9]));
        EXPECT_EQ(image.cpu.de, Word(f[12], f[11]));
        EXPECT_EQ(image.cpu.bc, Word(f[14], f[13]));
        EXPECT_EQ(image.cpu.iy, Word(f[16], f[15]));
        EXPECT_EQ(image.cpu.ix, Word(f[18], f[17]));
        EXPECT_EQ(image.cpu.iff2, (f[19] & 4) != 0);
        EXPECT_EQ(image.cpu.iff1, image.cpu.iff2) << "the commit sets both from the one bit";
        EXPECT_EQ(image.cpu.r, f[20]);
        EXPECT_EQ(image.cpu.af, Word(f[22], f[21]));
        EXPECT_EQ(image.cpu.im, f[25] & 3);
        EXPECT_EQ(image.border, f[26] & 7);

        if (!is128)
        {
            ASSERT_EQ(f.size(), 49179u);
            EXPECT_EQ(image.banks.size(), 3u);
            EXPECT_EQ(image.banks.at(5), Slice(f, 27, 16384));
            EXPECT_EQ(image.banks.at(2), Slice(f, 27 + 16384, 16384));
            EXPECT_EQ(image.banks.at(0), Slice(f, 27 + 2 * 16384, 16384));
            EXPECT_FALSE(image.paging.p7FFD.has_value()) << "a 48K file carries no paging";
            EXPECT_FALSE(image.trdosPaged);
            // The PC is on the stack at SP: low byte first
            const uint16_t sp = Word(f[24], f[23]);
            if (sp >= 0x4000 && sp < 0xFFFF)
            {
                const size_t at = 27 + (sp - 0x4000);
                EXPECT_EQ(image.cpu.pc, Word(f[at + 1], f[at]));
                EXPECT_EQ(image.cpu.sp, static_cast<uint16_t>(sp + 2));
            }
            continue;
        }

        const uint8_t p7ffd = f[49181];
        const uint8_t top = p7ffd & 7;
        // 5, 2 and the paged bank, then the other banks in ascending order: five of them, or six when the paged
        // bank is 5 or 2 (the third bank then repeats one already stored)
        const bool repeated = top == 5 || top == 2;
        ASSERT_EQ(f.size(), repeated ? 147487u : 131103u);
        EXPECT_EQ(image.cpu.pc, Word(f[49180], f[49179]));
        ASSERT_TRUE(image.paging.p7FFD.has_value());
        EXPECT_EQ(*image.paging.p7FFD, p7ffd);
        EXPECT_EQ(image.trdosPaged, f[49182] != 0);
        EXPECT_EQ(image.cpu.sp, Word(f[24], f[23])) << "128K files keep the PC out of the stack";

        EXPECT_EQ(image.banks.at(5), Slice(f, 27, 16384));
        EXPECT_EQ(image.banks.at(2), Slice(f, 27 + 16384, 16384));
        if (!repeated)
            EXPECT_EQ(image.banks.at(top), Slice(f, 27 + 2 * 16384, 16384));
        else
            EXPECT_EQ(Slice(f, 27 + 2 * 16384, 16384), Slice(f, repeated && top == 5 ? 27 : 27 + 16384, 16384))
                << "the repeated third bank";
        size_t at = 49183;
        for (uint16_t bank = 0; bank < 8; ++bank)
        {
            if (bank == 5 || bank == 2 || bank == top)
                continue;
            EXPECT_EQ(image.banks.at(bank), Slice(f, at, 16384)) << "bank " << bank;
            at += 16384;
        }
        EXPECT_EQ(image.banks.size(), 8u);
        EXPECT_EQ(at, f.size());
    }
}

TEST_F(SnapshotImageSna_Test, TheDumpNamesTheFileAndHashesTheBanks)
{
    const snapshot::Image image = ImageOf(TestPathHelper::GetTestDataPath("loaders/sna/action.sna"));
    const StateNode dump = snapshot::ToStateNode(image);
    ASSERT_NE(dump.find("format"), nullptr);
    EXPECT_EQ(dump.find("format")->s, "sna");
    ASSERT_NE(dump.find("banks"), nullptr);
    EXPECT_EQ(dump.find("banks")->items.size(), 8u);
    EXPECT_EQ(dump.find("paging")->find("p7FFD")->s, "19");
    EXPECT_EQ(snapshot::HashText(image.banks.at(1)).size(), 16u);
}

TEST_F(SnapshotImageSna_Test, ThePlanStepReportsTodaysCommit)
{
    snapshot::Image image = ImageOf(TestPathHelper::GetTestDataPath("loaders/sna/action.sna"));
    snapshot::Report report;
    EXPECT_TRUE(snapshot::Pipeline::Plan(image, &_context, snapshot::Options{}, report));
    EXPECT_EQ(report.commit, "legacy");
    EXPECT_FALSE(report.refused);
    EXPECT_EQ(report.format, "sna");
    EXPECT_EQ(report.machineHint, "128k-family");
    EXPECT_EQ(report.verdicts.size(), 1u);

    snapshot::Options forced;
    forced.commit = "legacy";
    snapshot::Report forcedReport;
    EXPECT_TRUE(snapshot::Pipeline::Plan(image, &_context, forced, forcedReport));

    snapshot::Options unknown;
    unknown.commit = "nonesuch";
    snapshot::Report refusedReport;
    EXPECT_FALSE(snapshot::Pipeline::Plan(image, &_context, unknown, refusedReport));
    EXPECT_TRUE(refusedReport.refused);
    EXPECT_NE(refusedReport.reason.find("nonesuch"), std::string::npos);
    EXPECT_NE(refusedReport.ToText().find("refused"), std::string::npos);
}

// ---------------------------------------------------------------------------------------------------------------------
// Z80
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
/// The format's RLE, written out for the oracle: ED ED count value = value repeated count times
std::vector<uint8_t> Unpack(const uint8_t* src, size_t size, size_t expected)
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i < size && out.size() < expected;)
    {
        if (i + 3 < size && src[i] == 0xED && src[i + 1] == 0xED)
        {
            out.insert(out.end(), src[i + 2], src[i + 3]);
            i += 4;
        }
        else
            out.push_back(src[i++]);
    }
    return out;
}

/// 0 = 48K, 1 = 128K, 2 = 256K (Scorpion); the hardware byte's meaning differs between v2 and v3
int MemoryClass(bool v3, uint8_t hardware)
{
    if (hardware == 10)
        return 2;
    if (hardware == 0 || hardware == 1 || (v3 && hardware == 3))
        return 0;
    return 1;
}
}  // namespace

class SnapshotImageZ80_Test : public ::testing::Test
{
protected:
    EmulatorContext _context{LoggerLevel::LogError};

    snapshot::Image ImageOf(const fs::path& path)
    {
        LoaderZ80CUT loader(&_context, path.string());
        EXPECT_TRUE(loader.validate()) << path;
        EXPECT_TRUE(loader.stageLoad()) << path;
        return loader.BuildImage();
    }
};

TEST_F(SnapshotImageZ80_Test, EveryFixtureAgreesWithItsOwnBytes)
{
    std::vector<fs::path> fixtures = Fixtures("loaders/z80", ".z80");
    for (const fs::path& p : Fixtures("loaders/z80/libspectrum", ".z80"))
        fixtures.push_back(p);
    ASSERT_GE(fixtures.size(), 10u);

    for (const fs::path& path : fixtures)
    {
        SCOPED_TRACE(path.filename().string());
        const std::vector<uint8_t> f = ReadFile(path);
        const snapshot::Image image = ImageOf(path);
        const bool v1 = (f[6] | f[7] << 8) != 0;
        const bool v3 = !v1 && (f[30] | f[31] << 8) != 23;
        const uint8_t hardware = v1 ? 0 : f[34];
        const int memory = v1 ? 0 : MemoryClass(v3, hardware);

        EXPECT_EQ(image.format, "z80");
        EXPECT_EQ(image.formatVersion, v1 ? "v1" : v3 ? "v3" : "v2");
        EXPECT_EQ(image.memoryModel, memory == 0   ? snapshot::MemoryModel::Mem48k
                                     : memory == 1 ? snapshot::MemoryModel::Mem128k
                                                   : snapshot::MemoryModel::Extended);

        // Registers, by the published header layout (WoS formats)
        EXPECT_EQ(image.cpu.af, Word(f[0], f[1]));
        EXPECT_EQ(image.cpu.bc, Word(f[3], f[2]));
        EXPECT_EQ(image.cpu.hl, Word(f[5], f[4]));
        EXPECT_EQ(image.cpu.pc, v1 ? Word(f[7], f[6]) : Word(f[33], f[32]));
        EXPECT_EQ(image.cpu.sp, Word(f[9], f[8]));
        EXPECT_EQ(image.cpu.i, f[10]);
        EXPECT_EQ(image.cpu.r, static_cast<uint8_t>((f[11] & 0x7F) | ((f[12] & 1) << 7)));
        EXPECT_EQ(image.border, (f[12] >> 1) & 7);
        EXPECT_EQ(image.cpu.de, Word(f[14], f[13]));
        EXPECT_EQ(image.cpu.bc2, Word(f[16], f[15]));
        EXPECT_EQ(image.cpu.de2, Word(f[18], f[17]));
        EXPECT_EQ(image.cpu.hl2, Word(f[20], f[19]));
        EXPECT_EQ(image.cpu.af2, Word(f[21], f[22]));
        EXPECT_EQ(image.cpu.iy, Word(f[24], f[23]));
        EXPECT_EQ(image.cpu.ix, Word(f[26], f[25]));
        EXPECT_EQ(image.cpu.iff1, f[27] != 0);
        EXPECT_EQ(image.cpu.iff2, f[28] != 0);
        EXPECT_EQ(image.cpu.im, f[29] & 3);

        if (v1)
            continue;

        // Paging, AY and the extended header
        if (memory != 0)
        {
            ASSERT_TRUE(image.paging.p7FFD.has_value());
            EXPECT_EQ(*image.paging.p7FFD, f[35]);
        }
        else
            EXPECT_FALSE(image.paging.p7FFD.has_value());
        const size_t extended = f[30] | f[31] << 8;
        if (v3 && extended >= 55 && memory != 0)
        {
            ASSERT_TRUE(image.paging.p1FFD.has_value());
            EXPECT_EQ(*image.paging.p1FFD, f[86]);
        }
        const bool hasAy = memory != 0 || (f[37] & 4) != 0;
        ASSERT_EQ(image.ay.size(), hasAy ? 1u : 0u);
        if (hasAy)
        {
            EXPECT_EQ(image.ay[0].selected, f[38]);
            for (size_t r = 0; r < 16; ++r)
                EXPECT_EQ(image.ay[0].registers[r], f[39 + r]) << "AY register " << r;
        }

        // The memory blocks: the page number says which bank
        std::map<uint16_t, std::vector<uint8_t>> expected;
        for (size_t at = 32 + extended; at + 3 <= f.size();)
        {
            const uint16_t length = static_cast<uint16_t>(f[at] | f[at + 1] << 8);
            const uint8_t page = f[at + 2];
            const bool raw = length == 0xFFFF;
            const size_t stored = raw ? 16384 : length;
            ASSERT_LE(at + 3 + stored, f.size());
            const std::vector<uint8_t> bytes = raw ? Slice(f, at + 3, 16384) : Unpack(&f[at + 3], stored, 16384);
            ASSERT_EQ(bytes.size(), 16384u);
            if (memory == 0)
            {
                if (page == 8) expected[5] = bytes;
                if (page == 4) expected[2] = bytes;
                if (page == 5) expected[0] = bytes;
            }
            else if (page >= 3 && page < (memory == 2 ? 19 : 11))
                expected[static_cast<uint16_t>(page - 3)] = bytes;
            at += 3 + stored;
        }
        EXPECT_EQ(image.banks, expected);
        EXPECT_FALSE(image.banks.empty());
    }
}

TEST_F(SnapshotImageZ80_Test, TheMachineHintComesFromTheHardwareByte)
{
    const auto hint = [&](const char* relative) {
        return ImageOf(TestPathHelper::GetTestDataPath(relative)).machineHint;
    };
    EXPECT_EQ(hint("loaders/z80/BBG128.z80"), "128k") << "v2 hardware 3";
    EXPECT_EQ(hint("loaders/z80/newbench.z80"), "48k");
    EXPECT_EQ(hint("loaders/z80/dizzyx.z80"), "pentagon128") << "v3 hardware 9";
    EXPECT_EQ(hint("loaders/z80/libspectrum/synth-128.z80"), "128k") << "v3 hardware 4";
    EXPECT_EQ(hint("loaders/z80/libspectrum/synth-plus3.z80"), "plus3");
    EXPECT_EQ(hint("loaders/z80/libspectrum/synth-plus2a.z80"), "plus2a");
    EXPECT_EQ(hint("loaders/z80/libspectrum/synth-scorpion.z80"), "scorpion256");
}

// ---------------------------------------------------------------------------------------------------------------------
// SZX: the oracle is libspectrum (Fuse's library), recorded next to every file by tools/verification/szx/szxtool
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
std::map<std::string, std::string> ReadOracle(const fs::path& path)
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

int Int(const std::map<std::string, std::string>& oracle, const std::string& key) { return std::stoi(oracle.at(key)); }

std::vector<fs::path> SzxCorpus()
{
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(TestPathHelper::GetTestDataPath("loaders/szx")))
    {
        if (entry.path().extension() == ".szx")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}
}  // namespace

TEST(SnapshotImageSzx_Test, EveryFixtureAgreesWithLibspectrum)
{
    const auto files = SzxCorpus();
    ASSERT_GE(files.size(), 10u);
    for (const fs::path& path : files)
    {
        SCOPED_TRACE(path.filename().string());
        const std::vector<uint8_t> bytes = ReadFile(path);
        szx::Stage stage;
        std::string error;
        ASSERT_TRUE(SzxReader::Parse(bytes.data(), bytes.size(), stage, error)) << error;
        const snapshot::Image image = LoaderSZX::BuildImage(stage, path.string());

        fs::path oraclePath = path;
        oraclePath.replace_extension(".libspectrum.txt");
        const auto oracle = ReadOracle(oraclePath);
        ASSERT_FALSE(oracle.empty());

        EXPECT_EQ(image.format, "szx");
        const snapshot::Cpu& c = image.cpu;
        EXPECT_EQ(c.af, (Int(oracle, "a") << 8) | Int(oracle, "f"));
        EXPECT_EQ(c.bc, Int(oracle, "bc"));
        EXPECT_EQ(c.de, Int(oracle, "de"));
        EXPECT_EQ(c.hl, Int(oracle, "hl"));
        EXPECT_EQ(c.af2, (Int(oracle, "a_") << 8) | Int(oracle, "f_"));
        EXPECT_EQ(c.bc2, Int(oracle, "bc_"));
        EXPECT_EQ(c.de2, Int(oracle, "de_"));
        EXPECT_EQ(c.hl2, Int(oracle, "hl_"));
        EXPECT_EQ(c.ix, Int(oracle, "ix"));
        EXPECT_EQ(c.iy, Int(oracle, "iy"));
        EXPECT_EQ(c.sp, Int(oracle, "sp"));
        EXPECT_EQ(c.pc, Int(oracle, "pc"));
        EXPECT_EQ(c.i, Int(oracle, "i"));
        EXPECT_EQ(c.r, Int(oracle, "r"));
        EXPECT_EQ(c.iff1, Int(oracle, "iff1") != 0);
        EXPECT_EQ(c.iff2, Int(oracle, "iff2") != 0);
        EXPECT_EQ(c.im, Int(oracle, "im"));
        ASSERT_TRUE(image.framePosition.has_value());
        EXPECT_EQ(*image.framePosition, static_cast<uint32_t>(std::stoul(oracle.at("tstates"))));
        ASSERT_TRUE(c.halted.has_value());
        EXPECT_EQ(*c.halted, Int(oracle, "halted") != 0);
        EXPECT_EQ(image.border, Int(oracle, "ula") & 7);

        if (stage.machineId != szx::Mid16K && stage.machineId != szx::Mid48K)
        {
            ASSERT_TRUE(image.paging.p7FFD.has_value());
            if (szx::HasAy(stage.machineId))
                EXPECT_EQ(*image.paging.p7FFD, Int(oracle, "p7ffd"));
        }
        if (szx::HasPort1FFD(stage.machineId))
        {
            ASSERT_TRUE(image.paging.p1FFD.has_value());
            EXPECT_EQ(*image.paging.p1FFD, Int(oracle, "p1ffd"));
        }
        if (szx::HasPortEFF7(stage.machineId))
        {
            ASSERT_TRUE(image.paging.pEFF7.has_value());
            EXPECT_EQ(*image.paging.pEFF7, Int(oracle, "p1ffd")) << "libspectrum calls SPCR byte 2 p1ffd for both";
        }
        EXPECT_EQ(image.trdosPaged, Int(oracle, "beta_paged") != 0);
        if (stage.ay)
        {
            ASSERT_EQ(image.ay.size(), 1u);
            EXPECT_EQ(image.ay[0].selected, Int(oracle, "ay_port"));
            std::istringstream list(oracle.at("ay"));
            std::string value;
            for (int reg = 0; std::getline(list, value, ','); reg++)
                EXPECT_EQ(image.ay[0].registers[reg], std::stoi(value)) << "AY register " << reg;
        }

        // Every page libspectrum saw, with the same bytes, as a logical bank
        size_t banks = 0;
        for (const auto& [key, crcText] : oracle)
        {
            if (key.rfind("page", 0) != 0)
                continue;
            const uint16_t bank = static_cast<uint16_t>(std::stoi(key.substr(4)));
            ASSERT_TRUE(image.banks.count(bank)) << "bank " << bank;
            const std::vector<uint8_t>& data = image.banks.at(bank);
            EXPECT_EQ(static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, data.data(), data.size())),
                      static_cast<uint32_t>(std::stoul(crcText, nullptr, 16)))
                << "bank " << bank;
            ++banks;
        }
        EXPECT_EQ(banks, image.banks.size());
    }
}

TEST(SnapshotImageSzx_Test, MachineHintAndMemoryModelFollowTheMachineId)
{
    const auto of = [](const char* relative) {
        const std::string path = TestPathHelper::GetTestDataPath(relative);
        const std::vector<uint8_t> bytes = ReadFile(path);
        szx::Stage stage;
        std::string error;
        EXPECT_TRUE(SzxReader::Parse(bytes.data(), bytes.size(), stage, error)) << error;
        return LoaderSZX::BuildImage(stage, path);
    };
    EXPECT_EQ(of("loaders/szx/libspectrum/synth-48.szx").machineHint, "48k");
    EXPECT_EQ(of("loaders/szx/libspectrum/synth-48.szx").memoryModel, snapshot::MemoryModel::Mem48k);
    EXPECT_EQ(of("loaders/szx/libspectrum/synth-128.szx").machineHint, "128k");
    EXPECT_EQ(of("loaders/szx/libspectrum/synth-plus3.szx").machineHint, "plus3");
    EXPECT_EQ(of("loaders/szx/libspectrum/synth-pentagon1024.szx").machineHint, "pentagon1024");
    EXPECT_EQ(of("loaders/szx/libspectrum/synth-pentagon1024.szx").memoryModel, snapshot::MemoryModel::Extended);
    EXPECT_EQ(of("loaders/szx/libspectrum/synth-pentagon1024.szx").banks.size(), 64u);
    EXPECT_EQ(of("loaders/szx/libspectrum/synth-scorpion.szx").machineHint, "scorpion256");
    const snapshot::Image crazy = of("loaders/szx/other/spectaculator-pentagon-crazylove.szx");
    EXPECT_FALSE(crazy.extensions.empty()) << "B128, BDSK, KEYB, JOY, AMXM and the blocks we do not take";
}

// ---------------------------------------------------------------------------------------------------------------------
// SPG: physical addresses; the golden hashes are the ones recorded after a byte-for-byte comparison with lvd's mhmt
// (core/tests/emulator/machines/tsconf/loaderspg_test.cpp)
// ---------------------------------------------------------------------------------------------------------------------

TEST(SnapshotImageSpg_Test, EveryFixtureIsPhysicalWithTheVerifiedBytes)
{
    struct Golden
    {
        const char* file;
        size_t blocks;
        uint16_t pc, sp;
        uint64_t hash;
    };
    const Golden golden[] = {
        {"empty.spg", 4, 0xE000, 0xDFFF, 0x72ED070C52ED9632ull},
        {"sprites.spg", 8, 0xE000, 0xDFFF, 0xA7A14F3FF21BFDEEull},
        {"slideshow.spg", 14, 0xE000, 0xDFFF, 0xE63E6AA164785EDCull},
    };
    for (const Golden& g : golden)
    {
        SCOPED_TRACE(g.file);
        const fs::path path = TestPathHelper::GetTestDataPath(std::string("machines/tsconf/spg/") + g.file);
        const std::vector<uint8_t> file = ReadFile(path);
        ASSERT_FALSE(file.empty()) << "testdata missing";
        LoaderSPG::Image spg;
        std::string error;
        ASSERT_TRUE(LoaderSPG::Parse(file, spg, error)) << error;
        const snapshot::Image image = LoaderSPG::BuildSnapshotImage(spg, path.string());

        EXPECT_EQ(image.format, "spg");
        EXPECT_EQ(image.machineHint, "tsconf");
        EXPECT_EQ(image.memoryModel, snapshot::MemoryModel::Physical);
        EXPECT_TRUE(image.banks.empty()) << "an SPG knows no logical banks";
        EXPECT_EQ(image.cpu.pc, g.pc);
        EXPECT_EQ(image.cpu.sp, g.sp);
        EXPECT_EQ(image.cpu.im, 1);
        EXPECT_EQ(image.cpu.i, 0x3F);
        ASSERT_EQ(image.physical.size(), g.blocks);

        uint64_t hash = 0xcbf29ce484222325ull;
        for (size_t i = 0; i < g.blocks; ++i)
        {
            // The address from the raw descriptor: page * #4000 + offset in 512-byte units
            const uint8_t* d = &file[0x100 + i * 3];
            EXPECT_EQ(image.physical[i].address, static_cast<uint32_t>(d[2]) * 0x4000u + (d[0] & 0x1Fu) * 512u);
            EXPECT_EQ(image.physical[i].data.size(), 16384u);
            for (uint8_t b : image.physical[i].data)
            {
                hash ^= b;
                hash *= 0x100000001b3ull;
            }
        }
        EXPECT_EQ(hash, g.hash);
        EXPECT_EQ(image.RamBytes(), g.blocks * 16384u);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// ZXP: one image per module; the oracle slices the raw bytes by the published layout
// ---------------------------------------------------------------------------------------------------------------------

TEST(SnapshotImageZxp_Test, EveryModuleAgreesWithTheRawLayout)
{
    const char* files[] = {"Alien8.zxp", "buratino_adventures.zxp", "ComandoQuatro.zxp", "fh.zxp"};
    for (const char* name : files)
    {
        SCOPED_TRACE(name);
        const fs::path path = TestPathHelper::GetTestDataPath(std::string("machines/zxpoly/zxp/") + name);
        const std::vector<uint8_t> f = ReadFile(path);
        ASSERT_GT(f.size(), 146u) << "testdata missing";
        LoaderZXP loader(nullptr, path.string());
        ASSERT_TRUE(loader.Parse()) << loader.GetError();

        // Big-endian: 146-byte header, then per module N, N x (page index, 16384 bytes)
        const auto be16 = [&](size_t at) { return static_cast<uint16_t>(f[at] << 8 | f[at + 1]); };
        size_t at = 146;
        for (size_t m = 0; m < 4; ++m)
        {
            const snapshot::Image image = loader.BuildImage(m);
            EXPECT_EQ(image.format, "zxp");
            EXPECT_EQ(image.machineHint, "zxpoly");
            EXPECT_EQ(image.paging.p7FFD, f[10 + m * 5]) << "module " << m;
            EXPECT_EQ(image.border, f[9] & 7);
            EXPECT_EQ(image.cpu.af, be16(30 + 0 * 8 + m * 2));
            EXPECT_EQ(image.cpu.af2, be16(30 + 1 * 8 + m * 2));
            EXPECT_EQ(image.cpu.bc, be16(30 + 2 * 8 + m * 2));
            EXPECT_EQ(image.cpu.de, be16(30 + 4 * 8 + m * 2));
            EXPECT_EQ(image.cpu.hl, be16(30 + 6 * 8 + m * 2));
            EXPECT_EQ(image.cpu.ix, be16(30 + 8 * 8 + m * 2));
            EXPECT_EQ(image.cpu.iy, be16(30 + 9 * 8 + m * 2));
            EXPECT_EQ(image.cpu.im, f[118 + m]);
            EXPECT_EQ(image.cpu.pc, be16(130 + m * 2));
            EXPECT_EQ(image.cpu.sp, be16(138 + m * 2));

            const size_t count = f[at++];
            EXPECT_EQ(image.banks.size(), count) << "module " << m;
            for (size_t i = 0; i < count; ++i)
            {
                const uint8_t page = f[at++];
                ASSERT_TRUE(image.banks.count(page)) << "module " << m << " bank " << int(page);
                EXPECT_EQ(image.banks.at(page), Slice(f, at, 16384)) << "module " << m << " bank " << int(page);
                at += 16384;
            }
        }
        EXPECT_EQ(at, f.size());
    }
}
