#pragma once

/// @file idecontroller.h
/// @brief The machine's IDE board: one channel (two on the Sprinter) with its
/// master and slave units, set up from the config ([HDD] Scheme, CDn, CHSn),
/// and their media slots. `Core` owns it (it replaces the UnrealSpeccy `HDD`
/// skeleton); the port decoder's adapter reaches a channel through `Channel()`.
/// Plan: docs/inprogress/2026-09-28-ide-atapi/implementation-plan.md (D1, D2, D8).
///
/// Units are numbered across the channels: unit = channel * 2 + position
/// (0 ide0.master, 1 ide0.slave, 2 ide1.master, 3 ide1.slave), the order of
/// `config.ide[]` and of the Sprinter BIOS drive codes #80-#83.

#include <array>
#include <atomic>
#include <memory>
#include <string>

#include "emulator/io/ide/ata/atachannel.h"
#include "emulator/io/ide/ideunitslot.h"
#include "emulator/platform.h"

class EmulatorContext;

class IdeController
{
public:
    /// Reads the scheme and the units from `context->config`; registers the
    /// unit slots with the media manager (when there is one and the scheme is
    /// not NONE)
    explicit IdeController(EmulatorContext* context);
    ~IdeController();

    IdeController(const IdeController&) = delete;
    IdeController& operator=(const IdeController&) = delete;

    static constexpr int kMaxChannels = 2;
    static constexpr int kMaxUnits = kMaxChannels * AtaChannel::kUnits;

    IDE_SCHEME Scheme() const { return _scheme; }
    bool Enabled() const { return _scheme != IDE_NONE; }
    /// Channels on this board: 2 for the Sprinter, 1 for every other board, 0 without one
    int ChannelCount() const { return Enabled() ? ChannelsFor(_scheme) : 0; }
    static int ChannelsFor(IDE_SCHEME scheme) { return scheme == IDE_SPRINTER ? 2 : 1; }
    /// Channel 0 (the only one on single-channel boards) or 1; an index out of range gives channel 0
    AtaChannel& Channel(int index = 0) { return _channels[index == 1 && ChannelCount() == 2 ? 1 : 0]; }
    /// Unit 0-3 (channel * 2 + position); nullptr for a unit this board does not have
    IdeUnitSlot* Slot(int unit) { return unit >= 0 && unit < kMaxUnits ? _slots[unit].get() : nullptr; }

    /// Blocks moved through either unit's data register so far (activity
    /// LEDs). Any thread
    uint64_t Activity() const { return _activity.load(std::memory_order_relaxed); }

    /// The machine's reset line: both units back to power-on
    void Reset();

    /// Swap a unit's drive for the other kind (a hard disk for a CD-ROM drive,
    /// or back), as a user would change the hardware: the unit must be empty,
    /// the emulator not running, TTD not recording. The machine's in-memory
    /// config (`ide[unit].cd`) follows, so a reset or RefitIde keeps it; a new
    /// machine starts from its config file
    bool SetUnitKind(int unit, bool cdrom, std::string* error = nullptr);
    /// 0-3 for "ide0.master" / "ide0.slave" / "ide1.master" / "ide1.slave", -1 for any other slot
    static int UnitForSlot(const std::string& slotId);

    /// "nemo", "profi", ...: the slots' tag for the board
    static const char* SchemeTag(IDE_SCHEME scheme);

    /// Whether the board fits the machine (IDE design §8.1): PROFI on the
    /// Profi, SMUC on the Scorpions, ATM on the ATM Turbo family and ZX-Evo,
    /// SPRINTER on the Sprinter (and only there: its PLD decodes every port);
    /// Nemo and DivIDE boards on any other machine
    static bool SchemeFits(IDE_SCHEME scheme, MEM_MODEL model);

private:
    /// The unit's device and its slot, from `_context->config.ide[unit]`
    void BuildUnit(int unit);
    /// No lower-numbered unit of the board has the same kind (the "hd" / "cd" alias)
    bool FirstOfKind(int unit) const;

    EmulatorContext* _context = nullptr;
    IDE_SCHEME _scheme = IDE_NONE;
    std::array<AtaChannel, kMaxChannels> _channels;
    std::array<std::unique_ptr<IdeUnitSlot>, kMaxUnits> _slots;
    bool _registered = false;
    std::atomic<uint64_t> _activity{0};
};
