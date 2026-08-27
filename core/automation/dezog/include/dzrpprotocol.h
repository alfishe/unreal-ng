#pragma once

#include "dzrptypes.h"
#include <vector>
#include <cstdint>
#include <string>
#include <optional>

namespace dzrp {

// Parsed command from DeZog
struct Command
{
    uint8_t seqNo = 0;
    CommandId cmdId = CommandId::CMD_INIT;
    std::vector<uint8_t> payload;
};

// Response to send back
struct Response
{
    uint8_t seqNo = 0;
    bool nak = false;
    std::vector<uint8_t> payload;
};

// Notification to send (seqNo = 0)
struct Notification
{
    NotificationId notifyId = NotificationId::NTF_PAUSE;
    std::vector<uint8_t> payload;
};

class Protocol
{
public:
    // Parse command from raw bytes (after length prefix removed)
    static std::optional<Command> parseCommand(const uint8_t* data, size_t len);

    // Serialize response to wire format (includes length prefix)
    static std::vector<uint8_t> serializeResponse(const Response& resp);

    // Serialize notification to wire format (includes length prefix)
    static std::vector<uint8_t> serializeNotification(const Notification& notif);

    // Read length-prefixed message from buffer, returns bytes consumed (0 if incomplete)
    static size_t readFramedMessage(const uint8_t* data, size_t len,
                                     std::vector<uint8_t>& outPayload);

    // Helper: read little-endian values
    static uint16_t readU16LE(const uint8_t* data);
    static uint32_t readU32LE(const uint8_t* data);

    // Helper: write little-endian values
    static void writeU16LE(uint8_t* data, uint16_t value);
    static void writeU32LE(uint8_t* data, uint32_t value);

    // Helper: read NUL-terminated string
    static std::string readNulString(const uint8_t* data, size_t maxLen);

    // Helper: write NUL-terminated string
    static void writeNulString(std::vector<uint8_t>& out, const std::string& str);

    // Build supported commands bitfield (32 bytes)
    static std::vector<uint8_t> buildSupportedCommandsBitfield();
};

} // namespace dzrp
