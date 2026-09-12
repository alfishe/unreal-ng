#pragma once

#include "debugger/analyzers/ianalyzer.h"
#include "emulator/sound/chips/soundchip_ay8910.h"  // AYLogRecord

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Forward declarations
class AnalyzerManager;
class EmulatorContext;

/// AYLogAnalyzer: ring-buffer logger of AY chip port writes (#FFFD register
/// select / #BFFD data write) with the OUT instruction PC, frame counter and
/// intra-frame tacts. On activation it installs a tap on the TurboSound
/// wrapper — the registered port device that sees every AY OUT for both of
/// its chips; on deactivation the tap is removed.
///
/// Intended automation flow: activate, let the program play, then read entries
/// to reconstruct the register-write timeline (who wrote what and when).
///
/// Thread-safety: entries are written from the emulation thread via the chip
/// tap and guarded by a mutex — AY writes are rare (<1K/sec), so locking per
/// write is acceptable. Capacity changes and clears should happen while the
/// emulator is paused (the WebAPI activation path does).
class AYLogAnalyzer : public IAnalyzer
{
public:
    static constexpr size_t DEFAULT_CAPACITY = 4096;
    static constexpr size_t MIN_CAPACITY = 16;
    static constexpr size_t MAX_CAPACITY = 65536;

    explicit AYLogAnalyzer(EmulatorContext* context);
    ~AYLogAnalyzer() override;

    // IAnalyzer interface
    std::string getName() const override { return "AYLogAnalyzer"; }
    std::string getUUID() const override { return _uuid; }

    void onActivate(AnalyzerManager* manager) override;
    void onDeactivate() override;

    /// region <Query API (cold path — called from WebAPI)>

    /// Drop all recorded entries and the dropped counter
    void clear();

    /// Change the ring capacity (clears recorded data; clamp to [MIN, MAX])
    void setCapacity(size_t capacity);

    /// Whether the tap is currently installed and recording
    bool isRecording() const { return _active.load(std::memory_order_relaxed); }

    size_t getEntryCount() const;
    size_t getCapacity() const { return _capacity; }
    size_t getDroppedCount() const { return _dropped; }

    /// Snapshot of entries in chronological order
    /// @param offset Skip this many oldest entries
    /// @param limit Maximum entries to return (0 = unlimited)
    std::vector<AYLogRecord> getEntries(size_t offset = 0, size_t limit = 0) const;

    /// endregion </Query API>

private:
    /// Chip tap callback (raw function pointer, no capture)
    static void onAYWrite(void* context, const AYLogRecord& record);

    EmulatorContext* _context;
    std::string _uuid;
    std::atomic<bool> _active{false};

    /// Ring buffer — written from the emulation thread, snapshotted via getEntries()
    mutable std::mutex _mutex;
    std::vector<AYLogRecord> _ring;
    size_t _capacity = DEFAULT_CAPACITY;
    size_t _writeIndex = 0;  // Next slot to write
    size_t _count = 0;       // Valid entries (<= capacity)
    size_t _dropped = 0;     // Entries overwritten by newer ones
};
