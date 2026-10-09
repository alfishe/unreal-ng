#include "base64.h"

namespace base64
{
std::string Encode(const std::vector<uint8_t>& bytes)
{
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3)
    {
        const uint32_t v = (static_cast<uint32_t>(bytes[i]) << 16) | (static_cast<uint32_t>(bytes[i + 1]) << 8) | bytes[i + 2];
        out.push_back(alphabet[(v >> 18) & 63]);
        out.push_back(alphabet[(v >> 12) & 63]);
        out.push_back(alphabet[(v >> 6) & 63]);
        out.push_back(alphabet[v & 63]);
    }
    if (i < bytes.size())
    {
        const uint32_t v = (static_cast<uint32_t>(bytes[i]) << 16) | (i + 1 < bytes.size() ? static_cast<uint32_t>(bytes[i + 1]) << 8 : 0u);
        out.push_back(alphabet[(v >> 18) & 63]);
        out.push_back(alphabet[(v >> 12) & 63]);
        out.push_back(i + 1 < bytes.size() ? alphabet[(v >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

bool Decode(const std::string& text, std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : text)
    {
        int value;
        if (c >= 'A' && c <= 'Z')
            value = c - 'A';
        else if (c >= 'a' && c <= 'z')
            value = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            value = c - '0' + 52;
        else if (c == '+' || c == '-')
            value = 62;
        else if (c == '/' || c == '_')
            value = 63;
        else if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t')
            continue;
        else
            return false;
        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<uint8_t>(buffer >> bits));
        }
    }
    return true;
}
}  // namespace base64
