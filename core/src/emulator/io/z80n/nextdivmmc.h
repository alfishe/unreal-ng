#pragma once

/// @file nextdivmmc.h
/// @brief The Next's DivMMC (esxdos-and-sd.md section 3): port #E3, the automap driven by NR #B8-#BB, and the
/// memory view that NextMemory shows at #0000-#3FFF. The SPI side (#E7 / #EB) is the decoder's.
///
/// #E3: bit 7 CONMEM (map in now), bit 6 MAPRAM (sticky: a write can only set it; NR #09 bit 3 clears it), bits 3:0
/// the RAM bank at #2000. Automap: NR #B8 enables the eight RST addresses (reset #83: #0000 #0008 #0038), NR #B9 says
/// which hold always (1) or only while the 48K BASIC ROM is paged (0), NR #BA which map instantly (the opcode already
/// comes from the DivMMC) rather than after the fetch, NR #BB the extras (#3Dxx, the off-area #1FF8-#1FFF, #056A
/// #04D7 #0562 #04C6, the NMI entry #0066). NR #0A bit 4 enables the automap as a whole.

#include <cstdint>
#include <functional>

#include "emulator/cpu/z80.h"
#include "emulator/memory/next/nextmemory.h"

class NextBoard;

class NextDivMmc final : public IMachineM1Hook
{
public:
    static constexpr uint8_t kPort = 0xE3;
    static constexpr uint8_t kRegAutomapEnable = 0xB8;
    static constexpr uint8_t kRegAutomapValid = 0xB9;
    static constexpr uint8_t kRegAutomapTiming = 0xBA;
    static constexpr uint8_t kRegAutomapExtra = 0xBB;

    NextDivMmc(NextMemory* memory, NextBoard* board) : _memory(memory), _board(board) {}

    /// Is the 48K BASIC ROM the one paged at #0000 (the ROM-3-only entries need it)
    std::function<bool()> isBasicRomPaged;

    void Reset();
    void WritePort(uint8_t value);
    uint8_t ReadPort() const { return static_cast<uint8_t>(_control & 0xCF); }
    /// NR #09 bit 3 = 1 clears the sticky MAPRAM
    void ClearMapram();

    bool Mapped() const { return _automap || (_control & 0x80) != 0; }
    bool Automapped() const { return _automap; }
    bool Mapram() const { return (_control & 0x40) != 0; }
    uint8_t Bank() const { return _control & 0x0F; }

    void BeforeMachineM1(uint16_t address) override;
    void OnMachineM1(uint16_t address) override;

private:
    enum class Hit : uint8_t { None, Delayed, Instant, MapOut };
    Hit Classify(uint16_t address) const;
    void Publish();

    NextMemory* _memory;
    NextBoard* _board;
    uint8_t _control = 0;
    bool _automap = false;
};
