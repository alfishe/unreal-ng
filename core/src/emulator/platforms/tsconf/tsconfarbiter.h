#pragma once

#include <cstdint>

struct TsConfLine;
class TsConfEngine;

/// @file tsconfarbiter.h
/// @brief The TS-Conf DRAM arbiter as the 14 MHz CPU sees it (TSConf
/// implementation-plan phase 8, TIM-1; [V] dram/arbiter.v, z80/zmem.v).
///
/// DRAM runs one cycle per 7 MHz dot (4 fclk of 28 MHz, phases c0..c3), 448
/// per line. Inside a line's fetch window video works in blocks of 8 / 4 / 2
/// cycles and needs 1 (ZX), 1 (16C), 1 (256C) or 4 (TXT) of them
/// (video_mode.v:128-133). Every cycle's owner is decided at c3 of the cycle
/// before (arbiter.v:171-189):
///   block start, video_go: the CPU if it asks, else video;
///                          vid_rem = need if the CPU took it, else need - 1
///   inside a block: video when vid_rem == blk_rem (the CPU is refused:
///                   cpu_next = 0), else the CPU if it asks, else video while
///                   vid_rem > 0, else TSU / DMA / refresh
/// Video fetches as early as it can, so cpu_next only drops after CPU grants
/// used a block's spare cycles; the TSU and the DMA never delay the CPU
/// (dev_over_cpu = 0).
///
/// The CPU (zmem.v:141-207, 14 MHz): an access's request (dram_beg) comes at
/// the falling clock edge in T2, 3 fclk after T1. A read (M1 or data) stalls
/// until c3 of the first cycle G with cpu_next = 1 and then through c1 (M1) /
/// c2 (data read) of G + 1: wait = 4G + 4 - request + 2 (M1) / + 3 (read) -
/// with G = the request's own cycle that is zmem.v's M1 table +3..+6 (c3..c0)
/// and +4..+7 for data reads (the RTL releases reads at c2; the comment table
/// there says +2..+5). A write does not wait while cpu_next = 1. Outside a
/// read (writes, refresh, internal and I/O cycles) the CPU clock stops for
/// every cycle with cpu_next = 0 (stall14_cyc) - such a cycle right after a
/// grant costs the CPU 4 fclk.
///
/// Worked example (256C 320x200 window, blocks of 2, video needs 1): a write
/// granted the block's first cycle leaves vid_rem == blk_rem = 1 for its own
/// cycle, so the CPU clock stops for it: LD (HL),A takes 24 fclk, 20 outside
/// the window.
///
/// State: the cycle last decided and the block counters. None of it crosses a
/// frame (lines 0-31 fetch nothing), so TTD needs nothing; a frame's first
/// access starts from a clean arbiter.
class TsConfArbiter
{
public:
    enum class Access : uint8_t
    {
        M1,
        Read,
        Write,
    };

    /// The fetch schedule of a line (video_sync.v:237, video_mode.v:85-88)
    struct Fetch
    {
        bool active = false;   ///< a graphics line (vpix) with graphics on (!NOGFX)
        uint16_t h0 = 0;       ///< first block decision, DRAM cycle of the line
        uint16_t h1 = 0;       ///< no block decided from here on
        uint8_t length = 8;    ///< cycles per block
        uint8_t need = 1;      ///< video cycles per block
    };

    static constexpr uint32_t kLineCycles = 448;

    explicit TsConfArbiter(const TsConfEngine& engine) : _engine(engine) {}

    /// A CPU DRAM access whose request comes at fclk `request` of the frame:
    /// the fclks the CPU clock stops for it, including the cycles it was
    /// already frozen for since the previous access
    uint32_t CpuAccess(uint32_t request, Access kind);

    /// Forget the block state (reset, clock switch)
    void Reset();

    static Fetch FetchOf(const TsConfLine& set, uint32_t line);

private:
    bool CpuNext() const { return !(_blkRem > 0 && _vidRem == _blkRem); }
    /// The decision at c3 of cycle _cycle (owner of the next one)
    void Step(bool cpuRequest);
    /// The state at `cycle` when nobody asked since a block boundary
    void SeekClean(uint32_t cycle);

    const TsConfEngine& _engine;
    uint32_t _cycle = 0;    ///< the cycle the state describes
    uint8_t _blkRem = 0;
    uint8_t _vidRem = 0;
    bool _charged = false;  ///< _cycle's CPU cost is already counted (the last access owns it)
};
