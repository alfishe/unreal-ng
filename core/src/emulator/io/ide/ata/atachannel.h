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
/// | no unit present | #FF (floating bus) |
/// | data to / from an absent unit | ignored / #FFFF |
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
};
