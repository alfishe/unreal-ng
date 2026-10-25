#include "card.h"

#include "emulator/slots/cards/amx/amxmousecard.h"
#include "emulator/slots/cards/fuller/fullerboxcard.h"
#include "emulator/slots/cards/kempston/kempstoncard.h"
#include "emulator/slots/cards/multisound/multisoundslotcard.h"

namespace
{

/// The ZX-MultiSound's devices: the card (its time base, CPLD, YM2203 pair, MIDI line, DACs), its SAA1099, its
/// SAM2695 and its General Sound
constexpr ttd::TTDDeviceType kMultiSoundTtdTypes[] = { ttd::TTDDeviceType::MultiSound, ttd::TTDDeviceType::Saa1099,
                                                      ttd::TTDDeviceType::Sam2695, ttd::TTDDeviceType::MultiSoundGs };

/// The Fuller Box: one blob, its time base and AY
constexpr ttd::TTDDeviceType kFullerBoxTtdTypes[] = { ttd::TTDDeviceType::FullerBox };

/// The AMX mouse: one blob, its PIO and pending motion
constexpr ttd::TTDDeviceType kAmxMouseTtdTypes[] = { ttd::TTDDeviceType::AmxMouse };

/// The Kempston joystick interface: its presence blob (the stick byte is the machine's KempstonJoystick device)
constexpr ttd::TTDDeviceType kKempstonTtdTypes[] = { ttd::TTDDeviceType::KempstonInterface };

/// One entry per card the slots build themselves (the others keep their legacy owners, slotmanager.h)
constexpr CardType kCardTypes[] = {
    { MultiSoundCard::kCardId, &MultiSoundSlotCard::Create, kMultiSoundTtdTypes },
    { FullerBoxCard::kCardId, &FullerBoxCard::Create, kFullerBoxTtdTypes },
    { AmxMouseCard::kCardId, &AmxMouseCard::Create, kAmxMouseTtdTypes },
    { KempstonCard::kCardId, &KempstonCard::Create, kKempstonTtdTypes },
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
