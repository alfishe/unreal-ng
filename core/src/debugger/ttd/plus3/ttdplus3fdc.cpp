#include "ttdplus3fdc.h"

#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/upd765.h"

namespace ttd
{
TTDPlus3Fdc::TTDPlus3Fdc(EmulatorContext* context) : _context(context)
{
}

size_t TTDPlus3Fdc::TTDStateSize() const
{
    return (_context && _context->pUPD765) ? _context->pUPD765->TTDStateSize() : 0;
}

void TTDPlus3Fdc::TTDSaveState(uint8_t* dst) const
{
    if (_context && _context->pUPD765)
        _context->pUPD765->TTDSaveState(dst);
}

void TTDPlus3Fdc::TTDLoadState(const uint8_t* src)
{
    if (_context && _context->pUPD765)
        _context->pUPD765->TTDLoadState(src);
}

uint64_t TTDPlus3Fdc::TTDHashState() const
{
    return (_context && _context->pUPD765) ? _context->pUPD765->TTDHashState() : 0;
}
} // namespace ttd
