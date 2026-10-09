#include "stdafx.h"

#include "devicememory.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/rtc/ds12887.h"
#include "emulator/io/rtc/rtcaccess.h"
#include "debugger/ttd/engine/ttdregiontracker.h"
#include "debugger/ttd/ttdmachineperipherals.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/memory/memorymap.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/state/devicestate.h"

namespace
{
/// A memory the time-travel engine records, seen through the registry: read straight from the device; written
/// straight to the bytes (no device side effects), marking the region's dirty piece for a recording. Read-only
/// when the device restores the memory through its own path, or when its dirty marks live in a serializer of the
/// registry this view was taken from rather than in the device (the write could not be recorded)
class TtdRegionMemory final : public IDeviceMemoryRegion
{
public:
    TtdRegionMemory(const ttd::TTDDeviceRegion& r, bool writable)
        : _name(r.desc.name), _memory(r.desc.memory), _size(r.desc.bytes), _writable(writable),
          _tracker(r.compareEachCapture ? nullptr : r.tracker)
    {
        _description = "Device memory recorded by time travel (" + _name + "), read straight from the device" +
                       (_writable ? std::string("; writes go to the bytes, without device side effects")
                                  : std::string("; read-only here: the device keeps it through its own path"));
    }

    bool Same(const ttd::TTDDeviceRegion& r) const
    {
        return _name == r.desc.name && _memory == r.desc.memory && _size == r.desc.bytes;
    }

    const char* Name() const override { return _name.c_str(); }
    const char* Description() const override { return _description.c_str(); }
    const char* TtdRegion() const override { return _name.c_str(); }
    uint32_t Size() const override { return _size; }
    uint32_t PageSize() const override { return std::min<uint32_t>(16 * 1024, _size); }
    bool Writable() const override { return _writable; }
    const char* WritePath() const override { return _writable ? "the bytes directly (no device side effects)" : "none"; }
    uint8_t Read(uint32_t offset) const override { return offset < _size ? _memory[offset] : 0xFF; }
    void Write(uint32_t offset, uint8_t value) override
    {
        if (!_writable || offset >= _size)
            return;
        _memory[offset] = value;
        if (_tracker)
            _tracker->Mark(offset);
    }

private:
    std::string _name;
    std::string _description;
    uint8_t* _memory;
    uint32_t _size;
    bool _writable;
    ttd::TTDRegionTracker* _tracker;
};

/// Per machine: the views handed out (kept until the machine goes: a caller may hold a pointer), the current ones
struct TtdViews
{
    std::vector<std::unique_ptr<TtdRegionMemory>> all;
    std::vector<TtdRegionMemory*> current;
};
std::mutex g_viewsMutex;
std::map<EmulatorContext*, TtdViews> g_views;

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// Whether `wanted` (lower case) is the region's name or one of its aliases
bool Answers(const IDeviceMemoryRegion& region, const std::string& wanted)
{
    if (wanted == Lower(region.Name()))
        return true;
    const std::string aliases = region.Aliases() ? region.Aliases() : "";
    size_t start = 0;
    while (start <= aliases.size())
    {
        const size_t end = std::min(aliases.find(',', start), aliases.size());
        if (end > start && Lower(aliases.substr(start, end - start)) == wanted)
            return true;
        start = end + 1;
    }
    return false;
}

/// The engine's regions this machine has, as registry views; `declared` regions that name a TTD region hide it
void AppendTtdRegions(EmulatorContext* context, std::vector<IDeviceMemoryRegion*>& regions)
{
    std::vector<std::string> covered;
    for (const IDeviceMemoryRegion* region : regions)
        if (region->TtdRegion())
            covered.emplace_back(region->TtdRegion());

    // A registry of its own (MachineStateTransfer does the same): it lists the devices' regions without touching
    // a session's registry; listing a device's regions is side-effect free (a tracker bound again keeps its marks)
    ttd::TTDPeripheralRegistry registry;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> owned;
    if (!ttd::RegisterMachinePeripherals(context, registry, owned))
        return;
    std::vector<std::pair<ttd::TTDDeviceRegion, bool>> found;
    for (ttd::ITTDRegionSource* source : registry.RegionSources())
    {
        std::vector<ttd::TTDDeviceRegion> device;
        source->TTDRegions(device);
        const auto* serializable = dynamic_cast<const ttd::TTDSerializable*>(source);
        const bool ownedHere = std::any_of(owned.begin(), owned.end(),
                                           [&](const auto& o) { return o.get() == serializable; });
        for (const ttd::TTDDeviceRegion& r : device)
        {
            if (!r.desc.memory || r.desc.bytes == 0 ||
                std::find(covered.begin(), covered.end(), r.desc.name) != covered.end())
                continue;
            // Writable: restored as plain bytes, and its dirty marks (if any) are the device's own
            const bool writable = !r.desc.restorePiece && (r.compareEachCapture || !r.tracker || !ownedHere);
            found.emplace_back(r, writable);
        }
    }

    std::lock_guard<std::mutex> lock(g_viewsMutex);
    TtdViews& views = g_views[context];
    views.current.clear();
    for (const auto& [r, writable] : found)
    {
        TtdRegionMemory* view = nullptr;
        for (const auto& v : views.all)
            if (v->Same(r) && v->Writable() == writable)
                view = v.get();
        if (!view)
        {
            views.all.push_back(std::make_unique<TtdRegionMemory>(r, writable));
            view = views.all.back().get();
        }
        views.current.push_back(view);
        regions.push_back(view);
    }
}
}  // namespace

namespace DeviceMemory
{

std::vector<IDeviceMemoryRegion*> Regions(EmulatorContext* context)
{
    std::vector<IDeviceMemoryRegion*> regions;
    if (context && context->pPortDecoder)
    {
        context->pPortDecoder->CollectMemoryRegions(regions);
        // The machine's CMOS clock, whichever board carries it: "rtc.cmos" (and the ZX-Evo "evo-avr.eeprom")
        if (Ds12887* rtc = RtcAccess::Find(context))
            rtc->CollectMemoryRegions(regions);
        AppendTtdRegions(context, regions);
    }
    return regions;
}

void Forget(EmulatorContext* context)
{
    std::lock_guard<std::mutex> lock(g_viewsMutex);
    g_views.erase(context);
}

IDeviceMemoryRegion* Find(EmulatorContext* context, const std::string& name, std::string* error)
{
    const std::string wanted = Lower(name);
    const std::vector<IDeviceMemoryRegion*> regions = Regions(context);
    for (IDeviceMemoryRegion* region : regions)
        if (Answers(*region, wanted))
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
        StateNode aliases = StateNode::Array();
        const std::string text = region->Aliases() ? region->Aliases() : "";
        for (size_t start = 0; start < text.size();)
        {
            const size_t end = std::min(text.find(',', start), text.size());
            if (end > start)
                aliases.push(text.substr(start, end - start));
            start = end + 1;
        }
        n["aliases"] = aliases;
        n["ttd_region"] = region->TtdRegion() ? StateNode(std::string(region->TtdRegion())) : StateNode();
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
    ret["note"] = "memory a device owns outside the CPU's RAM / ROM pages, and every memory time travel records: "
                  "read / write by name or alias (/memory/region/{name}, CLI memory region, region_read / region_write)";
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
