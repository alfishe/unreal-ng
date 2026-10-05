#include "card.h"

#include "emulator/slots/cards/multisound/multisoundslotcard.h"

namespace
{

/// One entry per card the slots build themselves (the others keep their legacy owners, slotmanager.h)
constexpr CardType kCardTypes[] = {
    { MultiSoundCard::kCardId, &MultiSoundSlotCard::Create },
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
