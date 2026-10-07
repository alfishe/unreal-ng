#pragma once

/// @file ttdslotcards.h
/// @brief The slot-built cards of a recorded session's machine, as a create request's slot set.
///
/// A slot-built card (one SlotManager builds itself, an ICard: the ZX-MultiSound) is in no shipped config, so a test
/// that loads a session recorded with one builds its machine with the slot set the recorder created it with
/// (`"slots": {"zxbus.1": "multisound"}`; the corpus's ZX-MultiSound sessions: ttdmultisoundsessions.h). A session
/// names its devices by id (TTDRecordedMachine::peripheralMask), not by slot: the corpus records every such card in
/// ZX-bus slot 1, and the session guard refuses a load into another slot with the reason. The cards a config
/// translates into its slots (General Sound, MoonSound) are not listed: the shipped configs carry them.
///
/// Example: a session with PeripheralId::MultiSound (58) among its devices gives {{"zxbus.1", "multisound"}}.

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "debugger/ttd/ttdfileinfo.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/slots/cards/multisound/multisoundcard.h"
#include "emulator/slots/slotcontrol.h"

namespace ttdtest
{

/// [SLOTS] key / value pairs of the slot-built cards of `machine` (in the corpus's slot)
inline std::vector<std::pair<std::string, std::string>> SlotCardsOf(const ttd::TTDRecordedMachine& machine)
{
    std::vector<std::pair<std::string, std::string>> slots;
    if ((machine.peripheralMask >> static_cast<uint8_t>(ttd::PeripheralId::MultiSound)) & 1u)
        slots.emplace_back("zxbus.1", MultiSoundCard::kCardId);
    return slots;
}

/// The create override for a slot set (empty for no slots; false with `error` for a malformed set)
inline bool SlotSetOverride(const std::vector<std::pair<std::string, std::string>>& slots,
                            std::function<void(CONFIG&)>& out, std::string& error)
{
    out = {};
    return slots.empty() || SlotControl::CreateOverride(slots, out, error);
}

/// A fresh machine for a recorded session: its model, its slot-built cards (with the shipped default MIDI bank, which
/// such a session names) and, with `fitGeneralSound`, its General Sound card fitted at creation (the shipped 48K /
/// 128K / +2 / +2A / +3, Profi and Sprinter configs have no GS since 2026-10-04, and a switch cannot fill an empty
/// slot; GeneralSoundFitScope puts it in the next free ZX-bus slot, so an engine session, whose fingerprint names
/// the slots, keeps the shipped config's card and switches it instead). nullptr when the machine cannot be built;
/// `error` says why for a malformed slot set
inline Emulator* CreateRecordedMachine(const ttd::TTDRecordedMachine& machine, std::string& error,
                                       bool fitGeneralSound = true)
{
    const std::vector<std::pair<std::string, std::string>> slots = SlotCardsOf(machine);
    std::function<void(CONFIG&)> slotSet;
    if (!SlotSetOverride(slots, slotSet, error))
        return nullptr;
    GeneralSoundFitScope fit(fitGeneralSound ? machine.generalSound : GSTypeKind::NONE);
    std::optional<SoundCardScope> bank;
    if (!slots.empty())
        bank.emplace(TestSound::DefaultMidiBank);
    return EmulatorTestHelper::CreateStandardEmulator(machine.model, LoggerLevel::LogError, RamPowerOn::Random, slotSet);
}

}  // namespace ttdtest
