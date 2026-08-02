#pragma once

/// @file ttd_probe.h
/// @brief TTD access probe — lightweight hot-path check used by reverse
///        watchpoint replay (parent TDD §9.2 + §9.4).
///
/// Per TDD §9.2:
///   "The write probe is not a BreakpointManager breakpoint (those pause the
///    emulator and post notifications — 5.10). It is a dedicated lightweight
///    check compiled into the TTD replay hook in MemoryWriteDebug."
///
/// The probe lives in EmulatorContext (inline instance) so every hot-path
/// call site (MemoryWriteDebug, MemoryReadDebug, Z80 M1 cycle, DecodePortOut)
/// can check `armed` with one predictable branch. When armed, every matching
/// access appends to `hits` for later inspection by the FindLastAccess
/// orchestrator (control thread, after the silent replay completes).
///
/// Threading: the probe is owned by EmulatorContext. Arming/disarming and
/// hits extraction happen on the control thread (under pause, around the
/// silent RunTStates batch). Hot-path Matches + RecordHit calls happen on
/// the emulator thread during that batch. Because the emulator thread is
/// the only writer of `hits` during replay and the control thread doesn't
/// touch it until ExitReplayMode, no internal lock is needed — matching
/// the page store's threading model.

#include <atomic>
#include <cstdint>
#include <vector>

#include "ttd_checkpoint.h"  // TTDTimePoint

namespace ttd {

/// @brief Kind of access a TTDSearchQuery is interested in.
///
/// Values are stable across the automation contract (WebAPI JSON, Python
/// strings, CLI flags all use the lowercase names: "write", "read",
/// "execute", "io"). Do not renumber existing values.
enum class TTDAccessType : uint8_t
{
    Write   = 0,
    Read    = 1,
    Execute = 2,
    Io      = 3,
};

/// @brief Stable lowercase string for each TTDAccessType.
const char* TTDAccessTypeToString(TTDAccessType k);
TTDAccessType TTDAccessTypeFromString(const char* s);

/// @brief Reverse-search query (parent TDD §9.4 — "Conditional Variants").
///
/// The default-constructed query matches any Write access anywhere — the
/// most common reverse-watchpoint use case ("who corrupted this byte?").
struct TTDSearchQuery
{
    uint16_t addrFrom = 0;             ///< Inclusive lower bound
    uint16_t addrTo   = 0xFFFF;        ///< Inclusive upper bound
    TTDAccessType access = TTDAccessType::Write;

    bool     hasValueFilter = false;
    uint8_t  value = 0;                ///< Valid iff hasValueFilter

    bool     hasPcFilter = false;
    uint16_t pcFrom = 0;               ///< Valid iff hasPcFilter
    uint16_t pcTo   = 0xFFFF;

    /// Only matches at-or-before this globalT (absolute t-state). Default
    /// "no upper bound" is UINT64_MAX — caller typically substitutes the
    /// current globalT before invoking FindLastAccess.
    uint64_t beforeGlobalT = UINT64_MAX;
};

/// @brief Single search hit returned to the caller.
struct TTDSearchResult
{
    TTDTimePoint time;
    uint16_t     pc = 0;
    uint8_t      value = 0;
    uint8_t      physPage = 0;
    TTDAccessType access = TTDAccessType::Write;
};

/// @brief Lightweight access probe, armed during reverse-search replay.
///
/// The probe is a POD-with-vector — cheap to construct, cheap to reset.
/// EmulatorContext owns one inline instance; the FindLastAccess orchestrator
/// (control thread) calls Arm() before each silent-replay batch and
/// Disarm() / ExtractHits() afterwards.
class TTDAccessProbe
{
public:
    /// @brief Arm the probe with a query. Subsequent hot-path access calls
    /// (MemoryWriteDebug etc.) will RecordHit() on every match.
    void Arm(const TTDSearchQuery& q);

    /// @brief Disarm and clear hits. Safe to call when not armed.
    void Disarm();

    /// @brief Reset to the disarmed state. Equivalent to Disarm() but also
    /// clears the query.
    void Reset();

    /// @brief True iff the probe is currently armed (hot-path check).
    /// Inline-able; the atomic load compiles to a plain mov on x86/ARM64
    /// because Arm/Disarm impose the necessary fences via the surrounding
    /// pause handshake.
    inline bool IsArmed() const { return _armed.load(std::memory_order_relaxed); }

    /// @brief Read-only view of the query (valid only when armed).
    inline const TTDSearchQuery& Query() const { return _query; }

    /// @brief Hot-path check. Returns true iff the probe is armed AND the
    /// access matches the query's predicate. Called from every memory/cpu
    /// hook site.
    inline bool Matches(uint16_t addr, TTDAccessType kind, uint8_t value, uint16_t pc) const
    {
        if (!IsArmed())
            return false;
        if (kind != _query.access)
            return false;
        if (addr < _query.addrFrom || addr > _query.addrTo)
            return false;
        if (_query.hasValueFilter && value != _query.value)
            return false;
        if (_query.hasPcFilter && (pc < _query.pcFrom || pc > _query.pcTo))
            return false;
        return true;
    }

    /// @brief Record a hit. Called by hot-path hooks after a successful
    /// Matches() check. Reconstructs the TTDTimePoint from the EmulatorState
    /// on the caller side (the probe doesn't reach into EmulatorContext —
    /// keeps it dependency-free).
    inline void RecordHit(const TTDTimePoint& t, uint16_t pc, uint8_t value,
                          uint8_t physPage, TTDAccessType kind)
    {
        _hits.push_back(TTDSearchResult{t, pc, value, physPage, kind});
    }

    /// @brief Read-only view of recorded hits (control thread, post-replay).
    inline const std::vector<TTDSearchResult>& Hits() const { return _hits; }

    /// @brief Move the hits out (control thread clears the probe's copy).
    std::vector<TTDSearchResult> ExtractHits();

private:
    std::atomic<bool>      _armed{false};
    TTDSearchQuery         _query;
    std::vector<TTDSearchResult> _hits;
};

} // namespace ttd
