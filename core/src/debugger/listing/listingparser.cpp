#include "stdafx.h"

#include "listingparser.h"
#include "emulator/emulatorcontext.h"

#include <algorithm>
#include <cctype>
#include <fstream>

/// region <Constructors / Destructors>

ListingParser::ListingParser(EmulatorContext* context)
{
    _context = context;

    if (_context)
    {
        _logger = _context->pModuleLogger;
    }
}

ListingParser::~ListingParser()
{
    Clear();

    _context = nullptr;
    _logger = nullptr;
}

/// endregion </Constructors / Destructors>

/// region <File operations>

bool ListingParser::LoadListing(const std::string& path)
{
    Clear();

    std::ifstream file(path);
    if (!file.is_open())
    {
        if (_logger)
            _logger->Error(_MODULE, _SUBMODULE, "ListingParser::LoadListing - cannot open file: %s", path.c_str());
        return false;
    }

    _sourcePath = path;

    if (!ParseStream(file) || _lines.empty())
    {
        if (_logger)
            _logger->Error(_MODULE, _SUBMODULE, "ListingParser::LoadListing - no listing rows parsed from: %s", path.c_str());
        Clear();
        return false;
    }

    RecountStatistics();
    _loaded = true;

    if (_logger)
        _logger->Info(_MODULE, _SUBMODULE, "ListingParser::LoadListing - %zu lines (%zu code lines, %zu bytes, range 0x%04X-0x%04X) from: %s",
                      _lines.size(), _codeLineCount, _totalBytes, _minAddress, _maxAddress, path.c_str());

    return true;
}

void ListingParser::Clear()
{
    _sourcePath.clear();
    _lines.clear();
    _indexByLineNumber.clear();
    _addressToLine.clear();
    _loaded = false;
    _minAddress = 0xFFFF;
    _maxAddress = 0;
    _codeLineCount = 0;
    _totalBytes = 0;
}

/// endregion </File operations>

/// region <Line lookup>

const ListingLine* ListingParser::FindLineByAddress(uint16_t address) const
{
    if (!_loaded || _addressToLine.empty())
        return nullptr;

    int32_t index = _addressToLine[address];
    if (index < 0 || static_cast<size_t>(index) >= _lines.size())
        return nullptr;

    return &_lines[static_cast<size_t>(index)];
}

const ListingLine* ListingParser::FindLineByNumber(int lineNumber) const
{
    auto it = _indexByLineNumber.find(lineNumber);
    if (it == _indexByLineNumber.end() || it->second >= _lines.size())
        return nullptr;

    return &_lines[it->second];
}

const ListingLine* ListingParser::FindNextCodeLine(int fromLineNumber) const
{
    for (auto it = _indexByLineNumber.lower_bound(fromLineNumber); it != _indexByLineNumber.end(); ++it)
    {
        if (it->second < _lines.size() && _lines[it->second].hasCode)
            return &_lines[it->second];
    }

    return nullptr;
}

const ListingLine* ListingParser::FindPrevCodeLine(int fromLineNumber) const
{
    auto it = _indexByLineNumber.upper_bound(fromLineNumber);
    while (it != _indexByLineNumber.begin())
    {
        --it;
        if (it->second < _lines.size() && _lines[it->second].hasCode)
            return &_lines[it->second];
    }

    return nullptr;
}

/// endregion </Line lookup>

/// region <Parsing>

bool ListingParser::ParseStream(std::istream& input)
{
    // Byte-level address map: 64 KiB, -1 = not covered by any code line
    _addressToLine.assign(65536, -1);

    std::string raw;
    while (std::getline(input, raw))
    {
        // Token 1: optional decimal source line number
        std::string token;
        size_t pos = 0;
        size_t t1Start = NextToken(raw, pos, token);
        if (token.empty())
            continue;

        int lineNumber = -1;
        size_t addrTokenStart = t1Start;
        std::string addrToken = token;

        if (IsAllDigits(token))
        {
            lineNumber = std::stoi(token);
            pos = t1Start + token.size();

            // Token 2: optional "XXXX:" address
            addrTokenStart = NextToken(raw, pos, addrToken);
            pos = addrTokenStart + addrToken.size();
        }
        else
        {
            // No line number — row starts at the address token (if any)
            pos = t1Start + token.size();
        }

        // Optional address: 1-8 hex digits followed by ':'
        bool hasAddress = false;
        uint32_t address = 0;
        if (!addrToken.empty() && addrToken.back() == ':')
        {
            std::string prefix = addrToken.substr(0, addrToken.size() - 1);
            if (!prefix.empty() && prefix.size() <= 8 && IsHexString(prefix))
            {
                address = ParseHex32(prefix);
                hasAddress = true;
            }
            else
            {
                // Looked like an address but is not — rewind so it stays part of the source text
                pos = addrTokenStart;
            }
        }
        else if (!addrToken.empty())
        {
            // Not an address token — rewind so it stays part of the source text
            pos = addrTokenStart;
        }

        // Byte tokens: exactly 2 hex digits each
        std::vector<uint8_t> rowBytes;
        while (true)
        {
            size_t bStart = NextToken(raw, pos, token);
            if (token.empty())
            {
                pos = raw.size();
                break;
            }
            if (!IsHexPair(token))
            {
                pos = bStart;
                break;
            }
            rowBytes.push_back(static_cast<uint8_t>(ParseHex32(token)));
            pos = bStart + token.size();
        }

        std::string source = TrimRight(raw.substr(pos < raw.size() ? pos : raw.size()));

        // Resolve the target ListingLine for this row
        size_t lineIndex = _lines.size();
        if (lineNumber >= 0)
        {
            auto it = _indexByLineNumber.find(lineNumber);
            if (it != _indexByLineNumber.end())
            {
                lineIndex = it->second;      // Continuation row — merge
            }
            else
            {
                ListingLine line;
                line.lineNumber = lineNumber;
                _lines.push_back(line);
                lineIndex = _lines.size() - 1;
                _indexByLineNumber[lineNumber] = lineIndex;
            }
        }
        else if (hasAddress && !_lines.empty())
        {
            lineIndex = _lines.size() - 1;   // Anonymous continuation row — append to previous line
        }
        else
        {
            continue;                        // Nothing useful to record
        }

        ListingLine& line = _lines[lineIndex];

        if (line.source.empty() && !source.empty())
            line.source = source;

        if (hasAddress)
        {
            if (!rowBytes.empty())
            {
                if (!line.hasCode)
                {
                    line.addressStart = static_cast<uint16_t>(address & 0xFFFF);
                    line.hasCode = true;
                }
                else if ((address & 0xFFFF) < line.addressStart)
                {
                    line.addressStart = static_cast<uint16_t>(address & 0xFFFF);
                }

                line.bytes.insert(line.bytes.end(), rowBytes.begin(), rowBytes.end());

                uint32_t last = address + static_cast<uint32_t>(rowBytes.size()) - 1;
                line.addressEnd = static_cast<uint16_t>(last & 0xFFFF);

                MapAddressRange(lineIndex, address, rowBytes.size());

                uint16_t start16 = static_cast<uint16_t>(address & 0xFFFF);
                if (start16 < _minAddress) _minAddress = start16;
                if ((last & 0xFFFF) > _maxAddress && last <= 0xFFFF) _maxAddress = static_cast<uint16_t>(last);
            }
            else if (!line.hasCode)
            {
                // Address without bytes (label-only row) — anchor only, no byte mapping
                line.addressStart = static_cast<uint16_t>(address & 0xFFFF);
                line.addressEnd = static_cast<uint16_t>(address & 0xFFFF);
            }
        }
    }

    return true;
}

void ListingParser::MapAddressRange(size_t lineIndex, uint32_t start, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        uint32_t address = start + static_cast<uint32_t>(i);
        if (address > 0xFFFF)
            break;

        _addressToLine[address] = static_cast<int32_t>(lineIndex);
    }
}

void ListingParser::RecountStatistics()
{
    _codeLineCount = 0;
    _totalBytes = 0;

    for (const auto& line : _lines)
    {
        if (line.hasCode)
        {
            _codeLineCount++;
            _totalBytes += line.bytes.size();
        }
    }
}

/// endregion </Parsing>

/// region <Token helpers>

size_t ListingParser::NextToken(const std::string& line, size_t pos, std::string& token)
{
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
        pos++;

    size_t start = pos;
    while (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos])))
        pos++;

    token = line.substr(start, pos - start);

    // Return the token start so callers can reconstruct the source tail
    return start;
}

std::string ListingParser::TrimRight(const std::string& str)
{
    size_t end = str.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(str[end - 1])))
        end--;

    return str.substr(0, end);
}

bool ListingParser::IsAllDigits(const std::string& str)
{
    if (str.empty())
        return false;

    return std::all_of(str.begin(), str.end(), [](unsigned char c) { return std::isdigit(c); });
}

bool ListingParser::IsHexDigit(char c)
{
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isdigit(uc) || (uc >= 'a' && uc <= 'f') || (uc >= 'A' && uc <= 'F');
}

bool ListingParser::IsHexPair(const std::string& str)
{
    return str.size() == 2 && IsHexDigit(str[0]) && IsHexDigit(str[1]);
}

bool ListingParser::IsHexString(const std::string& str)
{
    if (str.empty())
        return false;

    return std::all_of(str.begin(), str.end(), [](char c) { return IsHexDigit(c); });
}

uint32_t ListingParser::ParseHex32(const std::string& str)
{
    uint32_t value = 0;

    for (char c : str)
    {
        value <<= 4;

        unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= '0' && uc <= '9')
            value |= static_cast<uint32_t>(uc - '0');
        else if (uc >= 'a' && uc <= 'f')
            value |= static_cast<uint32_t>(uc - 'a' + 10);
        else if (uc >= 'A' && uc <= 'F')
            value |= static_cast<uint32_t>(uc - 'A' + 10);
    }

    return value;
}

/// endregion </Token helpers>
