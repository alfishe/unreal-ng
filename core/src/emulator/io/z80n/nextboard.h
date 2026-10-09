#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "emulator/io/z80n/nextcopper.h"
#include "emulator/io/z80n/nextsprites.h"
#include "emulator/io/z80n/nextvideoregs.h"
#include "emulator/io/z80n/z80nengine.h"

class NextMemory;
class NextInterruptSource;

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
    /// NR #8E: the Spectrum 128K memory mapping as the paging ports #7FFD / #DFFD / #1FFD carry it
    virtual void WriteMemoryMapping(uint8_t value) = 0;
    virtual uint8_t ReadMemoryMapping() const = 0;
    /// NR #09 bit 3 = 1: the DivMMC's sticky MAPRAM is cleared
    virtual void ClearDivMmcMapram() = 0;
    /// Between two instructions: the DMA moves its bytes
    virtual void StepDma() {}
    /// NR #06 / #08 / #09 changed (the sound's AY / YM, turbosound, DAC, stereo)
    virtual void AudioConfigChanged() {}
    /// NR #2C / #2D / #2E: the DAC's NextREG mirrors
    virtual void DacMirrorWrite(uint8_t, uint8_t) {}
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
    static constexpr uint8_t kRegAltRom = 0x8C;
    static constexpr uint8_t kRegMemoryMapping = 0x8E;
    static constexpr uint8_t kRegPeripheral2 = 0x08;
    static constexpr uint8_t kRegPeripheral4 = 0x09;  ///< bit 6: contention disable
    static constexpr uint8_t kRegPeripheral3 = 0x0A;  ///< bit 5: the SD card select swap
    static constexpr uint8_t kRegMmu0 = 0x50;  ///< #50-#57: MMU slot 0-7
    static constexpr uint8_t kMachineIdNext = 10;
    static constexpr uint8_t kRegCoreVersionSub = 0x0E;
    static constexpr uint8_t kCoreVersion = 0x32;     ///< 3.02 ...
    static constexpr uint8_t kCoreVersionSub = 0x03;  ///< ... .03: the FPGA sources the register map follows

    explicit NextBoard(NextMemory* memory) : _memory(memory)
    {
        _copper.SetWriter([this](uint8_t reg, uint8_t value) { Write(reg, value); });
    }
    void SetMachine(INextMachine* machine) { _machine = machine; }
    void SetInterrupts(NextInterruptSource* interrupts) { _interrupts = interrupts; }

    /// Power-on (hard) or reset (soft) state of the registers, config mode and boot ROM
    void Reset(bool hard);

    void SelectRegister(uint8_t reg) { _selected = reg; }
    uint8_t SelectedRegister() const { return _selected; }
    uint8_t ReadSelected() const
    {
        if (_readCounts)
            (*_readCounts)[_selected]++;
        return Read(_selected);
    }
    void WriteSelected(uint8_t value) { Write(_selected, value); }

    uint8_t Read(uint8_t reg) const;
    void Write(uint8_t reg, uint8_t value);
    /// Log every register write into `log` (null: off). The log is the caller's
    void SetWriteLog(std::vector<NextRegWrite>* log, const uint16_t* pc) { _log = log; _pc = pc; }
    /// Count the registers read (null: off): which registers the software looks at
    void SetReadCounts(std::map<uint8_t, uint32_t>* counts) { _readCounts = counts; }
    /// The raw stored byte of a register (reports, tests)
    uint8_t Stored(uint8_t reg) const { return _regs[reg]; }

    /// The `next_regs` report: one line per register of the table with its value and reset
    std::string DescribeRegisters() const;

    void WriteNextReg(uint8_t reg, uint8_t value) override { Write(reg, value); }
    void AfterInstruction() override;

    /// Machine type (NR #03 bits 2:0): 0 config mode, 1 48K, 2 128K, 3 +3, 4 Pentagon
    /// The bare personality's machine type (no firmware chooses it): 1 48K, 2 128K, 3 +3, 4 Pentagon
    void SetMachineType(uint8_t type);
    NextSprites& Sprites() { return _sprites; }
    NextCopper& Copper() { return _copper; }
    /// The CPU mapping of Layer 2 (port #123B, NR #12 / #13) into the memory's slot table
    void RefreshLayer2Mapping();
    NextVideoRegs& Video() { return _video; }
    const NextVideoRegs& Video() const { return _video; }
    uint8_t MachineType() const { return _regs[kRegMachineType] & 7; }
    /// The frame family in effect (NR #03 bits 6:4 once written, else the machine type)
    uint8_t Timing() const { return _timing; }
    bool SdSwap() const { return (_regs[kRegPeripheral3] & 0x20) != 0; }

private:
    NextMemory* _memory;
    NextVideoRegs _video;
    NextSprites _sprites;
    NextCopper _copper;
    NextInterruptSource* _interrupts = nullptr;
    INextMachine* _machine = nullptr;
    uint8_t _selected = 0;
    uint8_t _regs[256] = {};
    std::map<uint8_t, uint32_t>* _readCounts = nullptr;
    std::vector<NextRegWrite>* _log = nullptr;
    const uint16_t* _pc = nullptr;
    uint8_t _timing = 2;
    bool _resetPending = false;
    bool _resetHard = false;
};
