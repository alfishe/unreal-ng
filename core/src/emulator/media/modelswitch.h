#pragma once

/// @file modelswitch.h
/// @brief A model switch that keeps the media (FR-21, technical-design.md §8).
///
/// The new machine is created first, next to the old one. Media go into the
/// slot with the same id on the new machine as live objects, unsaved writes
/// included, so nothing is read again and nothing is lost. A medium the new
/// model has no slot for is "stranded": with unsaved writes the switch needs a
/// decision (save them, discard them, or keep the medium detached on the new
/// machine); without, it is closed and reported. Refused, the switch leaves
/// the old machine as it was and destroys the new one.
///
/// Example: Pentagon -> ZX-Evo keeps floppy A and the tape; ZX-Evo -> Pentagon
/// strands `sd.zc`: a card with 48 unsaved sectors refuses the switch
/// ("dirty") until the request says save, discard or keep.
///
/// The cards go along too (ZX-bus slots R-OP-9): the old machine's slot set is
/// planned against the new model, a card the new machine cannot take is
/// dropped and reported (ModelSwitchResult::slotCarry, also in the report lines).

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "emulator/media/mediamanager.h"
#include "emulator/platform.h"
#include "emulator/slots/slotconfig.h"
#include "emulator/slots/slotmanager.h"

class Emulator;

/// Stranded media with unsaved writes
enum class StrandedMedia : uint8_t
{
    Refuse,   ///< the switch fails with "dirty" and lists them
    Save,     ///< written into their own files first (floppies)
    Discard,  ///< their writes are dropped, the media closed
    Keep      ///< kept as detached media on the new machine (save / export them later)
};

struct ModelSwitchRequest
{
    std::string emulatorId;
    std::string model;       ///< short name: PENTAGON, 48K, ATM3, ...
    uint32_t ramKb = 0;      ///< 0: the model's default
    StrandedMedia stranded = StrandedMedia::Refuse;
    /// Called with the old machine stopped, before it is destroyed (a GUI
    /// unbinds its views here)
    std::function<void(Emulator& old)> beforeRelease;
    /// Power-on RAM of the new machine; unset = the old machine's mode, so a
    /// machine created with zeroed RAM stays that way across a model switch
    std::optional<RamPowerOn> ramPowerOn;
    /// The new machine's slot set, exactly ([SLOTS]): a restart with a planned
    /// slot change (SlotChange, ZX-bus slots SL-6). Unset: the old machine's
    /// cards are carried and planned against the new model (R-OP-9,
    /// SlotManager::Carry), the cards it cannot take reported in the result
    std::optional<SlotConfig> slotSet;
    /// The old instance's create-time config override applies to the new
    /// machine too (a restart of the same model: a machine variant's board, a
    /// create-time option); off for a switch to another model
    bool keepConfigOverride = false;
    /// Running settings the new machine takes over from the old one (configuration, not machine state), applied
    /// after the instance override and before the slot set: a slot restart carries [NETWORK] this way (owner decision
    /// 2026-10-05, ZX-bus slots tdd §16). Empty: the new machine starts from its configuration alone
    std::function<void(CONFIG&)> carrySettings;
};

struct ModelSwitchResult
{
    MediaResult result;
    std::shared_ptr<Emulator> emulator;  ///< the new machine, created and not started
    std::vector<SlotInfo> stranded;      ///< dirty media the new model has no slot for
    MediaTransferReport media;           ///< where the media went
    SlotManager::CarryReport slotCarry;  ///< the cards carried and dropped (a switch without an exact slot set)
};

class ModelSwitch
{
public:
    static ModelSwitchResult Run(const ModelSwitchRequest& request);

    /// "refuse" | "save" | "discard" | "keep"
    static bool ParseStranded(const std::string& text, StrandedMedia& value);
    static const char* StrandedName(StrandedMedia value);
};
