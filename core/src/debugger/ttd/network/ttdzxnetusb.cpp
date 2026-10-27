#include "debugger/ttd/network/ttdzxnetusb.h"

#include <cstring>

#include "common/modulelogger.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdinputjournal.h"
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

namespace
{
constexpr uint8_t kHasTail = 1;   ///< netstate::Adapters::reserved[0]: a tail follows the fixed part
}

void TTDZxNetUsb::TTDSaveStateTo(std::vector<uint8_t>& out) const
{
    netstate::Adapters& state = *_scratch;
    _tail.Clear();
    const SerialGuests serial = ComPort::SerialNetGuests(_context);
    if (_context && _context->pZxNetUsb)
        _context->pZxNetUsb->SaveState(state, _tail, serial);
    else
    {
        std::memset(&state, 0, sizeof(state));   // no card fitted at this frame
        state.version = netstate::kVersion;
        if (_context && _context->pVirtualNetwork)
        {
            state.networkPresent = 1;
            _context->pVirtualNetwork->SaveState(state.network, _tail, serial);
        }
    }
    state.reserved[0] = _tail.Empty() ? 0 : kHasTail;
    const uint8_t* fixed = reinterpret_cast<const uint8_t*>(&state);
    out.assign(fixed, fixed + sizeof(state));
    if (!_tail.Empty())
        _tail.AppendTo(out);
}

void TTDZxNetUsb::TTDSaveState(uint8_t* dst) const
{
    std::vector<uint8_t> blob;
    TTDSaveStateTo(blob);
    std::memcpy(dst, blob.data(), blob.size());
}

void TTDZxNetUsb::TTDLoadState(const uint8_t* src)
{
    if (!_context || (!_context->pZxNetUsb && !_context->pVirtualNetwork))
        return;
    netstate::Adapters& state = *_scratch;
    std::memcpy(&state, src, sizeof(state));
    if (state.version != netstate::kVersion)
        return;
    const netstate::Tail tail =
        (state.reserved[0] & kHasTail) ? netstate::Tail::Read(src + sizeof(state)) : netstate::Tail();

    // Received bytes come from the TTD journal (option A), or inline from the tail
    const ttd::TTDInputJournal* journal =
        _context->pTimeTravelHooks ? &_context->pTimeTravelHooks->InputJournal() : nullptr;
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

    const SerialGuests serial = ComPort::SerialNetGuests(_context);
    bool complete = true;
    if (_context->pZxNetUsb && state.present)
        complete = _context->pZxNetUsb->LoadState(state, tail, bytes, serial);
    else if (_context->pVirtualNetwork && state.networkPresent)
        _context->pVirtualNetwork->LoadState(state.network, tail, nullptr, serial);

    if (!complete && _context->pModuleLogger)
    {
        ModuleLogger* _logger = _context->pModuleLogger;
        const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DEBUGGER;
        const uint16_t _SUBMODULE = 0x0000;
        MLOGWARNING("TTDZxNetUsb: received bytes the checkpoint refers to are missing from the journal");
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
