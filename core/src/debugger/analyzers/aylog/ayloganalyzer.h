#pragma once

#include "debugger/analyzers/ianalyzer.h"
#include "emulator/sound/chips/soundchip_ay8910.h"  // AYLogRecord

#include <cstdint>
#include <string>
#include <vector>

// Forward declarations
class AnalyzerManager;
class EmulatorContext;

/// AYLogAnalyzer: the analyzer face of the machine's AY history (emulator/sound/chips/ayhistory.h), the
/// register-write log (#FFFD register select / #BFFD data write) with the OUT instruction PC, frame counter and
/// intra-frame tacts. Activation turns the history to a ring of the analyzer's capacity, deactivation turns it off;
/// the history planes (`history ay`, /history/{device}) see the same records.
///
/// Intended automation flow: activate, let the program play, then read entries
/// to reconstruct the register-write timeline (who wrote what and when).
///
/// Capacity changes and clears should happen while the emulator is paused (the WebAPI activation path does).
class AYLogAnalyzer : public IAnalyzer
{
public:
    static constexpr size_t DEFAULT_CAPACITY = 4096;
    static constexpr size_t MIN_CAPACITY = 16;
    static constexpr size_t MAX_CAPACITY = 65536;

    explicit AYLogAnalyzer(EmulatorContext* context);
    ~AYLogAnalyzer() override = default;

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

    /// Whether the AY history records (this analyzer active, or the history configured on a history plane)
    bool isRecording() const;

    size_t getEntryCount() const;
    size_t getCapacity() const { return _capacity; }
    size_t getDroppedCount() const;

    /// Snapshot of entries in chronological order
    /// @param offset Skip this many oldest entries
    /// @param limit Maximum entries to return (0 = unlimited)
    std::vector<AYLogRecord> getEntries(size_t offset = 0, size_t limit = 0) const;

    /// endregion </Query API>

private:
    /// The machine's AY history; nullptr without an AY
    AYHistory* History() const;

    EmulatorContext* _context;
    std::string _uuid;
    size_t _capacity = DEFAULT_CAPACITY;
};
