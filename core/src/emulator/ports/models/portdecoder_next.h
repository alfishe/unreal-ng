#pragma once
#include "stdafx.h"

#include <map>
#include <memory>

#include "emulator/io/z80n/nextboard.h"
#include "emulator/io/z80n/nextctc.h"
#include "emulator/io/z80n/nextdivmmc.h"
#include "emulator/io/z80n/nexti2c.h"
#include "emulator/io/z80n/nextinterrupts.h"
#include "emulator/io/z80n/z80nengine.h"
#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_spectrum128.h"

/// ZX Spectrum Next, skeleton personality: the 128K board's ports (ULA #FE, AY, #7FFD) with the Next's wider
/// decode of the paging ports - #7FFD, #1FFD, #DFFD rewrite the MMU slot table - and the NEXTREG pair #243B /
/// #253B. Owns the board (register file) and installs the Z80N CPU engine.
class PortDecoder_Next : public PortDecoder_Spectrum128, public INextMachine
{
public:
    PortDecoder_Next() = delete;
    explicit PortDecoder_Next(EmulatorContext* context);
    ~PortDecoder_Next() override;

    void reset() override;
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;
    void UpdateModelMemoryBanks() override;

    static constexpr uint16_t kPortRegSelect = 0x243B;
    static constexpr uint16_t kPortRegData = 0x253B;

    static bool IsPort_7FFD_Next(uint16_t port) { return (port & 0xC002) == 0x4000; }
    static bool IsPort_1FFD_Next(uint16_t port) { return (port & 0xF002) == 0x1000; }
    static bool IsPort_DFFD_Next(uint16_t port) { return (port & 0xF002) == 0xD000; }

    /// INextMachine: NR #02. Runs between two instructions
    void PerformReset(bool hard) override;
    void SetCpuSpeed(uint8_t ratio) override;
    void SetMachineTiming(uint8_t timing) override;
    void SetContentionDisabled(bool disabled) override;
    void WriteMemoryMapping(uint8_t value) override;
    void ClearDivMmcMapram() override { _divMmc->ClearMapram(); }
    uint8_t ReadMemoryMapping() const override;
    void OnFrameEnd() override;

    /// The SPI port pair #E7 (select) / #EB (data): one SD card per select line (NR #0A bit 5 swaps them)
    static constexpr uint16_t kPortSpiSelect = 0x00E7;
    static constexpr uint16_t kPortSpiData = 0x00EB;
    static constexpr double kSpiByteClocks = 16.0;  ///< CPU clocks per byte at any speed
    SdCardSpi& SdCard(unsigned index) { return _sd[index & 1]; }
    bool InsertSdCard(unsigned index, std::unique_ptr<IBlockDevice> media, SdCardSpi::WriteMode mode);
    bool InsertSdCard(unsigned index, const std::string& path, SdCardSpi::WriteMode mode);
    /// Ports no part of the machine answered (the bring-up list): raw port -> {count, last value}, in and out
    struct PortUse
    {
        uint32_t count = 0;
        uint8_t last = 0;
    };
    void SetPortLog(std::map<uint16_t, PortUse>* in, std::map<uint16_t, PortUse>* out) { _inLog = in; _outLog = out; }
    /// Accesses that began before the previous byte was done and were ignored, as on the board
    uint32_t SpiTooFastCount() const { return _spiTooFast; }

    NextBoard& Board() { return *_board; }
    NextInterruptSource& Interrupts() { return *_interrupts; }
    NextCtc& Ctc() { return _ctc; }
    NextDivMmc& DivMmc() { return *_divMmc; }
    NextI2c& I2c() { return _i2c; }
    /// The 28 MHz system clock since the machine started
    uint64_t Now28() const;
    Z80NEngine* Engine() { return _engine.get(); }

private:
    NextMemory& Mem() const { return *static_cast<NextMemory*>(_context->pMemory); }
    void Port_7FFD_Next(uint16_t port, uint8_t value, uint16_t pc);
    void Port_1FFD_Next(uint8_t value);
    void Port_DFFD_Next(uint8_t value);

    void InsertConfiguredCard();
    void ApplyTiming(uint8_t timing);
    /// Contention follows the frame family, the speed (3.5 MHz only) and NR #08 bit 6
    void UpdateContention();
    bool _nr08NoContention = false;
    uint8_t _ratio = 1;
    /// What the machine type allows: 48K has no paging ports, 128K / Pentagon no #1FFD, +3 all
    bool PagingAllowed(uint16_t port) const;
    uint8_t SpiRead();
    void SpiWrite(uint8_t value);
    void SpiSelect(uint8_t value);
    /// Time in 3.5 MHz T-states since the machine started (the frame's own t is in CPU clocks of the current speed)
    double Now() const;
    double SpeedRatio() const { return _state->hw_turbo_ratio_applied ? _state->hw_turbo_ratio_applied : 1.0; }

    std::map<uint16_t, PortUse>* _inLog = nullptr;
    std::map<uint16_t, PortUse>* _outLog = nullptr;
    SdCardSpi _sd[2];
    uint8_t _pendingTiming = 0;  ///< applied at the frame end
    int _spiSelected = -1;
    uint8_t _spiRx = 0xFF;
    double _spiBusyUntil = 0;
    uint32_t _spiTooFast = 0;
    std::unique_ptr<NextBoard> _board;
    std::unique_ptr<NextInterruptSource> _interrupts;
    std::unique_ptr<NextDivMmc> _divMmc;
    NextCtc _ctc;
    NextI2c _i2c;
    std::unique_ptr<Z80NEngine> _engine;
};
