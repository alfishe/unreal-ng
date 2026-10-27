#include "sprinterstatetransferhost.h"

#include "emulator/emulatorcontext.h"
#include "emulator/memory/sprinter/sprintermemory.h"
#include "emulator/ports/models/portdecoder_sprinter.h"

namespace
{
PortDecoder_Sprinter* DecoderOf(EmulatorContext& context) { return dynamic_cast<PortDecoder_Sprinter*>(context.pPortDecoder); }

/// The PLD cell that holds the physical page of Spectrum bank `bank`: #F0-#F7, #F8-#FF; #D0-#D7 / #D8-#DF for banks 16-31 of a
/// 512 KB mode (window 3's cell is ComputePg3: bit 5 = !#7FFD.7, bit 4 = 1, bit 3 = #7FFD.6, bits 2-0 = the low bits)
uint8_t CellOfBank(uint16_t bank)
{
    if (bank < 8)
        return static_cast<uint8_t>(0xF0 + bank);
    if (bank < 16)
        return static_cast<uint8_t>(0xF8 + (bank - 8));
    return static_cast<uint8_t>(bank < 24 ? 0xD0 + (bank - 16) : 0xD8 + (bank - 24));
}
}  // namespace

const SprinterStateTransferHost& SprinterStateTransferHost::Instance()
{
    static const SprinterStateTransferHost instance;
    return instance;
}

IStateTransferHost::Mode SprinterStateTransferHost::CurrentMode(EmulatorContext& context) const
{
    Mode mode;
    mode.machine = "Sprinter";
    PortDecoder_Sprinter* decoder = DecoderOf(context);
    if (!decoder)
        return mode;
    const SprinterPldState& pld = decoder->GetPldState();
    mode.active = pld.configState == SprinterConfigState::Configured && pld.romOff && !pld.cacheOn && (pld.allMode & 0x01) == 0;
    mode.paging7ffd = (pld.cnf & 0x20) == 0;
    mode.tooBig = (pld.cnf & 0x80) != 0;
    return mode;
}

uint8_t* SprinterStateTransferHost::BankPage(EmulatorContext& context, uint16_t bank) const
{
    PortDecoder_Sprinter* decoder = DecoderOf(context);
    if (!decoder)
        return nullptr;
    return context.pMemory->RAMPageAddress(decoder->GetPldState().Cell(CellOfBank(bank)));
}

uint8_t SprinterStateTransferHost::P7ffd(EmulatorContext& context) const
{
    PortDecoder_Sprinter* decoder = DecoderOf(context);
    return decoder ? decoder->GetPldState().pn : context.emulatorState.p7FFD;
}

bool SprinterStateTransferHost::BanksAreRam(EmulatorContext& context, unsigned banks, std::string& why) const
{
    PortDecoder_Sprinter* decoder = DecoderOf(context);
    if (!decoder)
        return false;
    const SprinterPldState& pld = decoder->GetPldState();
    for (unsigned bank = 0; bank < banks; bank++)
    {
        const uint8_t page = pld.Cell(CellOfBank(static_cast<uint16_t>(bank)));
        if (page == SprinterMemory::kPortTablePage || page == 0x41)
        {
            why = "the Sprinter mode's page table gives Spectrum bank " + std::to_string(bank) + " page " + std::to_string(page) +
                  ", which is not RAM for programs";
            return false;
        }
    }
    return true;
}

void SprinterStateTransferHost::FinishTarget(EmulatorContext& context, uint8_t p7ffd, uint8_t border, uint16_t pc) const
{
    PortDecoder_Sprinter* decoder = DecoderOf(context);
    SprinterMemory* memory = dynamic_cast<SprinterMemory*>(context.pMemory);
    if (!decoder || !memory)
        return;
    const SprinterPldState& pld = decoder->GetPldState();
    for (uint16_t bank = 0; bank < 16; bank++)
        memory->MarkRamPageEdited(pld.Cell(CellOfBank(bank)));
    memory->RefreshZxShadow(1);   // bank 5 at #4000
    if ((pld.cnf & 0x20) == 0)
    {
        decoder->SetPagingFromSnapshot(0x07, pc);   // the screen shadow follows bank 7 in window 3
        memory->RefreshZxShadow(3);
        decoder->SetPagingFromSnapshot(p7ffd, pc);
    }
    decoder->DecodePortOut(0x00FE, border, pc);
}
