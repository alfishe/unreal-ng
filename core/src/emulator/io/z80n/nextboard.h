#pragma once

#include <cstdint>

#include "emulator/io/z80n/z80nengine.h"

class NextMemory;

/// The Next board's register file (NEXTREG space, ports #243B select / #253B data) for the skeleton machine:
/// the identification registers read back what the board answers, the MMU registers #50-#57 are the memory's
/// slot table, every other register stores its byte and reads it back until the device behind it exists
/// (N3 onwards). The CPU's NEXTREG instructions arrive through INextRegHost.
class NextBoard : public INextRegHost
{
public:
    static constexpr uint8_t kRegMachineId = 0x00;
    static constexpr uint8_t kRegCoreVersion = 0x01;
    static constexpr uint8_t kRegResetType = 0x02;
    static constexpr uint8_t kRegMachineType = 0x03;
    static constexpr uint8_t kRegConfigMapping = 0x04;
    static constexpr uint8_t kRegCpuSpeed = 0x07;
    static constexpr uint8_t kRegMmu0 = 0x50;  ///< #50-#57: MMU slot 0-7
    static constexpr uint8_t kMachineIdNext = 10;
    static constexpr uint8_t kCoreVersion = 0x31;  ///< 3.1: what the register answers until a core is chosen

    explicit NextBoard(NextMemory* memory) : _memory(memory) {}

    void Reset();

    void SelectRegister(uint8_t reg) { _selected = reg; }
    uint8_t SelectedRegister() const { return _selected; }
    uint8_t ReadSelected() const { return Read(_selected); }
    void WriteSelected(uint8_t value) { Write(_selected, value); }

    uint8_t Read(uint8_t reg) const;
    void Write(uint8_t reg, uint8_t value);

    void WriteNextReg(uint8_t reg, uint8_t value) override { Write(reg, value); }

private:
    NextMemory* _memory;
    uint8_t _selected = 0;
    uint8_t _regs[256] = {};
};
