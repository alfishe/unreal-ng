#include "card.h"

#include "emulator/slots/cards/multisound/multisoundslotcard.h"

namespace
{

/// The ZX-MultiSound's devices: the card (its time base, CPLD, YM2203 pair, MIDI line, DACs), its SAA1099, its
/// SAM2695 and its General Sound
constexpr ttd::PeripheralId kMultiSoundTtdIds[] = { ttd::PeripheralId::MultiSound, ttd::PeripheralId::Saa1099,
                                                    ttd::PeripheralId::Sam2695, ttd::PeripheralId::MultiSoundGs };

/// One entry per card the slots build themselves (the others keep their legacy owners, slotmanager.h)
constexpr CardType kCardTypes[] = {
    { MultiSoundCard::kCardId, &MultiSoundSlotCard::Create, kMultiSoundTtdIds },
};

} // namespace

std::span<const CardType> CardTypes()
{
    return kCardTypes;
}

const CardType* FindCardType(std::string_view id)
{
    for (const CardType& type : kCardTypes)
    {
        if (id == type.id)
        {
            return &type;
        }
    }
    return nullptr;
}
