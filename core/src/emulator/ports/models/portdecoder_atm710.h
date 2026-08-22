#pragma once
#include "stdafx.h"

#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"

/// ATM Turbo 2+ v7.10 Port Decoder
///
/// Key ports:
/// - 0x7FFD: Standard 128K paging (A15=0, A2=1, A1=0)
/// - 0xFF77: ATM control register (video mode, turbo, mem swap, INT gate)
/// - 0xFFF7: Memory manager registers for 4 windows (low byte F7, window = A15:A14)
/// - 0xEFF7: Extended control (turbo 3.5MHz, ROCACHE)
///
/// Memory Map (with ATM paging enabled):
/// - Window 0 ($0000-$3FFF): ROM or RAM per FFF7[0]
/// - Window 1 ($4000-$7FFF): RAM per FFF7[1]
/// - Window 2 ($8000-$BFFF): RAM per FFF7[2]
/// - Window 3 ($C000-$FFFF): RAM per FFF7[3]
///
/// Video Modes (pFF77 bits 4,2,1):
/// - 0: EGA 16-color 320x200
/// - 2: Hardware multicolor 320x200
/// - 3: ZX Standard 256x192
/// - 6: Text mode 80x25
///
/// See: Unreal Speccy atm.cpp, io.cpp, memory.cpp

class PortDecoder_ATM710 : public PortDecoder
{
    /// region <Constants>
public:
    // EFF7 bit masks (values match macros in platform.h)
    static constexpr uint8_t ATM_EFF7_TURBO_3_5   = 0x10;  // Bit 4: 3.5MHz when pFF77.3=0
    static constexpr uint8_t ATM_EFF7_LOCKMEM     = 0x04;  // Bit 2: Lock memory configuration
    static constexpr uint8_t ATM_EFF7_ROCACHE     = 0x08;  // Bit 3: RAM at $0000 instead of ROM

    // FF77 bit masks (per original Unreal Speccy atm.cpp)
    static constexpr uint8_t ATM_FF77_MEMSWAP     = 0x01;  // Bit 0: Memory address swap
    static constexpr uint8_t ATM_FF77_VMODE_MASK  = 0x07;  // Bits 0,1,2: Video mode (0-7)
    static constexpr uint8_t ATM_FF77_TURBO       = 0x08;  // Bit 3: 14MHz turbo
    static constexpr uint8_t ATM_FF77_INTGATE     = 0x20;  // Bit 5: INT gate (1=pass)

    // aFF77 address bits
    static constexpr uint16_t ATM_AFF77_PEN       = 0x100; // Bit 8: Enable ATM paging
    static constexpr uint16_t ATM_AFF77_CPM       = 0x200; // Bit 9: ~CPM (0=TR-DOS mode)
    /// endregion </Constants>

    /// region <Constructors / Destructors>
public:
    PortDecoder_ATM710() = delete;
    PortDecoder_ATM710(EmulatorContext* context);
    virtual ~PortDecoder_ATM710();
    /// endregion </Constructors / Destructors>

    /// region <Interface methods>
public:
    void reset() override;
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;

    void SetRAMPage(uint8_t page) override;
    void SetROMPage(uint8_t page) override;

    void ApplyBootROMDefaults(ROMModeEnum mode) override;
    void UpdateModelMemoryBanks() override;
    /// endregion </Interface methods>

    /// region <Port detection>
public:
    bool IsPort_FE(uint16_t port);
    bool IsPort_7FFD(uint16_t port);
    bool IsPort_FF77(uint16_t port);
    virtual bool IsPort_FFF7(uint16_t port, uint8_t& windowIndex);
    bool IsPort_EFF7(uint16_t port);
    bool IsPort_BFFD(uint16_t port);
    bool IsPort_FFFD(uint16_t port);
    bool IsBeta128Port(uint16_t decodedPort);

    uint16_t decodePort(uint16_t port);
    /// endregion </Port detection>

    /// region <Port handlers>
protected:
    void Port_7FFD_Out(uint16_t port, uint8_t value, uint16_t pc);
    void Port_FF77_Out(uint16_t port, uint8_t value, uint16_t pc);
    void Port_FFF7_Out(uint16_t port, uint8_t value, uint8_t windowIndex, uint16_t pc);
    void Port_EFF7_Out(uint16_t port, uint8_t value, uint16_t pc);

    virtual void updateMemoryBanks();
    virtual void updateTurboMode();
    virtual void atmMemSwap();
    /// endregion </Port handlers>

    /// region <Debug methods>
protected:
    std::string Dump_7FFD_value(uint8_t value);
    std::string Dump_FF77_value(uint8_t value);
    std::string Dump_FFF7_value(unsigned value);
    std::string Dump_EFF7_value(uint8_t value);
    /// endregion </Debug methods>
};
