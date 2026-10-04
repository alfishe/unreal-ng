#include "emulator/io/serial/esp/zifigit.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "emulator/io/serial/esp/zifistate.h"

// A port of ZiFi-ESP32-S3-Zero 2e5ba83 src/git_sha1.cpp and src/github_tree.cpp (the same arithmetic and checks)

namespace zifigit
{
namespace
{
uint32_t RotateLeft(uint32_t value, unsigned bits)
{
    return (value << bits) | (value >> (32 - bits));
}

int HexValue(char digit)
{
    if (digit >= '0' && digit <= '9')
        return digit - '0';
    if (digit >= 'a' && digit <= 'f')
        return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F')
        return digit - 'A' + 10;
    return -1;
}

/// JsonCursor: a strict reader over the answer (no allocation, refuses what it does not know)
class JsonCursor
{
public:
    JsonCursor(const char* text, size_t length) : _position(text), _end(text + length) {}

    void SkipSpace()
    {
        while (_position < _end && (*_position == ' ' || *_position == '\t' || *_position == '\r' || *_position == '\n'))
            ++_position;
    }
    bool Peek(char expected)
    {
        SkipSpace();
        return _position < _end && *_position == expected;
    }
    bool Take(char expected)
    {
        if (!Peek(expected))
            return false;
        ++_position;
        return true;
    }
    bool String(char* output, size_t capacity)
    {
        if (!Take('"'))
            return false;
        size_t used = 0;
        while (_position < _end)
        {
            char symbol = *_position++;
            if (symbol == '"')
            {
                if (output != nullptr)
                    output[used] = 0;
                return true;
            }
            if (static_cast<unsigned char>(symbol) < 0x20)
                return false;
            if (symbol != '\\')
            {
                if (!Put(output, capacity, used, symbol))
                    return false;
                continue;
            }
            if (_position >= _end)
                return false;
            symbol = *_position++;
            switch (symbol)
            {
                case '"':
                case '\\':
                case '/': break;
                case 'b': symbol = '\b'; break;
                case 'f': symbol = '\f'; break;
                case 'n': symbol = '\n'; break;
                case 'r': symbol = '\r'; break;
                case 't': symbol = '\t'; break;
                case 'u':
                {
                    uint32_t code = 0;
                    if (!Hex4(code) || code == 0)
                        return false;
                    if (code >= 0xD800 && code <= 0xDBFF)
                    {
                        uint32_t low = 0;
                        if (_end - _position < 2 || _position[0] != '\\' || _position[1] != 'u')
                            return false;
                        _position += 2;
                        if (!Hex4(low) || low < 0xDC00 || low > 0xDFFF)
                            return false;
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    else if (code >= 0xDC00 && code <= 0xDFFF)
                        return false;
                    if (!PutUtf8(output, capacity, used, code))
                        return false;
                    continue;
                }
                default: return false;
            }
            if (!Put(output, capacity, used, symbol))
                return false;
        }
        return false;
    }
    bool Number(uint64_t& value)
    {
        SkipSpace();
        if (_position >= _end || *_position < '0' || *_position > '9')
            return false;
        if (*_position == '0' && _end - _position > 1 && _position[1] >= '0' && _position[1] <= '9')
            return false;
        value = 0;
        while (_position < _end && *_position >= '0' && *_position <= '9')
        {
            const uint64_t next = value * 10U + static_cast<unsigned>(*_position - '0');
            if (next < value)
                return false;
            value = next;
            ++_position;
        }
        return _position >= _end || (*_position != '.' && *_position != 'e' && *_position != 'E');
    }
    bool Literal(const char* word)
    {
        SkipSpace();
        const size_t length = std::strlen(word);
        if (static_cast<size_t>(_end - _position) < length || std::memcmp(_position, word, length) != 0)
            return false;
        _position += length;
        return true;
    }
    bool SkipValue(unsigned depth = 0)
    {
        if (depth > 32)
            return false;
        SkipSpace();
        if (_position >= _end)
            return false;
        switch (*_position)
        {
            case '"': return String(nullptr, 0);
            case '{':
                ++_position;
                if (Take('}'))
                    return true;
                do
                {
                    if (!String(nullptr, 0) || !Take(':') || !SkipValue(depth + 1))
                        return false;
                } while (Take(','));
                return Take('}');
            case '[':
                ++_position;
                if (Take(']'))
                    return true;
                do
                {
                    if (!SkipValue(depth + 1))
                        return false;
                } while (Take(','));
                return Take(']');
            case 't': return Literal("true");
            case 'f': return Literal("false");
            case 'n': return Literal("null");
            default: return SkipNumber();
        }
    }
    bool AtEnd()
    {
        SkipSpace();
        return _position >= _end;
    }

private:
    static bool Digit(char symbol) { return symbol >= '0' && symbol <= '9'; }
    void SkipDigits()
    {
        while (_position < _end && Digit(*_position))
            ++_position;
    }
    bool SkipNumber()
    {
        if (_position < _end && *_position == '-')
            ++_position;
        if (_position >= _end || !Digit(*_position))
            return false;
        if (*_position == '0')
            ++_position;
        else
            SkipDigits();
        if (_position < _end && *_position == '.')
        {
            ++_position;
            if (_position >= _end || !Digit(*_position))
                return false;
            SkipDigits();
        }
        if (_position < _end && (*_position == 'e' || *_position == 'E'))
        {
            ++_position;
            if (_position < _end && (*_position == '+' || *_position == '-'))
                ++_position;
            if (_position >= _end || !Digit(*_position))
                return false;
            SkipDigits();
        }
        return true;
    }
    bool Hex4(uint32_t& value)
    {
        if (_end - _position < 4)
            return false;
        value = 0;
        for (int index = 0; index < 4; ++index)
        {
            const int digit = HexValue(*_position++);
            if (digit < 0)
                return false;
            value = (value << 4) | static_cast<uint32_t>(digit);
        }
        return true;
    }
    static bool Put(char* output, size_t capacity, size_t& used, char symbol)
    {
        if (output == nullptr)
            return true;
        if (used + 1 >= capacity)
            return false;
        output[used++] = symbol;
        return true;
    }
    static bool PutUtf8(char* output, size_t capacity, size_t& used, uint32_t code)
    {
        if (code < 0x80)
            return Put(output, capacity, used, static_cast<char>(code));
        if (code < 0x800)
            return Put(output, capacity, used, static_cast<char>(0xC0 | code >> 6)) &&
                   Put(output, capacity, used, static_cast<char>(0x80 | (code & 0x3F)));
        if (code < 0x10000)
            return Put(output, capacity, used, static_cast<char>(0xE0 | code >> 12)) &&
                   Put(output, capacity, used, static_cast<char>(0x80 | (code >> 6 & 0x3F))) &&
                   Put(output, capacity, used, static_cast<char>(0x80 | (code & 0x3F)));
        return Put(output, capacity, used, static_cast<char>(0xF0 | code >> 18)) &&
               Put(output, capacity, used, static_cast<char>(0x80 | (code >> 12 & 0x3F))) &&
               Put(output, capacity, used, static_cast<char>(0x80 | (code >> 6 & 0x3F))) &&
               Put(output, capacity, used, static_cast<char>(0x80 | (code & 0x3F)));
    }

    const char* _position;
    const char* _end;
};

bool ReadKey(JsonCursor& json, char* key, size_t capacity)
{
    return json.String(key, capacity) && json.Take(':');
}

bool IsHex40(const char* text)
{
    if (std::strlen(text) != 40)
        return false;
    uint8_t digest[Sha1::kDigestSize];
    return ParseSha1Hex(text, digest);
}

bool PathIsSafe(const char* path)
{
    if (path[0] == 0 || path[0] == '/')
        return false;
    const char* part = path;
    for (;;)
    {
        const char* slash = std::strchr(part, '/');
        const size_t length = slash == nullptr ? std::strlen(part) : static_cast<size_t>(slash - part);
        if (length == 0 || (length == 1 && part[0] == '.') || (length == 2 && part[0] == '.' && part[1] == '.'))
            return false;
        for (size_t index = 0; index < length; ++index)
        {
            const unsigned char symbol = static_cast<unsigned char>(part[index]);
            if (symbol < 0x20 || symbol == '\\')
                return false;
        }
        if (slash == nullptr)
            return true;
        part = slash + 1;
    }
}
}  // namespace

// --- SHA-1 ---------------------------------------------------------------------------------------------------------

void Sha1::Reset()
{
    _state[0] = 0x67452301UL;
    _state[1] = 0xEFCDAB89UL;
    _state[2] = 0x98BADCFEUL;
    _state[3] = 0x10325476UL;
    _state[4] = 0xC3D2E1F0UL;
    _length = 0;
    _used = 0;
}

void Sha1::Block(const uint8_t* data)
{
    uint32_t words[80];
    for (unsigned index = 0; index < 16; ++index)
        words[index] = static_cast<uint32_t>(data[4 * index]) << 24 | static_cast<uint32_t>(data[4 * index + 1]) << 16 |
                       static_cast<uint32_t>(data[4 * index + 2]) << 8 | static_cast<uint32_t>(data[4 * index + 3]);
    for (unsigned index = 16; index < 80; ++index)
        words[index] = RotateLeft(words[index - 3] ^ words[index - 8] ^ words[index - 14] ^ words[index - 16], 1);
    uint32_t a = _state[0], b = _state[1], c = _state[2], d = _state[3], e = _state[4];
    for (unsigned index = 0; index < 80; ++index)
    {
        uint32_t f, k;
        if (index < 20)
        {
            f = (b & c) | (~b & d);
            k = 0x5A827999UL;
        }
        else if (index < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1UL;
        }
        else if (index < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCUL;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6UL;
        }
        const uint32_t next = RotateLeft(a, 5) + f + e + k + words[index];
        e = d;
        d = c;
        c = RotateLeft(b, 30);
        b = a;
        a = next;
    }
    _state[0] += a;
    _state[1] += b;
    _state[2] += c;
    _state[3] += d;
    _state[4] += e;
}

void Sha1::Update(const uint8_t* data, size_t length)
{
    if (data == nullptr)
        return;
    _length += length;
    while (length != 0)
    {
        const size_t part = length < 64 - _used ? length : 64 - _used;
        std::memcpy(_buffer + _used, data, part);
        _used += part;
        data += part;
        length -= part;
        if (_used == 64)
        {
            Block(_buffer);
            _used = 0;
        }
    }
}

void Sha1::Finish(uint8_t digest[kDigestSize])
{
    const uint64_t bits = _length * 8U;
    const uint8_t marker = 0x80;
    Update(&marker, 1);
    const uint8_t zero = 0;
    while (_used != 56)
        Update(&zero, 1);
    uint8_t tail[8];
    for (unsigned index = 0; index < 8; ++index)
        tail[index] = static_cast<uint8_t>(bits >> (56 - 8 * index));
    Update(tail, sizeof(tail));
    for (unsigned index = 0; index < 5; ++index)
    {
        digest[4 * index] = static_cast<uint8_t>(_state[index] >> 24);
        digest[4 * index + 1] = static_cast<uint8_t>(_state[index] >> 16);
        digest[4 * index + 2] = static_cast<uint8_t>(_state[index] >> 8);
        digest[4 * index + 3] = static_cast<uint8_t>(_state[index]);
    }
    Reset();
}

void Sha1::Save(ZiFiStateWriter& w) const
{
    for (uint32_t s : _state)
        w.U32(s);
    w.U64(_length);
    w.Bytes(_buffer, sizeof(_buffer));
    w.U8(static_cast<uint8_t>(_used));
}

void Sha1::Load(ZiFiStateReader& r)
{
    for (uint32_t& s : _state)
        s = r.U32();
    _length = r.U64();
    const std::vector<uint8_t> buffer = r.Bytes(64);
    std::memset(_buffer, 0, sizeof(_buffer));
    std::memcpy(_buffer, buffer.data(), std::min(buffer.size(), sizeof(_buffer)));
    _used = std::min<size_t>(r.U8(), 63);
}

void GitBlobBegin(Sha1& sha, uint32_t size)
{
    char header[24];
    const int length = std::snprintf(header, sizeof(header), "blob %lu", static_cast<unsigned long>(size));
    sha.Reset();
    sha.Update(reinterpret_cast<const uint8_t*>(header), static_cast<size_t>(length) + 1);
}

bool ParseSha1Hex(const char* text, uint8_t digest[Sha1::kDigestSize])
{
    if (text == nullptr)
        return false;
    for (size_t index = 0; index < Sha1::kDigestSize; ++index)
    {
        const int high = HexValue(text[2 * index]);
        const int low = high < 0 ? -1 : HexValue(text[2 * index + 1]);
        if (low < 0)
            return false;
        digest[index] = static_cast<uint8_t>(high << 4 | low);
    }
    return true;
}

std::string FormatSha1Hex(const uint8_t digest[Sha1::kDigestSize])
{
    static const char kDigits[] = "0123456789abcdef";
    std::string text(40, '0');
    for (size_t index = 0; index < Sha1::kDigestSize; ++index)
    {
        text[2 * index] = kDigits[digest[index] >> 4];
        text[2 * index + 1] = kDigits[digest[index] & 0x0F];
    }
    return text;
}

// --- The GitHub answers --------------------------------------------------------------------------------------------

bool ParseRefCommit(const char* json, size_t length, char commit[41])
{
    if (json == nullptr || commit == nullptr)
        return false;
    JsonCursor cursor(json, length);
    if (!cursor.Take('{'))
        return false;
    bool found = false;
    char key[16];
    if (!cursor.Peek('}'))
    {
        do
        {
            if (!ReadKey(cursor, key, sizeof(key)))
                return false;
            if (std::strcmp(key, "object") != 0)
            {
                if (!cursor.SkipValue())
                    return false;
                continue;
            }
            if (!cursor.Take('{'))
                return false;
            char type[16] = {};
            char sha[48] = {};
            if (!cursor.Peek('}'))
            {
                do
                {
                    if (!ReadKey(cursor, key, sizeof(key)))
                        return false;
                    if (std::strcmp(key, "sha") == 0)
                    {
                        if (!cursor.String(sha, sizeof(sha)))
                            return false;
                    }
                    else if (std::strcmp(key, "type") == 0)
                    {
                        if (!cursor.String(type, sizeof(type)))
                            return false;
                    }
                    else if (!cursor.SkipValue())
                        return false;
                } while (cursor.Take(','));
            }
            if (!cursor.Take('}') || std::strcmp(type, "commit") != 0 || !IsHex40(sha))
                return false;
            std::memcpy(commit, sha, 41);
            found = true;
        } while (cursor.Take(','));
    }
    return cursor.Take('}') && cursor.AtEnd() && found;
}

bool ParseTree(const char* json, size_t length, TreeEntry* entries, size_t capacity, size_t& count, bool& truncated)
{
    count = 0;
    truncated = true;
    if (json == nullptr || entries == nullptr)
        return false;
    JsonCursor cursor(json, length);
    if (!cursor.Take('{'))
        return false;
    bool haveTree = false;
    bool haveTruncated = false;
    char key[16];
    if (!cursor.Peek('}'))
    {
        do
        {
            if (!ReadKey(cursor, key, sizeof(key)))
                return false;
            if (std::strcmp(key, "truncated") == 0)
            {
                if (cursor.Literal("true"))
                    truncated = true;
                else if (cursor.Literal("false"))
                    truncated = false;
                else
                    return false;
                haveTruncated = true;
                continue;
            }
            if (std::strcmp(key, "tree") != 0)
            {
                if (!cursor.SkipValue())
                    return false;
                continue;
            }
            if (!cursor.Take('['))
                return false;
            haveTree = true;
            if (cursor.Take(']'))
                continue;
            do
            {
                if (!cursor.Take('{'))
                    return false;
                TreeEntry entry;
                char type[16] = {};
                char sha[48] = {};
                bool haveSize = false;
                bool havePath = false;
                if (!cursor.Peek('}'))
                {
                    do
                    {
                        if (!ReadKey(cursor, key, sizeof(key)))
                            return false;
                        if (std::strcmp(key, "path") == 0)
                        {
                            if (!cursor.String(entry.path, sizeof(entry.path)))
                                return false;
                            havePath = true;
                        }
                        else if (std::strcmp(key, "type") == 0)
                        {
                            if (!cursor.String(type, sizeof(type)))
                                return false;
                        }
                        else if (std::strcmp(key, "sha") == 0)
                        {
                            if (!cursor.String(sha, sizeof(sha)))
                                return false;
                        }
                        else if (std::strcmp(key, "size") == 0)
                        {
                            uint64_t size = 0;
                            if (!cursor.Number(size) || size > 0xFFFFFFFFULL)
                                return false;
                            entry.size = static_cast<uint32_t>(size);
                            haveSize = true;
                        }
                        else if (!cursor.SkipValue())
                            return false;
                    } while (cursor.Take(','));
                }
                if (!cursor.Take('}'))
                    return false;
                if (std::strcmp(type, "commit") == 0)
                    continue;   // a submodule: no content in this tree
                const bool blob = std::strcmp(type, "blob") == 0;
                if ((!blob && std::strcmp(type, "tree") != 0) || !havePath || !PathIsSafe(entry.path) || !IsHex40(sha) ||
                    (blob && !haveSize))
                    return false;
                ParseSha1Hex(sha, entry.sha);
                entry.directory = !blob;
                if (!blob)
                    entry.size = 0;
                if (count >= capacity)
                    return false;
                entries[count++] = entry;
            } while (cursor.Take(','));
            if (!cursor.Take(']'))
                return false;
        } while (cursor.Take(','));
    }
    return cursor.Take('}') && cursor.AtEnd() && haveTree && haveTruncated;
}

}  // namespace zifigit
