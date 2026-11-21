#include "card.h"

#include "emulator/slots/cards/amx/amxmousecard.h"
#include "emulator/slots/cards/divmmc/divmmccard.h"
#include "emulator/slots/cards/fuller/fullerboxcard.h"
#include "emulator/slots/cards/kempston/kempstoncard.h"
#include "emulator/slots/cards/multisound/multisoundslotcard.h"
#include "emulator/slots/cards/plusd/plusdcard.h"

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

/// The MGT +D: one blob, the card with its RAM, WD1772 and drives
constexpr ttd::TTDDeviceType kPlusDTtdTypes[] = { ttd::TTDDeviceType::PlusD };

/// The MGT DISCiPLE: one blob, as the +D's with the swap, the reset hold-off and the inhibit button
constexpr ttd::TTDDeviceType kDiscipleTtdTypes[] = { ttd::TTDDeviceType::Disciple };

/// The DivMMC: one blob, the board's latches and its two SD cards' protocol state; its RAM as an engine region
constexpr ttd::TTDDeviceType kDivMmcTtdTypes[] = { ttd::TTDDeviceType::DivMmc };

/// One entry per card the slots build themselves (the others keep their legacy owners, slotmanager.h)
constexpr CardType kCardTypes[] = {
    { MultiSoundCard::kCardId, &MultiSoundSlotCard::Create, kMultiSoundTtdTypes },
    { FullerBoxCard::kCardId, &FullerBoxCard::Create, kFullerBoxTtdTypes },
    { AmxMouseCard::kCardId, &AmxMouseCard::Create, kAmxMouseTtdTypes },
    { KempstonCard::kCardId, &KempstonCard::Create, kKempstonTtdTypes },
    { PlusDCard::kCardId, &PlusDCard::Create, kPlusDTtdTypes },
    { PlusDCard::kDiscipleCardId, &PlusDCard::CreateDisciple, kDiscipleTtdTypes },
    { DivMmcCard::kCardId, &DivMmcCard::Create, kDivMmcTtdTypes },
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
