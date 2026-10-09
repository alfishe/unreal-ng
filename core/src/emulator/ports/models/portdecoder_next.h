#pragma once
#include "stdafx.h"

#include <memory>

#include "emulator/io/z80n/nextboard.h"
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

    /// The SPI port pair #E7 (select) / #EB (data): one SD card per select line (NR #0A bit 5 swaps them)
    static constexpr uint16_t kPortSpiSelect = 0x00E7;
    static constexpr uint16_t kPortSpiData = 0x00EB;
    static constexpr uint64_t kSpiByteClocks = 16;
    SdCardSpi& SdCard(unsigned index) { return _sd[index & 1]; }
    bool InsertSdCard(unsigned index, std::unique_ptr<IBlockDevice> media, SdCardSpi::WriteMode mode);
    bool InsertSdCard(unsigned index, const std::string& path, SdCardSpi::WriteMode mode);
    /// Accesses that began before the previous byte was done and were ignored, as on the board
    uint32_t SpiTooFastCount() const { return _spiTooFast; }

    NextBoard& Board() { return *_board; }
    Z80NEngine* Engine() { return _engine.get(); }

private:
    NextMemory& Mem() { return *static_cast<NextMemory*>(_context->pMemory); }
    void Port_7FFD_Next(uint16_t port, uint8_t value, uint16_t pc);
    void Port_1FFD_Next(uint8_t value);
    void Port_DFFD_Next(uint8_t value);

    uint8_t SpiRead();
    void SpiWrite(uint8_t value);
    void SpiSelect(uint8_t value);
    uint64_t Clocks() const;

    SdCardSpi _sd[2];
    int _spiSelected = -1;
    uint8_t _spiRx = 0xFF;
    uint64_t _spiBusyUntil = 0;
    uint32_t _spiTooFast = 0;
    std::unique_ptr<NextBoard> _board;
    std::unique_ptr<Z80NEngine> _engine;
};
