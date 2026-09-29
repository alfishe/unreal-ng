#pragma once

/// @file neogsmemory.h
/// @brief NeoGS card memory map (neogs-tdd.md §3.2; FPGA memmap/memmap.v,
/// ports.v:410-442).
///
/// Four 16 KB windows, each with a page byte (PG0-PG3):
///   bits 4:0 page in a 512 KB chip, 6:5 RAM chip, 7 second bank (4 MB boards)
///   -> RAM page = byte & 0xFF (4 MB) or & 0x7F (2 MB: bit 7 has no effect)
///   -> flash page = byte & 0x1F (32 pages mirror through the byte)
/// ROM mode (GSCFG0.NOROM = 0): windows 0, 2, 3 show flash, window 1 RAM.
/// RAM mode: all four RAM. Write protection (RAMRO = 1 and NOROM = 1): pages
/// whose bits 6:1 are 0 (RAM pages 0, 1, 128, 129) in any window.
///
/// MPAG (#00): normal PG2 = {d6..d0,0}, PG3 = {d6..d0,1}; EXPAG PG2 = {d6..d0,d7}.
/// MPAGEX (#10), EXPAG only: PG3 = {d6..d0,d7}.
///
/// Reads and writes go through window pointers rebuilt on every page or mode
/// change (the classic card's scheme); flash windows fall back to the chip
/// while it is not in read-array mode.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "emulator/io/flash/flash29f040b.h"

class NeoGSMemory
{
public:
    static constexpr size_t PAGE_SIZE = 0x4000;

    // GSCFG0 bits used here
    static constexpr uint8_t CFG_NOROM = 0x01;
    static constexpr uint8_t CFG_RAMRO = 0x02;
    static constexpr uint8_t CFG_EXPAG = 0x08;

    NeoGSMemory(size_t ramKB, Flash29F040B* flash);

    /// FPGA reset: PG0 = 0, PG1 = 3; PG2/PG3 have no reset clause
    void resetRegisters();
    /// Power-on: also PG2 = 0, PG3 = 2 (undefined on the board, fixed here so
    /// replays are deterministic), MPAG shadow 0, RAM cleared
    void powerOn();

    // Page registers
    void writeMpag(uint8_t value);
    void writeMpagEx(uint8_t value);
    void writePage(int window, uint8_t value);
    uint8_t page(int window) const { return _pg[window & 3]; }
    uint8_t lastMpag() const { return _mpag; }

    /// GSCFG0 (only NOROM, RAMRO and EXPAG matter to the memory map)
    void setConfig(uint8_t gscfg0);
    uint8_t config() const { return _cfg; }

    // CPU access (`now` in card units, for the flash chip's timing)
    uint8_t read(uint16_t addr, int64_t now)
    {
        const int window = addr >> 14;
        const uint8_t* p = _readPtr[window];
        if (p)
            return p[addr & (PAGE_SIZE - 1)];
        return readFlash(window, addr, now);
    }

    void write(uint16_t addr, uint8_t value, int64_t now)
    {
        const int window = addr >> 14;
        uint8_t* p = _writePtr[window];
        if (p)
            p[addr & (PAGE_SIZE - 1)] = value;
        else if (_windowFlash[window])
            writeFlash(window, addr, value, now);
        // else: write-protected RAM - dropped
    }

    /// Debugger/automation view: no side effects, flash shown as its array
    uint8_t peek(uint16_t addr) const;
    void poke(uint16_t addr, uint8_t value);

    /// Physical location of a CPU address: flash or RAM, byte offset
    bool isFlash(int window) const { return _windowFlash[window & 3]; }
    uint32_t physical(uint16_t addr) const;

    uint8_t* ram() { return _ram.data(); }
    const uint8_t* ram() const { return _ram.data(); }
    size_t ramSize() const { return _ram.size(); }
    size_t ramPages() const { return _ram.size() / PAGE_SIZE; }

    /// Re-point the windows (after a flash mode change or a state restore)
    void rebuild();

    // Raw register access for TTD
    void setPagesRaw(const uint8_t pages[4], uint8_t mpag, uint8_t cfg);

private:
    uint8_t readFlash(int window, uint16_t addr, int64_t now);
    void writeFlash(int window, uint16_t addr, uint8_t value, int64_t now);
    size_t ramPageIndex(uint8_t pageByte) const { return pageByte & _ramPageMask; }

    std::vector<uint8_t> _ram;
    Flash29F040B* _flash;
    uint8_t _ramPageMask;

    uint8_t _pg[4] = {0, 3, 0, 2};
    uint8_t _mpag = 0;
    uint8_t _cfg = 0x30;

    const uint8_t* _readPtr[4] = {};
    uint8_t* _writePtr[4] = {};
    bool _windowFlash[4] = {};
};
