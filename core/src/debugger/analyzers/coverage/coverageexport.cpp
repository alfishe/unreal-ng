#include "debugger/analyzers/coverage/coverageexport.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <vector>

#include "common/filehelper.h"
#include "debugger/analyzers/coverage/coverageanalyzer.h"

namespace coverageexport
{
namespace
{

constexpr char kSkoolkitMap[] = "skoolkit-map";

/// Decimal, or hex with 0x / $ / #; false past 0xFFFF or on anything else
bool ParseAddress(const std::string& text, uint16_t& out)
{
    std::string s = text;
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        s = s.substr(2);
        base = 16;
    }
    else if (!s.empty() && (s[0] == '$' || s[0] == '#'))
    {
        s = s.substr(1);
        base = 16;
    }
    if (s.empty() || s.size() > 5)
        return false;
    uint32_t value = 0;
    for (const char c : s)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        uint32_t digit = 0;
        if (std::isdigit(u))
            digit = static_cast<uint32_t>(u - '0');
        else if (base == 16 && std::isxdigit(u))
            digit = static_cast<uint32_t>(std::toupper(u) - 'A' + 10);
        else
            return false;
        value = value * static_cast<uint32_t>(base) + digit;
    }
    if (value > 0xFFFF)
        return false;
    out = static_cast<uint16_t>(value);
    return true;
}

bool ParseBool(const std::string& text, bool& out)
{
    if (text == "true" || text == "1" || text == "yes" || text == "on")
        out = true;
    else if (text == "false" || text == "0" || text == "no" || text == "off" || text.empty())
        out = false;
    else
        return false;
    return true;
}

Result Fail(Error error, std::string message)
{
    Result r;
    r.error = error;
    r.message = std::move(message);
    return r;
}

/// The addresses of a map file already there (any of the text forms sna2ctl reads by their "$HHHH" / "0xHHHH" start;
/// other lines are skipped). False when it cannot be read
bool ReadMap(const std::string& path, std::vector<bool>& executed, size_t& count)
{
    std::ifstream in(FileHelper::ToFsPath(path));
    if (!in.is_open())
        return false;
    std::string line;
    while (std::getline(in, line))
    {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        size_t end = start;
        while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end])) && line[end] != ',')
            ++end;
        uint16_t address = 0;
        const std::string word = line.substr(start, end - start);
        if ((word[0] == '$' || word.rfind("0x", 0) == 0) && ParseAddress(word, address) && !executed[address])
        {
            executed[address] = true;
            ++count;
        }
    }
    return true;
}

std::string Hex4(uint32_t value)
{
    static const char kDigits[] = "0123456789ABCDEF";
    std::string out = "$0000";
    for (int i = 4; i >= 1; --i, value >>= 4)
        out[static_cast<size_t>(i)] = kDigits[value & 0xF];
    return out;
}

}  // namespace

const std::vector<std::string>& Formats()
{
    static const std::vector<std::string> formats = {kSkoolkitMap};
    return formats;
}

const std::vector<std::string>& OptionNames()
{
    static const std::vector<std::string> names = {"format", "start", "end", "path", "merge"};
    return names;
}

std::string SkoolkitMap(const CoverageAnalyzer& coverage, uint16_t start, uint16_t end, size_t& count)
{
    std::string out;
    count = 0;
    for (uint32_t address = start; address <= end; ++address)
        if (coverage.isExecuted(static_cast<uint16_t>(address)))
        {
            out += Hex4(address);
            out += '\n';
            ++count;
        }
    return out;
}

Result Export(const CoverageAnalyzer* coverage, const Options& options)
{
    for (const auto& [name, value] : options)
        if (std::find(OptionNames().begin(), OptionNames().end(), name) == OptionNames().end())
            return Fail(Error::BadRequest, "Unknown option: " + name + " (options: format, start, end, path, merge)");
    if (coverage == nullptr)
        return Fail(Error::NotAvailable, "Coverage analyzer not available");

    Result r;
    const auto option = [&](const char* name) {
        const auto it = options.find(name);
        return it == options.end() ? std::string() : it->second;
    };
    r.format = option("format").empty() ? std::string(kSkoolkitMap) : option("format");
    if (std::find(Formats().begin(), Formats().end(), r.format) == Formats().end())
        return Fail(Error::BadRequest, "Unknown coverage format: " + r.format + " (formats: skoolkit-map)");
    if (!option("start").empty() && !ParseAddress(option("start"), r.start))
        return Fail(Error::BadRequest, "start is no address 0..0xFFFF: " + option("start"));
    if (!option("end").empty() && !ParseAddress(option("end"), r.end))
        return Fail(Error::BadRequest, "end is no address 0..0xFFFF: " + option("end"));
    if (r.start > r.end)
        return Fail(Error::BadRequest, "start is past end");
    bool merge = false;
    if (!ParseBool(option("merge"), merge))
        return Fail(Error::BadRequest, "merge is true or false: " + option("merge"));
    r.path = option("path");
    if (merge && r.path.empty())
        return Fail(Error::BadRequest, "merge needs a path (the map file to add to)");

    if (!merge)
    {
        r.text = SkoolkitMap(*coverage, r.start, r.end, r.count);
    }
    else
    {
        // The union of the file's addresses and the recorded ones in the window (a missing file is an empty map)
        std::vector<bool> executed(65536, false);
        if (std::ifstream(FileHelper::ToFsPath(r.path)).is_open() && !ReadMap(r.path, executed, r.merged))
            return Fail(Error::BadRequest, "Cannot read " + r.path);
        for (uint32_t address = r.start; address <= r.end; ++address)
            if (coverage->isExecuted(static_cast<uint16_t>(address)))
                executed[address] = true;
        for (uint32_t address = 0; address <= 0xFFFF; ++address)
            if (executed[address])
            {
                r.text += Hex4(address);
                r.text += '\n';
                ++r.count;
            }
    }
    if (r.path.empty())
    {
        r.message = "Coverage map: " + std::to_string(r.count) + " instruction start(s)";
        return r;
    }
    std::ofstream out(FileHelper::ToFsPath(r.path), std::ios::binary | std::ios::trunc);
    out.write(r.text.data(), static_cast<std::streamsize>(r.text.size()));
    if (!out.good())
        return Fail(Error::BadRequest, "Cannot write " + r.path);
    r.text.clear();
    r.message = "Coverage map written: " + r.path + " (" + std::to_string(r.count) + " instruction start(s)" +
                (merge ? ", " + std::to_string(r.merged) + " from the file" : std::string()) + ")";
    return r;
}

}  // namespace coverageexport
