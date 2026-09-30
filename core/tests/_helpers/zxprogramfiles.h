#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

/// Reference files for a ZX Spectrum test program built by the test suite (ctprobe, snowtest): a tape with a
/// BASIC loader, a TR-DOS disk that boots it, and a symbol file for debuggers and the co-emulation harness.
/// The loader is `10 CLEAR org-1 / 20 <load> / 30 RANDOMIZE USR org`, tokenized by hand (BasicEncoder::tokenize
/// writes neither the hidden numbers nor keywords after a statement's first, as TrdosBootInjector notes)
namespace ZxProgramFiles
{
inline std::string ReadText(const std::string& path)
{
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline std::vector<uint8_t> ReadBinary(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

inline std::vector<uint8_t> Concat(std::initializer_list<std::vector<uint8_t>> parts)
{
    std::vector<uint8_t> out;
    for (const auto& p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

/// A tokenized BASIC line
inline std::vector<uint8_t> BasicLine(uint16_t number, const std::vector<uint8_t>& body)
{
    std::vector<uint8_t> line{ static_cast<uint8_t>(number >> 8), static_cast<uint8_t>(number & 0xFF) };
    const size_t length = body.size() + 1;
    line.push_back(static_cast<uint8_t>(length & 0xFF));
    line.push_back(static_cast<uint8_t>(length >> 8));
    line.insert(line.end(), body.begin(), body.end());
    line.push_back(0x0D);
    return line;
}

/// A number as BASIC stores it: the digits, then the hidden 5-byte small integer
inline std::vector<uint8_t> BasicNumber(uint16_t value)
{
    const std::string digits = std::to_string(value);
    std::vector<uint8_t> n(digits.begin(), digits.end());
    n.insert(n.end(), { 0x0E, 0x00, 0x00, static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8), 0x00 });
    return n;
}

constexpr uint8_t kClear = 0xFD, kLoad = 0xEF, kCode = 0xAF, kRandomize = 0xF9, kUsr = 0xC0, kRem = 0xEA;

/// 10 CLEAR org-1 / 20 <load> / 30 RANDOMIZE USR org
inline std::vector<uint8_t> Loader(uint16_t org, const std::vector<uint8_t>& loadStatement)
{
    return Concat({ BasicLine(10, Concat({ { kClear }, BasicNumber(static_cast<uint16_t>(org - 1)) })),
                    BasicLine(20, loadStatement), BasicLine(30, Concat({ { kRandomize, kUsr }, BasicNumber(org) })) });
}

inline std::vector<uint8_t> TapBlock(uint8_t flag, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> block;
    const size_t length = data.size() + 2;
    block.push_back(static_cast<uint8_t>(length & 0xFF));
    block.push_back(static_cast<uint8_t>(length >> 8));
    block.push_back(flag);
    uint8_t sum = flag;
    for (uint8_t b : data)
    {
        block.push_back(b);
        sum ^= b;
    }
    block.push_back(sum);
    return block;
}

inline std::vector<uint8_t> TapHeader(uint8_t type, const std::string& name, uint16_t length, uint16_t param1,
                                      uint16_t param2)
{
    std::vector<uint8_t> h{ type };
    std::string padded = name;
    padded.resize(10, ' ');
    h.insert(h.end(), padded.begin(), padded.end());
    for (uint16_t w : { length, param1, param2 })
    {
        h.push_back(static_cast<uint8_t>(w & 0xFF));
        h.push_back(static_cast<uint8_t>(w >> 8));
    }
    return h;
}

/// <name>.tap: a BASIC loader (auto-runs line 10) and the code at `org`
inline std::vector<uint8_t> BuildTap(const std::string& name, uint16_t org, const std::vector<uint8_t>& code)
{
    const std::vector<uint8_t> basic = Loader(org, { kLoad, '"', '"', kCode });  // LOAD "" CODE
    std::vector<uint8_t> tap;
    auto append = [&](const std::vector<uint8_t>& block) { tap.insert(tap.end(), block.begin(), block.end()); };
    const uint16_t basicLength = static_cast<uint16_t>(basic.size());
    append(TapBlock(0x00, TapHeader(0, name, basicLength, 10, basicLength)));
    append(TapBlock(0xFF, basic));
    append(TapBlock(0x00, TapHeader(3, name, static_cast<uint16_t>(code.size()), org, 32768)));
    append(TapBlock(0xFF, code));
    return tap;
}

/// <name>.sym: every label and constant, "NAME equ #ADDR"
inline std::vector<uint8_t> BuildSym(const std::string& heading, const std::map<std::string, uint32_t>& symbols)
{
    std::string text = heading + "\n";
    char line[80];
    for (const auto& [name, value] : symbols)
    {
        std::snprintf(line, sizeof line, "%s equ #%04X\n", name.c_str(), static_cast<unsigned>(value & 0xFFFF));
        text += line;
    }
    return std::vector<uint8_t>(text.begin(), text.end());
}

/// <name>.trd: TR-DOS 80 tracks, two sides; "boot" (BASIC, auto-runs line 10) loads `name` (CODE, `org`)
inline std::vector<uint8_t> BuildTrd(const std::string& name, uint16_t org, const std::vector<uint8_t>& code)
{
    constexpr size_t sector = 256;
    std::vector<uint8_t> trd(160 * 16 * sector, 0);

    // RANDOMIZE USR 15619: REM : LOAD "<name>" CODE - TR-DOS runs the command after the REM
    const std::string quoted = "\"" + name + "\"";
    std::vector<uint8_t> basic = Loader(org, Concat({ { kRandomize, kUsr }, BasicNumber(15619), { ':', kRem, ':', kLoad },
                                                      std::vector<uint8_t>(quoted.begin(), quoted.end()), { kCode } }));
    const uint16_t basicLength = static_cast<uint16_t>(basic.size());
    basic.insert(basic.end(), { 0x80, 0xAA, 10, 0 });  // autostart line 10, after the program

    size_t next = 16;  // logical sector: track 1, sector 0
    int files = 0;
    auto addFile = [&](const std::string& fileName, char type, uint16_t start, uint16_t length,
                       const std::vector<uint8_t>& data) {
        const uint8_t sectors = static_cast<uint8_t>((data.size() + sector - 1) / sector);
        uint8_t* e = &trd[static_cast<size_t>(files) * 16];
        std::string padded = fileName;
        padded.resize(8, ' ');
        std::copy(padded.begin(), padded.end(), e);
        e[8] = static_cast<uint8_t>(type);
        e[9] = static_cast<uint8_t>(start & 0xFF);
        e[10] = static_cast<uint8_t>(start >> 8);
        e[11] = static_cast<uint8_t>(length & 0xFF);
        e[12] = static_cast<uint8_t>(length >> 8);
        e[13] = sectors;
        e[14] = static_cast<uint8_t>(next % 16);
        e[15] = static_cast<uint8_t>(next / 16);
        std::copy(data.begin(), data.end(), trd.begin() + static_cast<std::ptrdiff_t>(next * sector));
        next += sectors;
        files++;
    };
    addFile("boot", 'B', basicLength, basicLength, basic);
    addFile(name, 'C', org, static_cast<uint16_t>(code.size()), code);

    uint8_t* info = &trd[8 * sector];
    const uint16_t freeSectors = static_cast<uint16_t>(160 * 16 - next);
    info[0xE1] = static_cast<uint8_t>(next % 16);
    info[0xE2] = static_cast<uint8_t>(next / 16);
    info[0xE3] = 0x16;  // 80 tracks, two sides
    info[0xE4] = static_cast<uint8_t>(files);
    info[0xE5] = static_cast<uint8_t>(freeSectors & 0xFF);
    info[0xE6] = static_cast<uint8_t>(freeSectors >> 8);
    info[0xE7] = 0x10;  // TR-DOS
    std::string label = name;
    label.resize(8, ' ');
    std::copy(label.begin(), label.end(), info + 0xF5);
    return trd;
}
}  // namespace ZxProgramFiles
