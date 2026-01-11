#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

/// @class UUID
/// @brief Cross-platform strongly-typed UUID implementation
///
/// This class provides a portable UUID implementation that works on all platforms
/// without requiring platform-specific libraries like libuuid. It stores the 16-byte
/// UUID value internally and provides conversion to/from string representation.
///
/// UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (36 characters with hyphens)
class UUID
{
public:
    /// Raw 16-byte UUID storage
    std::array<uint8_t, 16> bytes;

    /// Default constructor - creates a nil (all zeros) UUID
    UUID()
    {
        bytes.fill(0);
    }

    /// Construct from raw bytes
    explicit UUID(const std::array<uint8_t, 16>& data) : bytes(data) {}

    /// Construct from string representation
    /// @param str UUID string in format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    explicit UUID(const std::string& str)
    {
        bytes.fill(0);
        parse(str);
    }

    /// Generate a new random UUID (version 4)
    static UUID Generate()
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<uint32_t> dis(0, 255);

        UUID uuid;
        for (size_t i = 0; i < 16; ++i)
        {
            uuid.bytes[i] = static_cast<uint8_t>(dis(gen));
        }

        // Set version to 4 (random UUID) - bits 12-15 of time_hi_and_version
        uuid.bytes[6] = (uuid.bytes[6] & 0x0F) | 0x40;

        // Set variant to RFC 4122 - bits 6-7 of clock_seq_hi_and_reserved
        uuid.bytes[8] = (uuid.bytes[8] & 0x3F) | 0x80;

        return uuid;
    }

    /// Check if this is a nil (all zeros) UUID
    bool isNil() const
    {
        for (uint8_t b : bytes)
        {
            if (b != 0)
                return false;
        }
        return true;
    }

    /// Clear the UUID (set to all zeros)
    void clear()
    {
        bytes.fill(0);
    }

    /// Convert to string representation (lowercase)
    /// @return UUID string in format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    std::string toString() const
    {
        std::ostringstream ss;
        ss << std::hex << std::setfill('0');

        for (size_t i = 0; i < 16; ++i)
        {
            if (i == 4 || i == 6 || i == 8 || i == 10)
            {
                ss << '-';
            }
            ss << std::setw(2) << static_cast<int>(bytes[i]);
        }

        return ss.str();
    }

    /// Parse a UUID string
    /// @param str UUID string in format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    /// @return true if parsing succeeded, false otherwise
    bool parse(const std::string& str)
    {
        // Expected format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (36 chars)
        if (str.length() != 36)
        {
            clear();
            return false;
        }

        // Validate hyphen positions
        if (str[8] != '-' || str[13] != '-' || str[18] != '-' || str[23] != '-')
        {
            clear();
            return false;
        }

        // Parse hex digits
        size_t byteIndex = 0;
        for (size_t i = 0; i < 36 && byteIndex < 16; ++i)
        {
            if (str[i] == '-')
                continue;

            if (i + 1 >= 36)
            {
                clear();
                return false;
            }

            char highChar = str[i];
            char lowChar = str[i + 1];

            int high = hexCharToInt(highChar);
            int low = hexCharToInt(lowChar);

            if (high < 0 || low < 0)
            {
                clear();
                return false;
            }

            bytes[byteIndex++] = static_cast<uint8_t>((high << 4) | low);
            ++i;  // Skip the next char since we consumed two
        }

        return byteIndex == 16;
    }

    /// Equality comparison
    bool operator==(const UUID& other) const
    {
        return bytes == other.bytes;
    }

    /// Inequality comparison
    bool operator!=(const UUID& other) const
    {
        return bytes != other.bytes;
    }

    /// Less-than comparison (for use in ordered containers)
    bool operator<(const UUID& other) const
    {
        return bytes < other.bytes;
    }

    /// Implicit conversion to string for convenience
    operator std::string() const
    {
        return toString();
    }

private:
    /// Convert a hex character to its integer value
    /// @return 0-15 for valid hex chars, -1 for invalid
    static int hexCharToInt(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F')
            return 10 + (c - 'A');
        return -1;
    }
};
