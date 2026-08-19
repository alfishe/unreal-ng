#pragma once

#include "stdafx.h"

class Z80;
class Memory;
class EmulatorContext;

/// ULA contention pattern: delay at each t-state offset within an 8T character cell.
/// Source: https://faqwiki.zxnet.co.uk/wiki/Contended_memory
static const uint8_t contentionPattern[8] = {6, 5, 4, 3, 2, 1, 0, 0};

///
/// Video controller fetch architecture — determines floating bus behavior.
///
/// ULA_FERRANTI:
///   Used by ZX-Spectrum 48K, 128K, +2, +3.
///   The Ferranti ULA chip has an internal 8-T-state state machine:
///   4 T-states fetch pixel + attribute bytes, 4 T-states shift pixel data
///   through the internal shift register (bus idle = 0xFF during shift).
///   Pipeline: fetch 2 cells ahead, shift 4T per cycle.
///
/// ULA_DISCRETE_LOGIC:
///   Used by Pentagon, Scorpion, Profi, and other Soviet clones built from
///   discrete TTL logic (counters + multiplexers instead of a custom ULA chip).
///   The video address counter runs continuously — there are NO shift/dead cycles.
///   Every T-state during the paper area has VRAM data on the bus.
///   Pipeline: per 4T cell, phases 0-1 = pixel byte, phases 2-3 = attribute byte.
///
enum UlaFetchType : uint8_t
{
    ULA_FERRANTI = 0,        // Standard ZX Spectrum ULA
    ULA_DISCRETE_LOGIC = 1   // Pentagon / Scorpion / Soviet clones
};

/// Compact snapshot of raster timing needed by the contention engine.
/// Pushed by Screen::SetVideoMode() whenever video mode changes.
struct ContentionRaster
{
    uint32_t configFrameDuration = 0;
    uint32_t screenAreaStart = 0;
    uint32_t screenAreaEnd = 0;
    uint32_t tstatesPerLine = 0;
    uint32_t screenLineAreaStart = 0;
    uint32_t screenLineAreaEnd = 0;
};

///
/// Standalone ULA contention component.
///
/// Encapsulates all ZX Spectrum ULA-specific timing behaviors:
///   - Memory contention on contended VRAM (0x4000-0x7FFF)
///   - IO port contention (C:1 / C:3 patterns)
///   - Floating bus (video byte on undecoded port reads)
///
/// Fast bypass: when contention is disabled (Pentagon, Scorpion, etc.),
/// GetContentionDelay() and GetIOContentionDelay() return 0 in a single branch.
/// GetFloatingBus() still executes — many models without contention still
/// expose video bytes on undecoded port reads (e.g. Pentagon port #FF).
///
class UlaContention
{
public:
    UlaContention() = default;
    ~UlaContention() = default;

    /// Wire up dependencies. Called once after Core::Init().
    void SetDependencies(Z80* cpu, Memory* memory, EmulatorContext* context);

    /// Push new raster timing snapshot. Called by Screen::SetVideoMode().
    void UpdateRaster(const ContentionRaster& raster);

    /// Enable or disable contention for the current model.
    /// When false, all methods return instantly with 0 / 0xFF.
    void SetContentionEnabled(bool enabled) { _contentionEnabled = enabled; }
    bool IsContentionEnabled() const { return _contentionEnabled; }

    /// Set the video controller fetch architecture type.
    /// Controls floating bus phase behavior (8T Ferranti pipeline vs 4T discrete).
    void SetFetchType(UlaFetchType type) { _fetchType = type; }
    UlaFetchType GetFetchType() const { return _fetchType; }

    // ── Core API (hot path) ──────────────────────────────────

    /// Memory contention delay for a contended VRAM access (0x4000-0x7FFF).
    /// Returns 0 when contention is disabled or outside paper area.
    /// Not inline because it accesses Z80 members.
    uint8_t GetContentionDelay() const;

    /// Is this Z80 address in contended memory on the current model?
    /// 48K/128K: 0x4000-0x7FFF (page 5 behind the ULA).
    /// 128K additionally: 0xC000-0xFFFF when an odd RAM page (1/3/5/7) is
    /// mapped into bank 3 - those pages share the ULA's memory bus.
    /// Always false when contention is disabled (Pentagon etc.).
    ///
    /// HOT PATH: called from Z80::rd()/wd() on every memory access. Fully
    /// inline, touches only member fields (the bank-3 page parity is cached
    /// by SetBank3ContendedPage from Memory::SetRAMPageToBank3 - a cold path
    /// hit only on port 7FFD writes). Pentagon exits on the first branch.
    inline bool IsAddressContended(uint16_t addr) const
    {
        if (!_contentionEnabled)
            return false;
        if ((addr & 0xC000) == 0x4000)
            return true;
        return (addr & 0xC000) == 0xC000 && _bank3Contended;
    }

    /// Cache update: called by Memory::SetRAMPageToBank3 with the mapped
    /// page's parity (odd RAM pages are contended on 128K)
    void SetBank3ContendedPage(bool contended) { _bank3Contended = contended; }

    /// Raster timing snapshot (test/diagnostic access)
    const ContentionRaster& GetRaster() const { return _raster; }

    /// IO port contention delay. Follows the Contended_I/O rules
    /// (different for 48K vs 128K).
    uint8_t GetIOContentionDelay(uint16_t port) const;

    /// Floating bus: returns the VRAM byte the ULA is currently fetching.
    /// Returns 0xFF outside the paper area. Note: this works even when
    /// contention is disabled (e.g. Pentagon), because the floating bus
    /// is a physical property of the shared data bus, not of contention.
    uint8_t GetFloatingBus() const;

private:
    /// Shared computation for both memory and IO contention.
    uint8_t ComputeContentionDelay(uint32_t t) const;

    // ── State ────────────────────────────────────────────────

    Z80* _cpu = nullptr;
    Memory* _memory = nullptr;
    EmulatorContext* _context = nullptr;

    bool _contentionEnabled = false;
    bool _bank3Contended = false;  // 128K: odd RAM page mapped at 0xC000 (default page 0 = false)
    UlaFetchType _fetchType = ULA_FERRANTI;

    ContentionRaster _raster;
};
