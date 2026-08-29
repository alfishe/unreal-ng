#include "dzrpprotocol.h"
#include <cstring>

namespace dzrp {

std::optional<Command> Protocol::parseCommand(const uint8_t* data, size_t len)
{
    if (len < 2)
        return std::nullopt;

    Command cmd;
    cmd.seqNo = data[0] & 0x0F;
    cmd.cmdId = static_cast<CommandId>(data[1]);

    if (len > 2)
    {
        cmd.payload.assign(data + 2, data + len);
    }

    return cmd;
}

std::vector<uint8_t> Protocol::serializeResponse(const Response& resp)
{
    std::vector<uint8_t> out;

    // Length = seqNo(1) + payload
    uint32_t length = 1 + static_cast<uint32_t>(resp.payload.size());
    out.resize(4 + length);

    writeU32LE(out.data(), length);

    // SeqNo with NAK bit
    out[4] = resp.seqNo | (resp.nak ? NAK_BIT : 0);

    // Payload
    if (!resp.payload.empty())
    {
        std::memcpy(out.data() + 5, resp.payload.data(), resp.payload.size());
    }

    return out;
}

std::vector<uint8_t> Protocol::serializeNotification(const Notification& notif)
{
    std::vector<uint8_t> out;

    // Length = seqNo(1) + notifyId(1) + payload
    uint32_t length = 2 + static_cast<uint32_t>(notif.payload.size());
    out.resize(4 + length);

    writeU32LE(out.data(), length);

    // SeqNo = 0 for notifications
    out[4] = SEQ_NOTIFICATION;
    out[5] = static_cast<uint8_t>(notif.notifyId);

    // Payload
    if (!notif.payload.empty())
    {
        std::memcpy(out.data() + 6, notif.payload.data(), notif.payload.size());
    }

    return out;
}

size_t Protocol::readFramedMessage(const uint8_t* data, size_t len,
                                    std::vector<uint8_t>& outPayload)
{
    if (len < 4)
        return 0;

    // DZRP COMMAND framing (client → server), per DeZog dzrpbufferremote.ts:
    //   [length(4) = DATA length only][seqNo(1)][command(1)][data(length)]
    // The length field does NOT count the seqNo and command bytes, so the full
    // frame after the 4-byte header is 2 + length bytes. (Responses use a
    // different convention — length includes the seqNo — handled in
    // serializeResponse; readFramedMessage is only used to parse commands.)
    uint32_t dataLen = readU32LE(data);

    // Sanity check
    if (dataLen > 1024 * 1024)
        return 0;

    const size_t total = 4 + 2 + static_cast<size_t>(dataLen);  // header + seqNo + cmd + data
    if (len < total)
        return 0;

    // Return seqNo + command + data for parseCommand().
    outPayload.assign(data + 4, data + total);
    return total;
}

uint16_t Protocol::readU16LE(const uint8_t* data)
{
    return static_cast<uint16_t>(data[0]) |
           (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t Protocol::readU32LE(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void Protocol::writeU16LE(uint8_t* data, uint16_t value)
{
    data[0] = static_cast<uint8_t>(value & 0xFF);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void Protocol::writeU32LE(uint8_t* data, uint32_t value)
{
    data[0] = static_cast<uint8_t>(value & 0xFF);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

std::string Protocol::readNulString(const uint8_t* data, size_t maxLen)
{
    size_t len = 0;
    while (len < maxLen && data[len] != 0)
        ++len;
    return std::string(reinterpret_cast<const char*>(data), len);
}

void Protocol::writeNulString(std::vector<uint8_t>& out, const std::string& str)
{
    out.insert(out.end(), str.begin(), str.end());
    out.push_back(0);
}

std::vector<uint8_t> Protocol::buildSupportedCommandsBitfield()
{
    std::vector<uint8_t> bitfield(32, 0);

    auto setBit = [&](uint8_t cmdId) {
        bitfield[cmdId / 8] |= (1 << (cmdId % 8));
    };

    // Tier 1: Core
    setBit(static_cast<uint8_t>(CommandId::CMD_INIT));
    setBit(static_cast<uint8_t>(CommandId::CMD_CLOSE));
    setBit(static_cast<uint8_t>(CommandId::CMD_GET_REGISTERS));
    setBit(static_cast<uint8_t>(CommandId::CMD_SET_REGISTER));
    setBit(static_cast<uint8_t>(CommandId::CMD_CONTINUE));
    setBit(static_cast<uint8_t>(CommandId::CMD_PAUSE));
    setBit(static_cast<uint8_t>(CommandId::CMD_READ_MEM));
    setBit(static_cast<uint8_t>(CommandId::CMD_WRITE_MEM));
    setBit(static_cast<uint8_t>(CommandId::CMD_GET_SUPPORTED_COMMANDS));

    // Tier 2: Debugging
    setBit(static_cast<uint8_t>(CommandId::CMD_ADD_BREAKPOINT));
    setBit(static_cast<uint8_t>(CommandId::CMD_REMOVE_BREAKPOINT));
    setBit(static_cast<uint8_t>(CommandId::CMD_ADD_WATCHPOINT));
    setBit(static_cast<uint8_t>(CommandId::CMD_REMOVE_WATCHPOINT));

    // Tier 3: ZX Spectrum features
    setBit(static_cast<uint8_t>(CommandId::CMD_SET_SLOT));
    setBit(static_cast<uint8_t>(CommandId::CMD_WRITE_BANK));
    setBit(static_cast<uint8_t>(CommandId::CMD_SET_BORDER));
    setBit(static_cast<uint8_t>(CommandId::CMD_READ_PORT));
    setBit(static_cast<uint8_t>(CommandId::CMD_WRITE_PORT));
    setBit(static_cast<uint8_t>(CommandId::CMD_READ_STATE));
    setBit(static_cast<uint8_t>(CommandId::CMD_WRITE_STATE));

    // Unreal-NG extensions: TTD-backed instruction history (reverse debugging)
    setBit(static_cast<uint8_t>(CommandId::CMD_GET_HISTORY_INFO));
    setBit(static_cast<uint8_t>(CommandId::CMD_GET_HISTORY_ENTRY));

    return bitfield;
}

} // namespace dzrp
