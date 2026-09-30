#pragma once

/// @file floppydriveslot.h
/// @brief The floppy drives as media manager slots (`fdd.a` ... `fdd.d`).
///
/// A slot puts the manager's disk image into its FDD: the drive keeps its
/// mechanics (motor, head, index, write-protect sense), the medium owns the
/// image. The slots are registered for the drives the machine's active disk
/// controller serves: four for the Beta Disk WD1793, two (A and B) on the +3,
/// whose uPD765 takes over the same drives. Guest writes reach the manager
/// through NoteSlotWrite (a TTD replay barrier, once per frame).
/// Design: docs/inprogress/2026-09-28-storage-manager/integration-floppy.md §2-§3.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "emulator/media/mediaslot.h"

class EmulatorContext;

class FloppyDriveSlot : public IMediaSlot
{
public:
    FloppyDriveSlot(EmulatorContext* context, uint8_t drive);

    /// "fdd.a" ... "fdd.d"
    static std::string IdFor(uint8_t drive);

    /// A controller reports a guest write to `drive` (emulation thread):
    /// the manager's barrier when there is one, else a direct TTD marker
    static void NoteSlotWrite(EmulatorContext* context, uint8_t drive, const char* detail);

    uint8_t Drive() const { return _drive; }

    const SlotDescriptor& Descriptor() const override { return _descriptor; }
    void Attach(Medium& medium) override;
    void Detach() override;
    void SetWriteProtectSwitch(bool on) override;
    void SourceChanged(Medium& medium) override;

private:
    void ApplyWriteProtect();
    void MirrorPath(const Medium* medium);

    EmulatorContext* _context = nullptr;
    uint8_t _drive = 0;
    SlotDescriptor _descriptor;
    Medium* _medium = nullptr;
    bool _writeProtectSwitch = false;
};

/// The floppy slots of one machine, registered while this object lives
class FloppyDriveSlots
{
public:
    explicit FloppyDriveSlots(EmulatorContext* context);
    ~FloppyDriveSlots();

    FloppyDriveSlots(const FloppyDriveSlots&) = delete;
    FloppyDriveSlots& operator=(const FloppyDriveSlots&) = delete;

    /// How many drives the active controller serves: 2 with the +3 uPD765, else 4
    static uint8_t DriveCount(const EmulatorContext* context);

private:
    EmulatorContext* _context = nullptr;
    std::vector<std::unique_ptr<FloppyDriveSlot>> _slots;
};
