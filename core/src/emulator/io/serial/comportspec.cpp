#include "emulator/io/serial/comportspec.h"

#include <cctype>
#include <cstdlib>

namespace
{
std::string Trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

std::string Upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool ParseUnsigned(const std::string& s, uint32_t max, uint32_t& out)
{
    if (s.empty() || s.size() > 10)
        return false;
    uint64_t v = 0;
    for (char c : s)
    {
        if (c < '0' || c > '9')
            return false;
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    if (v > max)
        return false;
    out = static_cast<uint32_t>(v);
    return true;
}

/// RFC 1123 host name: labels of letters, digits and '-', not starting or
/// ending with '-', 1..63 characters each, 253 at most in all
bool ValidHostName(const std::string& s)
{
    if (s.empty() || s.size() > 253)
        return false;
    size_t label = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c == '.')
        {
            if (label == 0 || s[i - 1] == '-')
                return false;
            label = 0;
            continue;
        }
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-') || (c == '-' && label == 0))
            return false;
        if (++label > 63)
            return false;
    }
    return label != 0 && s.back() != '-';
}

bool ParseIpv4(const std::string& s, uint32_t& out)
{
    uint32_t addr = 0;
    size_t start = 0;
    for (int part = 0; part < 4; ++part)
    {
        const size_t dot = s.find('.', start);
        const std::string piece = s.substr(start, (part < 3 ? dot : s.size()) - start);
        uint32_t v = 0;
        if ((part < 3 && dot == std::string::npos) || !ParseUnsigned(piece, 255, v))
            return false;
        addr = (addr << 8) | v;
        start = dot + 1;
    }
    out = addr;
    return true;
}
}  // namespace

bool ComPortSpec::Parse(const std::string& text, ComPortSpec& out, std::string& error)
{
    out = ComPortSpec();
    const std::string value = Trim(text);
    const std::string upper = Upper(value);
    if (value.empty() || upper == "NONE")
        return true;
    if (upper == "LOOPBACK")
    {
        out.kind = Kind::Loopback;
        return true;
    }
    // ESPNET / AT, optionally with the module's rate: ESPNET,38400
    {
        const size_t comma = upper.find(',');
        const std::string head = Trim(upper.substr(0, comma));
        if (head == "ESPNET" || head == "AT")
        {
            out.kind = head == "ESPNET" ? Kind::Espnet : Kind::At;
            out.baud = 0;
            if (comma != std::string::npos &&
                (!ParseUnsigned(Trim(upper.substr(comma + 1)), 4000000, out.baud) || out.baud == 0))
            {
                error = "expected " + head + "[,<baud>]";
                return false;
            }
            return true;
        }
    }
    // MODEM, optionally with the guest TCP port it answers calls on: MODEM,2323
    {
        const size_t comma = upper.find(',');
        if (Trim(upper.substr(0, comma)) == "MODEM")
        {
            out.kind = Kind::Modem;
            uint32_t port = 0;
            if (comma != std::string::npos &&
                (!ParseUnsigned(Trim(upper.substr(comma + 1)), 65535, port) || port == 0))
            {
                error = "expected MODEM[,<guest port>]";
                return false;
            }
            out.port = static_cast<uint16_t>(port);
            return true;
        }
    }
    if (upper.rfind("TCP:", 0) == 0)
    {
        const std::string rest = value.substr(4);
        const size_t colon = rest.rfind(':');
        uint32_t port = 0;
        if (colon == std::string::npos || !ParseUnsigned(rest.substr(colon + 1), 65535, port) || port == 0)
        {
            error = "expected TCP:<host>:<port>";
            return false;
        }
        out.host = rest.substr(0, colon);
        if (!ParseIpv4(out.host, out.addr))
        {
            out.addr = 0;
            // A digits-and-dots string that is not an address is a typo, not a name
            const bool numeric = out.host.find_first_not_of("0123456789.") == std::string::npos;
            if (numeric || !ValidHostName(out.host))
            {
                error = "TCP:<host>:<port>: '" + out.host + "' is neither an IPv4 address nor a host name";
                return false;
            }
            for (char& c : out.host)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        out.port = static_cast<uint16_t>(port);
        out.kind = Kind::Tcp;
        return true;
    }
    if (upper.rfind("SERIAL:", 0) == 0)
    {
        std::string rest = value.substr(7);
        const size_t comma = rest.rfind(',');
        if (comma != std::string::npos)
        {
            if (!ParseUnsigned(Trim(rest.substr(comma + 1)), 4000000, out.baud) || out.baud == 0)
            {
                error = "expected SERIAL:<device>[,<baud>]";
                return false;
            }
            rest = rest.substr(0, comma);
        }
        out.device = Trim(rest);
        if (out.device.empty())
        {
            error = "expected SERIAL:<device>[,<baud>]";
            return false;
        }
        out.kind = Kind::Serial;
        return true;
    }
    error = "unknown value (NONE | LOOPBACK | TCP:<host>:<port> | SERIAL:<device>[,<baud>] | ESPNET[,<baud>] | AT[,<baud>] | "
            "MODEM[,<guest port>])";
    return false;
}

std::string ComPortSpec::ToString() const
{
    switch (kind)
    {
        case Kind::Loopback: return "LOOPBACK";
        case Kind::Tcp:
            return "TCP:" + host + ":" + std::to_string(port);
        case Kind::Serial: return "SERIAL:" + device + "," + std::to_string(baud);
        case Kind::Espnet: return baud ? "ESPNET," + std::to_string(baud) : std::string("ESPNET");
        case Kind::At: return baud ? "AT," + std::to_string(baud) : std::string("AT");
        case Kind::Modem: return port ? "MODEM," + std::to_string(port) : std::string("MODEM");
        default: return "NONE";
    }
}
