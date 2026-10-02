#pragma once

/// @file cli-memory-region.h
/// @brief CLI `memory region ...` (devicememory.h; Sprinter automation audit G5): memory a device
/// owns outside the CPU's pages - the Sprinter's 256 KB video RAM "vram" - by name. Header-only
/// so core-tests drive it without a CLI socket; every call is the core's DeviceMemory, the same
/// functions the WebAPI, Lua, Python and MCP use.
///
///   memory regions                                      list (name, size, pages, write path)
///   memory region read <name> <offset> [len]            hex dump (len default 256, at most 64 KB)
///   memory region write <name> <offset> <hex bytes...>  through the device's write path
///   memory region save <name> <file> [offset] [len]     to a file (len 0 / omitted = to the end)
///   memory region load <name> <file> [offset]           from a file
///
/// Worked example (Sprinter): `memory region write vram 0x17F0 00 00 A8` makes text paper 5 blue.

#include <cstdio>
#include <string>
#include <vector>

#include "emulator/memory/devicememory.h"
#include "emulator/memory/memorymap.h"
#include "emulator/state/devicestate.h"

namespace CliMemoryRegion
{
inline std::string Usage(const char* newline)
{
    return std::string("Usage:") + newline + "  memory regions" + newline +
           "  memory region read <name> <offset> [len]" + newline +
           "  memory region write <name> <offset> <hex bytes...>" + newline +
           "  memory region save <name> <file> [offset] [len]" + newline +
           "  memory region load <name> <file> [offset]" + newline;
}

/// args[0] = "regions" or "region"
inline std::string Text(EmulatorContext* context, const std::vector<std::string>& args, const char* newline = "\n")
{
    if (args.empty())
        return Usage(newline);
    if (args[0] == "regions" || (args.size() == 1) || (args.size() > 1 && args[1] == "list"))
    {
        const std::vector<IDeviceMemoryRegion*> regions = DeviceMemory::Regions(context);
        if (regions.empty())
            return std::string("No device memory regions on this machine") + newline;
        std::string out = "Device memory regions:" + std::string(newline);
        for (IDeviceMemoryRegion* r : regions)
        {
            char line[160];
            std::snprintf(line, sizeof line, "  %-8s %7u bytes, %u pages of %u%s", r->Name(), r->Size(),
                          r->PageSize() ? r->Size() / r->PageSize() : 0u, r->PageSize(), r->Writable() ? "" : ", read-only");
            out += line + std::string(newline) + "           " + r->Description() + newline;
        }
        return out;
    }

    const std::string& sub = args[1];
    if (args.size() < 4)
        return Usage(newline);
    const std::string& name = args[2];
    std::string error;
    if (sub == "read")
    {
        uint32_t offset = 0, length = 256;
        if (!DeviceMemory::ParseNumber(args[3], offset) || (args.size() > 4 && !DeviceMemory::ParseNumber(args[4], length)))
            return std::string("Error: offset / len: decimal, 0x or # hex") + newline;
        if (length > 65536)
            return std::string("Error: len at most 65536 (memory region save for more)") + newline;
        std::vector<uint8_t> bytes;
        if (!DeviceMemory::Read(context, name, offset, length, bytes, error))
            return "Error: " + error + newline;
        return FormatHexDump(bytes.data(), bytes.size(), offset);
    }
    if (sub == "write")
    {
        uint32_t offset = 0;
        if (!DeviceMemory::ParseNumber(args[3], offset))
            return std::string("Error: offset: decimal, 0x or # hex") + newline;
        std::string hex;
        for (size_t i = 4; i < args.size(); i++)
            hex += args[i];
        std::vector<uint8_t> bytes;
        if (hex.empty() || !DeviceMemory::ParseHexBytes(hex, bytes))
            return std::string("Error: bytes: hex pairs (00 00 A8 or 0000A8)") + newline;
        if (!DeviceMemory::Write(context, name, offset, bytes, "CLI region write", error))
            return "Error: " + error + newline;
        return "Wrote " + std::to_string(bytes.size()) + " bytes to " + name + newline;
    }
    if (sub == "save")
    {
        uint32_t offset = 0, length = 0;
        if ((args.size() > 4 && !DeviceMemory::ParseNumber(args[4], offset)) ||
            (args.size() > 5 && !DeviceMemory::ParseNumber(args[5], length)))
            return std::string("Error: offset / len: decimal, 0x or # hex") + newline;
        if (!DeviceMemory::Save(context, name, args[3], offset, length, error))
            return "Error: " + error + newline;
        return "Saved " + name + " to " + args[3] + newline;
    }
    if (sub == "load")
    {
        uint32_t offset = 0;
        if (args.size() > 4 && !DeviceMemory::ParseNumber(args[4], offset))
            return std::string("Error: offset: decimal, 0x or # hex") + newline;
        size_t written = 0;
        if (!DeviceMemory::Load(context, name, args[3], offset, written, error))
            return "Error: " + error + newline;
        return "Loaded " + std::to_string(written) + " bytes into " + name + newline;
    }
    return "Error: unknown subcommand '" + sub + "'" + newline + Usage(newline);
}
}  // namespace CliMemoryRegion
