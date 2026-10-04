#pragma once

/// @file flash29f040b.h
/// @brief 512 KB parallel NOR flash of the 29F040B class (ST M29F040B, AMD
/// Am29F040B) - the NeoGS card's firmware chip (neogs-tdd.md §3.8).
///
/// Command set (JEDEC, unlock cycles compare address bits A10..A0 only, so
/// `555`/`2AA` at any page work):
///   AA@555 55@2AA F0          reset (also plain F0 at any address)
///   AA@555 55@2AA 90          autoselect: offset 0 = manufacturer, 1 = device,
///                             2 = sector protection (0: unprotected)
///   AA@555 55@2AA A0, PA/PD   program one byte (bits can only go 1 -> 0)
///   AA@555 55@2AA 80 AA@555 55@2AA 30@SA [30@SA ...]   sector erase (64 KB)
///   AA@555 55@2AA 80 AA@555 55@2AA 10@555               chip erase
///
/// While an operation runs, every read returns status - at ANY address (the
/// NeoGS flasher polls #8000 of the current page, not the target):
///   DQ7 inverted data bit 7 (program) / 0 (erase), DQ6 toggles on every read,
///   DQ5 set on failure (programming a 0 back to 1), DQ3 set once the erase
///   command window has closed. A failed operation stays in status mode until
///   a reset command.
///
/// Busy times are fixed, chosen near the datasheet's typical values so polling
/// loops behave: 10 us per byte, 1 s per sector, 8 s for the whole chip, and a
/// 50 us window for further sector-erase commands. Time is supplied by the
/// owner in its own unit (`unitsPerSecond`), so the chip never reads a clock.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "debugger/ttd/engine/ttdregiontracker.h"

class Flash29F040B
{
public:
    static constexpr size_t SIZE = 512 * 1024;
    static constexpr size_t SECTOR_SIZE = 64 * 1024;
    static constexpr size_t SECTORS = SIZE / SECTOR_SIZE;

    enum class Vendor : uint8_t
    {
        ST,  // M29F040B: manufacturer 20, device E2 (fitted on NeoGS boards)
        AMD, // Am29F040B: manufacturer 01, device A4
    };

    /// Serialized state size (TTDSaveState / TTDLoadState), contents excluded
    static constexpr size_t STATE_SIZE = 32;

    explicit Flash29F040B(double unitsPerSecond, Vendor vendor = Vendor::ST);

    /// Contents: `size` bytes copied, the rest erased (#FF). Resets the command
    /// state machine and the modified flag.
    void load(const uint8_t* data, size_t size);
    const uint8_t* data() const { return _data.data(); }
    uint8_t* data() { return _data.data(); }

    /// Power-on / hardware reset: back to read-array, no operation in progress
    void reset();

    /// When false every write is ignored (FlashWrite=off)
    void setWritable(bool writable) { _writable = writable; }
    void setVendor(Vendor vendor) { _vendor = vendor; }

    /// Reads return the array contents (the owner may read _data directly).
    /// False while autoselect, a pending operation or a failed state is active.
    bool arrayMode() const { return _mode == Mode::Read; }

    uint8_t read(uint32_t offset, int64_t now);
    void write(uint32_t offset, uint8_t value, int64_t now);

    /// Completes a finished operation (reads and writes do this too); the owner
    /// calls it to find out whether array mode is back
    void update(int64_t now);

    /// Time when the running operation completes (INT64_MAX when idle)
    int64_t busyUntil() const;

    /// Contents changed since load() (persistence, neogs-tdd.md §5.8)
    bool modified() const { return _modified; }
    void clearModified() { _modified = false; }

    /// Time-travel engine region (Phase 1, Step 6): every change of the array
    /// is marked in @p tracker while it is set (null = not recording)
    void setTracker(ttd::TTDRegionTracker* tracker) { _tracker = tracker; }
    /// The array was changed at @p offset from outside the chip (a debugger edit)
    void markWritten(uint32_t offset)
    {
        if (_tracker)
            _tracker->Mark(offset);
    }

    void saveState(uint8_t* dst) const;
    void loadState(const uint8_t* src);

private:
    enum class Mode : uint8_t
    {
        Read,
        Unlock1,      // AA seen
        Unlock2,      // AA 55 seen
        Autoselect,
        ProgramSetup, // A0 seen: next write is the byte
        EraseSetup,   // 80 seen
        EraseUnlock1,
        EraseUnlock2,
        EraseWindow,  // sector erase accepted, more 30 commands welcome
        Busy,         // program or erase running
        Failed,       // DQ5 set; only reset leaves
    };

    void finishIfDone(int64_t now);
    uint8_t status(); // advances the DQ6 toggle
    void startProgram(uint32_t offset, uint8_t value, int64_t now);

    std::vector<uint8_t> _data;
    ttd::TTDRegionTracker* _tracker = nullptr;
    double _unitsPerSecond;
    Vendor _vendor;
    bool _writable = true;
    bool _modified = false;

    Mode _mode = Mode::Read;
    bool _erasing = false;
    uint8_t _toggle = 0;           // DQ6
    uint8_t _statusData = 0;       // DQ7 source (programmed byte)
    uint8_t _eraseSectors = 0;     // bit per sector
    int64_t _windowEnd = 0;
    int64_t _busyEnd = 0;
    // A pending byte program
    uint32_t _programOffset = 0;
    uint8_t _programValue = 0;
};
