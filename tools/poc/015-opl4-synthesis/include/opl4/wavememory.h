// libopl4 — default ROM+SRAM wave memory (core TDD §6).
//
// Allocates exactly romSizeBytes + ramSizeBytes (never a fixed 4 MiB block),
// exposes a 4 KB-granular dirty bitmap over the SRAM region for host-side
// TTD delta capture (TTD integration §5.3), and gives the PCM slot loop an
// inlinable non-virtual fast path (core TDD §11).
#pragma once

#include "opl4/iwavememory.h"

#include <memory>

namespace opl4
{

class WaveMemory final : public IWaveMemory
{
public:
    static constexpr uint32_t kAddressMask = 0x3FFFFF; // 22-bit space
    static constexpr uint32_t kDirtyPageSize = 4096;
    static constexpr uint32_t kMaxDirtyPages = 1024;   // 4 MiB / 4 KB

    WaveMemory() = default;
    ~WaveMemory() override = default;

    void Configure(uint32_t romSizeBytes, uint32_t ramSizeBytes);

    // IWaveMemory — hot path, deliberately non-virtual-shaped.
    uint8_t Read(uint32_t addr) override
    {
        addr &= kAddressMask;
        // Past the configured ROM+RAM window the bus floats high.
        return (addr < _ramEnd) ? _data[addr] : 0xFF;
    }

    void Write(uint32_t addr, uint8_t v) override
    {
        addr &= kAddressMask;
        if (addr < _romEnd)
            return; // writes into ROM are discarded
        if (addr >= _ramEnd)
            return; // writes past the configured RAM window are discarded
        _data[addr] = v;
        _dirtyPages[addr / kDirtyPageSize] = 1;
    }

    uint32_t RomEnd() const override { return _romEnd; }
    uint32_t RamEnd() const override { return _ramEnd; }

    // Host ROM image loading (D10 in the integration doc: zero-fill on miss).
    uint8_t* RomData() { return _data.get(); }
    const uint8_t* Data() const { return _data.get(); }
    uint32_t Size() const { return _ramEnd; }

    // Dirty bitmap: one bit per 4 KB page over the whole 22-bit space, packed
    // as one byte per page for simplicity. Cleared by the host after capture.
    const uint8_t* DirtyBitmap() const { return _dirtyPages.get(); }
    void ClearDirty();
    uint32_t DirtyPageCount() const;

    // SRAM fill for tests / restore.
    void WriteSram(uint32_t addr, const uint8_t* src, uint32_t len);

private:
    std::unique_ptr<uint8_t[]> _data; // value-initialized zero fill
    uint32_t _romEnd = 0;
    uint32_t _ramEnd = 0;
    std::unique_ptr<uint8_t[]> _dirtyPages;
};

} // namespace opl4
