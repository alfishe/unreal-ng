#include <gtest/gtest.h>
#include "dzrpprotocol.h"
#include "dzrptypes.h"

using namespace dzrp;

class DZRPProtocolTest : public ::testing::Test
{
protected:
    void SetUp() override {}
};

// --- Little-endian helpers ---

TEST_F(DZRPProtocolTest, ReadU16LE)
{
    uint8_t data[] = {0x34, 0x12};
    EXPECT_EQ(Protocol::readU16LE(data), 0x1234);
}

TEST_F(DZRPProtocolTest, ReadU32LE)
{
    uint8_t data[] = {0x78, 0x56, 0x34, 0x12};
    EXPECT_EQ(Protocol::readU32LE(data), 0x12345678u);
}

TEST_F(DZRPProtocolTest, WriteU16LE)
{
    uint8_t data[2] = {0};
    Protocol::writeU16LE(data, 0x1234);
    EXPECT_EQ(data[0], 0x34);
    EXPECT_EQ(data[1], 0x12);
}

TEST_F(DZRPProtocolTest, WriteU32LE)
{
    uint8_t data[4] = {0};
    Protocol::writeU32LE(data, 0x12345678);
    EXPECT_EQ(data[0], 0x78);
    EXPECT_EQ(data[1], 0x56);
    EXPECT_EQ(data[2], 0x34);
    EXPECT_EQ(data[3], 0x12);
}

// --- NUL string ---

TEST_F(DZRPProtocolTest, ReadNulString)
{
    uint8_t data[] = {'H', 'e', 'l', 'l', 'o', 0, 'X'};
    EXPECT_EQ(Protocol::readNulString(data, 7), "Hello");
}

TEST_F(DZRPProtocolTest, ReadNulStringNoTerminator)
{
    uint8_t data[] = {'A', 'B', 'C'};
    EXPECT_EQ(Protocol::readNulString(data, 3), "ABC");
}

TEST_F(DZRPProtocolTest, WriteNulString)
{
    std::vector<uint8_t> out;
    Protocol::writeNulString(out, "Hi");
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0], 'H');
    EXPECT_EQ(out[1], 'i');
    EXPECT_EQ(out[2], 0);
}

// --- Command parsing ---

TEST_F(DZRPProtocolTest, ParseCommandValid)
{
    // seqNo=5, cmdId=3 (GET_REGISTERS), no payload
    uint8_t data[] = {0x05, 0x03};
    auto cmd = Protocol::parseCommand(data, 2);
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->seqNo, 5);
    EXPECT_EQ(cmd->cmdId, CommandId::CMD_GET_REGISTERS);
    EXPECT_TRUE(cmd->payload.empty());
}

TEST_F(DZRPProtocolTest, ParseCommandWithPayload)
{
    // seqNo=1, cmdId=8 (READ_MEM), payload: [0, 0x00, 0x40, 0x10, 0x00]
    uint8_t data[] = {0x01, 0x08, 0x00, 0x00, 0x40, 0x10, 0x00};
    auto cmd = Protocol::parseCommand(data, 7);
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->seqNo, 1);
    EXPECT_EQ(cmd->cmdId, CommandId::CMD_READ_MEM);
    ASSERT_EQ(cmd->payload.size(), 5u);
    EXPECT_EQ(cmd->payload[2], 0x40);  // addr low
}

TEST_F(DZRPProtocolTest, ParseCommandTooShort)
{
    uint8_t data[] = {0x05};
    auto cmd = Protocol::parseCommand(data, 1);
    EXPECT_FALSE(cmd.has_value());
}

// --- Response serialization ---

TEST_F(DZRPProtocolTest, SerializeResponseACK)
{
    Response resp;
    resp.seqNo = 7;
    resp.nak = false;
    resp.payload = {0xAA, 0xBB};

    auto data = Protocol::serializeResponse(resp);
    // length(4) + seqNo(1) + payload(2) = 7 bytes total
    // length field = 3 (seqNo + payload)
    ASSERT_EQ(data.size(), 7u);
    EXPECT_EQ(Protocol::readU32LE(data.data()), 3u);
    EXPECT_EQ(data[4], 0x07);  // seqNo, no NAK bit
    EXPECT_EQ(data[5], 0xAA);
    EXPECT_EQ(data[6], 0xBB);
}

TEST_F(DZRPProtocolTest, SerializeResponseNAK)
{
    Response resp;
    resp.seqNo = 3;
    resp.nak = true;

    auto data = Protocol::serializeResponse(resp);
    ASSERT_EQ(data.size(), 5u);
    EXPECT_EQ(Protocol::readU32LE(data.data()), 1u);
    EXPECT_EQ(data[4], 0x83);  // seqNo=3 | NAK_BIT
}

TEST_F(DZRPProtocolTest, SerializeResponseEmptyPayload)
{
    Response resp;
    resp.seqNo = 15;
    resp.nak = false;

    auto data = Protocol::serializeResponse(resp);
    ASSERT_EQ(data.size(), 5u);
    EXPECT_EQ(Protocol::readU32LE(data.data()), 1u);
    EXPECT_EQ(data[4], 15);
}

// --- Notification serialization ---

TEST_F(DZRPProtocolTest, SerializeNotification)
{
    Notification notif;
    notif.notifyId = NotificationId::NTF_PAUSE;
    // reason=2 (BP), addr=0x8000, bank=0, message=""
    notif.payload = {0x02, 0x00, 0x80, 0x00, 0x00};

    auto data = Protocol::serializeNotification(notif);
    // length(4) + seqNo(1) + notifyId(1) + payload(5) = 11 bytes
    ASSERT_EQ(data.size(), 11u);
    EXPECT_EQ(Protocol::readU32LE(data.data()), 7u);  // seqNo + notifyId + payload
    EXPECT_EQ(data[4], 0);  // seqNo = 0 for notifications
    EXPECT_EQ(data[5], 1);  // NTF_PAUSE
    EXPECT_EQ(data[6], 2);  // reason = breakpoint
}

// --- Framed message reading ---

TEST_F(DZRPProtocolTest, ReadFramedMessageComplete)
{
    // length=3, then 3 bytes of payload
    uint8_t data[] = {0x03, 0x00, 0x00, 0x00, 0xAA, 0xBB, 0xCC};
    std::vector<uint8_t> payload;

    size_t consumed = Protocol::readFramedMessage(data, 7, payload);
    EXPECT_EQ(consumed, 7u);
    ASSERT_EQ(payload.size(), 3u);
    EXPECT_EQ(payload[0], 0xAA);
}

TEST_F(DZRPProtocolTest, ReadFramedMessageIncomplete)
{
    // length=10 but only 5 bytes available
    uint8_t data[] = {0x0A, 0x00, 0x00, 0x00, 0xAA};
    std::vector<uint8_t> payload;

    size_t consumed = Protocol::readFramedMessage(data, 5, payload);
    EXPECT_EQ(consumed, 0u);
}

TEST_F(DZRPProtocolTest, ReadFramedMessageTooShortForHeader)
{
    uint8_t data[] = {0x03, 0x00};
    std::vector<uint8_t> payload;

    size_t consumed = Protocol::readFramedMessage(data, 2, payload);
    EXPECT_EQ(consumed, 0u);
}

// --- Supported commands bitfield ---

TEST_F(DZRPProtocolTest, BuildSupportedCommandsBitfield)
{
    auto bitfield = Protocol::buildSupportedCommandsBitfield();
    ASSERT_EQ(bitfield.size(), 32u);

    auto hasBit = [&](uint8_t cmdId) {
        return (bitfield[cmdId / 8] & (1 << (cmdId % 8))) != 0;
    };

    // Tier 1
    EXPECT_TRUE(hasBit(1));   // CMD_INIT
    EXPECT_TRUE(hasBit(2));   // CMD_CLOSE
    EXPECT_TRUE(hasBit(3));   // CMD_GET_REGISTERS
    EXPECT_TRUE(hasBit(8));   // CMD_READ_MEM
    EXPECT_TRUE(hasBit(24));  // CMD_GET_SUPPORTED_COMMANDS

    // Tier 2
    EXPECT_TRUE(hasBit(40));  // CMD_ADD_BREAKPOINT
    EXPECT_TRUE(hasBit(41));  // CMD_REMOVE_BREAKPOINT
    EXPECT_TRUE(hasBit(42));  // CMD_ADD_WATCHPOINT
    EXPECT_TRUE(hasBit(43));  // CMD_REMOVE_WATCHPOINT

    // Tier 3
    EXPECT_TRUE(hasBit(50));  // CMD_READ_STATE
    EXPECT_TRUE(hasBit(51));  // CMD_WRITE_STATE

    // Not implemented
    EXPECT_FALSE(hasBit(11)); // CMD_GET_TBBLUE_REG
    EXPECT_FALSE(hasBit(13)); // CMD_SET_BREAKPOINTS
}

// --- Round-trip test ---

TEST_F(DZRPProtocolTest, ResponseRoundTrip)
{
    Response original;
    original.seqNo = 12;
    original.nak = false;
    original.payload = {0x01, 0x02, 0x03, 0x04};

    auto serialized = Protocol::serializeResponse(original);

    // Parse back (skip length prefix)
    std::vector<uint8_t> payload;
    size_t consumed = Protocol::readFramedMessage(serialized.data(), serialized.size(), payload);
    EXPECT_EQ(consumed, serialized.size());

    // Verify
    EXPECT_EQ(payload[0] & 0x0F, original.seqNo);
    EXPECT_EQ(payload[0] & 0x80, 0);  // Not NAK
    EXPECT_EQ(payload.size() - 1, original.payload.size());
}

// --- Sequence number masking ---

TEST_F(DZRPProtocolTest, ParseCommandMasksSeqHighBits)
{
    // A seq byte with high bits set (e.g. NAK bit echoed by a broken peer)
    // must be masked down to the 4-bit sequence number
    uint8_t data[] = {0x85, 0x03};
    auto cmd = Protocol::parseCommand(data, 2);
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->seqNo, 5);
    EXPECT_EQ(cmd->cmdId, CommandId::CMD_GET_REGISTERS);
}

// --- Framing limits ---

TEST_F(DZRPProtocolTest, ReadFramedMessageOversizedRejected)
{
    // length = 1MB + 1 exceeds the sanity cap and must be rejected
    uint8_t data[] = {0x01, 0x00, 0x10, 0x00};
    std::vector<uint8_t> payload;
    EXPECT_EQ(Protocol::readFramedMessage(data, 4, payload), 0u);
}

TEST_F(DZRPProtocolTest, ReadFramedMessageCoalesced)
{
    // Two frames in one buffer: the first call must consume exactly frame 1
    // so the caller can re-invoke on the remainder for frame 2 (sessionLoop
    // relies on this for TCP coalescing)
    uint8_t data[] = {0x02, 0x00, 0x00, 0x00, 0xAA, 0xBB,
                      0x01, 0x00, 0x00, 0x00, 0xCC};
    std::vector<uint8_t> payload;

    size_t consumed = Protocol::readFramedMessage(data, sizeof(data), payload);
    ASSERT_EQ(consumed, 6u);
    ASSERT_EQ(payload.size(), 2u);
    EXPECT_EQ(payload[0], 0xAA);

    consumed = Protocol::readFramedMessage(data + consumed, sizeof(data) - consumed, payload);
    ASSERT_EQ(consumed, 5u);
    ASSERT_EQ(payload.size(), 1u);
    EXPECT_EQ(payload[0], 0xCC);
}
