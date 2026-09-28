#pragma once

#include "stdafx.h"

class Z80;
class Memory;
class EmulatorContext;

/// ULA contention pattern: delay at each t-state offset within an 8T character cell.
/// Source: https://faqwiki.zxnet.co.uk/wiki/Contended_memory
static const uint8_t contentionPattern[8] = {6, 5, 4, 3, 2, 1, 0, 0};

/// +2A/+3 gate array pattern (over a 129 T window, see ComputeContentionDelay): the same cells, the waits one step earlier (MAME, ZXMAK2, BizHawk, ZX-M8XXX,
/// Spectral, xpeccy-plus, zxsp, jnext all agree on the shape; the first wait falls on the 128K's first
/// contended T in four of them, which is what is modelled)
static const uint8_t gateArrayContentionPattern[8] = {1, 0, 7, 6, 5, 4, 3, 2};

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

/// The machine's contention rule (UlaContention::GetRule): which video logic makes the CPU wait
enum class ContentionRule : uint8_t
{
    None,       // no contention (Pentagon, Scorpion, Profi, ATM and the other clones; TS-Config)
    Ula48,      // Ferranti ULA, 48K: 6,5,4,3,2,1,0,0 on page 5; I/O contention on even ports
    Ula128,     // Ferranti ULA, 128K / +2: the same pattern on odd pages; 128K I/O rules
    GateArray,  // Amstrad gate array, +2A / +3: 1,0,7,6,5,4,3,2 on pages 4-7 in any slot; no I/O contention
};

const char* ContentionRuleName(ContentionRule rule);

/// Kinds of bus cycles the contention statistics count (the unreal-z80 wait-hook vocabulary, reduced to what
/// the core distinguishes): bytes read at PC (opcode, prefixes, operands), data reads, data writes, ports,
/// internal cycles
enum ContentionAccessKind : uint8_t
{
    CONTENTION_FETCH = 0,
    CONTENTION_READ,
    CONTENTION_WRITE,
    CONTENTION_IO,
    CONTENTION_IDLE,  // internal (no-MREQ) T-states with a contended address on the bus (Ferranti ULA only)
    CONTENTION_KINDS
};

/// Contended accesses and the wait T-states they cost, per kind
struct ContentionCounters
{
    uint64_t accesses[CONTENTION_KINDS] = {};
    uint64_t waitT[CONTENTION_KINDS] = {};
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
/// Machines without contention never ask: Core::SelectMemoryInterface gives them the plain memory
/// interfaces and no I/O contention rule, so neither memory nor port accesses reach this component.
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

    /// +2A/+3 Amstrad gate array instead of the Ferranti ULA: the 1,0,7,6,5,4,3,2 pattern, no I/O
    /// contention, RAM pages 4-7 contended in any slot, and a floating bus only on ports
    /// 0000 xxxx xxxx xx01 (see GetGateArrayFloatingBus)
    void SetGateArray(bool gateArray) { _gateArray = gateArray; }
    bool IsGateArray() const { return _gateArray; }

    /// The machine's rule, from the flags above (None while contention is disabled for the model)
    ContentionRule GetRule() const
    {
        if (!_contentionEnabled)
            return ContentionRule::None;
        if (_gateArray)
            return ContentionRule::GateArray;
        return _raster.tstatesPerLine >= 228 ? ContentionRule::Ula128 : ContentionRule::Ula48;
    }

    /// Last byte of a contended memory access (+2A/+3 floating bus between screen fetches)
    uint8_t GetLatchedByte() const { return _lastContendedByte; }

    // ── Statistics (debugger only) ───────────────────────────
    // Counted by the debug contended memory interface and by in / out while the debugger is on; the fast
    // interfaces never touch them. Every access to a contended slot or port counts, with its wait (0 outside
    // the screen fetch window)

    void CountAccess(ContentionAccessKind kind, uint8_t wait)
    {
        _statsFrame.accesses[kind]++;
        _statsFrame.waitT[kind] += wait;
        _statsTotal.accesses[kind]++;
        _statsTotal.waitT[kind] += wait;
    }

    /// Frame boundary (Core::CPUFrameCycle while the debugger is on): the current frame's counters become
    /// the last frame's
    void OnFrameStart()
    {
        _statsLastFrame = _statsFrame;
        _statsFrame = ContentionCounters{};
    }

    void ResetStatistics()
    {
        _statsFrame = ContentionCounters{};
        _statsLastFrame = ContentionCounters{};
        _statsTotal = ContentionCounters{};
    }

    const ContentionCounters& GetStatisticsCurrentFrame() const { return _statsFrame; }
    const ContentionCounters& GetStatisticsLastFrame() const { return _statsLastFrame; }
    const ContentionCounters& GetStatisticsTotal() const { return _statsTotal; }

    /// Whether a RAM page is contended on this machine: odd pages behind the 128K ULA (1/3/5/7; on the
    /// 48K the only mapped odd page is 5), pages 4-7 behind the gate array
    bool IsRamPageContended(uint16_t page) const { return _gateArray ? page >= 4 : (page & 1) != 0; }

    // ── Core API (hot path) ──────────────────────────────────

    /// Memory contention delay for a contended VRAM access (0x4000-0x7FFF) starting at the CPU's current T.
    /// Returns 0 when contention is disabled or outside paper area.
    /// Not inline because it accesses Z80 members.
    uint8_t GetContentionDelay() const;

    /// Wait of a memory access to contended RAM that starts at T-state `t` (0 outside the fetch window).
    /// The contended memory interfaces call it for every access to a contended slot
    /// (Memory::MemoryReadContended); they are selected only while contention is enabled
    uint8_t DelayAt(uint32_t t) const { return ComputeContentionDelay(t); }

    /// Is this Z80 address in contended memory on the current model?
    /// 48K/128K: the slots holding an odd RAM page (page 5 at 0x4000, and 0xC000 when page 1/3/5/7 is
    /// mapped there) - those pages share the ULA's memory bus. +2A/+3: the slots holding pages 4-7,
    /// in the all-RAM layouts #0000 included.
    /// Always false when contention is disabled (Pentagon etc.).
    ///
    /// Diagnostics and tests; the contended memory interfaces test the slot flag alone (IsSlotContended),
    /// since they run only while contention is enabled. One flag per slot, cached by Memory
    /// (UpdateSlotContention) whenever a slot is mapped - a cold path hit only on paging port writes.
    inline bool IsAddressContended(uint16_t addr) const
    {
        return _contentionEnabled && _slotContended[addr >> 14];
    }

    /// Cache update: Memory calls it whenever it maps a slot (cold path: paging port writes). ROM is never
    /// contended; a RAM page per IsRamPageContended. On the +2A/+3 the all-RAM layouts put contended pages
    /// into any slot, #0000 included
    void SetSlotContended(uint8_t slot, bool contended) { _slotContended[slot & 0x03] = contended; }
    bool IsSlotContended(uint8_t slot) const { return _slotContended[slot & 0x03]; }

    /// +2A/+3: the gate array keeps the last byte of a contended memory access on its bus; the floating
    /// bus shows it between screen fetches. Called by the contended memory interfaces on contended accesses
    /// only - opcode fetches included
    inline void LatchContendedByte(uint8_t value) { _lastContendedByte = value; }

    /// Raster timing snapshot (test/diagnostic access)
    const ContentionRaster& GetRaster() const { return _raster; }

    /// I/O contention of the Ferranti ULA (48K / 128K / +2), the four patterns of the "Contended I/O" rules
    /// (faqwiki; FUSE, MAME):
    ///
    ///   high byte in contended memory | ULA port (A0 = 0) | the 4 T I/O cycle
    ///   no                            | no                | N:4
    ///   no                            | yes               | N:1, C:3
    ///   yes                           | yes               | C:1, C:3
    ///   yes                           | no                | C:1, C:1, C:1, C:1
    ///
    /// "High byte in contended memory" is the port address read as a memory address against the current
    /// mapping (the slot flags: page 5 at #4000, on the 128K also an odd page at #C000). C:n = the ULA's wait
    /// at that T, then n T. The +2A/+3 gate array and the machines without contention: none (0).
    ///
    /// IoWaitBeforeIorq: the wait at the cycle's first T (`cycleStartT`), before IORQ (C:1 or N:1).
    uint8_t IoWaitBeforeIorq(uint16_t port, uint32_t cycleStartT) const;

    /// IoWaitAfterIorq: the waits of the rest of the cycle from the IORQ T (`iorqT`, after the first wait):
    /// C:3 on a ULA port, three C:1 checkpoints when only the high byte is contended, none otherwise. The
    /// cycle's own 3 T are the caller's; the returned value is the waits alone
    uint8_t IoWaitAfterIorq(uint16_t port, uint32_t iorqT) const;

    /// Floating bus: returns the VRAM byte the ULA is currently fetching.
    /// Returns 0xFF outside the paper area. Note: this works even when
    /// contention is disabled (e.g. Pentagon), because the floating bus
    /// is a physical property of the shared data bus, not of contention.
    uint8_t GetFloatingBus() const;

    /// +2A/+3 floating bus: only ports 0000 xxxx xxxx xx01 (#0FFD and its mirrors) while #7FFD paging is
    /// not locked. During a screen fetch the fetched byte, between fetches the last contended memory byte,
    /// bit 0 forced to 1 in both (BizHawk, xpeccy-plus; jnext keeps bit 0 of the latched byte). Any other
    /// port or a locked machine reads #FF
    uint8_t GetGateArrayFloatingBus(uint16_t port) const;

    /// Scorpion floating bus: attribute byte of the screen cell the video
    /// controller is fetching right now. On Scorpion a read from ANY
    /// non-existent port returns this value - the attribute latch keeps
    /// driving the data bus after the fetch (programmer's manual, port #FF);
    /// border/blanking reads return #FF. Not gated by the FloatBus config
    /// toggle: unlike the ZX "unstable bus" emulation this is documented
    /// port-map behavior of the model (the ProfROM monitor times its
    /// raster waits on this stream).
    uint8_t GetFloatingBusAttribute() const;

private:
    /// Shared computation for both memory and IO contention.
    uint8_t ComputeContentionDelay(uint32_t t) const;

    /// Shared floating-bus position math: resolves the VRAM cell being
    /// fetched at the current T-state (with the 4T fetch pipeline offset).
    /// Returns false outside the fetch area (border/blanking).
    bool LocateFloatingBusCell(uint32_t& y, uint32_t& cellIndex) const;

    /// The VRAM byte the video logic is fetching right now; false between fetches and outside the paper
    bool FetchedByte(uint8_t& value) const;

    /// Attribute address of a screen cell (0x5800-0x5AFF interleaved layout).
    static uint16_t AttributeCellAddress(uint32_t y, uint32_t cellIndex);

    // ── State ────────────────────────────────────────────────

    /// Contention starts this many T before the first paper pixel is displayed:
    /// 48K INT + 14335 vs 14340, 128K INT + 14361 vs 14366 (classic contention
    /// onset; display per Xpeccy ULA.48/128 and MiSTer ula.sv)
    static constexpr uint32_t kContentionLeadT = 5;

    Z80* _cpu = nullptr;
    Memory* _memory = nullptr;
    EmulatorContext* _context = nullptr;

    bool _contentionEnabled = false;
    bool _gateArray = false;
    bool _slotContended[4] = { false, true, false, false };  // 48K / 128K power-on layout: page 5 at #4000
    uint8_t _lastContendedByte = 0xFF;
    UlaFetchType _fetchType = ULA_FERRANTI;

    ContentionRaster _raster;

    ContentionCounters _statsFrame;
    ContentionCounters _statsLastFrame;
    ContentionCounters _statsTotal;
};
