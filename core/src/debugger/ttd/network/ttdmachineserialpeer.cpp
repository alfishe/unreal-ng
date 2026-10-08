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

namespace
{
/// netstate::Com::reserved[0]: a tail follows (1 was "incomplete" before 2026-10-08, ignored)
constexpr uint8_t kHasTail = 2;
}  // namespace

void TTDMachineSerialPeer::TTDSaveStateTo(std::vector<uint8_t>& out) const
{
    netstate::Com& state = *_scratch;
    std::memset(&state, 0, sizeof(state));
    _tail.Clear();
    if (_context && _context->pMachineSerialPeer)
    {
        state.present = 1;
        ComPort::SavePeer(_context->pMachineSerialPeer, state, _tail);
    }
    state.reserved[0] = _tail.Empty() ? 0 : kHasTail;
    const uint8_t* fixed = reinterpret_cast<const uint8_t*>(&state);
    out.assign(fixed, fixed + sizeof(state));
    if (!_tail.Empty())
        _tail.AppendTo(out);
}

void TTDMachineSerialPeer::TTDSaveState(uint8_t* dst) const
{
    std::vector<uint8_t> blob;
    TTDSaveStateTo(blob);
    std::memcpy(dst, blob.data(), blob.size());
}

void TTDMachineSerialPeer::TTDLoadState(const uint8_t* src)
{
    if (!_context || !_context->pMachineSerialPeer)
        return;
    netstate::Com& state = *_scratch;
    std::memcpy(&state, src, sizeof(state));
    if (!state.present)
        return;
    const netstate::Tail tail =
        (state.reserved[0] & kHasTail) ? netstate::Tail::Read(src + sizeof(state)) : netstate::Tail();
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
    const bool complete = ComPort::LoadPeer(_context->pMachineSerialPeer, state, tail, bytes);
    if (!complete && _context->pModuleLogger)
    {
        ModuleLogger* _logger = _context->pModuleLogger;
        const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DEBUGGER;
        const uint16_t _SUBMODULE = 0x0000;
        MLOGWARNING("TTDMachineSerialPeer: received bytes the checkpoint refers to are missing from the journal");
    }
}

}  // namespace ttd
