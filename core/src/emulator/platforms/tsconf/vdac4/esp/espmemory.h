#pragma once

/// @file espmemory.h
/// @brief Guest memory of the VDAC4 ESP32-S3 HLE: the map and the heaps.
///
/// Design: docs/inprogress/2026-11-15-tsconf-vdac4/design.md §8.
///
/// Map (one instance per board):
///
///   0x3C000000 + 8 MB   PSRAM, D-bus view (heap_caps MALLOC_CAP_SPIRAM; app ELFs, big buffers)
///   0x42000000 + 8 MB   the same PSRAM bytes, I-bus view (code runs here; data reads work too)
///   0x3FC88000 + 416 KB internal SRAM (heap_caps MALLOC_CAP_INTERNAL; task stacks, small blocks)
///   0x40378000 + 416 KB the same SRAM bytes, I-bus view (SRAM1's direct mirror; module code in code_ram runs here)
///   0x40000000 + 4 KB   trap page: no bytes; reaching a PC here is a host call
///   kEspRomBase         the HLE ROM (newlib, libm, libgcc, shims), a private RW copy
///   0x600C4000 + 4 KB   EXTMEM (cache control registers), the values the firmware's boot left; apps read them
///
/// The heaps are first fit over a host-side table, addresses 16-byte aligned, so the same calls give the same
/// addresses on every host (TTD, determinism). heap_caps_malloc with default caps follows the firmware's
/// sdkconfig: blocks of up to 4096 bytes come from internal SRAM (SPIRAM_MALLOC_ALWAYSINTERNAL), larger ones
/// from PSRAM, each falling back to the other.

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace vdac4
{

/// First-fit heap over [base, base + size), 16-byte granules; the block table lives on the host
class EspHeap
{
public:
    EspHeap(uint32_t base, uint32_t size);

    /// 0 when it does not fit
    uint32_t Alloc(uint32_t size);
    /// false when `address` is not a block start
    bool Free(uint32_t address);
    /// Size the block was allocated with (rounded to 16), 0 when not a block start
    uint32_t BlockSize(uint32_t address) const;
    /// The block at `address` to `size` bytes where it is: smaller always, larger when the space after it is
    /// free. False: not a block start, or no room to grow
    bool Resize(uint32_t address, uint32_t size);
    bool Contains(uint32_t address) const { return address >= _base && address - _base < _size; }
    uint32_t FreeBytes() const;
    void Clear();
    /// The block table (TTD): start -> size
    const std::map<uint32_t, uint32_t>& Blocks() const { return _used; }
    void SetBlocks(std::map<uint32_t, uint32_t> blocks) { _used = std::move(blocks); }

private:
    uint32_t _base;
    uint32_t _size;
    std::map<uint32_t, uint32_t> _used;  ///< start -> size
};

class EspMemory
{
public:
    static constexpr uint32_t kPsramDbus = 0x3C000000;
    static constexpr uint32_t kPsramIbus = 0x42000000;
    static constexpr uint32_t kPsramSize = 8u << 20;
    static constexpr uint32_t kSramBase = 0x3FC88000;
    static constexpr uint32_t kSramSize = 416u << 10;
    /// SOC_DIRAM_IRAM_LOW: the I-bus address of kSramBase (SOC_DIRAM_DRAM_LOW)
    static constexpr uint32_t kSramIbus = 0x40378000;
    /// EXTMEM register page: DCACHE_CTRL at +0x00, ICACHE_CTRL at +0x60 (ESP32-S3 TRM)
    static constexpr uint32_t kExtmemBase = 0x600C4000;
    static constexpr uint32_t kExtmemSize = 0x1000;
    /// sdkconfig of the firmware: data cache 32 KB, 64-byte lines (0.1.155), enabled
    static constexpr uint32_t kDcacheCtrl = 0x11;
    /// instruction cache 16 KB, 8 ways, 32-byte lines, enabled
    static constexpr uint32_t kIcacheCtrl = 0x0B;
    static constexpr uint32_t kTrapPage = 0x40000000;
    static constexpr uint32_t kTrapPageSize = 0x1000;
    /// zm_core_t function slot i is the trap kZmTrapBase + 4 * i (ROM traps sit below)
    static constexpr uint32_t kZmTrapBase = 0x40000800;
    /// heap_caps default caps: up to this many bytes from internal SRAM
    static constexpr uint32_t kAlwaysInternal = 4096;

    /// heap_caps capability bits the apps and the firmware use
    static constexpr uint32_t kCapSpiram = 1u << 10;   // MALLOC_CAP_SPIRAM
    static constexpr uint32_t kCapInternal = 1u << 11; // MALLOC_CAP_INTERNAL

    EspMemory();

    /// Power-on: PSRAM and SRAM zero (the firmware's heap after boot), ROM image fresh, heaps empty
    void Reset();

    /// Host view of [address, address + size) when it lies inside one region; nullptr otherwise
    uint8_t* Bytes(uint32_t address, uint32_t size);
    const uint8_t* Bytes(uint32_t address, uint32_t size) const;

    uint32_t Read32(uint32_t address) const;
    void Write32(uint32_t address, uint32_t value);
    /// A NUL-terminated guest string (at most `max` bytes); empty when unmapped
    std::string ReadString(uint32_t address, size_t max = 256) const;
    /// Copy a host string with its NUL into guest memory
    void WriteString(uint32_t address, const std::string& text);

    /// heap_caps_malloc: caps 0 = default (size rule), else SPIRAM / INTERNAL; the other kind as fallback
    uint32_t HeapCapsMalloc(uint32_t size, uint32_t caps);
    void HeapCapsFree(uint32_t address);
    /// heap_caps_get_allocated_size
    uint32_t AllocatedSize(uint32_t address) const;
    /// heap_caps_realloc without a move (EspHeap::Resize in the block's heap)
    bool Resize(uint32_t address, uint32_t size);

    EspHeap& Psram() { return _psramHeap; }
    EspHeap& Sram() { return _sramHeap; }
    const EspHeap& Psram() const { return _psramHeap; }
    const EspHeap& Sram() const { return _sramHeap; }

    std::vector<uint8_t>& PsramBytes() { return _psram; }
    std::vector<uint8_t>& SramBytes() { return _sram; }
    std::vector<uint8_t>& RomBytes() { return _rom; }
    std::vector<uint8_t>& ExtmemBytes() { return _extmem; }

    bool IsTrap(uint32_t pc) const { return pc >= kTrapPage && pc - kTrapPage < kTrapPageSize; }

private:
    std::vector<uint8_t> _psram;
    std::vector<uint8_t> _sram;
    std::vector<uint8_t> _rom;
    std::vector<uint8_t> _extmem;
    EspHeap _psramHeap;
    EspHeap _sramHeap;
};

}  // namespace vdac4
