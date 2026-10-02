#pragma once

/// @file atachannel.h
/// @brief One IDE channel: a master and a slave unit on one cable, and the
/// bus rules between them (IDE design §6.2).
///
/// | Situation | Behavior |
/// |---|---|
/// | task-file / control write | every unit latches it; only the selected one executes a command |
/// | EXECUTE DEVICE DIAGNOSTIC | both units run it |
/// | register read, selected unit present | it answers |
/// | selected unit absent, the other present | the other answers the task file; status and alternate status read #00 |
/// | no unit present | the board's empty-bus value: #FF (floating bus) by default, #7F with a DD7 pull-down |
/// | data to / from an absent unit | ignored / the empty-bus word (#FFFF, #FF7F with the pull-down) |
///
/// The ATA standard asks the host for a 10 kOhm pull-down on DD7 so that an
/// empty channel reads BSY = 0 and the firmware sees "no device" at once
/// instead of waiting for BSY to drop; the other lines float high (#7F). Which
/// boards fit it is the board's choice (IdeController): the Sprinter does.
///
/// A unit is configured as a disk or a CD drive (the machine's config); its
/// medium comes and goes through the media manager's slot.

#include <array>
#include <cstdint>
#include <memory>

#include "emulator/io/ide/ata/atadevice.h"

class AtaChannel
{
public:
    static constexpr int kUnits = 2;

    AtaChannel() = default;

    /// Configure unit 0 (master) or 1 (slave); nullptr: no unit
    void SetUnit(int unit, std::unique_ptr<AtaDevice> device);
    AtaDevice* Unit(int unit) const { return unit >= 0 && unit < kUnits ? _units[unit].get() : nullptr; }

    /// Registers 1..7 of CS0, and ata::Control for CS1 register 6
    uint8_t ReadRegister(uint8_t reg);
    void WriteRegister(uint8_t reg, uint8_t value);
    uint16_t ReadData();
    void WriteData(uint16_t word);

    /// The channel's interrupt line (either unit, after nIEN)
    bool Intrq() const;
    /// The machine's reset line: both units back to power-on
    void HardReset();

    /// What the bus reads when no unit drives it (registers: the low byte)
    void SetEmptyBus(uint16_t word) { _emptyBus = word; }
    uint16_t EmptyBus() const { return _emptyBus; }
    /// The empty-bus word of a host with the ATA DD7 pull-down
    static constexpr uint16_t kEmptyBusDd7PullDown = 0xFF7F;
    /// The empty-bus word of a host without it: every line floats high
    static constexpr uint16_t kEmptyBusFloating = 0xFFFF;

    /// The unit the device register selects
    int Selected() const { return _selected; }
    bool AnyPresent() const;

    /// TTD: the selected unit (the units carry their own state)
    uint8_t SelectedState() const { return _selected; }
    void SetSelectedState(uint8_t selected) { _selected = selected & 1; }

private:
    AtaDevice* Present(int unit) const;

    std::array<std::unique_ptr<AtaDevice>, kUnits> _units;
    uint8_t _selected = 0;
    uint16_t _emptyBus = kEmptyBusFloating;
};
