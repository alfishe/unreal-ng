#pragma once

#include <cstdint>
#include <vector>

#include "emulator/io/z80n/z80nengine.h"

class NextMemory;

/// One register write as the board saw it (the NextREG golden sequence of the boot chain)
struct NextRegWrite
{
    uint8_t reg;
    uint8_t value;
    uint16_t pc;
};

/// What the board asks of the machine around it: a reset (NR #02)
class INextMachine
{
public:
    virtual ~INextMachine() = default;
    /// Runs between two instructions
    virtual void PerformReset(bool hard) = 0;
    /// NR #07: the CPU clock as a multiple of 3.5 MHz (1, 2, 4, 8)
    virtual void SetCpuSpeed(uint8_t ratio) = 0;
    /// NR #03: the frame family (NextTiming) the video logic follows from the next frame
    virtual void SetMachineTiming(uint8_t timing) = 0;
    /// NR #08 bit 6: the video memory contention is disabled
    virtual void SetContentionDisabled(bool disabled) = 0;
};

/// The Next board's register file (NEXTREG space, ports #243B select / #253B data): the identification registers
/// read back what the board answers, NR #02 / #03 / #04 are the reset, machine type (config mode) and config
/// mapping, #50-#57 are the memory's slot table, every other register stores its byte and reads it back until
/// the device behind it exists (N3 onwards). The CPU's NEXTREG instructions arrive through INextRegHost.
class NextBoard : public INextRegHost
{
public:
    static constexpr uint8_t kRegMachineId = 0x00;
    static constexpr uint8_t kRegCoreVersion = 0x01;
    static constexpr uint8_t kRegResetType = 0x02;
    static constexpr uint8_t kRegMachineType = 0x03;
    static constexpr uint8_t kRegConfigMapping = 0x04;
    static constexpr uint8_t kRegCpuSpeed = 0x07;
    static constexpr uint8_t kRegPeripheral2 = 0x08;  ///< bit 6: contention disable
    static constexpr uint8_t kRegPeripheral3 = 0x0A;  ///< bit 5: the SD card select swap
    static constexpr uint8_t kRegMmu0 = 0x50;  ///< #50-#57: MMU slot 0-7
    static constexpr uint8_t kMachineIdNext = 10;
    static constexpr uint8_t kRegCoreVersionSub = 0x0E;
    static constexpr uint8_t kCoreVersion = 0x32;     ///< 3.02 ...
    static constexpr uint8_t kCoreVersionSub = 0x03;  ///< ... .03: the FPGA sources the register map follows

    explicit NextBoard(NextMemory* memory) : _memory(memory) {}
    void SetMachine(INextMachine* machine) { _machine = machine; }

    /// Power-on (hard) or reset (soft) state of the registers, config mode and boot ROM
    void Reset(bool hard);

    void SelectRegister(uint8_t reg) { _selected = reg; }
    uint8_t SelectedRegister() const { return _selected; }
    uint8_t ReadSelected() const { return Read(_selected); }
    void WriteSelected(uint8_t value) { Write(_selected, value); }

    uint8_t Read(uint8_t reg) const;
    void Write(uint8_t reg, uint8_t value);
    /// Log every register write into `log` (null: off). The log is the caller's
    void SetWriteLog(std::vector<NextRegWrite>* log, const uint16_t* pc) { _log = log; _pc = pc; }
    /// The raw stored byte of a register (reports, tests)
    uint8_t Stored(uint8_t reg) const { return _regs[reg]; }

    void WriteNextReg(uint8_t reg, uint8_t value) override { Write(reg, value); }
    void AfterInstruction() override;

    /// Machine type (NR #03 bits 2:0): 0 config mode, 1 48K, 2 128K, 3 +3, 4 Pentagon
    /// The bare personality's machine type (no firmware chooses it): 1 48K, 2 128K, 3 +3, 4 Pentagon
    void SetMachineType(uint8_t type) { _regs[kRegMachineType] = static_cast<uint8_t>((_regs[kRegMachineType] & 0xF8) | (type & 7)); }
    uint8_t MachineType() const { return _regs[kRegMachineType] & 7; }
    /// The frame family in effect (NR #03 bits 6:4 once written, else the machine type)
    uint8_t Timing() const { return _timing; }
    bool SdSwap() const { return (_regs[kRegPeripheral3] & 0x20) != 0; }

private:
    NextMemory* _memory;
    INextMachine* _machine = nullptr;
    uint8_t _selected = 0;
    uint8_t _regs[256] = {};
    std::vector<NextRegWrite>* _log = nullptr;
    const uint16_t* _pc = nullptr;
    uint8_t _timing = 2;
    bool _resetPending = false;
    bool _resetHard = false;
};
