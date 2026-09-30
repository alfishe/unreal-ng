#include "common/network/dnsmessage.h"

namespace dns
{

static uint16_t Read16(const uint8_t* p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static void Put16(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

static void Put32(std::vector<uint8_t>& out, uint32_t v)
{
    Put16(out, static_cast<uint16_t>(v >> 16));
    Put16(out, static_cast<uint16_t>(v & 0xFFFF));
}

bool ParseQuery(const uint8_t* data, size_t length, Question& out)
{
    if (!data || length < 12)
        return false;
    out.id = Read16(data);
    out.flags = Read16(data + 2);
    const uint16_t qdcount = Read16(data + 4);
    if ((out.flags & 0x8000) != 0 || qdcount == 0)  // a response, or no question
        return false;

    // QNAME: labels, no compression in a query's first question
    size_t pos = 12;
    std::string name;
    for (;;)
    {
        if (pos >= length)
            return false;
        const uint8_t len = data[pos++];
        if (len == 0)
            break;
        if ((len & 0xC0) != 0 || pos + len > length || name.size() + len + 1 > 253)
            return false;
        if (!name.empty())
            name.push_back('.');
        for (uint8_t i = 0; i < len; ++i)
        {
            char c = static_cast<char>(data[pos + i]);
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
            name.push_back(c);
        }
        pos += len;
    }
    if (pos + 4 > length)
        return false;
    out.name = name;
    out.qtype = Read16(data + pos);
    out.qclass = Read16(data + pos + 2);
    out.questionEnd = pos + 4;
    return true;
}

std::vector<uint8_t> BuildAnswer(const uint8_t* query, size_t length, const Question& q,
                                 const std::vector<uint32_t>& addresses, uint8_t rcode, uint32_t ttl)
{
    std::vector<uint8_t> out;
    if (!query || q.questionEnd > length || q.questionEnd < 12)
        return out;

    const bool answer = rcode == kRcodeNoError && q.qtype == kTypeA && q.qclass == kClassIn;
    // At most 8 records: the reply stays far below the classic 512-byte UDP limit
    const uint16_t ancount = answer ? static_cast<uint16_t>(addresses.size() < 8 ? addresses.size() : 8) : 0;

    // Header: QR=1, opcode and RD copied from the query, RA=1, rcode
    Put16(out, q.id);
    const uint16_t flags = static_cast<uint16_t>(0x8000 | (q.flags & 0x7900) | 0x0080 | (rcode & 0x0F));
    Put16(out, flags);
    Put16(out, 1);        // QDCOUNT: the first question only
    Put16(out, ancount);
    Put16(out, 0);        // NSCOUNT
    Put16(out, 0);        // ARCOUNT

    // The question, as the guest wrote it
    out.insert(out.end(), query + 12, query + q.questionEnd);

    for (uint16_t i = 0; i < ancount; ++i)
    {
        Put16(out, 0xC00C);   // name: pointer to the question name
        Put16(out, kTypeA);
        Put16(out, kClassIn);
        Put32(out, ttl);
        Put16(out, 4);
        Put32(out, addresses[i]);
    }
    return out;
}


std::vector<uint8_t> BuildQuery(uint16_t id, const std::string& name)
{
    std::vector<uint8_t> out;
    if (name.empty() || name.size() > 253)
        return out;
    Put16(out, id);
    Put16(out, 0x0100);   // standard query, recursion desired
    Put16(out, 1);        // one question
    Put16(out, 0);
    Put16(out, 0);
    Put16(out, 0);
    size_t start = 0;
    while (start <= name.size())
    {
        size_t dot = name.find('.', start);
        if (dot == std::string::npos)
            dot = name.size();
        const size_t label = dot - start;
        if (label == 0 || label > 63)
            return {};
        out.push_back(static_cast<uint8_t>(label));
        out.insert(out.end(), name.begin() + static_cast<std::ptrdiff_t>(start),
                   name.begin() + static_cast<std::ptrdiff_t>(dot));
        start = dot + 1;
        if (dot == name.size())
            break;
    }
    out.push_back(0);
    Put16(out, kTypeA);
    Put16(out, kClassIn);
    return out;
}

/// Skip a (possibly compressed) name; returns the offset after it, 0 on error
static size_t SkipName(const uint8_t* data, size_t length, size_t pos)
{
    while (pos < length)
    {
        const uint8_t len = data[pos];
        if ((len & 0xC0) == 0xC0)
            return pos + 2 <= length ? pos + 2 : 0;   // a pointer ends the name
        if (len == 0)
            return pos + 1;
        pos += 1u + len;
    }
    return 0;
}

bool ParseAnswer(const uint8_t* data, size_t length, uint16_t id, std::vector<uint32_t>& addresses, uint8_t& rcode)
{
    addresses.clear();
    rcode = kRcodeServFail;
    if (!data || length < 12 || Read16(data) != id)
        return false;
    const uint16_t flags = Read16(data + 2);
    if (!(flags & 0x8000))
        return false;   // not an answer
    rcode = static_cast<uint8_t>(flags & 0x0F);
    const uint16_t qdcount = Read16(data + 4);
    const uint16_t ancount = Read16(data + 6);
    size_t pos = 12;
    for (uint16_t q = 0; q < qdcount; ++q)
    {
        pos = SkipName(data, length, pos);
        if (pos == 0 || pos + 4 > length)
            return false;
        pos += 4;
    }
    for (uint16_t a = 0; a < ancount; ++a)
    {
        pos = SkipName(data, length, pos);
        if (pos == 0 || pos + 10 > length)
            return false;
        const uint16_t type = Read16(data + pos);
        const uint16_t cls = Read16(data + pos + 2);
        const uint16_t rdlength = Read16(data + pos + 8);
        pos += 10;
        if (pos + rdlength > length)
            return false;
        if (type == kTypeA && cls == kClassIn && rdlength == 4)
            addresses.push_back((static_cast<uint32_t>(data[pos]) << 24) | (static_cast<uint32_t>(data[pos + 1]) << 16) |
                                (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3]);
        pos += rdlength;
    }
    return true;
}

} // namespace dns
