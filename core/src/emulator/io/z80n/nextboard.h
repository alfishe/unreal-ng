#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "emulator/io/z80n/nextcopper.h"
#include "emulator/io/z80n/nextkeyjoystick.h"
#include "emulator/io/z80n/nextregjournal.h"
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
    /// NR #0A bit 4 (the DivMMC's automap) or NR #83 bit 0 (its port) was written: automap_reset follows them at once
    virtual void DivMmcEnableChanged() {}
    /// Between two instructions: the DMA moves its bytes
    virtual void StepDma() {}
    /// Port #7FFD is locked (48K machine type, or the lock bit): NR #08 bit 7 reads its inverse
    virtual bool PagingLocked() const { return false; }
    /// NR #08 bit 7 written as 1 clears the lock bit of #7FFD
    virtual void UnlockPaging() {}
    /// The shadow ULA screen is bit 3 of #7FFD; NR #69 bit 6 reads and writes that bit
    virtual bool ShadowScreen() const { return false; }
    virtual void SetShadowScreen(bool) {}
    /// NR #B0 / #B1 / #B2 read: the extended keys and the MD pad's extra buttons (the keyboard and the pads are the machine's)
    virtual uint8_t ReadExtendedInput(uint8_t reg) const { (void)reg; return 0; }
    /// NR #06 / #08 / #09 changed (the sound's AY / YM, turbosound, DAC, stereo)
    virtual void AudioConfigChanged() {}
    /// NR #A8 / #A9 changed: the pins the board drives (the ESP's GPIO0)
    virtual void BoardPinsChanged() {}
    /// The Pi's I2S audio as the mixer takes it (zxnext.vhd pi_audio_L / pi_audio_R, 10 bit unsigned, #200 silence)
    virtual void PiAudioChanged(unsigned left, unsigned right) { (void)left; (void)right; }
    /// NR #2C / #2D / #2E: the DAC's NextREG mirrors
    virtual void DacMirrorWrite(uint8_t, uint8_t) {}
    /// An instruction ended (a safe place to stop the machine: not inside the picture's catch-up of the copper)
    virtual void OnInstructionDone() {}
    /// A RETN completed: the DivMMC drops its automap and the button latch
    virtual void OnRetn() {}
    /// NR #02 bit 2 written 1 (by the CPU or the copper): the DRIVE button's NMI. True when the board took it (NR #06 bit 4 on, no
    /// handler running); NR #02 bit 2 then reads 1 until it is written 0
    virtual bool GenerateDriveNmi() { return false; }
    /// NR #02 bit 3 written 1: the M1 button's NMI (the Multiface; NextZXOS starts snapshots with it)
    virtual bool GenerateMultifaceNmi() { return false; }
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
        _copper.SetWriter([this](uint8_t reg, uint8_t value) {
            _copperWrite = true;
            Write(reg, value);
            _copperWrite = false;
        });
    }
    void SetMachine(INextMachine* machine) { _machine = machine; }
    /// Called before a NextREG write of the CPU (not of the copper): the screen draws the lines the beam has passed with the old state
    /// What the header's other end drives on GPIO bank 0-3 (pins 0-7, 8-15, 16-23, 24-27) as bits; unset: low
    void SetGpioInput(std::function<uint8_t(unsigned bank)> input) { _gpioInput = std::move(input); }
    /// The 28 pins of the Pi header as the FPGA reads them (i_GPIO): an enabled output shows its value, the peripherals (SPI, UART, I2S)
    /// own their pins when NR #A0 / #A2 enable them (zxnext.vhd 2292-2350), the others what the header gives (the peer, else low; GPIO 2
    /// and 3, the I2C pair, are pulled up as on a Pi)
    uint32_t GpioPins() const;
    /// The I2S sample the Pi sends (pi_i2s_audio_L / R, 10 bit unsigned, #200 silence): what NR #A2 turns into the mixer's input, NR #2C-#2E
    /// and the EAR of port #FE
    void SetI2sSample(unsigned left, unsigned right);
    unsigned PiAudioLeft() const;
    unsigned PiAudioRight() const;
    bool PiEar() const { return _piEar; }
    void SetBeforeCpuWrite(std::function<void()> hook) { _beforeCpuWrite = std::move(hook); }
    void SetInterrupts(NextInterruptSource* interrupts) { _interrupts = interrupts; }
    /// The key joystick (NR #28 / #29 / #2B write its map; the decoder ANDs its keys into port #FE)
    const NextKeyJoystick& KeyJoystick() const { return _keyJoystick; }
    /// Every NextREG access of the program (a read through #253B, a write by NEXTREG, #253B or the copper) is told to the hook with the
    /// door it came through, before the register takes the write (the breakpoints; a tool's write and the board's own never are)
    using AccessHook = std::function<void(bool write, uint8_t reg, uint8_t value, NextRegSource source)>;
    void SetAccessHook(AccessHook hook) { _accessHook = std::move(hook); }
    /// The program's read of a register through port #253B (the select latch's register), told to the access hook
    uint8_t ReadSelectedByProgram() const
    {
        const uint8_t value = ReadSelected();
        if (_accessHook) [[unlikely]]
            _accessHook(false, _selected, value, NextRegSource::Port);
        return value;
    }

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
    void WriteSelected(uint8_t value)
    {
        SourceScope scope(this, NextRegSource::Port);
        Write(_selected, value);
    }

    uint8_t Read(uint8_t reg) const;
    void Write(uint8_t reg, uint8_t value);
    /// Log every register write into `log` (null: off). The log is the caller's
    void SetWriteLog(std::vector<NextRegWrite>* log, const uint16_t* pc) { _log = log; _pc = pc; }
    /// Count the registers read (null: off): which registers the software looks at
    void SetReadCounts(std::map<uint8_t, uint32_t>* counts) { _readCounts = counts; }
    /// The write journal (design-nextreg-journal.md). The clock fills frame, T and PC of an event (the board does not know the CPU)
    NextRegJournal& Journal() { return _journal; }
    const NextRegJournal& Journal() const { return _journal; }
    void SetJournalClock(std::function<void(uint32_t& frame, uint32_t& t, uint16_t& pc)> clock) { _journalClock = std::move(clock); }
    /// The +3 FDC I/O trap latched its cause (NR #DA: 1 read of #2FFD, 2 read of #3FFD, 3 write of #3FFD) and, for a write, the byte
    /// (NR #D9): zxnext.vhd 3869-3896
    void LatchIoTrap(uint8_t cause, bool write, uint8_t value)
    {
        _regs[0xDA] = cause & 3;
        if (write)
            _regs[0xD9] = value;
    }
    /// The video frame sync (zxnext.vhd 6700-6710): the 50 / 60 Hz and scandoubler bits of NR #05 and the scanlines of NR #09 are read back
    /// as they were taken at the last sync, not as written
    void LatchVideoFrameSync()
    {
        _eff05 = _regs[0x05] & 0x05;
        _eff09 = _regs[0x09] & 0x03;
    }
    /// NR #05 bit 2 as the video logic took it at the last frame sync (eff_nr_05_5060)
    bool EffectiveSixtyHz() const { return (_eff05 & 0x04) != 0; }
    /// The raw stored byte of a register (reports, tests)
    uint8_t Stored(uint8_t reg) const { return _regs[reg]; }
    /// Change bits of a stored register without the write's side effects: the one flop two paths drive (ULA+ enable: NR #68 bit 3 and #FF3B)
    void SetStoredBits(uint8_t reg, uint8_t mask, uint8_t value) { _regs[reg] = static_cast<uint8_t>((_regs[reg] & ~mask) | (value & mask)); }
    /// NR #02 bits 1:0 as a read returns them: how the last reset happened (the VHDL's nr_02_reset_type: 0 power on, then 2, then 1)
    uint8_t ResetBits() const { return static_cast<uint8_t>(_resetType & 3); }

    /// The `next_regs` report: one line per register of the table with its value and reset
    std::string DescribeRegisters() const;

    void WriteNextReg(uint8_t reg, uint8_t value) override
    {
        SourceScope scope(this, NextRegSource::NextReg);
        Write(reg, value);
    }
    void AfterInstruction() override;
    void OnRetn() override
    {
        if (_machine)
            _machine->OnRetn();
    }

    /// Machine type (NR #03 bits 2:0): 0 config mode, 1 48K, 2 128K, 3 +3, 4 Pentagon
    /// The bare personality's machine type (no firmware chooses it): 1 48K, 2 128K, 3 +3, 4 Pentagon
    void SetMachineType(uint8_t type);
    NextSprites& Sprites() { return _sprites; }
    NextCopper& Copper() { return _copper; }
    /// What the firmware leaves in NR #10 once the core is booted (the VHDL's power-on value is 1)
    void SetCoreId(uint8_t id) { _regs[0x10] = id & 0x1F; }
    /// The CPU mapping of Layer 2 (port #123B, NR #12 / #13) into the memory's slot table
    void RefreshLayer2Mapping();
    /// The CPU speed in force: the programmed NR #07 value, 3.5 MHz while the expansion bus is enabled (NR #80 bit 7)
    uint8_t ActiveCpuSpeed() const;
    void ApplyCpuSpeed();
    NextVideoRegs& Video() { return _video; }
    const NextVideoRegs& Video() const { return _video; }
    uint8_t MachineType() const { return _regs[kRegMachineType] & 7; }
    /// The frame family in effect (NR #03 bits 6:4 once written, else the machine type)
    uint8_t Timing() const { return _timing; }
    bool SdSwap() const { return (_regs[kRegPeripheral3] & 0x20) != 0; }

private:
    friend struct NextStateCodec;  // the TTD state codec (nextstatecodec.cpp)
    NextMemory* _memory;
    NextVideoRegs _video;
    NextSprites _sprites;
    NextCopper _copper;
    NextKeyJoystick _keyJoystick;
    NextRegJournal _journal;
    std::function<void(uint32_t&, uint32_t&, uint16_t&)> _journalClock;
    NextRegSource _source = NextRegSource::Internal;
    struct SourceScope
    {
        NextBoard* board;
        NextRegSource saved;
        SourceScope(NextBoard* b, NextRegSource s) : board(b), saved(b->_source) { b->_source = s; }
        ~SourceScope() { board->_source = saved; }
    };
    NextInterruptSource* _interrupts = nullptr;
    INextMachine* _machine = nullptr;
    uint8_t _selected = 0;
    uint8_t _regs[256] = {};
    std::map<uint8_t, uint32_t>* _readCounts = nullptr;
    std::vector<NextRegWrite>* _log = nullptr;
    std::function<void()> _beforeCpuWrite;
    std::function<uint8_t(unsigned)> _gpioInput;
    unsigned _i2sLeft = 0x200, _i2sRight = 0x200;
    bool _piEar = false;
    mutable uint8_t _i2sLow = 0;  ///< nr_2d_i2s_sample: the low bits of the sample NR #2C / #2E read last
    void UpdatePiEar();
    AccessHook _accessHook;
    bool _copperWrite = false;
    const uint16_t* _pc = nullptr;
    uint8_t _timing = 2;
    bool _resetPending = false;
    uint8_t _eff05 = 0, _eff09 = 0;  ///< NR #05 bits 2 / 0 and NR #09 bits 1:0 as the video logic took them at the last frame sync
    uint8_t _resetType = 4;  ///< zxnext.vhd nr_02_reset_type, power on '100'
    bool _resetHard = false;
};
