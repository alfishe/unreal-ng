#include "unrealasm/symbols/symbol.h"

#include <array>
#include <cctype>
#include <charconv>

namespace unrealasm::symbols
{
namespace
{
constexpr std::array<std::string_view, 7> kKindNames = {"unknown", "code", "data", "const", "port", "entry", "local"};

bool IsNameChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

bool AllNameChars(std::string_view s)
{
    if (s.empty())
        return false;
    for (const char c : s)
        if (!IsNameChar(c))
            return false;
    return true;
}

/// "rom2" -> page 2 when `text` is `prefix` + a decimal page number
bool PagedSpace(std::string_view text, std::string_view prefix, uint16_t& page)
{
    if (text.size() <= prefix.size() || text.substr(0, prefix.size()) != prefix)
        return false;
    const std::string_view digits = text.substr(prefix.size());
    unsigned value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error != std::errc() || end != digits.data() + digits.size() || value > 0xFFFF)
        return false;
    page = static_cast<uint16_t>(value);
    return true;
}
}  // namespace

std::string AddressSpace::Format() const
{
    if (kind == SpaceKind::CpuView)
        return "cpu:" + cpu;
    std::string out = cpu == "main" ? std::string() : cpu + ".";
    switch (kind)
    {
        case SpaceKind::Rom: return out + "rom" + std::to_string(page);
        case SpaceKind::Ram: return out + "ram" + std::to_string(page);
        case SpaceKind::Cache: return out + "cache" + std::to_string(page);
        case SpaceKind::Device: return out + region;
        case SpaceKind::Constant: return out + "const";
        case SpaceKind::Port: return out + "port";
        case SpaceKind::CpuView: break;
    }
    return out;
}

bool AddressSpace::Parse(std::string_view text, AddressSpace& out)
{
    AddressSpace s;
    if (text.substr(0, 4) == "cpu:")
    {
        if (!AllNameChars(text.substr(4)))
            return false;
        s.cpu = std::string(text.substr(4));
        out = s;
        return true;
    }
    const size_t dot = text.find('.');
    if (dot != std::string_view::npos)
    {
        if (!AllNameChars(text.substr(0, dot)))
            return false;
        s.cpu = std::string(text.substr(0, dot));
        text = text.substr(dot + 1);
    }
    if (text == "const")
        s.kind = SpaceKind::Constant;
    else if (text == "port")
        s.kind = SpaceKind::Port;
    else if (PagedSpace(text, "rom", s.page))
        s.kind = SpaceKind::Rom;
    else if (PagedSpace(text, "ram", s.page))
        s.kind = SpaceKind::Ram;
    else if (PagedSpace(text, "cache", s.page))
        s.kind = SpaceKind::Cache;
    // A device region; a name that starts like a page ("ram65536", "rom") is a mistake, not a region
    else if (AllNameChars(text) && text != "cpu" && text.substr(0, 3) != "rom" && text.substr(0, 3) != "ram" && text.substr(0, 5) != "cache")
    {
        s.kind = SpaceKind::Device;
        s.region = std::string(text);
    }
    else
        return false;
    out = s;
    return true;
}

uint32_t AddressSpace::Extent() const
{
    switch (kind)
    {
        case SpaceKind::Rom:
        case SpaceKind::Ram:
        case SpaceKind::Cache: return 0x4000;
        case SpaceKind::CpuView:
        case SpaceKind::Port: return 0x10000;
        case SpaceKind::Device:
        case SpaceKind::Constant: return 0;
    }
    return 0;
}

std::string_view KindName(SymbolKind kind)
{
    const size_t i = static_cast<size_t>(kind);
    return i < kKindNames.size() ? kKindNames[i] : kKindNames[0];
}

bool ParseKind(std::string_view text, SymbolKind& out)
{
    for (size_t i = 0; i < kKindNames.size(); ++i)
    {
        const std::string_view name = kKindNames[i];
        if (name.size() != text.size())
            continue;
        bool same = true;
        for (size_t k = 0; k < name.size() && same; ++k)
            same = std::tolower(static_cast<unsigned char>(text[k])) == name[k];
        if (same)
        {
            out = static_cast<SymbolKind>(i);
            return true;
        }
    }
    return false;
}
}  // namespace unrealasm::symbols
