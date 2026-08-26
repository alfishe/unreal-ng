#pragma once
#include "stdafx.h"

#include <atomic>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <vector>

#include "common/ringbuffer.h"

/// ============================================================================
/// PORT DIAGNOSTIC RECORDER (PDR)
/// ============================================================================
/// Structured, low-overhead ring buffer of port I/O events for diagnosing
/// port-decode and peripheral-behavior bugs (TurboSound chip-select drops,
/// Beta128 gating, mask collisions, ghost reads).
///
/// Runtime-gated by FeatureManager feature "porttrace" (alias "pt") — there is
/// NO compile-time gate. With the feature off the recorder is not instantiated
/// and the hot path costs a single cached-bool test in PortDecoder.
///
/// Design: docs/inprogress/2026-08-24-diagnostic-observability/
/// ============================================================================

/// region <Types>

/// Device attribution for a trace event, resolved from the decoded port
enum class PortDeviceId : uint8_t
{
    None           = 0x00,  // No handler found (unmapped port, returned 0xFF)
    ULA_FE         = 0x01,  // Port #FE — keyboard, border, beeper, tape
    Memory_7FFD    = 0x02,  // Port #7FFD — memory paging
    Memory_1FFD    = 0x03,  // Port #1FFD — +3 extended paging
    AY_FFFD        = 0x04,  // Port #FFFD — AY register select / TurboSound chip-select
    AY_BFFD        = 0x05,  // Port #BFFD — AY data write
    WD1793_Status  = 0x06,  // Port #1F — FDC Status/Command register
    WD1793_Track   = 0x07,  // Port #3F — FDC Track register
    WD1793_Sector  = 0x08,  // Port #5F — FDC Sector register
    WD1793_Data    = 0x09,  // Port #7F — FDC Data register
    Beta128_System = 0x0A,  // Port #FF — Beta128 system (DRQ/INTRQ, drive select)
    Covox          = 0x0B,  // Port #FB — COVOX/SOUNDRIVE DAC
    Memory_DFFD    = 0x0C,  // Port #DFFD — Profi extended paging
    Custom         = 0x0E,  // Other registered PortDevice
};

/// Flag bits packed into PortTraceEvent::flags
namespace PortTraceFlags
{
constexpr uint8_t kDirectionOut      = 1u << 0;  // 0=IN, 1=OUT
constexpr uint8_t kWasDecoded        = 1u << 1;  // A hardware device responded (_lastPortDecoded)
constexpr uint8_t kHadHandler        = 1u << 2;  // PortDevice existed in _portDevices map
constexpr uint8_t kBeta128Gated      = 1u << 3;  // Beta128 port blocked because CF_TRDOS was clear
constexpr uint8_t kHandledInline     = 1u << 4;  // Decoder handled it directly (not via PeripheralPortIn/Out)
constexpr uint8_t kCfTrdosActive     = 1u << 5;  // CF_TRDOS state at event time
constexpr uint8_t kViaLegacyBasePath = 1u << 6;  // Captured on legacy base-class DecodePortIn/Out path
}  // namespace PortTraceFlags

/// Decode-rule index sentinel values (PortTraceEvent::decodeRuleIndex)
namespace PortTraceRule
{
constexpr uint8_t kNoMatch     = 0xFF;  // No decode rule matched (unmapped)
constexpr uint8_t kBdiFallback = 0xFE;  // Resolved by the BDI #1F/#3F/#5F/#7F fallback decode
constexpr uint8_t kNoTable     = 0xFD;  // Model has no mask/match table (if-chain decoder)
}  // namespace PortTraceRule

/// One structured record per Z80 I/O operation. 24 bytes.
/// Authoritative layout: docs/.../use-cases.md
struct PortTraceEvent
{
    uint64_t timestamp = 0;       // Absolute T-state: frame_counter * tStatesPerFrame + t-in-frame
    uint32_t frameNumber = 0;     // Emulator frame counter
    uint16_t rawPort = 0;         // Full 16-bit address bus value seen by the Z80
    uint16_t decodedPort = 0;     // Port after model-specific decoding (0x0000 = unmapped)
    uint16_t pc = 0;              // Program counter of the IN/OUT instruction
    uint8_t  value = 0;           // Data byte read or written
    uint8_t  decodeRuleIndex = PortTraceRule::kNoMatch;  // Which decode rule fired (see PortTraceRule)
    PortDeviceId deviceId = PortDeviceId::None;          // Which peripheral this belongs to
    uint8_t  flags = 0;           // PortTraceFlags bitfield

    bool operator==(const PortTraceEvent& other) const
    {
        return timestamp == other.timestamp && frameNumber == other.frameNumber &&
               rawPort == other.rawPort && decodedPort == other.decodedPort && pc == other.pc &&
               value == other.value && decodeRuleIndex == other.decodeRuleIndex &&
               deviceId == other.deviceId && flags == other.flags;
    }

    bool isOut() const           { return flags & PortTraceFlags::kDirectionOut; }
    bool wasDecoded() const      { return flags & PortTraceFlags::kWasDecoded; }
    bool hadHandler() const      { return flags & PortTraceFlags::kHadHandler; }
    bool wasBeta128Gated() const { return flags & PortTraceFlags::kBeta128Gated; }
    bool wasHandledInline() const{ return flags & PortTraceFlags::kHandledInline; }
    bool cfTrdosActive() const   { return flags & PortTraceFlags::kCfTrdosActive; }
};

/// Result of a table-based decodePort() lookup: resolved port + which rule matched
struct DecodeResult
{
    uint16_t port = 0x0000;                          // 0x0000 = no match / unmapped
    uint8_t ruleIndex = PortTraceRule::kNoMatch;     // Index into the model's mask/match table
};

/// Filled by each model's DecodePortIn/Out from its decode locals and passed to
/// OnPortInComplete/OnPortOutComplete so the recorder captures decode attribution
/// that only exists as locals inside the subclass dispatch
struct PortDecodeDisposition
{
    uint16_t decodedPort = 0x0000;                      // 0x0000 = unmapped
    uint8_t decodeRuleIndex = PortTraceRule::kNoMatch;  // See PortTraceRule sentinels
    bool wasDecoded = false;        // A hardware device actually responded
    bool wasBeta128Gated = false;   // Beta128 port dropped because CF_TRDOS clear
    bool wasHandledInline = false;  // Handled by decoder switch, not PeripheralPortIn/Out
    bool viaLegacyBasePath = false; // Came through base-class DecodePortIn/Out
};

/// Ring buffer behavior when full
enum class PortTraceOverflowMode : uint8_t
{
    Ring,          // Evict oldest events (default) — keeps the most recent window
    StopWhenFull,  // Stop capturing when full — keeps the start of the run (boot debugging)
};

enum class PortTraceSessionState : uint8_t
{
    Stopped,
    Capturing,
    Paused,
};

/// One include/exclude filter rule. All set conditions must match (AND).
/// A rule with a single condition behaves as a simple rule; multiple
/// conditions form a compound rule. See recording-control.md.
struct PortTraceFilterRule
{
    std::optional<uint16_t> decodedPort;
    std::optional<uint16_t> rawPort;
    std::optional<PortDeviceId> device;
    std::optional<bool> directionOut;                        // false=IN, true=OUT
    std::optional<std::pair<uint16_t, uint16_t>> pcRange;    // inclusive [lo, hi]
    std::optional<std::pair<uint8_t, uint8_t>> valueRange;   // inclusive [lo, hi]
    bool unmappedOnly = false;                               // Match only decodedPort == 0

    bool matches(const PortTraceEvent& event) const;
};

/// Two-layer filter: include (OR of AND-compound rules; empty = include all)
/// then exclude (any match = reject). Exclude always wins.
struct PortTraceFilterSet
{
    std::vector<PortTraceFilterRule> include;
    std::vector<PortTraceFilterRule> exclude;

    bool matches(const PortTraceEvent& event) const;
};

/// Export file format for saveToFile()
enum class PortTraceExportFormat : uint8_t
{
    JSON,    // "unreal-ng-porttrace-v1": session + decode_rules + device_map + compact events
    CSV,     // Comment-header metadata + one row per event (hex ports/values)
    Binary,  // "PTRC" v1: 32-byte header + decode-rule table + raw little-endian events
    BinaryCompressed,  // "PTR2" v2: columnar delta/xor transform + one zstd frame
                       // (typically 50-100x smaller than v1 — timestamps dominate the
                       // entropy and their deltas are near-constant)
};

/// One mask/match decode rule, exported into trace headers so saved traces are
/// self-describing (offline tools never hardcode per-model masks)
struct PortTraceDecodeRule
{
    uint16_t mask = 0;
    uint16_t match = 0;
    uint16_t port = 0;
};

/// Session metadata written into every exported trace. Assembled by
/// PortDecoder::getPortTraceSessionInfo() (the recorder itself has no
/// knowledge of the emulator context).
struct PortTraceSessionInfo
{
    std::string emulatorId;                     // Emulator UUID
    std::string modelName;                      // "Pentagon", "Spectrum128", ...
    uint32_t tStatesPerFrame = 0;               // Timing base for absolute timestamps
    std::vector<PortTraceDecodeRule> decodeRules;  // Model decode table (empty for if-chain decoders)
};

/// Frame-scoped rolling counters for quick diagnostics without the full ring
/// buffer (debugger status panel reads once per frame). Counters reset when
/// the frame number advances.
struct PortActivitySummary
{
    uint32_t frameNumber = 0;      // Frame these counters belong to
    uint16_t inCount = 0;          // IN operations this frame
    uint16_t outCount = 0;         // OUT operations this frame
    uint16_t unmappedInCount = 0;  // INs with no decode (decodedPort == 0)
    uint16_t unmappedOutCount = 0; // OUTs with no decode
    uint16_t beta128GatedCount = 0;// Accesses blocked by the TR-DOS gate

    void onEvent(uint32_t frame, bool isOut, const PortDecodeDisposition& disp);
    void reset(uint32_t frame);
};

/// endregion </Types>

/// Structured port I/O event recorder. Single producer (emulator thread via
/// PortDecoder hooks), occasional consumers (CLI/WebAPI/tests).
///
/// Locking is honest, not lock-free: RingBuffer<T> takes a unique_lock on a
/// shared_mutex per push; the filter is read under a shared_lock. Uncontended
/// single-producer cost is tens of nanoseconds and only paid while capturing.
class PortDiagnosticRecorder
{
public:
    // Default ring capacity. Unfiltered capture on a busy program produces
    // 50k-200k events/s, so a small buffer wraps within a second or two.
    // 1M events = 24 MB — cheap for an opt-in diagnostics feature and holds
    // roughly 5-20 s of unfiltered capture (much more with capture filters).
    static constexpr size_t kDefaultCapacity = 1048576;

    PortDiagnosticRecorder() : _events(std::make_unique<RingBuffer<PortTraceEvent>>(kDefaultCapacity)) {}
    explicit PortDiagnosticRecorder(size_t capacity)
        : _capacity(capacity ? capacity : kDefaultCapacity),
          _events(std::make_unique<RingBuffer<PortTraceEvent>>(capacity ? capacity : kDefaultCapacity))
    {
    }

    /// region <Session control>
    void start();    // Arm capture; clears the buffer
    void stop();     // Disarm capture; data preserved
    void pause();    // Suspend capture; data preserved
    void resume();   // Resume from paused
    void clear();    // Purge buffer (any state)

    PortTraceSessionState getSessionState() const { return _sessionState.load(std::memory_order_acquire); }
    bool isCapturing() const { return getSessionState() == PortTraceSessionState::Capturing; }
    /// True when capture auto-stopped because the buffer filled in StopWhenFull mode
    bool wasAutoStopped() const { return _autoStopped.load(std::memory_order_acquire); }
    /// endregion </Session control>

    /// region <Configuration (only while stopped)>
    bool setCapacity(size_t events);                    // false if capturing/paused or events == 0
    bool setOverflowMode(PortTraceOverflowMode mode);   // false if capturing/paused
    size_t capacity() const { return _capacity; }
    PortTraceOverflowMode overflowMode() const { return _overflowMode; }
    /// endregion </Configuration>

    /// region <Filtering (safe to call while capturing)>
    void setFilter(const PortTraceFilterSet& filter);
    void addIncludeRule(const PortTraceFilterRule& rule);
    void addExcludeRule(const PortTraceFilterRule& rule);
    void clearIncludeRules();
    void clearExcludeRules();
    void clearAllRules();
    PortTraceFilterSet getFilter() const;

    // Convenience presets (clear all rules, then apply)
    void presetAll();       // Capture everything
    void presetAyOnly();    // Include #FFFD + #BFFD
    void presetFdcOnly();   // Include WD1793 registers + Beta128 system
    void presetNoFdc();     // Exclude WD1793 registers + Beta128 system
    void presetOutsOnly();  // Include direction=OUT
    void presetInsOnly();   // Include direction=IN
    void presetUnmapped();  // Include only unmapped events
    void presetNoFe();      // Exclude #FE (skip high-frequency ULA keyboard/border traffic)
    void presetSound();     // Include AY_FFFD + AY_BFFD + Covox devices
    void presetPaging();    // Include Memory_7FFD + Memory_1FFD + Memory_DFFD devices
    /// endregion </Filtering>

    /// region <Hot path>
    /// Push one event if capturing and the filter accepts it.
    /// Called from PortDecoder hooks on the emulator thread.
    void record(const PortTraceEvent& event);
    /// endregion </Hot path>

    /// region <Retrieval>
    std::vector<PortTraceEvent> getAll() const { return _events->getAll(); }
    std::vector<PortTraceEvent> getSince(uint64_t timestamp) const { return _events->getSince(timestamp); }
    std::vector<PortTraceEvent> getLast(size_t count) const;
    size_t eventCount() const { return _events->size(); }
    uint64_t totalProduced() const { return _events->totalEventsProduced(); }
    uint64_t totalEvicted() const { return _events->totalEventsEvicted(); }
    /// Events rejected by the filter while capturing (not counted as produced)
    uint64_t totalFiltered() const { return _totalFiltered.load(std::memory_order_relaxed); }
    /// endregion </Retrieval>

    /// region <Export>
    /// Save the buffered events to a file. Implemented in the core (not in
    /// transports) so every interface produces identical output.
    bool saveToFile(const std::string& path, PortTraceExportFormat format,
                    const PortTraceSessionInfo& info) const;

    /// Load a binary trace file (PTRC v1 or compressed PTR2 v2) back into
    /// events + session metadata (tpf and decode rules; model/emulator id are
    /// not stored in binary traces). Decompression lives here in the core so
    /// transports (WebAPI readfile endpoint) can serve compressed traces to
    /// clients that have no zstd of their own.
    static bool loadFromFile(const std::string& path, PortTraceSessionInfo& outInfo,
                             std::vector<PortTraceEvent>& outEvents);

    /// Human-readable one-line description of the active filter
    std::string describeFilter() const;
    /// endregion </Export>

    /// Resolve device attribution from a decoded port
    static PortDeviceId ResolveDeviceId(uint16_t decodedPort);

    /// Enum name for a device id ("AY_FFFD", "None", ...)
    static const char* DeviceIdToString(PortDeviceId id);

private:
    std::atomic<PortTraceSessionState> _sessionState{PortTraceSessionState::Stopped};
    std::atomic<bool> _autoStopped{false};
    std::atomic<uint64_t> _totalFiltered{0};

    size_t _capacity = kDefaultCapacity;
    PortTraceOverflowMode _overflowMode = PortTraceOverflowMode::Ring;
    std::unique_ptr<RingBuffer<PortTraceEvent>> _events;

    // Filter: replaced wholesale under unique_lock, read under shared_lock in record().
    // Deliberately a plain shared_mutex (not atomic<shared_ptr>) — portable and honest;
    // filter changes are rare control-plane operations.
    mutable std::shared_mutex _filterMutex;
    PortTraceFilterSet _filter;
};
