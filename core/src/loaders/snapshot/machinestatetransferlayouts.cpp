#include "machinestatetransferlayouts.h"

#include <algorithm>
#include <cstring>

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/rom.h"
#include "emulator/ports/models/portdecoder_atm3.h"
#include "emulator/ports/models/portdecoder_atm710.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/ports/statetransferhost.h"

namespace statetransfer
{
namespace
{
uint8_t RomBanksOf(EmulatorContext& context)
{
    return context.pCore && context.pCore->GetROM() ? context.pCore->GetROM()->GetROMBanksLoaded() : 0;
}

/// A ROM selector of the ATM pager (page number) in the target's ROM image: the standard set is the last four pages of both images
bool TranslateRomPage(uint8_t page, uint8_t sourceBanks, uint8_t targetBanks, uint8_t& out)
{
    if (sourceBanks < 4 || targetBanks < 4)
        return false;
    const uint8_t mask = static_cast<uint8_t>(sourceBanks - 1);
    const uint8_t base = static_cast<uint8_t>(sourceBanks - 4);
    const uint8_t masked = page & mask;
    if (masked < base)
        return false;   // a page outside the standard set (a service ROM of this image): the target has no equivalent
    out = static_cast<uint8_t>(targetBanks - 4 + (masked - base));
    return true;
}

constexpr unsigned kRegTypeMask = 0x300;
constexpr unsigned kRegRomFrom7ffd = 0x100;
constexpr unsigned kRegRomFromReg = 0x300;
}  // namespace

uint8_t* BankPage(EmulatorContext& context, uint16_t bank)
{
    if (const IStateTransferHost* host = context.pPortDecoder ? context.pPortDecoder->GetStateTransferHost() : nullptr)
        return host->BankPage(context, bank);
    return context.pMemory->RAMPageAddress(bank);
}

bool IsSpectrum128Layout(EmulatorContext& context)
{
    Memory& memory = *context.pMemory;
    return memory.IsBank0ROM() && memory.GetRAMPageForBank(1) == 5 && memory.GetRAMPageForBank(2) == 2 &&
           memory.GetRAMPageForBank(3) == (context.emulatorState.p7FFD & 0x07);
}

bool CanMoveAtmTurboState(EmulatorContext& source, EmulatorContext& target, std::string& why)
{
    const EmulatorState& s = source.emulatorState;
    const uint8_t sourceBanks = RomBanksOf(source);
    const uint8_t targetBanks = RomBanksOf(target);
    // The pager only matters while it is on (#xx77 bit 8: PEN); the register set #7FFD bit 4 picks is the one in use
    const bool pagerOn = (s.atm.aFF77 & 0x0100) != 0;
    const unsigned inUse = (s.p7FFD & 0x10) ? 4u : 0u;
    for (unsigned i = 0; i < 8; i++)
    {
        const unsigned type = s.atm.pFFF7[i] & kRegTypeMask;
        if (type != kRegRomFrom7ffd && type != kRegRomFromReg)
            continue;
        uint8_t ignored = 0;
        if (!TranslateRomPage(static_cast<uint8_t>(s.atm.pFFF7[i] & 0xFF), sourceBanks, targetBanks, ignored) && pagerOn && i >= inUse &&
            i < inUse + 4)
        {
            why = "the source's pager maps ROM page " + std::to_string(s.atm.pFFF7[i] & 0xFF) + " (window " + std::to_string(i - inUse) +
                  "), a page outside the standard ROM set that the target's ROM image has no equivalent for";
            return false;
        }
    }
    return true;
}

bool MoveAtmTurboState(EmulatorContext& source, EmulatorContext& target, std::string& why)
{
    if (!CanMoveAtmTurboState(source, target, why))
        return false;
    const EmulatorState& s = source.emulatorState;
    EmulatorState& t = target.emulatorState;
    const uint8_t sourceBanks = RomBanksOf(source);
    const uint8_t targetBanks = RomBanksOf(target);
    const uint16_t pc = source.pCore->GetZ80()->pc;

    for (unsigned i = 0; i < 8; i++)
    {
        unsigned reg = s.atm.pFFF7[i];
        const unsigned type = reg & kRegTypeMask;
        if (type == kRegRomFrom7ffd || type == kRegRomFromReg)
        {
            uint8_t translated = 0;
            if (TranslateRomPage(static_cast<uint8_t>(reg & 0xFF), sourceBanks, targetBanks, translated))
                reg = (reg & ~0xFFu) | translated;
        }
        t.atm.pFFF7[i] = reg;
    }
    for (int i = 0; i < 16; i++)
    {
        t.atm.palette[i] = s.atm.palette[i];
        t.atm.paletteRegs[i] = s.atm.paletteRegs[i];
    }
    t.atm.borderBright = s.atm.borderBright;
    std::memcpy(t.atm.fontRam, s.atm.fontRam, sizeof(t.atm.fontRam));
    t.atm.fontByte = s.atm.fontByte;
    t.pEFF7 = static_cast<uint8_t>(t.pEFF7 | PortDecoder_ATM710::ATM_EFF7_LOCKMEM);   // the ATM3's 7.10-compatible mode (#xx77 / pager as 7.10)

    // The DOS signal the pager's ROM selectors refer to (TR-DOS paged in, the Beta 128 ports live): the flags move with it
    constexpr uint32_t kDosFlags = CF_TRDOS | CF_DOSPORTS;
    t.flags = (t.flags & ~kDosFlags) | (s.flags & kDosFlags);

    PortDecoder& ports = *target.pPortDecoder;
    ports.UnlockPaging();
    ports.DecodePortOut(0x7FFD, s.p7FFD, pc);
    t.p7FFD = s.p7FFD;
    // The #xx77 latch: pager on, ~CPM, video mode, turbo - replayed through the target's decoder, which rebuilds the banks from the
    // window registers above
    ports.DecodePortOut(static_cast<uint16_t>((s.atm.aFF77 & 0xFF00) | 0x0077), s.pFF77, pc);
    ports.UpdateModelMemoryBanks();
    return true;
}
}  // namespace statetransfer
