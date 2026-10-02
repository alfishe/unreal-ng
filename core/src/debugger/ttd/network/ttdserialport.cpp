#include "debugger/ttd/network/ttdserialport.h"

#include <cstring>

#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
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

void TTDSerialPort::TTDSaveState(uint8_t* dst) const
{
    netstate::SerialPort& state = *_scratch;
    std::memset(&state, 0, sizeof(state));
    state.version = netstate::kSerialPortVersion;
    if (const ComPort* com = _port ? _port() : nullptr)
    {
        if (!com->SaveState(state.com))
            state.incomplete = 1;
    }
    std::memcpy(dst, &state, _size);
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

    // Received bytes come from the TTD journal (option A)
    const ttd::TTDInputJournal* journal =
        _context->pTimeTravelManager ? &_context->pTimeTravelManager->GetInputJournal() : nullptr;
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
    const bool complete = state.com.present && com->LoadState(state.com, bytes);
    if ((!complete || state.incomplete) && _context->pModuleLogger)
    {
        ModuleLogger* _logger = _context->pModuleLogger;
        const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DEBUGGER;
        const uint16_t _SUBMODULE = 0x0000;
        MLOGWARNING("TTD %s: the serial port state was restored incompletely (%s)", _name.c_str(),
                    state.incomplete ? "it did not fit the checkpoint limits" : "received bytes missing from the journal");
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
