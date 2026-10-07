#pragma once

/// @file sprinterstatetransferhost.h
/// @brief The Sprinter's side of MachineStateTransfer: a Spectrum state moves into / out of the ZX mode the machine runs, through
/// the PLD cell table (bank n is behind cell #F0 + n ..., #D0 + ... in a 512 KB mode). The machine is not reset: a reset would leave
/// the mode. Owner rule 2026-10-07; the transfer is its own mechanism, never merged with the snapshot pipeline.

#include "emulator/ports/statetransferhost.h"

class SprinterStateTransferHost final : public IStateTransferHost
{
public:
    static const SprinterStateTransferHost& Instance();

    Mode CurrentMode(EmulatorContext& context) const override;
    uint8_t* BankPage(EmulatorContext& context, uint16_t bank) const override;
    uint8_t P7ffd(EmulatorContext& context) const override;
    bool BanksAreRam(EmulatorContext& context, unsigned banks, std::string& why) const override;
    void FinishTarget(EmulatorContext& context, uint8_t p7ffd, uint8_t border, uint16_t pc) const override;
};
