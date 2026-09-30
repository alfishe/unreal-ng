#pragma once

/// @file evoturbooverlay.h
/// @brief ZX-Evo BaseConf wait states at 14 MHz: the DRAM's two one-word caches
/// (docs/inprogress/2026-09-29-machine-waits/tdd.md section 3, research-zxevo.md section A).
///
/// At 14 MHz the CPU is faster than the DRAM. The FPGA keeps the last 16-bit word read by an opcode fetch
/// (the code word) and by a data read (the data word); a read of either word takes no wait, any other RAM read
/// waits for the DRAM: 2 or 3 clocks, by the parity of the clock the access starts on. In clocks at 14 MHz
/// (`t` = the access's start clock from the frame origin):
///
/// | access | rule |
/// |:--|:--|
/// | opcode fetch or data read, RAM window | hit on either word: 0; miss: `2 + (t & 1)`, the word is loaded |
/// | write, RAM window | 0; the matching word(s) become invalid |
/// | any access to a ROM window | 0; both words become invalid |
/// | I/O cycle, interrupt acknowledge | both words become invalid (the port decoder adds 3 for an external port) |
///
/// Worked example: a NOP stream in RAM starting at an even address and an even clock takes 6, 4, 6, 4 clocks
/// (the even address misses, the odd one hits the code word); `JR $` fetches from one word and takes no wait.
///
/// Installed by the ATM3 port decoder while the clock select says 14 MHz (PortDecoder_ATM3::SyncTurboWaits). It
/// keeps the cache words up to date always and adds waits only while the CPU runs at 14 MHz
/// (`hw_turbo_ratio_applied`: unreal-ng applies the ATM3's clock select at the next frame) and the `contention`
/// feature is on.

#include <cstdint>

#include "emulator/memory/hostbusoverlay.h"

class Core;
class Memory;
class Z80;
struct EmulatorState;

class EvoTurboOverlay final : public HostBusOverlay
{
public:
    /// The TTD blob's content (TTDEvoTurboCache): the two cache words and their valid flags
    struct CacheState
    {
        uint16_t codeWord = 0;  ///< address >> 1 of the last opcode fetch from RAM
        uint16_t dataWord = 0;  ///< address >> 1 of the last data read from RAM
        bool codeValid = false;
        bool dataValid = false;
    };

    EvoTurboOverlay(Core* core, Z80* cpu, Memory* memory, const EmulatorState* state)
        : _core(core), _cpu(cpu), _memory(memory), _state(state)
    {
    }

    /// The CPU runs at 14 MHz and the `contention` feature is on: the waits apply
    bool WaitsApply() const;

    uint8_t onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged) override;
    uint8_t onReadM1(uint16_t addr, uint8_t normal, bool romPaged) override;
    void onWrite(uint16_t addr, uint8_t value, bool romPaged) override;
    void onInterruptAcknowledge() override { Invalidate(); }

    /// An I/O cycle (the port decoder calls it for every port access) or a restore: both words become invalid
    void Invalidate()
    {
        _cache.codeValid = false;
        _cache.dataValid = false;
    }

    const CacheState& GetCacheState() const { return _cache; }
    void SetCacheState(const CacheState& state) { _cache = state; }

private:
    /// A read from RAM or ROM: the wait rule, then the word it leaves in the cache
    void Read(uint16_t addr, bool opcodeFetch);

    Core* _core = nullptr;
    Z80* _cpu = nullptr;
    Memory* _memory = nullptr;
    const EmulatorState* _state = nullptr;
    CacheState _cache;
};
