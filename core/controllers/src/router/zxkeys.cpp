#include "router/zxkeys.h"

#include <algorithm>
#include <cctype>

namespace controllers
{

namespace
{
/// ZXKeysEnum values (emulator/io/keyboard/keyboard.h). Not ASCII throughout: I is 0x48 and H 0x49
enum : uint8_t
{
    kCaps = 0x04,
    kSym = 0x05,
    kEnter = 0x0A,
    kSpace = 0x20,
    k0 = 0x30,
    kA = 0x41,
};

uint8_t Letter(char c)
{
    if (c == 'h')
        return 0x49;
    if (c == 'i')
        return 0x48;
    return static_cast<uint8_t>(kA + (c - 'a'));
}

struct NamedKeys
{
    const char* name;
    uint8_t first;
    uint8_t second;  ///< 0: one key
};

constexpr NamedKeys kNamed[] = {
    {"caps", kCaps, 0},       {"shift", kCaps, 0},     {"capsshift", kCaps, 0}, {"caps_shift", kCaps, 0},
    {"cs", kCaps, 0},         {"symbol", kSym, 0},     {"sym", kSym, 0},        {"symshift", kSym, 0},
    {"sym_shift", kSym, 0},   {"ss", kSym, 0},         {"enter", kEnter, 0},    {"return", kEnter, 0},
    {"space", kSpace, 0},     {" ", kSpace, 0},
    // The extended keys: CAPS or SYM and a key (keyboard.h)
    {"up", kCaps, k0 + 7},    {"down", kCaps, k0 + 6}, {"left", kCaps, k0 + 5}, {"right", kCaps, k0 + 8},
    {"delete", kCaps, k0},    {"backspace", kCaps, k0}, {"del", kCaps, k0},     {"break", kCaps, kSpace},
    {"edit", kCaps, k0 + 1},  {".", kSym, kA + 12},    {"dot", kSym, kA + 12},  {",", kSym, kA + 13},
    {"comma", kSym, kA + 13}, {"+", kSym, kA + 10},    {"plus", kSym, kA + 10}, {"-", kSym, kA + 9},
    {"minus", kSym, kA + 9},  {"*", kSym, kA + 1},     {"multiply", kSym, kA + 1}, {"/", kSym, kA + 21},
    {"divide", kSym, kA + 21}, {"=", kSym, kA + 11},   {"equal", kSym, kA + 11}, {"equals", kSym, kA + 11},
    {"\"", kSym, kA + 15},    {"quote", kSym, kA + 15}, {"dblquote", kSym, kA + 15},
};

std::string Lower(const std::string& text)
{
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

bool OneName(const std::string& name, std::vector<uint8_t>& keys)
{
    if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z')
    {
        keys.push_back(Letter(name[0]));
        return true;
    }
    if (name.size() == 1 && name[0] >= '0' && name[0] <= '9')
    {
        keys.push_back(static_cast<uint8_t>(k0 + (name[0] - '0')));
        return true;
    }
    for (const NamedKeys& named : kNamed)
    {
        if (name == named.name)
        {
            keys.push_back(named.first);
            if (named.second != 0)
                keys.push_back(named.second);
            return true;
        }
    }
    return false;
}
}  // namespace

bool ZxKeysFromName(const std::string& name, std::vector<uint8_t>& keys)
{
    const std::string lower = Lower(name);
    std::vector<uint8_t> found;
    // `+` joins keys; the plus key itself is "+" alone or "plus" in a join
    if (!OneName(lower, found))
    {
        size_t start = 0;
        while (start <= lower.size())
        {
            size_t end = lower.find('+', start);
            if (end == std::string::npos)
                end = lower.size();
            if (!OneName(lower.substr(start, end - start), found))
                return false;
            start = end + 1;
        }
    }
    if (found.empty())
        return false;
    for (uint8_t key : found)
    {
        if (std::find(keys.begin(), keys.end(), key) == keys.end())
            keys.push_back(key);
    }
    return true;
}

std::vector<std::string> KnownZxKeyNames()
{
    std::vector<std::string> names;
    for (char c = 'a'; c <= 'z'; c++)
        names.emplace_back(1, c);
    for (char c = '0'; c <= '9'; c++)
        names.emplace_back(1, c);
    for (const NamedKeys& named : kNamed)
        names.emplace_back(named.name);
    return names;
}

}  // namespace controllers
