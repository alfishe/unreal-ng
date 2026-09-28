#include "stdafx.h"

#include "floppydriveslot.h"

#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/medium.h"

namespace
{
    constexpr uint32_t kFloppySwapDelayMs = 2000;  ///< WinUAE: TR-DOS / +3DOS see the disk change
}

/// region <FloppyDriveSlot>

FloppyDriveSlot::FloppyDriveSlot(EmulatorContext* context, uint8_t drive) : _context(context), _drive(drive)
{
    _descriptor.id = IdFor(drive);
    _descriptor.kind = MediaKind::Floppy;
    _descriptor.label = std::string("Drive ") + static_cast<char>('A' + drive);
    _descriptor.removable = true;
    _descriptor.swapDelayMs = kFloppySwapDelayMs;
    _descriptor.acceptsFolder = true;
    _descriptor.defaultAccess = AccessMode::Session;
    _descriptor.hasWriteProtectSwitch = true;

    // The +3's uPD765 runs +3DOS; the Beta 128 WD1793 runs TR-DOS
    const bool plus3 = context && context->pUPD765;
    _descriptor.tags = {plus3 ? "upd765" : "wd1793", plus3 ? "plus3dos" : "trdos"};
    if (drive == 0)
        _descriptor.tags.push_back("boot");
    _descriptor.aliases = {std::string(1, static_cast<char>('A' + drive))};
}

std::string FloppyDriveSlot::IdFor(uint8_t drive)
{
    return std::string("fdd.") + static_cast<char>('a' + drive);
}

void FloppyDriveSlot::NoteSlotWrite(EmulatorContext* context, uint8_t drive, const char* detail)
{
    if (!context)
        return;
    if (context->pMediaManager)
    {
        context->pMediaManager->NoteWrite(IdFor(drive), detail);
        return;
    }
    // A controller without a machine around it (unit tests): the marker alone
    if (context->pTimeTravelManager)
        context->pTimeTravelManager->RecordExternalEvent(ttd::TTDExternalEventKind::DiskWrite, detail);
}

void FloppyDriveSlot::Attach(Medium& medium)
{
    FDD* fdd = _context->coreState.diskDrives[_drive];
    if (!fdd || !medium.Floppy())
        return;
    _medium = &medium;
    fdd->insertDisk(medium.Floppy());
    ApplyWriteProtect();
    MirrorPath(&medium);
}

void FloppyDriveSlot::Detach()
{
    if (FDD* fdd = _context->coreState.diskDrives[_drive])
    {
        fdd->ejectDisk();
        fdd->setWriteProtect(false);
    }
    _medium = nullptr;
    MirrorPath(nullptr);
}

void FloppyDriveSlot::SetWriteProtectSwitch(bool on)
{
    _writeProtectSwitch = on;
    ApplyWriteProtect();
}

void FloppyDriveSlot::SourceChanged(Medium& medium)
{
    MirrorPath(&medium);
}

void FloppyDriveSlot::ApplyWriteProtect()
{
    // The floppy's tab is sensed by the drive and honored by the controller;
    // a read-only medium is a disk with its tab open
    FDD* fdd = _context->coreState.diskDrives[_drive];
    if (fdd)
        fdd->setWriteProtect(_writeProtectSwitch || (_medium && _medium->Access() == AccessMode::ReadOnly));
}

void FloppyDriveSlot::MirrorPath(const Medium* medium)
{
    // coreState.diskFilePaths is what the surfaces show until they read the manager (M4)
    std::string& path = _context->coreState.diskFilePaths[_drive];
    if (!medium)
        path.clear();
    else if (medium->Source().type == MediaSourceType::Blank)
        path = "<blank>";
    else
        path = medium->Source().path;
}

/// endregion </FloppyDriveSlot>

/// region <FloppyDriveSlots>

FloppyDriveSlots::FloppyDriveSlots(EmulatorContext* context) : _context(context)
{
    if (!_context || !_context->pMediaManager)
        return;
    const uint8_t count = DriveCount(_context);
    for (uint8_t drive = 0; drive < count; drive++)
    {
        if (!_context->coreState.diskDrives[drive])
            continue;
        _slots.push_back(std::make_unique<FloppyDriveSlot>(_context, drive));
        _context->pMediaManager->RegisterSlot(*_slots.back());
    }
}

FloppyDriveSlots::~FloppyDriveSlots()
{
    if (!_context || !_context->pMediaManager)
        return;
    for (const auto& slot : _slots)
        _context->pMediaManager->UnregisterSlot(slot->Descriptor().id);
}

uint8_t FloppyDriveSlots::DriveCount(const EmulatorContext* context)
{
    // The +3's uPD765 decodes two unit-select lines' worth of drives (A / B);
    // the WD1793 on a Beta Disk interface serves four
    return (context && context->pUPD765) ? 2 : 4;
}

/// endregion </FloppyDriveSlots>
