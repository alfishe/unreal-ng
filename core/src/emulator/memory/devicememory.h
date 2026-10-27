#pragma once

#include <cstdint>
#include <string>
#include <vector>

class EmulatorContext;

/// @file devicememory.h
/// @brief Memory a device owns outside the CPU's RAM / ROM pages, reached by name on every
/// automation interface (Sprinter automation audit G5: the Sprinter's 256 KB video RAM).
///
/// The list has two sources. A machine declares regions through PortDecoder::CollectMemoryRegions (writes take
/// the device's path, with its side effects). Every memory the time-travel engine records is listed too, by the
/// engine's region name ("neogs.ram", "moonsound.wave", "vdac2.ram_g", "evo.flash", ...), unless a declared
/// region already covers it (IDeviceMemoryRegion::TtdRegion): those are read straight from the device, and
/// written straight to the bytes where that is safe (memory-spaces design, docs/inprogress/2026-10-08-memory-spaces).
/// Names are canonical dotted ones; the older short names stay as aliases ("vram" = "sprinter.vram").
///
/// A machine lists its regions through PortDecoder::CollectMemoryRegions; the automation
/// interfaces only call the functions below, so the WebAPI (/memory/regions,
/// /memory/region/{name}, /memory/page/{name}/{n}), the CLI (`memory region ...`), Lua and
/// Python (memory_regions / region_read / region_write / region_save / region_load) and MCP
/// read the same bytes and write through the same path.
///
/// Writes go through the device's own write path (Write below), so its side effects happen
/// as for the guest: on the Sprinter a palette byte refreshes its pen, a mode byte with the
/// INT pattern moves the frame INT, the beam catches up first. They are tool edits:
/// Emulator::EditMemoryFromTool marks them for TTD like any debugger write.
///
/// Worked example (Sprinter): region "vram", 262 144 bytes in 16 pages of 16 KB; pen 1029
/// (text paper 5) is at offset 5 x 1024 + #3E0 + 4 x 4 = #017F0: writing #00,#00,#A8 there
/// makes it blue (R, G, B in this order).
class IDeviceMemoryRegion
{
public:
    virtual ~IDeviceMemoryRegion() = default;

    /// Canonical lower-case name ("sprinter.vram"), the key on every interface
    virtual const char* Name() const = 0;
    /// Other names it answers to, comma-separated ("vram"); empty when none
    virtual const char* Aliases() const { return ""; }
    /// The time-travel engine's region holding these bytes ("sprinter.vram"), or null
    virtual const char* TtdRegion() const { return nullptr; }
    /// What it is and how it is laid out (one or two sentences)
    virtual const char* Description() const = 0;
    virtual uint32_t Size() const = 0;
    /// The device's page size for /memory/page/{name}/{n} (16 KB unless the device says otherwise)
    virtual uint32_t PageSize() const { return 16 * 1024; }
    virtual bool Writable() const { return true; }
    /// How a write reaches the device (shown in the region list)
    virtual const char* WritePath() const { return "the device's own write path"; }

    /// A side-effect-free read
    virtual uint8_t Read(uint32_t offset) const = 0;
    /// A write through the device's own path (offset < Size())
    virtual void Write(uint32_t offset, uint8_t value) = 0;
};

namespace DeviceMemory
{
/// The machine's regions now (empty on a machine without any)
std::vector<IDeviceMemoryRegion*> Regions(EmulatorContext* context);
/// By name or alias (case-insensitive); null with `error` when there is none
IDeviceMemoryRegion* Find(EmulatorContext* context, const std::string& name, std::string* error = nullptr);

/// [offset, offset + length) of the region; false with `error` when out of range
bool Read(EmulatorContext* context, const std::string& name, uint32_t offset, uint32_t length, std::vector<uint8_t>& out,
          std::string& error);
/// Through the device's write path, as a tool edit (TTD marker); false with `error` when out of range or read-only
bool Write(EmulatorContext* context, const std::string& name, uint32_t offset, const std::vector<uint8_t>& bytes,
           const char* source, std::string& error);
/// Save [offset, offset + length) to a file (length 0 = to the end); false with `error`
bool Save(EmulatorContext* context, const std::string& name, const std::string& path, uint32_t offset, uint32_t length,
          std::string& error);
/// Load a file into the region at `offset` through Write (the whole file, clipped at the end); false with `error`
bool Load(EmulatorContext* context, const std::string& name, const std::string& path, uint32_t offset, size_t& written,
          std::string& error);

/// Drop what the registry keeps for this machine (its time-travel region views); the context is going away
void Forget(EmulatorContext* context);

/// Parse "1234", "0x4D2", "#4D2" (offsets and lengths on every interface); false on garbage
bool ParseNumber(const std::string& text, uint32_t& value);
/// Bytes from "A8 00 FF" / "A800FF" (hex, spaces optional); false on an odd digit count or a bad digit
bool ParseHexBytes(const std::string& text, std::vector<uint8_t>& out);
}  // namespace DeviceMemory
