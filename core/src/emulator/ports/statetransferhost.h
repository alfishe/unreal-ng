#pragma once

/// @file statetransferhost.h
/// @brief A machine that holds a Spectrum state INSIDE a running mode of its own (a Sprinter running a ZX mode): the banks sit behind
/// the machine's own mapping, the machine must not be reset to receive a state, and its #7FFD lives in its own latch. The decoder hands
/// one out (PortDecoder::GetStateTransferHost); MachineStateTransfer reads and writes banks through it and knows nothing of the model.

#include <cstdint>
#include <string>

class EmulatorContext;

class IStateTransferHost
{
public:
    virtual ~IStateTransferHost() = default;

    struct Mode
    {
        std::string machine;       ///< for messages: "Sprinter"
        bool active = false;       ///< a Spectrum mode runs (not the machine's own software)
        bool paging7ffd = false;   ///< the mode has #7FFD paging (a 128K mode; else 48K)
        bool tooBig = false;       ///< the mode has more than the 8 banks of a Spectrum 128K (a 512 KB mode)
    };

    virtual Mode CurrentMode(EmulatorContext& context) const = 0;
    /// Spectrum bank `bank` of the running mode (the page its mapping names), nullptr when there is none
    virtual uint8_t* BankPage(EmulatorContext& context, uint16_t bank) const = 0;
    /// The #7FFD the mode runs with
    virtual uint8_t P7ffd(EmulatorContext& context) const = 0;
    /// Does the mode keep banks 0..banks-1 in RAM a program may use? `why` says what is wrong when not
    virtual bool BanksAreRam(EmulatorContext& context, unsigned banks, std::string& why) const = 0;
    /// After the banks were written into this machine (it is NOT reset): the screen shadow, the #7FFD latch, the border
    virtual void FinishTarget(EmulatorContext& context, uint8_t p7ffd, uint8_t border, uint16_t pc) const = 0;
};
