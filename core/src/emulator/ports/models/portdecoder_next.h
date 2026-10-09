#pragma once
#include "stdafx.h"

#include <memory>

#include "emulator/io/z80n/nextboard.h"
#include "emulator/io/z80n/z80nengine.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_spectrum128.h"

/// ZX Spectrum Next, skeleton personality: the 128K board's ports (ULA #FE, AY, #7FFD) with the Next's wider
/// decode of the paging ports - #7FFD, #1FFD, #DFFD rewrite the MMU slot table - and the NEXTREG pair #243B /
/// #253B. Owns the board (register file) and installs the Z80N CPU engine.
class PortDecoder_Next : public PortDecoder_Spectrum128
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

    NextBoard& Board() { return *_board; }
    Z80NEngine* Engine() { return _engine.get(); }

private:
    NextMemory& Mem() { return *static_cast<NextMemory*>(_context->pMemory); }
    void Port_7FFD_Next(uint16_t port, uint8_t value, uint16_t pc);
    void Port_1FFD_Next(uint8_t value);
    void Port_DFFD_Next(uint8_t value);

    std::unique_ptr<NextBoard> _board;
    std::unique_ptr<Z80NEngine> _engine;
};
