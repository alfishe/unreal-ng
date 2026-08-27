#ifdef ENABLE_GDB_AUTOMATION

#include <gtest/gtest.h>
#include "gdbpacket.h"
#include <cstdint>
#include <string>
#include <vector>

class GDBPacketFuzzTest : public ::testing::Test
{
protected:
    GDBPacketReader reader;
};

TEST_F(GDBPacketFuzzTest, EmptyInput)
{
    EXPECT_FALSE(reader.isComplete());
    EXPECT_FALSE(reader.hasInterrupt());
}

TEST_F(GDBPacketFuzzTest, ValidPacket)
{
    std::string packet = "$g#67";
    for (char c : packet)
    {
        reader.feed(c);
    }
    EXPECT_TRUE(reader.isComplete());
    EXPECT_TRUE(reader.isChecksumValid());
    EXPECT_EQ(reader.extractData(), "g");
}

TEST_F(GDBPacketFuzzTest, InvalidChecksum)
{
    std::string packet = "$g#00";
    for (char c : packet)
    {
        reader.feed(c);
    }
    EXPECT_TRUE(reader.isComplete());
    EXPECT_FALSE(reader.isChecksumValid());
}

TEST_F(GDBPacketFuzzTest, InterruptCharacter)
{
    reader.feed('\x03');
    EXPECT_TRUE(reader.hasInterrupt());
}

TEST_F(GDBPacketFuzzTest, InterruptMidPacket)
{
    reader.feed('$');
    reader.feed('g');
    reader.feed('\x03');
    EXPECT_TRUE(reader.hasInterrupt());
    EXPECT_FALSE(reader.isComplete());
}

TEST_F(GDBPacketFuzzTest, InvalidHexInChecksum)
{
    std::string packet = "$g#zz";
    for (char c : packet)
    {
        reader.feed(c);
    }
    EXPECT_TRUE(reader.isError());
}

TEST_F(GDBPacketFuzzTest, TruncatedPacket)
{
    reader.feed('$');
    reader.feed('g');
    EXPECT_FALSE(reader.isComplete());
    EXPECT_FALSE(reader.isError());
}

TEST_F(GDBPacketFuzzTest, EmptyData)
{
    std::string packet = "$#00";
    for (char c : packet)
    {
        reader.feed(c);
    }
    EXPECT_TRUE(reader.isComplete());
    EXPECT_TRUE(reader.isChecksumValid());
    EXPECT_EQ(reader.extractData(), "");
}

TEST_F(GDBPacketFuzzTest, LongData)
{
    std::string data(10000, 'A');
    uint8_t checksum = 0;
    for (char c : data)
    {
        checksum += static_cast<uint8_t>(c);
    }

    reader.feed('$');
    for (char c : data)
    {
        reader.feed(c);
    }
    reader.feed('#');
    std::string hex = GDBPacket::toHex(checksum, 2);
    reader.feed(hex[0]);
    reader.feed(hex[1]);

    EXPECT_TRUE(reader.isComplete());
    EXPECT_TRUE(reader.isChecksumValid());
    EXPECT_EQ(reader.extractData().size(), 10000u);
}

TEST_F(GDBPacketFuzzTest, NullBytesInData)
{
    reader.feed('$');
    reader.feed('\0');
    reader.feed('\0');
    reader.feed('\0');
    reader.feed('#');
    reader.feed('0');
    reader.feed('0');

    EXPECT_TRUE(reader.isComplete());
}

TEST_F(GDBPacketFuzzTest, HighBytesInData)
{
    reader.feed('$');
    reader.feed('\xff');
    reader.feed('\xfe');
    reader.feed('#');
    std::string hex = GDBPacket::toHex(0xff + 0xfe, 2);
    reader.feed(hex[0]);
    reader.feed(hex[1]);

    EXPECT_TRUE(reader.isComplete());
    EXPECT_TRUE(reader.isChecksumValid());
}

TEST_F(GDBPacketFuzzTest, ResetAfterComplete)
{
    std::string packet = "$g#67";
    for (char c : packet)
    {
        reader.feed(c);
    }
    EXPECT_TRUE(reader.isComplete());

    reader.reset();
    EXPECT_FALSE(reader.isComplete());
    EXPECT_FALSE(reader.hasInterrupt());

    for (char c : packet)
    {
        reader.feed(c);
    }
    EXPECT_TRUE(reader.isComplete());
}

TEST_F(GDBPacketFuzzTest, MultiplePacketsSequential)
{
    std::string packet = "$g#67";
    for (char c : packet)
    {
        reader.feed(c);
    }
    EXPECT_TRUE(reader.isComplete());
    reader.extractData();

    for (char c : packet)
    {
        reader.feed(c);
    }
    EXPECT_TRUE(reader.isComplete());
}

TEST_F(GDBPacketFuzzTest, GarbageBeforeStart)
{
    std::string garbage = "random garbage \t\n\r";
    for (char c : garbage)
    {
        reader.feed(c);
    }
    EXPECT_FALSE(reader.isComplete());

    std::string packet = "$g#67";
    for (char c : packet)
    {
        reader.feed(c);
    }
    EXPECT_TRUE(reader.isComplete());
}

TEST(GDBPacketEncodeTest, BasicEncode)
{
    std::string encoded = GDBPacket::encode("g");
    EXPECT_EQ(encoded, "$g#67");
}

TEST(GDBPacketDecodeTest, BasicDecode)
{
    auto data = GDBPacket::decode("$g#67");
    EXPECT_TRUE(data.has_value());
    EXPECT_EQ(*data, "g");
}

TEST(GDBPacketDecodeTest, InvalidDecodes)
{
    EXPECT_FALSE(GDBPacket::decode("").has_value());
    EXPECT_FALSE(GDBPacket::decode("g#67").has_value());
    EXPECT_FALSE(GDBPacket::decode("$g67").has_value());
    EXPECT_FALSE(GDBPacket::decode("$g#6").has_value());
    EXPECT_FALSE(GDBPacket::decode("$g#00").has_value());
    EXPECT_FALSE(GDBPacket::decode("$g#zz").has_value());
}

TEST(GDBPacketHexTest, ParseHex)
{
    EXPECT_EQ(GDBPacket::parseHex("00"), 0u);
    EXPECT_EQ(GDBPacket::parseHex("ff"), 255u);
    EXPECT_EQ(GDBPacket::parseHex("FF"), 255u);
    EXPECT_EQ(GDBPacket::parseHex("1234"), 0x1234u);
    EXPECT_FALSE(GDBPacket::parseHex("").has_value());
    EXPECT_FALSE(GDBPacket::parseHex("zz").has_value());
    EXPECT_FALSE(GDBPacket::parseHex("1g").has_value());
}

TEST(GDBPacketHexTest, ToHex)
{
    EXPECT_EQ(GDBPacket::toHex(0, 2), "00");
    EXPECT_EQ(GDBPacket::toHex(255, 2), "ff");
    EXPECT_EQ(GDBPacket::toHex(0x1234, 4), "1234");
    EXPECT_EQ(GDBPacket::toHex(0xABCD, 4), "abcd");
}

TEST(GDBPacketHexTest, HexEncodeDecode)
{
    std::string original = "Hello, World!";
    std::string encoded = GDBPacket::hexEncode(original);
    std::string decoded = GDBPacket::hexDecode(encoded);
    EXPECT_EQ(decoded, original);
}

TEST(GDBPacketBinaryTest, EscapeUnescape)
{
    std::vector<uint8_t> data = {0x00, '#', '$', '}', '*', 0xFF};
    std::string escaped = GDBPacket::escapeBinary(data);
    std::vector<uint8_t> unescaped = GDBPacket::unescapeBinary(escaped);
    EXPECT_EQ(unescaped, data);
}

TEST(GDBPacketRLETest, CompressExpand)
{
    std::string data = "AAAAAAAAAAAA";
    std::string compressed = GDBPacket::compressRLE(data);
    EXPECT_LT(compressed.size(), data.size());

    std::string expanded = GDBPacket::expandRLE(compressed);
    EXPECT_EQ(expanded, data);
}

TEST(GDBPacketRLETest, NoCompression)
{
    std::string data = "ABC";
    std::string compressed = GDBPacket::compressRLE(data);
    EXPECT_EQ(compressed, data);
}

TEST(GDBPacketRLETest, EdgeCases)
{
    EXPECT_EQ(GDBPacket::compressRLE(""), "");
    EXPECT_EQ(GDBPacket::expandRLE(""), "");
    EXPECT_EQ(GDBPacket::compressRLE("A"), "A");
    EXPECT_EQ(GDBPacket::expandRLE("A"), "A");
}

TEST(GDBPacketBytesTest, BytesToHex)
{
    std::vector<uint8_t> bytes = {0x00, 0x12, 0xAB, 0xFF};
    std::string hex = GDBPacket::bytesToHex(bytes);
    EXPECT_EQ(hex, "0012abff");
}

TEST(GDBPacketBytesTest, HexToBytes)
{
    std::vector<uint8_t> bytes = GDBPacket::hexToBytes("0012ABFF");
    EXPECT_EQ(bytes.size(), 4u);
    EXPECT_EQ(bytes[0], 0x00);
    EXPECT_EQ(bytes[1], 0x12);
    EXPECT_EQ(bytes[2], 0xAB);
    EXPECT_EQ(bytes[3], 0xFF);
}

TEST(GDBPacketBytesTest, OddLengthHex)
{
    std::vector<uint8_t> bytes = GDBPacket::hexToBytes("123");
    EXPECT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x12);
}

#endif // ENABLE_GDB_AUTOMATION
