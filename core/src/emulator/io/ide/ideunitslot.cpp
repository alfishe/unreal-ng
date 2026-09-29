#include "stdafx.h"

#include "ideunitslot.h"

#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/medium.h"

namespace
{
    constexpr uint32_t kCdSwapDelayMs = 3000;  ///< a disc change: the drive reports "not ready" meanwhile
}

IdeUnitSlot::IdeUnitSlot(EmulatorContext* context, std::string id, AtaDevice& device, const DriveConfig& config)
    : _context(context), _device(device), _config(config)
{
    const bool cd = device.Kind() == AtaDeviceKind::Cdrom;
    _descriptor.id = std::move(id);
    _descriptor.kind = cd ? MediaKind::Optical : MediaKind::Block;
    _descriptor.removable = cd;
    _descriptor.swapDelayMs = cd ? kCdSwapDelayMs : 0;
    _descriptor.acceptsFolder = !cd;
    _descriptor.defaultAccess = cd ? AccessMode::ReadOnly : AccessMode::WriteThrough;
    _descriptor.defaultFs = FatType::Fat16;
    _descriptor.hasWriteProtectSwitch = !cd;

    const std::string slotId = _descriptor.id;
    _device.SetWriteListener([context, slotId] {
        if (context && context->pMediaManager)
            context->pMediaManager->NoteWrite(slotId, "IDE write");
    });
}

std::string IdeUnitSlot::IdFor(int channel, int unit)
{
    return "ide" + std::to_string(channel) + (unit == 0 ? ".master" : ".slave");
}

void IdeUnitSlot::Attach(Medium& medium)
{
    IBlockDevice* block = medium.Block();
    if (!block)
        return;
    _device.AttachMedium(*block, _config);
}

void IdeUnitSlot::Detach()
{
    _device.DetachMedium();
}

bool IdeUnitSlot::IsBusy() const
{
    // A sector (or a command packet) half way through the data register: swap
    // at the next frame. A transfer the guest abandoned between blocks does
    // not hold a swap back forever
    const AtaDeviceState& s = _device.State();
    return static_cast<AtaPhase>(s.phase) != AtaPhase::Idle && s.bufferPos > 0 && s.bufferPos < s.bufferLen;
}

void IdeUnitSlot::SetWriteProtectSwitch(bool on)
{
    // Only what WRITE answers changes: the disk, its state and a transfer in
    // flight stay as they are (called from the API thread while the machine runs)
    _device.SetWriteProtectSwitch(on);
}
