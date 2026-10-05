#include "debugger/ttd/network/ttdmachineserialpeer.h"

#include <cstring>

#include "common/modulelogger.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdinputjournal.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/serial/comport.h"

namespace ttd
{

TTDMachineSerialPeer::TTDMachineSerialPeer(EmulatorContext* context)
    : _context(context), _scratch(std::make_unique<netstate::Com>())
{
}

void TTDMachineSerialPeer::TTDSaveState(uint8_t* dst) const
{
    netstate::Com& state = *_scratch;
    std::memset(&state, 0, sizeof(state));
    if (_context && _context->pMachineSerialPeer)
    {
        state.present = 1;
        if (!ComPort::SavePeer(_context->pMachineSerialPeer, state))
            state.reserved[0] = 1;   // incomplete: did not fit the limits
    }
    std::memcpy(dst, &state, sizeof(state));
}

void TTDMachineSerialPeer::TTDLoadState(const uint8_t* src)
{
    if (!_context || !_context->pMachineSerialPeer)
        return;
    netstate::Com& state = *_scratch;
    std::memcpy(&state, src, sizeof(state));
    if (!state.present)
        return;
    const ttd::TTDInputJournal* journal =
        _context->pTimeTravelHooks ? &_context->pTimeTravelHooks->InputJournal() : nullptr;
    ComPort::ByteSource bytes = [journal](uint32_t source, uint32_t offset, uint32_t length, std::vector<uint8_t>& out) {
        if (!journal)
            return false;
        const TTDNetInput* net = journal->NetAt(source);
        if (!net || static_cast<uint64_t>(offset) + length > net->payloadLength)
            return false;
        const uint8_t* payload = journal->PayloadOf(*net);
        if (!payload)
            return false;
        out.assign(payload + offset, payload + offset + length);
        return true;
    };
    const bool complete = ComPort::LoadPeer(_context->pMachineSerialPeer, state, bytes) && !state.reserved[0];
    if (!complete && _context->pModuleLogger)
    {
        ModuleLogger* _logger = _context->pModuleLogger;
        const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DEBUGGER;
        const uint16_t _SUBMODULE = 0x0000;
        MLOGWARNING("TTDMachineSerialPeer: the serial peer was restored incompletely");
    }
}

}  // namespace ttd
