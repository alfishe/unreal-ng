#include "gdbpacket.h"

#include <iomanip>
#include <sstream>

uint8_t GDBPacket::calculateChecksum(std::string_view data)
{
    uint8_t sum = 0;
    for (char c : data)
    {
        sum += static_cast<uint8_t>(c);
    }
    return sum;
}

std::string GDBPacket::encode(std::string_view data)
{
    uint8_t checksum = calculateChecksum(data);

    std::ostringstream ss;
    ss << '$' << data << '#';
    ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(checksum);

    return ss.str();
}

std::optional<std::string> GDBPacket::decode(std::string_view raw)
{
    if (raw.size() < 4)
        return std::nullopt;

    if (raw[0] != '$')
        return std::nullopt;

    auto hashPos = raw.find('#');
    if (hashPos == std::string_view::npos || hashPos + 2 >= raw.size())
        return std::nullopt;

    std::string_view data = raw.substr(1, hashPos - 1);
    std::string_view checksumHex = raw.substr(hashPos + 1, 2);

    auto expectedChecksum = parseHex(checksumHex);
    if (!expectedChecksum)
        return std::nullopt;

    uint8_t actualChecksum = calculateChecksum(data);
    if (actualChecksum != static_cast<uint8_t>(*expectedChecksum))
        return std::nullopt;

    return std::string(data);
}

std::string GDBPacket::escapeBinary(const std::vector<uint8_t>& data)
{
    std::string result;
    result.reserve(data.size() * 2);

    for (uint8_t byte : data)
    {
        if (byte == '#' || byte == '$' || byte == '}' || byte == '*')
        {
            result += '}';
            result += static_cast<char>(byte ^ 0x20);
        }
        else
        {
            result += static_cast<char>(byte);
        }
    }

    return result;
}

std::vector<uint8_t> GDBPacket::unescapeBinary(std::string_view data)
{
    std::vector<uint8_t> result;
    result.reserve(data.size());

    bool escaped = false;
    for (char c : data)
    {
        if (escaped)
        {
            result.push_back(static_cast<uint8_t>(c) ^ 0x20);
            escaped = false;
        }
        else if (c == '}')
        {
            escaped = true;
        }
        else
        {
            result.push_back(static_cast<uint8_t>(c));
        }
    }

    return result;
}

std::string GDBPacket::compressRLE(std::string_view data)
{
    if (data.empty())
        return "";

    std::string result;
    result.reserve(data.size());

    size_t i = 0;
    while (i < data.size())
    {
        char c = data[i];
        size_t count = 1;

        while (i + count < data.size() && data[i + count] == c && count < 126)
        {
            count++;
        }

        if (count >= 4)
        {
            result += c;
            result += '*';
            result += static_cast<char>(count + 29 - 1);
            i += count;
        }
        else
        {
            result += c;
            i++;
        }
    }

    return result;
}

std::string GDBPacket::expandRLE(std::string_view data)
{
    std::string result;
    result.reserve(data.size() * 2);

    for (size_t i = 0; i < data.size(); i++)
    {
        if (i + 1 < data.size() && data[i + 1] == '*')
        {
            if (i + 2 < data.size())
            {
                char c = data[i];
                int count = static_cast<uint8_t>(data[i + 2]) - 29 + 1;
                for (int j = 0; j < count; j++)
                {
                    result += c;
                }
                i += 2;
            }
        }
        else
        {
            result += data[i];
        }
    }

    return result;
}

std::string GDBPacket::bytesToHex(const std::vector<uint8_t>& bytes)
{
    return bytesToHex(bytes.data(), bytes.size());
}

std::string GDBPacket::bytesToHex(const uint8_t* data, size_t len)
{
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');

    for (size_t i = 0; i < len; i++)
    {
        ss << std::setw(2) << static_cast<int>(data[i]);
    }

    return ss.str();
}

std::vector<uint8_t> GDBPacket::hexToBytes(std::string_view hex)
{
    std::vector<uint8_t> result;
    result.reserve(hex.size() / 2);

    for (size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        auto byte = parseHex(hex.substr(i, 2));
        if (byte)
        {
            result.push_back(static_cast<uint8_t>(*byte));
        }
    }

    return result;
}

std::optional<uint64_t> GDBPacket::parseHex(std::string_view hex)
{
    if (hex.empty())
        return std::nullopt;

    uint64_t result = 0;
    for (char c : hex)
    {
        result <<= 4;

        if (c >= '0' && c <= '9')
        {
            result |= (c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            result |= (c - 'a' + 10);
        }
        else if (c >= 'A' && c <= 'F')
        {
            result |= (c - 'A' + 10);
        }
        else
        {
            return std::nullopt;
        }
    }

    return result;
}

std::string GDBPacket::toHex(uint64_t value, int width)
{
    std::ostringstream ss;
    ss << std::hex;

    if (width > 0)
    {
        ss << std::setfill('0') << std::setw(width);
    }

    ss << value;
    return ss.str();
}

std::string GDBPacket::hexEncode(std::string_view str)
{
    return bytesToHex(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

std::string GDBPacket::hexDecode(std::string_view hex)
{
    auto bytes = hexToBytes(hex);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// GDBPacketReader implementation

bool GDBPacketReader::feed(char c)
{
    // Handle interrupt character anywhere
    if (c == 0x03)
    {
        _interrupt = true;
        return false;
    }

    switch (_state)
    {
        case State::WaitingForStart:
            if (c == '$')
            {
                _state = State::ReadingData;
                _data.clear();
                _checksum = 0;
            }
            break;

        case State::ReadingData:
            if (c == '#')
            {
                _state = State::ReadingChecksum1;
            }
            else
            {
                _data += c;
                _checksum += static_cast<uint8_t>(c);
            }
            break;

        case State::ReadingChecksum1:
        {
            auto nibble = GDBPacket::parseHex(std::string_view(&c, 1));
            if (nibble)
            {
                _receivedChecksum = static_cast<uint8_t>(*nibble << 4);
                _state = State::ReadingChecksum2;
            }
            else
            {
                _state = State::Error;
            }
            break;
        }

        case State::ReadingChecksum2:
        {
            auto nibble = GDBPacket::parseHex(std::string_view(&c, 1));
            if (nibble)
            {
                _receivedChecksum |= static_cast<uint8_t>(*nibble);
                _checksumValid = (_checksum == _receivedChecksum);
                _state = State::Complete;
            }
            else
            {
                _state = State::Error;
            }
            break;
        }

        case State::Complete:
        case State::Error:
            break;
    }

    return _state == State::Complete;
}

std::string GDBPacketReader::extractData()
{
    std::string result = std::move(_data);
    reset();
    return result;
}

void GDBPacketReader::reset()
{
    _state = State::WaitingForStart;
    _data.clear();
    _checksum = 0;
    _receivedChecksum = 0;
    _checksumValid = false;
}
