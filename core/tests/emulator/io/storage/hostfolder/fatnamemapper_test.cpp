// FatNameMapper: host names -> 8.3 short names (CP866 / CP1251) + LFN, and back
// (technical design §6.3)

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "emulator/io/storage/hostfolder/fatnamemapper.h"

namespace
{
    std::string Short(const FatMappedName& m)
    {
        return std::string(m.shortName.begin(), m.shortName.end());
    }
}  // namespace

TEST(FatNameMapper_Test, PlainNamesNeedNoLongName)
{
    const auto names = FatNameMapper::MapFolder({"SD_BOOT.$C", "GAMES", "A.TXT"});
    ASSERT_TRUE(names[0].ok && names[1].ok && names[2].ok);
    EXPECT_EQ(Short(names[0]), "SD_BOOT $C ");
    EXPECT_EQ(Short(names[1]), "GAMES      ");
    EXPECT_EQ(Short(names[2]), "A       TXT");
    EXPECT_FALSE(names[0].hasLongName || names[1].hasLongName || names[2].hasLongName);
}

TEST(FatNameMapper_Test, CaseOnlyGetsALongNameNotATail)
{
    const auto names = FatNameMapper::MapFolder({"readme.txt"});
    EXPECT_EQ(Short(names[0]), "README  TXT");
    EXPECT_TRUE(names[0].hasLongName);
    EXPECT_EQ(names[0].longName, u"readme.txt");
}

TEST(FatNameMapper_Test, LossyNamesGetWindowsTails)
{
    const auto names = FatNameMapper::MapFolder({"LongFileName1.txt", "LongFileName2.txt", "a+b.bin", ".profile", "archive.tar.gz"});
    EXPECT_EQ(Short(names[0]), "LONGFI~1TXT");
    EXPECT_EQ(Short(names[1]), "LONGFI~2TXT");
    EXPECT_EQ(Short(names[2]), "A_B~1   BIN");
    EXPECT_EQ(Short(names[3]), "PROFIL~1   ") << "a leading dot is dropped";
    EXPECT_EQ(Short(names[4]), "ARCHIV~1GZ ") << "extension after the last dot";
    for (const auto& n : names)
        EXPECT_TRUE(n.hasLongName);
}

TEST(FatNameMapper_Test, TailsNeverStack)
{
    std::vector<std::string> many;
    for (int i = 0; i < 12; i++)
        many.push_back("AB+" + std::to_string(i) + ".c");  // all map to AB_<n>
    const auto names = FatNameMapper::MapFolder(many);
    EXPECT_EQ(Short(names[0]), "AB_0~1  C  ");
    std::vector<std::string> shorts;
    for (const auto& n : names)
    {
        const std::string s = Short(n);
        EXPECT_EQ(s.find("~1~"), std::string::npos) << s;
        shorts.push_back(s);
    }
    std::sort(shorts.begin(), shorts.end());
    EXPECT_EQ(std::unique(shorts.begin(), shorts.end()), shorts.end()) << "unique within the folder";

    const auto twins = FatNameMapper::MapFolder({"LONGNAME.TXT", "longname.txt"});
    EXPECT_EQ(Short(twins[0]), "LONGNAMETXT");
    EXPECT_EQ(Short(twins[1]), "LONGNA~1TXT") << "a case-only twin gets a tail";
}

TEST(FatNameMapper_Test, CyrillicInCp866AndCp1251)
{
    const auto cp866 = FatNameMapper::MapFolder({"Длинное имя.txt"}, CodePage::Cp866);
    const FatShortName expected866 = {0x84, 0x8B, 0x88, 0x8D, 0x8D, 0x8E, '~', '1', 'T', 'X', 'T'};
    EXPECT_EQ(cp866[0].shortName, expected866);
    EXPECT_EQ(cp866[0].longName, u"Длинное имя.txt");

    const auto cp1251 = FatNameMapper::MapFolder({"Длинное имя.txt"}, CodePage::Cp1251);
    const FatShortName expected1251 = {0xC4, 0xCB, 0xC8, 0xCD, 0xCD, 0xCE, '~', '1', 'T', 'X', 'T'};
    EXPECT_EQ(cp1251[0].shortName, expected1251);

    EXPECT_EQ(FatNameMapper::ShortNameToUtf8(cp866[0].shortName, CodePage::Cp866), "ДЛИННО~1.TXT");
    EXPECT_EQ(FatNameMapper::ShortNameToUtf8(cp1251[0].shortName, CodePage::Cp1251), "ДЛИННО~1.TXT");

    const auto euro = FatNameMapper::MapFolder({"€.txt"}, CodePage::Cp866);
    EXPECT_EQ(Short(euro[0]), "_~1     TXT") << "a character the page cannot hold becomes _";
}

/// #E5 marks a deleted directory entry, so a short name whose first byte is
/// #E5 (CP866 'х') is stored as #05 and reads back as 'х'
TEST(FatNameMapper_Test, LeadingByteE5IsStoredAs05)
{
    FatShortName stored = FatNameMapper::ShortNameFromText("A.BIN");
    stored[0] = 0x05;
    EXPECT_EQ(FatNameMapper::ShortNameToUtf8(stored, CodePage::Cp866), "х.BIN");
    EXPECT_EQ(FatNameMapper::ShortNameToUtf8(stored, CodePage::Cp1251), "е.BIN") << "#E5 is 'е' in CP1251";
}

TEST(FatNameMapper_Test, LongNameEntriesLayoutAndChecksum)
{
    const FatShortName shortName = FatNameMapper::ShortNameFromText("LONGFI~1.TXT");
    // Reference checksum computed by hand from the spec formula
    uint8_t sum = 0;
    for (uint8_t b : shortName)
        sum = static_cast<uint8_t>(((sum & 1) << 7) + (sum >> 1) + b);
    EXPECT_EQ(FatNameMapper::Checksum(shortName), sum);

    const std::u16string name = u"LongFileName1.txt";  // 17 units -> 2 entries
    const auto entries = FatNameMapper::LongNameEntries(name, sum);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0][0], 0x42) << "on disk: sequence 2 with the last flag first";
    EXPECT_EQ(entries[1][0], 0x01);
    for (const auto& e : entries)
    {
        EXPECT_EQ(e[11], 0x0F);
        EXPECT_EQ(e[13], sum);
        EXPECT_EQ(e[26], 0);
        EXPECT_EQ(e[27], 0);
    }
    // Entry 1 holds units 0-12 "LongFileName1"; entry 2 units 13-16 ".txt",
    // then the #0000 terminator in slot 4 (offset 9) and #FFFF padding
    EXPECT_EQ(entries[1][1], 'L');
    EXPECT_EQ(entries[1][3], 'o');
    EXPECT_EQ(entries[1][30], '1') << "unit 12 in the last slot (offset 30)";
    EXPECT_EQ(entries[0][1], '.');
    EXPECT_EQ(entries[0][7], 't') << "unit 16 in slot 3 (offset 7)";
    EXPECT_EQ(entries[0][9], 0x00);
    EXPECT_EQ(entries[0][10], 0x00);
    EXPECT_EQ(entries[0][14], 0xFF);
    EXPECT_EQ(entries[0][31], 0xFF);
}

TEST(FatNameMapper_Test, TooLongOrEmptyNamesAreRefused)
{
    const auto names = FatNameMapper::MapFolder({std::string(256, 'a'), ""});
    EXPECT_FALSE(names[0].ok);
    EXPECT_NE(names[0].reason.find("255"), std::string::npos);
    EXPECT_FALSE(names[1].ok);
}
