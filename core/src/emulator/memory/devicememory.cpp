#include "stdafx.h"

#include "devicememory.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/rtc/ds12887.h"
#include "emulator/io/rtc/rtcaccess.h"
#include "emulator/memory/memorymap.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/state/devicestate.h"

namespace DeviceMemory
{

std::vector<IDeviceMemoryRegion*> Regions(EmulatorContext* context)
{
    std::vector<IDeviceMemoryRegion*> regions;
    if (context && context->pPortDecoder)
    {
        context->pPortDecoder->CollectMemoryRegions(regions);
        // The machine's CMOS clock, whichever board carries it: "cmos" (and the ZX-Evo "eeprom")
        if (Ds12887* rtc = RtcAccess::Find(context))
            rtc->CollectMemoryRegions(regions);
    }
    return regions;
}

IDeviceMemoryRegion* Find(EmulatorContext* context, const std::string& name, std::string* error)
{
    std::string wanted = name;
    std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::vector<IDeviceMemoryRegion*> regions = Regions(context);
    for (IDeviceMemoryRegion* region : regions)
        if (wanted == region->Name())
            return region;
    if (error)
    {
        std::string names;
        for (IDeviceMemoryRegion* region : regions)
            names += (names.empty() ? "" : ", ") + std::string(region->Name());
        *error = "no memory region '" + name + "' on this machine" +
                 (names.empty() ? std::string(" (it has none)") : " (regions: " + names + ")");
    }
    return nullptr;
}

namespace
{
bool CheckRange(const IDeviceMemoryRegion& region, uint64_t offset, uint64_t length, std::string& error)
{
    if (offset >= region.Size() || length > region.Size() - offset)
    {
        error = StringHelper::Format("offset 0x%llX + length 0x%llX is outside the region '%s' (0x%X bytes)",
                                     static_cast<unsigned long long>(offset), static_cast<unsigned long long>(length),
                                     region.Name(), region.Size());
        return false;
    }
    return true;
}
}  // namespace

bool Read(EmulatorContext* context, const std::string& name, uint32_t offset, uint32_t length, std::vector<uint8_t>& out,
          std::string& error)
{
    IDeviceMemoryRegion* region = Find(context, name, &error);
    if (!region || !CheckRange(*region, offset, length, error))
        return false;
    out.resize(length);
    for (uint32_t i = 0; i < length; i++)
        out[i] = region->Read(offset + i);
    return true;
}

bool Write(EmulatorContext* context, const std::string& name, uint32_t offset, const std::vector<uint8_t>& bytes,
           const char* source, std::string& error)
{
    IDeviceMemoryRegion* region = Find(context, name, &error);
    if (!region || !CheckRange(*region, offset, bytes.size(), error))
        return false;
    if (!region->Writable())
    {
        error = std::string("the region '") + region->Name() + "' is read-only";
        return false;
    }
    auto edit = [&]() {
        for (size_t i = 0; i < bytes.size(); i++)
            region->Write(static_cast<uint32_t>(offset + i), bytes[i]);
    };
    if (context->pEmulator)
        context->pEmulator->EditMemoryFromTool(source, edit);
    else
        edit();
    return true;
}

bool Save(EmulatorContext* context, const std::string& name, const std::string& path, uint32_t offset, uint32_t length,
          std::string& error)
{
    IDeviceMemoryRegion* region = Find(context, name, &error);
    if (!region)
        return false;
    if (offset < region->Size() && length == 0)
        length = region->Size() - offset;
    std::vector<uint8_t> bytes;
    if (!Read(context, name, offset, length, bytes, error))
        return false;
    if (path.empty() || !FileHelper::SaveBufferToFile(path, bytes.data(), bytes.size()))
    {
        error = "cannot write '" + path + "'";
        return false;
    }
    return true;
}

bool Load(EmulatorContext* context, const std::string& name, const std::string& path, uint32_t offset, size_t& written,
          std::string& error)
{
    written = 0;
    IDeviceMemoryRegion* region = Find(context, name, &error);
    if (!region)
        return false;
    if (path.empty() || !FileHelper::FileExists(path))
    {
        error = "no file '" + path + "'";
        return false;
    }
    if (offset >= region->Size())
    {
        error = StringHelper::Format("offset 0x%X is outside the region '%s' (0x%X bytes)", offset, region->Name(), region->Size());
        return false;
    }
    const size_t room = region->Size() - offset;
    const size_t size = std::min(FileHelper::GetFileSize(path), room);
    std::vector<uint8_t> bytes(size);
    if (size && FileHelper::ReadFileToBuffer(path, bytes.data(), size) != size)
    {
        error = "cannot read '" + path + "'";
        return false;
    }
    if (!Write(context, name, offset, bytes, "memory region load", error))
        return false;
    written = size;
    return true;
}

bool ParseNumber(const std::string& text, uint32_t& value)
{
    std::string t = text;
    int base = 10;
    if (!t.empty() && (t[0] == '#' || t[0] == '$'))
    {
        t = t.substr(1);
        base = 16;
    }
    else if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
    {
        t = t.substr(2);
        base = 16;
    }
    if (t.empty() || t.size() > 8)
        return false;
    uint64_t v = 0;
    for (char c : t)
    {
        int digit = -1;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        if (digit < 0)
            return false;
        v = v * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
    }
    if (v > 0xFFFFFFFFull)
        return false;
    value = static_cast<uint32_t>(v);
    return true;
}

bool ParseHexBytes(const std::string& text, std::vector<uint8_t>& out)
{
    out.clear();
    int high = -1;
    for (char c : text)
    {
        if (c == ' ' || c == ',' || c == '\t' || c == '\n')
            continue;
        int digit = -1;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        if (digit < 0)
            return false;
        if (high < 0)
            high = digit;
        else
        {
            out.push_back(static_cast<uint8_t>(high << 4 | digit));
            high = -1;
        }
    }
    return high < 0;
}

}  // namespace DeviceMemory

namespace DeviceState
{

StateNode MemoryRegions(EmulatorContext* context)
{
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    StateNode list = StateNode::Array();
    for (IDeviceMemoryRegion* region : DeviceMemory::Regions(context))
    {
        StateNode n = StateNode::Object();
        n["name"] = region->Name();
        n["description"] = region->Description();
        n["size"] = static_cast<uint64_t>(region->Size());
        n["size_hex"] = StringHelper::Format("0x%X", region->Size());
        n["page_size"] = static_cast<uint64_t>(region->PageSize());
        n["pages"] = static_cast<uint64_t>(region->PageSize() ? region->Size() / region->PageSize() : 0);
        n["writable"] = region->Writable();
        n["write_path"] = region->WritePath();
        list.push(n);
    }
    ret["regions"] = list;
    ret["note"] = "memory a device owns outside the CPU's RAM / ROM pages: read / write by name "
                  "(/memory/region/{name}, CLI memory region, region_read / region_write)";
    return ret;
}

StateNode MemoryRegionRead(EmulatorContext* context, const std::string& name, uint32_t offset, uint32_t length,
                           const std::string& format)
{
    std::vector<uint8_t> bytes;
    std::string error;
    if (!DeviceMemory::Read(context, name, offset, length, bytes, error))
    {
        StateNode n = StateNode::Object();
        n["available"] = false;
        n["description"] = error;
        return n;
    }
    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["region"] = name;
    ret["offset"] = StringHelper::Format("0x%05X", offset);
    ret["length"] = static_cast<uint64_t>(length);
    if (format == "data")
    {
        StateNode data = StateNode::Array();
        for (uint8_t b : bytes)
            data.push(int(b));
        ret["data"] = data;
    }
    else if (format == "sparse")
    {
        StateNode segments = StateNode::Array();
        for (const MemorySparseSegment& segment : BuildSparseSegments(bytes.data(), bytes.size()))
        {
            StateNode item = StateNode::Object();
            item["offset"] = static_cast<uint64_t>(offset + segment.offset);
            item["length"] = static_cast<uint64_t>(segment.length);
            item["is_fill"] = segment.isFill;
            if (segment.isFill)
                item["fill"] = StringHelper::Format("0x%02X", segment.fill);
            else
                item["hex"] = segment.hex;
            segments.push(item);
        }
        ret["segments"] = segments;
    }
    else
    {
        std::string hex;
        hex.reserve(bytes.size() * 2);
        for (uint8_t b : bytes)
            hex += StringHelper::Format("%02X", b);
        ret["hex"] = hex;
    }
    return ret;
}

}  // namespace DeviceState
