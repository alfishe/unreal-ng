#include "debugger/ttd/network/ttdzxnetusb.h"

#include <cstring>

#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/network/zxnetusb.h"
#include "emulator/io/serial/comport.h"

namespace ttd
{

TTDZxNetUsb::TTDZxNetUsb(EmulatorContext* context)
    : _context(context), _scratch(std::make_unique<netstate::Adapters>())
{
}

void TTDZxNetUsb::TTDSaveState(uint8_t* dst) const
{
    netstate::Adapters& state = *_scratch;
    ComPort* com = _context ? _context->pComPort : nullptr;
    INetGuest* comGuest = com ? com->NetGuest() : nullptr;
    bool complete = true;
    if (_context && _context->pZxNetUsb)
        complete = _context->pZxNetUsb->SaveState(state, comGuest);
    else
    {
        std::memset(&state, 0, sizeof(state));   // no card fitted at this frame
        state.version = netstate::kVersion;
        if (_context && _context->pVirtualNetwork)
        {
            state.networkPresent = 1;
            complete = _context->pVirtualNetwork->SaveState(state.network, comGuest);
        }
    }
    if (!complete)
        state.incomplete = 1;
    std::memcpy(dst, &state, sizeof(state));
}

void TTDZxNetUsb::TTDLoadState(const uint8_t* src)
{
    if (!_context || (!_context->pZxNetUsb && !_context->pVirtualNetwork))
        return;
    netstate::Adapters& state = *_scratch;
    std::memcpy(&state, src, sizeof(state));
    if (state.version != netstate::kVersion)
        return;

    // Received bytes come from the TTD journal (option A)
    const ttd::TTDInputJournal* journal =
        _context->pTimeTravelManager ? &_context->pTimeTravelManager->GetInputJournal() : nullptr;
    W5300::ByteSource bytes = [journal](uint32_t source, uint32_t offset, uint32_t length, std::vector<uint8_t>& out) {
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

    ComPort* com = _context->pComPort;
    INetGuest* comGuest = com ? com->NetGuest() : nullptr;
    bool complete = true;
    if (_context->pZxNetUsb && state.present)
        complete = _context->pZxNetUsb->LoadState(state, bytes, comGuest);
    else if (_context->pVirtualNetwork && state.networkPresent)
        _context->pVirtualNetwork->LoadState(state.network, nullptr, comGuest);

    if ((!complete || state.incomplete) && _context->pModuleLogger)
    {
        ModuleLogger* _logger = _context->pModuleLogger;
        const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DEBUGGER;
        const uint16_t _SUBMODULE = 0x0000;
        MLOGWARNING("TTDZxNetUsb: the network adapter state was restored incompletely (%s)",
                    state.incomplete ? "it did not fit the checkpoint limits" : "received bytes missing from the journal");
    }
}

uint64_t TTDZxNetUsb::TTDHashState() const
{
    if (!_context || !_context->pZxNetUsb)
        return 0;
    // Socket states and buffer levels: enough to see a replay leave the recorded path
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    const ZxNetUsb& card = *_context->pZxNetUsb;
    mix(card.Control());
    mix(card.Mode());
    mix(card.AddressHigh());
    for (int n = 0; n < W5300::kSockets; ++n)
    {
        const W5300::SocketView v = card.Chip().GetSocket(n);
        mix(v.state);
        mix(v.ir);
        mix(v.rxReceived);
        mix(v.txFree);
    }
    return h;
}

}  // namespace ttd
