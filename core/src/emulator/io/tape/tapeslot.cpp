#include "stdafx.h"

#include "tapeslot.h"

#include "emulator/emulatorcontext.h"
#include "emulator/io/tape/tape.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/medium.h"

TapeSlot::TapeSlot(EmulatorContext* context) : _context(context)
{
    _descriptor.id = kId;
    _descriptor.kind = MediaKind::Tape;
    _descriptor.label = "Tape";
    _descriptor.removable = true;
    _descriptor.swapDelayMs = 0;
    _descriptor.acceptsFolder = true;
    _descriptor.defaultAccess = AccessMode::ReadOnly;
    _descriptor.tags = {"cassette"};
    _descriptor.aliases = {"tape"};

    if (_context && _context->pMediaManager)
    {
        _context->pMediaManager->RegisterSlot(*this);
        _registered = true;
    }
}

TapeSlot::~TapeSlot()
{
    if (_registered && _context && _context->pMediaManager)
        _context->pMediaManager->UnregisterSlot(_descriptor.id);
}

void TapeSlot::Attach(Medium& medium)
{
    Tape* tape = _context ? _context->pTape : nullptr;
    if (!tape || !medium.Tape())
        return;
    const std::string path = medium.Source().type == MediaSourceType::Blank ? "<blank>" : medium.Source().path;
    tape->AttachImage(medium.Tape(), path, medium.Tape()->formatId);
}

void TapeSlot::Detach()
{
    if (Tape* tape = _context ? _context->pTape : nullptr)
        tape->DetachImage();
}
