#pragma once

/// @file ideunitslot.h
/// @brief One IDE unit as a media manager slot: `ide0.master`, `ide0.slave`
/// (integration-ide-cd.md §2, implementation-plan.md D1-D2).
///
/// | Unit | Kind | Removable | Swap delay | Folder | Default access |
/// |---|---|---|---|---|---|
/// | hard disk | Block | no (insert / eject while paused) | - | yes: a FAT16 volume | WriteThrough (a folder: Session) |
/// | CD drive | Optical | yes | 3 s | no | ReadOnly |
///
/// The slot hands the manager's medium to its unit (non-owning) and reports
/// every sector the guest writes to the manager (a TTD replay barrier).

#include <string>

#include "emulator/io/ide/ata/atadevice.h"
#include "emulator/media/mediaslot.h"

class EmulatorContext;

class IdeUnitSlot : public IMediaSlot
{
public:
    /// `label` / `tags` / `aliases` are set by the controller that knows the board
    IdeUnitSlot(EmulatorContext* context, std::string id, AtaDevice& device, const DriveConfig& config);

    /// "ide0.master", "ide1.slave"
    static std::string IdFor(int channel, int unit);

    SlotDescriptor& MutableDescriptor() { return _descriptor; }
    AtaDevice& Device() { return _device; }

    const SlotDescriptor& Descriptor() const override { return _descriptor; }
    void Attach(Medium& medium) override;
    void Detach() override;
    bool IsBusy() const override;
    void SetWriteProtectSwitch(bool on) override;

private:
    EmulatorContext* _context = nullptr;
    SlotDescriptor _descriptor;
    AtaDevice& _device;
    DriveConfig _config;
};
