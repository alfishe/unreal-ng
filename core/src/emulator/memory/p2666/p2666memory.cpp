#include "stdafx.h"

#include "p2666memory.h"

#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"

bool P2666Memory::UpdateModelBanks()
{
    EmulatorState& state = _context->emulatorState;

    // CF_TRDOS is the board's DOS signal (kept); the session flags are not used on this machine. In DOS the FDC,
    // the dispatcher and the ATM register are on the bus (CF_DOSPORTS: shown by the debugger, read by the IDE gate)
    state.flags &= ~(CF_DOSPORTS | CF_Z80FBUS | CF_LEAVEDOSRAM | CF_LEAVEDOSADR | CF_SETDOSROM);
    if (state.flags & CF_TRDOS)
        state.flags |= CF_DOSPORTS;

    if (_context->pPortDecoder)
        _context->pPortDecoder->UpdateModelMemoryBanks();
    return true;
}
