#pragma once

/// @file gdbmonitormemory.h
/// @brief GDB `monitor` commands over every memory of the machine (memory-spaces design, step 2,
/// docs/inprogress/2026-10-08-memory-spaces): `monitor regions` lists the device memories, `monitor mem
/// <space:offset> [len]` dumps bytes of any space (cpu, ram5 / rom2 / cache0, ram, or a region by name or alias:
/// sprinter.vram, neogs.ram, ...), and `monitor ttd findlast` takes `space:offset` for the Sprinter's video and fast
/// RAM. Numbers are hex without a prefix, as everywhere in GDB's protocol. Header-only: the session calls these, the
/// core tests too.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "debugger/memory/memoryread.h"
#include "emulator/memory/devicememory.h"

namespace GdbMonitorMemory
{
constexpr uint32_t kDefaultLength = 0x40;
constexpr uint32_t kMaxLength = 0x1000;

/// Hex digits (an optional 0x / # / $ prefix allowed) into `value`; false on anything else
inline bool ParseHex(const std::string& text, uint32_t& value)
{
    std::string digits = text;
    if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
        digits = digits.substr(2);
    else if (!digits.empty() && (digits[0] == '#' || digits[0] == '$'))
        digits = digits.substr(1);
    if (digits.empty() || digits.size() > 8 || digits.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return false;
    value = static_cast<uint32_t>(std::strtoul(digits.c_str(), nullptr, 16));
    return true;
}

/// "space:offset" (or a bare offset = cpu) into its parts
inline bool ParseLocation(const std::string& text, std::string& space, uint32_t& offset)
{
    const size_t colon = text.rfind(':');
    space = colon == std::string::npos ? "cpu" : text.substr(0, colon);
    return !space.empty() && ParseHex(colon == std::string::npos ? text : text.substr(colon + 1), offset);
}

/// `monitor regions`
inline std::string Regions(EmulatorContext* context)
{
    const std::vector<IDeviceMemoryRegion*> regions = DeviceMemory::Regions(context);
    if (regions.empty())
        return "No device memories on this machine (spaces: cpu, ram, ramN, romN, cacheN)\n";
    std::string out = "Memories (monitor mem <name>:<offset> [len]):\n";
    for (const IDeviceMemoryRegion* r : regions)
    {
        out += "  " + std::string(r->Name());
        if (r->Aliases() && *r->Aliases())
            out += " (" + std::string(r->Aliases()) + ")";
        char size[16];
        std::snprintf(size, sizeof size, "%X", r->Size());
        out += " size " + std::string(size) + (r->Writable() ? "" : " read-only") + "\n";
    }
    return out;
}

/// `monitor mem <space:offset> [len]`: a hex dump, 16 bytes a line
inline std::string Dump(EmulatorContext* context, const std::string& args)
{
    const size_t blank = args.find(' ');
    const std::string where = args.substr(0, blank);
    std::string space;
    uint32_t offset = 0;
    uint32_t length = kDefaultLength;
    if (where.empty() || !ParseLocation(where, space, offset) ||
        (blank != std::string::npos && !ParseHex(args.substr(args.find_first_not_of(' ', blank)), length)))
        return "Usage: monitor mem <space:offset> [len] - hex numbers; space cpu, ramN, romN, cacheN, ram or a region "
               "(monitor regions)\n";
    length = std::min(std::max<uint32_t>(length, 1), kMaxLength);
    const MemoryRead::Result read = MemoryRead::Bytes(context, space, offset, length);
    if (!read.error.empty())
        return "Error: " + read.error + "\n";
    std::string out;
    char text[64];
    for (size_t i = 0; i < read.bytes.size(); i += 16)
    {
        std::snprintf(text, sizeof text, ":%05X:", static_cast<unsigned>(offset + i));
        out += read.space + text;
        for (size_t j = i; j < std::min(i + 16, read.bytes.size()); j++)
        {
            std::snprintf(text, sizeof text, " %02X", read.bytes[j]);
            out += text;
        }
        out += "\n";
    }
    return out;
}
}  // namespace GdbMonitorMemory
