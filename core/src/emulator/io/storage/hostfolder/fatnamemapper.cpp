#include "stdafx.h"

#include "fatnamemapper.h"

#include <algorithm>
#include <set>


namespace
{
    constexpr size_t kMaxLongName = 255;  ///< UTF-16 units

    bool IsAllowedAscii(char32_t c)
    {
        if ((c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9'))
            return true;
        switch (c)
        {
            case U'$': case U'%': case U'\'': case U'-': case U'_': case U'@': case U'~': case U'`':
            case U'!': case U'(': case U')': case U'{': case U'}': case U'^': case U'#': case U'&':
                return true;
            default:
                return false;
        }
    }

    /// One part (base or extension) -> CP866 bytes; `lossy` / `caseChanged` accumulate
    std::vector<uint8_t> MapPart(const std::u32string& part, CodePage page, bool& lossy, bool& caseChanged)
    {
        std::vector<uint8_t> bytes;
        for (char32_t c : part)
        {
            if (c == U' ' || c == U'.')
            {
                lossy = true;
                continue;
            }
            const char32_t upper = UnicodeHelper::ToUpper(c);
            if (upper != c)
                caseChanged = true;

            uint8_t byte = '_';
            if (upper < 0x80)
            {
                if (IsAllowedAscii(upper))
                    byte = static_cast<uint8_t>(upper);
                else
                    lossy = true;
            }
            else if (!UnicodeHelper::ToCodePage(page, upper, byte))
            {
                byte = '_';
                lossy = true;
            }
            bytes.push_back(byte);
        }
        return bytes;
    }

    FatShortName Compose(const std::vector<uint8_t>& base, const std::vector<uint8_t>& ext)
    {
        FatShortName name;
        name.fill(' ');
        std::copy_n(base.begin(), std::min<size_t>(base.size(), 8), name.begin());
        std::copy_n(ext.begin(), std::min<size_t>(ext.size(), 3), name.begin() + 8);
        if (name[0] == 0xE5)
            name[0] = 0x05;  // #E5 marks a deleted entry; CP866 'х' is stored as #05
        return name;
    }
}  // namespace

std::vector<FatMappedName> FatNameMapper::MapFolder(const std::vector<std::string>& utf8Names, CodePage page)
{
    std::vector<FatMappedName> result(utf8Names.size());
    std::set<FatShortName> used;

    // Two passes: exact short names first, so a lossless name keeps its short
    // name even when a lossy sibling earlier in the list would map onto it
    std::vector<std::vector<uint8_t>> bases(utf8Names.size());
    std::vector<std::vector<uint8_t>> exts(utf8Names.size());
    std::vector<bool> needsTail(utf8Names.size(), false);

    for (size_t i = 0; i < utf8Names.size(); i++)
    {
        FatMappedName& out = result[i];
        const std::u32string name = UnicodeHelper::DecodeUtf8(utf8Names[i]);

        out.longName = UnicodeHelper::ToUtf16(name);
        if (name.empty())
        {
            out.reason = "empty name";
            continue;
        }
        if (out.longName.size() > kMaxLongName)
        {
            out.reason = "name longer than 255 UTF-16 units";
            continue;
        }

        // Leading dots and spaces never start a short name
        size_t start = 0;
        while (start < name.size() && (name[start] == U'.' || name[start] == U' '))
            start++;
        bool lossy = start > 0;
        bool caseChanged = false;

        const std::u32string rest = name.substr(start);
        const size_t dot = rest.rfind(U'.');
        const std::u32string basePart = dot == std::u32string::npos ? rest : rest.substr(0, dot);
        const std::u32string extPart = dot == std::u32string::npos ? std::u32string() : rest.substr(dot + 1);

        std::vector<uint8_t> base = MapPart(basePart, page, lossy, caseChanged);
        std::vector<uint8_t> ext = MapPart(extPart, page, lossy, caseChanged);
        if (base.size() > 8 || ext.size() > 3)
            lossy = true;
        if (base.empty())
        {
            base.push_back('_');
            lossy = true;
        }

        bases[i] = base;
        exts[i] = ext;
        needsTail[i] = lossy;
        out.ok = true;
        out.hasLongName = lossy || caseChanged;

        if (!lossy)
        {
            const FatShortName exact = Compose(base, ext);
            if (used.insert(exact).second)
                out.shortName = exact;
            else
                needsTail[i] = true;  // a case-only twin of a sibling
        }
    }

    for (size_t i = 0; i < utf8Names.size(); i++)
    {
        FatMappedName& out = result[i];
        if (!out.ok || !needsTail[i])
            continue;
        out.hasLongName = true;
        for (uint32_t n = 1; n <= 999999; n++)
        {
            const std::string tail = "~" + std::to_string(n);
            std::vector<uint8_t> base = bases[i];
            base.resize(std::min<size_t>(base.size(), 8 - tail.size()));
            base.insert(base.end(), tail.begin(), tail.end());
            const FatShortName candidate = Compose(base, exts[i]);
            if (used.insert(candidate).second)
            {
                out.shortName = candidate;
                break;
            }
        }
    }
    return result;
}

uint8_t FatNameMapper::Checksum(const FatShortName& shortName)
{
    uint8_t sum = 0;
    for (const uint8_t byte : shortName)
        sum = static_cast<uint8_t>(((sum & 1) ? 0x80 : 0) + (sum >> 1) + byte);
    return sum;
}

std::vector<std::array<uint8_t, 32>> FatNameMapper::LongNameEntries(const std::u16string& longName, uint8_t checksum)
{
    // 13 UTF-16 units per entry: 5 at offset 1, 6 at 14, 2 at 28. After the
    // name: one #0000 terminator (unless it fills the entry exactly), then #FFFF
    static constexpr size_t kOffsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    const size_t count = (longName.size() + 12) / 13;

    std::vector<std::array<uint8_t, 32>> entries(count);
    for (size_t e = 0; e < count; e++)
    {
        std::array<uint8_t, 32>& entry = entries[count - 1 - e];  // on disk the last part comes first
        entry.fill(0);
        entry[0] = static_cast<uint8_t>((e + 1) | (e + 1 == count ? 0x40 : 0));
        entry[11] = 0x0F;  // attribute: read-only | hidden | system | volume = LFN
        entry[13] = checksum;
        for (size_t k = 0; k < 13; k++)
        {
            const size_t index = e * 13 + k;
            uint16_t unit;
            if (index < longName.size())
                unit = longName[index];
            else if (index == longName.size())
                unit = 0x0000;
            else
                unit = 0xFFFF;
            entry[kOffsets[k]] = static_cast<uint8_t>(unit);
            entry[kOffsets[k] + 1] = static_cast<uint8_t>(unit >> 8);
        }
    }
    return entries;
}

std::string FatNameMapper::ShortNameToUtf8(const FatShortName& shortName, CodePage page)
{
    auto part = [&shortName, page](size_t from, size_t length) {
        std::string text;
        size_t end = from + length;
        while (end > from && shortName[end - 1] == ' ')
            end--;
        for (size_t i = from; i < end; i++)
        {
            const uint8_t byte = (i == 0 && shortName[i] == 0x05) ? 0xE5 : shortName[i];
            UnicodeHelper::AppendUtf8(text, UnicodeHelper::FromCodePage(page, byte));
        }
        return text;
    };
    const std::string base = part(0, 8);
    const std::string ext = part(8, 3);
    return ext.empty() ? base : base + "." + ext;
}

FatShortName FatNameMapper::ShortNameFromText(const std::string& utf8, bool isLabel, CodePage page)
{
    const std::u32string text = UnicodeHelper::DecodeUtf8(utf8);
    FatShortName name;
    name.fill(' ');
    auto put = [page](char32_t c) {
        const char32_t upper = UnicodeHelper::ToUpper(c);
        if (upper < 0x80)
            return (upper >= 0x20 && upper != U'.') ? static_cast<uint8_t>(upper) : uint8_t{'_'};
        uint8_t byte = '_';
        return UnicodeHelper::ToCodePage(page, upper, byte) ? byte : uint8_t{'_'};
    };
    if (isLabel)
    {
        // A label is 11 characters, spaces allowed, no dot split
        for (size_t i = 0; i < text.size() && i < 11; i++)
            name[i] = put(text[i]);
    }
    else
    {
        const size_t dot = text.rfind(U'.');
        const std::u32string base = dot == std::u32string::npos ? text : text.substr(0, dot);
        const std::u32string ext = dot == std::u32string::npos ? std::u32string() : text.substr(dot + 1);
        for (size_t i = 0; i < base.size() && i < 8; i++)
            name[i] = put(base[i]);
        for (size_t i = 0; i < ext.size() && i < 3; i++)
            name[8 + i] = put(ext[i]);
    }
    if (name[0] == 0xE5)
        name[0] = 0x05;
    return name;
}
