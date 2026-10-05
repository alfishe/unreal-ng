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
/// there says +2..+5). A write does not wait while cpu_next = 1.
///
/// Refused cycles: once CPU grants used a block's spare cycles, cpu_next stays
/// 0 to the block's end, and the Z80 clock stops in each fclk of those cycles
/// in which the FPGA does not see a memory read on the pins (stall14_cyc =
/// memrd ? stall14_cycrd : !cpu_next; a read waits for its grant through
/// stall14_cycrd instead). A stopped fclk suppresses the clock edge at the end
/// of the next one (zclock.v), and a pin change is seen one fclk after its
/// edge (pin delay 1), so: the clock runs while MREQ + RD are seen, stops at
/// the first fclk they are not, lets the edge already on its way through, and
/// stays stopped to the end of the refused cycles unless that edge was a
/// read's T1 falling edge. CpuAccess opens such a window after the grant
/// (Refused) and simulates it fclk by fclk as the machine cycles that follow
/// show where the reads are (Settle, SettleBeforeM1, the next CpuAccess); the
/// stopped fclk are charged to the machine cycle whose clock edge they delay.
/// RTL simulation: tools/machines/tsconf/rtl-sim, CPU DRAM waits at 14 MHz.
///
/// Worked example (256C 320x200 window, blocks of 2, video needs 1): a write
/// granted the block's first cycle leaves vid_rem == blk_rem = 1 for its own
/// cycle, so the CPU clock stops for its 4 fclk: LD (HL),A takes 24 fclk, 20
/// outside the window. Depending on the write's phase they stretch the write
/// or the next M1 - by 1 fclk only if that M1's MREQ + RD arrive in the
/// cycle's second fclk.
///
/// State: the cycle last decided, the block counters and an open refused
/// window. None of it crosses a frame (lines 0-31 fetch nothing), so TTD needs
/// nothing; a frame's first access starts from a clean arbiter.
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

    /// A CPU DRAM access whose request comes at fclk `request` of the frame
    /// (T1 + 3): the fclks the CPU clock stops in it - the refused-cycle stops
    /// still open before its request, its own wait, and the refused-cycle stops
    /// that already fall into this machine cycle
    uint32_t CpuAccess(uint32_t request, Access kind);

    /// A machine cycle with T1 at fclk `t1` that takes no DRAM cycle (ROM, a
    /// cache hit, a write the window does not take): the refused-cycle stops
    /// that fall into it. Only while a refused window is open (Refusing)
    uint32_t Settle(uint32_t t1, Access kind);
    /// Before an opcode fetch with T1 at fclk `t1`: the stops that delayed it
    /// (they belong to the machine cycles before it)
    uint32_t SettleBeforeM1(uint32_t t1);
    /// A refused window is open: the CPU clock may still stop
    bool Refusing() const { return _refused.open; }

    /// Forget the block state and an open refused window (reset, clock switch)
    void Reset();

    static Fetch FetchOf(const TsConfLine& set, uint32_t line);

private:
    static constexpr uint8_t kMaxRuns = 2;  ///< RTL simulation: never more than 1 uncharged

    /// The refused cycles after a grant, simulated fclk by fclk. Edges are
    /// counted in "window coordinates": the fclk at whose end an edge would
    /// come without this window, i.e. the emulator's fclk minus `shift`
    struct Refused
    {
        bool open = false;
        bool stopped = false;  ///< the clock was stopped in period - 1
        uint8_t runs = 0;
        uint32_t period = 0;  ///< the next fclk to simulate (frame fclk)
        uint32_t end = 0;     ///< the first fclk with cpu_next = 1 again
        uint32_t edge = 0;    ///< the next Z80 clock edge
        uint32_t shift = 0;   ///< fclk charged since the window opened
        uint32_t runEdge[kMaxRuns] = {};  ///< stopped fclk not charged yet: the edge they delay
        uint32_t runFclk[kMaxRuns] = {};  ///< and how many
    };

    bool CpuNext() const { return !(_blkRem > 0 && _vidRem == _blkRem); }
    /// The decision at c3 of cycle _cycle (owner of the next one)
    void Step(bool cpuRequest);
    /// The state at `cycle` when nobody asked since a block boundary
    void SeekClean(uint32_t cycle);

    /// After a grant whose machine cycle has its T1 (as its later edges see it) at `t1`: open the window of the
    /// refused cycles that follow, if any, and charge what already falls into this machine cycle
    uint32_t OpenRefused(uint32_t t1, Access kind);
    /// The refused-cycle stops of a machine cycle with T1 at `t1` (emulator fclk) as far as it decides them,
    /// charged when they delay one of its clock edges (up to its shortest end: T1 + 8 for an M1, + 6 else)
    uint32_t SettleCycle(uint32_t t1, Access kind);
    /// Simulate on while the read state of the next edge is known (edges `readFrom`..`readTo` see a read, edges
    /// after `known` are not known yet)
    void Advance(uint32_t readFrom, uint32_t readTo, uint32_t known);
    /// Charge the stopped fclk that delay edges up to `limit` (window coordinates)
    uint32_t Charge(uint32_t limit);
    /// Window coordinates of emulator fclk `t1`, or false (and the window dropped) when the window is stale
    bool ToWindow(uint32_t t1, uint32_t& tw);

    const TsConfEngine& _engine;
    uint32_t _cycle = 0;    ///< the cycle the state describes
    uint8_t _blkRem = 0;
    uint8_t _vidRem = 0;
    Refused _refused;
};
