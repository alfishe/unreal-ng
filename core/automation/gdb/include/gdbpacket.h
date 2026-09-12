#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// @brief GDB Remote Serial Protocol packet framing
///
/// Handles RSP packet format: $<data>#<checksum>
/// Supports:
/// - Checksum calculation and verification
/// - ACK/NACK handling
/// - RLE compression (optional)
/// - Binary escaping for special characters
/// - NoAckMode after QStartNoAckMode handshake
class GDBPacket
{
public:
    /// @brief Calculate checksum for packet data
    static uint8_t calculateChecksum(std::string_view data);

    /// @brief Format data into a complete RSP packet with checksum
    static std::string encode(std::string_view data);

    /// @brief Parse a raw packet, verify checksum, extract data
    /// @return Extracted data if valid, nullopt if invalid checksum
    static std::optional<std::string> decode(std::string_view raw);

    /// @brief Escape binary data for transmission (0x23 '#', 0x24 '$', 0x7d '}', 0x2a '*')
    static std::string escapeBinary(const std::vector<uint8_t>& data);

    /// @brief Unescape received binary data
    static std::vector<uint8_t> unescapeBinary(std::string_view data);

    /// @brief Apply RLE compression to data
    static std::string compressRLE(std::string_view data);

    /// @brief Expand RLE-compressed data
    static std::string expandRLE(std::string_view data);

    /// @brief Convert bytes to hex string (uppercase)
    static std::string bytesToHex(const std::vector<uint8_t>& bytes);
    static std::string bytesToHex(const uint8_t* data, size_t len);

    /// @brief Convert hex string to bytes
    static std::vector<uint8_t> hexToBytes(std::string_view hex);

    /// @brief Parse hex value from string
    static std::optional<uint64_t> parseHex(std::string_view hex);

    /// @brief Format value as hex string
    static std::string toHex(uint64_t value, int width = 0);

    /// @brief Hex-encode a string (for qRcmd output)
    static std::string hexEncode(std::string_view str);

    /// @brief Hex-decode a string (for qRcmd input)
    static std::string hexDecode(std::string_view hex);
};

/// @brief Packet reader state machine for streaming input
class GDBPacketReader
{
public:
    enum class State
    {
        WaitingForStart,  // Looking for '$'
        ReadingData,      // Reading packet data
        ReadingChecksum1, // First checksum hex digit
        ReadingChecksum2, // Second checksum hex digit
        Complete,         // Packet complete, ready to extract
        Error             // Invalid packet
    };

    GDBPacketReader() = default;

    /// @brief Feed a byte into the state machine
    /// @return true if a complete packet is ready
    bool feed(char c);

    /// @brief Check if a complete packet is ready
    bool isComplete() const { return _state == State::Complete; }

    /// @brief Check if an error occurred
    bool hasError() const { return _state == State::Error; }

    /// @brief Extract the packet data (resets state)
    std::string extractData();

    /// @brief Check if checksum was valid
    bool isChecksumValid() const { return _checksumValid; }

    /// @brief Reset state machine
    void reset();

    /// @brief Check if an interrupt (0x03) was received
    bool hasInterrupt() const { return _interrupt; }

    /// @brief Clear interrupt flag
    void clearInterrupt() { _interrupt = false; }

private:
    State _state = State::WaitingForStart;
    std::string _data;
    uint8_t _checksum = 0;
    uint8_t _receivedChecksum = 0;
    bool _checksumValid = false;
    bool _interrupt = false;
};
