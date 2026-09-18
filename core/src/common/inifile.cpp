#include "stdafx.h"

#include "common/inifile.h"

#include "common/filehelper.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string_view>

/// region <Helpers>

namespace
{
    constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

    bool IsSpaceChar(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
    }

    char FoldAscii(char c)
    {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    bool NamesEqualFold(std::string_view left, std::string_view right)
    {
        if (left.size() != right.size())
        {
            return false;
        }
        for (size_t i = 0; i < left.size(); i++)
        {
            if (FoldAscii(left[i]) != FoldAscii(right[i]))
            {
                return false;
            }
        }
        return true;
    }

    std::string_view Trimmed(std::string_view text)
    {
        size_t begin = 0;
        size_t end = text.size();
        while (begin < end && IsSpaceChar(text[begin]))
        {
            begin++;
        }
        while (end > begin && IsSpaceChar(text[end - 1]))
        {
            end--;
        }
        return text.substr(begin, end - begin);
    }

    // Backward scan from end of line; the first ';' / '#' / '//' marker found
    // from the end truncates the value. Reproduces the local SimpleIni patch the
    // shipped parser carried (see core/tests - the old simpleini_test.cpp):
    // heritage configs write "intlen=128 ; t-states" and expect 128, and a
    // value containing '#'
    // ("ffmpeg.vout=video#.avi" -> "video") is truncated as well.
    std::string_view StripInlineComment(std::string_view value)
    {
        for (size_t i = value.size(); i > 0; i--)
        {
            const char c = value[i - 1];
            if (c == ';' || c == '#')
            {
                return value.substr(0, i - 1);
            }
            if (c == '/' && i >= 2 && value[i - 2] == '/')
            {
                return value.substr(0, i - 2);
            }
        }
        return value;
    }
}

/// endregion </Helpers>

/// region <Load / Save>

bool IniFile::LoadFile(const std::string& path)
{
    std::ifstream stream(FileHelper::ToFsPath(path), std::ios::binary);
    if (!stream.is_open())
    {
        return false;
    }

    std::string data((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    LoadData(data);
    return true;
}

bool IniFile::SaveFile(const std::string& path) const
{
    std::ofstream stream(FileHelper::ToFsPath(path), std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        return false;
    }

    const std::string data = SaveData();
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    return stream.good();
}

void IniFile::LoadData(const std::string& data)
{
    Clear();

    std::string_view input = data;
    if (input.size() >= kUtf8Bom.size() && input.substr(0, kUtf8Bom.size()) == kUtf8Bom)
    {
        input.remove_prefix(kUtf8Bom.size());
    }

    std::string currentSection;
    size_t position = 0;
    while (position < input.size())
    {
        const size_t lineEnd = input.find('\n', position);
        const std::string_view lineView = (lineEnd == std::string_view::npos)
            ? input.substr(position)
            : input.substr(position, lineEnd - position);

        // Politely decline a pathological single line: skip it without ever
        // allocating a copy of it (the std::string construction below is
        // what would otherwise scale with line length) or touching
        // currentSection - parsing continues with the next line. Bounds
        // worst-case per-line memory/time regardless of how large a hostile
        // or corrupted file's single "line" is.
        if (lineView.size() <= kMaxLineLength)
        {
            ParseLine(std::string(Trimmed(lineView)), currentSection);
        }

        if (lineEnd == std::string_view::npos)
        {
            break;
        }
        position = lineEnd + 1;
    }
}

std::string IniFile::SaveData() const
{
    std::string result;
    for (size_t i = 0; i < _sections.size(); i++)
    {
        if (i != 0)
        {
            result += '\n';
        }
        result += '[';
        result += _sections[i].name;
        result += "]\n";
        for (const Entry& entry : _sections[i].entries)
        {
            result += entry.key;
            result += " = ";
            result += entry.value;
            result += '\n';
        }
    }
    return result;
}

/// endregion </Load / Save>

/// region <Lookup>

const char* IniFile::GetValue(const char* section, const char* key, const char* defaultValue) const
{
    if (section == nullptr || key == nullptr)
    {
        return defaultValue;
    }

    const Section* target = FindSection(section);
    if (target == nullptr)
    {
        return defaultValue;
    }

    for (const Entry& entry : target->entries)
    {
        if (NamesEqualFold(entry.key, key))
        {
            return entry.value.c_str();
        }
    }
    return defaultValue;
}

long IniFile::GetLongValue(const char* section, const char* key, long defaultValue) const
{
    const char* raw = GetValue(section, key, nullptr);
    if (raw == nullptr || raw[0] == '\0')
    {
        return defaultValue;
    }

    long result = defaultValue;
    char* suffix = nullptr;
    if (raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X'))
    {
        if (raw[2] == '\0')
        {
            return defaultValue;
        }
        result = std::strtol(raw + 2, &suffix, 16);
    }
    else
    {
        result = std::strtol(raw, &suffix, 10);
    }

    // Any trailing characters (including kept inline comments) mean "not a number"
    if (*suffix != '\0')
    {
        return defaultValue;
    }
    return result;
}

double IniFile::GetDoubleValue(const char* section, const char* key, double defaultValue) const
{
    const char* raw = GetValue(section, key, nullptr);
    if (raw == nullptr || raw[0] == '\0')
    {
        return defaultValue;
    }

    char* suffix = nullptr;
    const double result = std::strtod(raw, &suffix);

    // Any trailing characters mean "not a number"
    if (*suffix != '\0')
    {
        return defaultValue;
    }
    return result;
}

std::vector<std::string> IniFile::GetAllSections() const
{
    std::vector<std::string> result;
    result.reserve(_sections.size());
    for (const Section& section : _sections)
    {
        result.push_back(section.name);
    }
    return result;
}

/// endregion </Lookup>

/// region <Mutation>

void IniFile::SetValue(const char* section, const char* key, const char* value)
{
    if (section == nullptr || key == nullptr || value == nullptr)
    {
        return;
    }
    SetEntry(section, key, value);
}

bool IniFile::IsEmpty() const
{
    return _sections.empty();
}

void IniFile::Clear()
{
    _sections.clear();
}

/// endregion </Mutation>

/// region <Internals>

void IniFile::ParseLine(const std::string& line, std::string& currentSection)
{
    if (line.empty() || line[0] == ';' || line[0] == '#')
    {
        return;
    }

    if (line[0] == '[')
    {
        const std::string_view rest = std::string_view(line).substr(1);
        const size_t close = rest.find(']');
        if (close == std::string_view::npos)
        {
            return; // invalid header: skip the line, keep the current section
        }

        currentSection = std::string(Trimmed(rest.substr(0, close)));
        return;
    }

    const size_t assignment = line.find('=');
    if (assignment == std::string::npos)
    {
        return;
    }

    const std::string_view keyView = Trimmed(std::string_view(line).substr(0, assignment));
    if (keyView.empty())
    {
        return;
    }

    const std::string_view valueView = Trimmed(StripInlineComment(std::string_view(line).substr(assignment + 1)));
    SetEntry(currentSection, std::string(keyView), std::string(valueView));
}

IniFile::Section* IniFile::FindSection(const char* name)
{
    for (Section& section : _sections)
    {
        if (NamesEqualFold(section.name, name))
        {
            return &section;
        }
    }
    return nullptr;
}

const IniFile::Section* IniFile::FindSection(const char* name) const
{
    for (const Section& section : _sections)
    {
        if (NamesEqualFold(section.name, name))
        {
            return &section;
        }
    }
    return nullptr;
}

IniFile::Section& IniFile::GetOrCreateSection(const char* name)
{
    if (Section* found = FindSection(name))
    {
        return *found;
    }
    _sections.push_back(Section{std::string(name), {}});
    return _sections.back();
}

void IniFile::SetEntry(const std::string& section, const std::string& key, const std::string& value)
{
    Section& target = GetOrCreateSection(section.c_str());
    for (Entry& entry : target.entries)
    {
        if (NamesEqualFold(entry.key, key))
        {
            entry.value = value; // last write wins, first key spelling kept
            return;
        }
    }
    target.entries.push_back(Entry{key, value});
}

/// endregion </Internals>
