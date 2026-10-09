#include "debugger/ttd/network/ttdserialport.h"

#include <cstring>

#include "common/modulelogger.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdinputjournal.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/serial/comport.h"

namespace ttd
{

TTDSerialPort::TTDSerialPort(EmulatorContext* context)
    : TTDSerialPort(context, [context]() { return context ? context->pComPort : nullptr; }, PeripheralId::SerialPort,
                    "SerialPort")
{
}

TTDSerialPort::TTDSerialPort(EmulatorContext* context, std::function<ComPort*()> port, PeripheralId id, std::string name)
    : _context(context), _port(std::move(port)), _id(id), _name(std::move(name)),
      _scratch(std::make_unique<netstate::SerialPort>())
{
    const ComPort* com = _port ? _port() : nullptr;
    if (com && com->Peer())
        _size = sizeof(netstate::SerialPort);
}

void TTDSerialPort::TTDSaveStateTo(std::vector<uint8_t>& out) const
{
    netstate::SerialPort& state = *_scratch;
    std::memset(&state, 0, sizeof(state));
    state.version = netstate::kSerialPortVersion;
    _tail.Clear();
    if (const ComPort* com = _port ? _port() : nullptr)
        com->SaveState(state.com, _tail);
    const bool tail = HasPeer() && !_tail.Empty();   // the short blob has no peer, so nothing for a tail
    state.flags = tail ? netstate::kSerialPortHasTail : 0;
    const uint8_t* fixed = reinterpret_cast<const uint8_t*>(&state);
    out.assign(fixed, fixed + _size);
    if (tail)
        _tail.AppendTo(out);
}

void TTDSerialPort::TTDSaveState(uint8_t* dst) const
{
    std::vector<uint8_t> blob;
    TTDSaveStateTo(blob);
    std::memcpy(dst, blob.data(), blob.size());
}

void TTDSerialPort::TTDLoadState(const uint8_t* src)
{
    ComPort* com = _port ? _port() : nullptr;
    if (!com)
        return;
    netstate::SerialPort& state = *_scratch;
    std::memset(&state, 0, sizeof(state));
    std::memcpy(&state, src, _size);
    if (state.version != netstate::kSerialPortVersion)
        return;
    if (_size < sizeof(netstate::SerialPort))
        state.com.peerKind = 0;   // the short blob: the UART only
    const netstate::Tail tail =
        HasPeer() && (state.flags & netstate::kSerialPortHasTail) ? netstate::Tail::Read(src + _size) : netstate::Tail();

    // Received bytes come from the TTD journal (option A), or inline from the tail
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
    const bool complete = state.com.present && com->LoadState(state.com, tail, bytes);
    if (!complete && _context->pModuleLogger)
    {
        ModuleLogger* _logger = _context->pModuleLogger;
        const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DEBUGGER;
        const uint16_t _SUBMODULE = 0x0000;
        MLOGWARNING("TTD %s: received bytes the checkpoint refers to are missing from the journal", _name.c_str());
    }
}

uint64_t TTDSerialPort::TTDHashState() const
{
    const ComPort* com = _port ? _port() : nullptr;
    if (!com)
        return 0;
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    const Uart16550::View u = com->Uart().GetView();
    mix(u.lcr);
    mix(u.mcr);
    mix(u.lsr);
    mix(u.rxCount);
    mix(u.txCount);
    mix(u.bytesIn);
    mix(u.bytesOut);
    return h;
}

}  // namespace ttd
