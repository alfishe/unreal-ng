#pragma once

/// @file profiwaitoverlay.h
/// @brief The Profi boards' CPU wait states (docs/inprogress/2026-10-01-profi-v3-v5: research-profi-v5-wait.md,
/// research-profi-v3-turbo-floatbus.md, design.md sections 6.2 and 6.4).
///
/// The two boards arbitrate the DRAM between the CPU and the video differently:
///
/// | Board, clock | Who waits | Rule (extra CPU clocks per access) |
/// |:--|:--|:--|
/// | v5, 3.5 MHz | RAM opcode fetch / read / write in the paper fetch window | 1 on every other T of the window; the parity is the board's power-on phase ([PROFI] WaitPhase); none in the border; none with the jumper SB8 in its PENTAGON position ([PROFI] WaitConfig=pentagon) |
/// | v5, 3.5 MHz | ROM reads | 0, or 1 with [PROFI] RomWait=1 (the 200 ns one-shot ends inside the Z80's WAIT setup window) |
/// | v5, 7 MHz | RAM accesses | 1 in the border, 2 in the paper (an approximation: the board's turbo arbitration depends on the slot ring's history, research-profi-v5-wait.md section 3) |
/// | v5, 7 MHz | ROM reads | 1 (the one-shot) |
/// | v3, 3.5 MHz | nobody | the video never holds the CPU |
/// | v3, 7 MHz | RAM opcode fetch / read / write, paper or border alike | 2 when T1 starts on an even 7 MHz clock, 3 on an odd one |
///
/// ROM, I/O, interrupt acknowledge and refresh never wait on either board ("RAM" is the RAM select: RAM paged at
/// #0000 waits, ROM never does). Worked example, v3 turbo: a NOP stream in RAM settles on 6 clocks per NOP, so code
/// in RAM gains only 1.33x; code in ROM runs 2x.
///
/// Installed by PortDecoder_Profi::SyncWaits only while a rule can apply (v5 Spectrum mode, or turbo), and it adds
/// waits only with the `contention` feature on, so a Profi without them, and every other machine, pays nothing.

#include <cstdint>

#include "emulator/memory/hostbusoverlay.h"

class Core;
class Memory;
class Z80;
struct EmulatorState;

class ProfiWaitOverlay final : public HostBusOverlay
{
public:
    /// The board's setup, from [PROFI] and the model
    struct Setup
    {
        bool v5 = true;
        uint8_t phase = 0;          ///< v5 power-on phase 0..3 (1 = no waits at 3.5 MHz)
        bool pentagonJumper = false;///< v5 SB8 in its PENTAGON position: no waits at 3.5 MHz
        bool romWait = false;       ///< v5: the ROM one-shot gives 1 wait at 3.5 MHz too
        uint32_t paperStartT = 0;   ///< frame T (3.5 MHz) of the first paper fetch of line 0
        uint8_t zq3MHz = 20;        ///< v5: the third crystal (the hi-res clock is ZQ3 / 4, turbo ZQ3 / 2)
    };

    ProfiWaitOverlay(Core* core, Z80* cpu, Memory* memory, const EmulatorState* state, const Setup& setup);

    /// Whether any rule applies right now (the clock, the hi-res mode, the contention switch)
    bool WaitsApply() const;

    /// Extra 3.5 MHz T for a v5 RAM access whose T1 starts at frame T `t` (the 3.5 MHz rule)
    uint32_t V5RamWait(uint32_t t) const;
    /// Whether frame T `t` is inside the paper fetch window (192 lines x 128 T)
    bool InFetchWindow(uint32_t t) const;
    /// Extra CPU clocks for an access starting at CPU clock `start` of the frame (Z80::AccessStartClock: 7 MHz
    /// clocks in turbo, 3.5 MHz T otherwise; the hi-res clock in hi-res)
    uint32_t ExtraClocks(bool rom, uint32_t start) const;
    /// Hi-res (#DFFD bit 7): `start` in CPU clocks at the hi-res clock (EmulatorState::hw_turbo_ratio_applied
    /// sevenths of 3.5 MHz)
    uint32_t HiresExtraClocks(bool rom, uint32_t start) const;
    /// Signed ns from frame instant `tNs` to the nearest v5 video request edge, false when no request runs there
    /// (outside the fetch windows, unless #7FFD bit 5 runs them all line long)
    bool DistanceToRequestNs(double tNs, double& d) const;

    const Setup& GetSetup() const { return _setup; }

    uint8_t onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged) override;
    uint8_t onReadM1(uint16_t addr, uint8_t normal, bool romPaged) override;
    void onWrite(uint16_t addr, uint8_t value, bool romPaged) override;

private:
    void Wait(uint16_t addr, bool write);

    Core* _core = nullptr;
    Z80* _cpu = nullptr;
    Memory* _memory = nullptr;
    const EmulatorState* _state = nullptr;
    Setup _setup;
};
