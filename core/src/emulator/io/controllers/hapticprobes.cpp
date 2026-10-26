#include "hapticprobes.h"

#include <algorithm>

#include "debugger/debugmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace controllers
{

HapticProbes::HapticProbes(EmulatorContext* context) : _context(context) {}

HapticProbes::~HapticProbes()
{
    // The owner disarms while the machine still has its debugger (ControllerManager's destructor); this only forgets
    _ids.clear();
}

BreakpointManager* HapticProbes::Breakpoints() const
{
    if (_context == nullptr || _context->pDebugManager == nullptr)
        return nullptr;
    return _context->pDebugManager->GetBreakpointsManager();
}

uint8_t HapticProbes::ReadByte(const HapticProbe& probe) const
{
    Memory* memory = _context != nullptr ? _context->pMemory : nullptr;
    if (memory == nullptr)
        return 0;
    if (probe.page == 0xFFFF)
        return memory->DirectReadFromZ80Memory(probe.address);
    const uint8_t* page = memory->RAMPageAddress(probe.page);
    return page != nullptr ? page[probe.address & 0x3FFF] : 0;
}

void HapticProbes::Arm(const std::vector<HapticProbe>& probes)
{
    Disarm();
    BreakpointManager* breakpoints = Breakpoints();
    if (breakpoints == nullptr || probes.empty())
        return;
    _sink = breakpoints->RegisterTapSink(this);
    if (_sink == BRK_NO_TAP)
        return;
    _probes.assign(probes.begin(), probes.begin() + std::min<size_t>(probes.size(), HapticFrame::kMaxProbes));
    // One repaint for all of them: the tap demand switches once
    BreakpointManager::Batch batch(*breakpoints);
    for (size_t index = 0; index < _probes.size(); index++)
    {
        const HapticProbe& probe = _probes[index];
        auto* descriptor = new BreakpointDescriptor();
        descriptor->hidden = true;
        descriptor->owner = kOwner;
        descriptor->note = "haptic probe";
        descriptor->tapSink = _sink;
        descriptor->tapCookie = static_cast<uint32_t>(index);
        if (probe.kind == HapticProbeKind::PortIn || probe.kind == HapticProbeKind::PortOut)
        {
            descriptor->type = BRK_IO;
            descriptor->ioType = probe.kind == HapticProbeKind::PortIn ? BRK_IO_IN : BRK_IO_OUT;
            descriptor->z80address = probe.address;
            descriptor->portMask = probe.portMask;
        }
        else
        {
            descriptor->type = BRK_MEMORY;
            descriptor->memoryType = probe.kind == HapticProbeKind::Exec   ? BRK_MEM_EXECUTE
                                     : probe.kind == HapticProbeKind::Read ? BRK_MEM_READ
                                                                           : BRK_MEM_WRITE;
            descriptor->z80address = probe.page == 0xFFFF ? probe.address : (probe.address & 0x3FFF);
            if (probe.end > probe.address)
            {
                descriptor->isRange = true;
                descriptor->z80addressEnd = probe.page == 0xFFFF ? probe.end : (probe.end & 0x3FFF);
            }
            if (probe.page != 0xFFFF)
            {
                descriptor->matchType = BRK_MATCH_BANK_ADDR;
                descriptor->page = static_cast<uint8_t>(probe.page);
                descriptor->pageType = BANK_RAM;
            }
            if (probe.kind == HapticProbeKind::Write)
                _last[index] = ReadByte(probe);
        }
        _ids.push_back(breakpoints->AddBreakpoint(descriptor));
    }
    Clear();
}

void HapticProbes::Disarm()
{
    // Unregistering removes the taps (and with them what they held on: the exec check, the write hook, the debug path)
    BreakpointManager* breakpoints = Breakpoints();
    if (breakpoints != nullptr && _sink != BRK_NO_TAP)
        breakpoints->UnregisterTapSink(_sink);
    _sink = BRK_NO_TAP;
    _ids.clear();
    _probes.clear();
    Clear();
}

void HapticProbes::Clear()
{
    _hits.fill(0);
    _eventCount = 0;
}

void HapticProbes::TakeFrame(HapticFrame& frame)
{
    frame.probeCount = static_cast<unsigned>(_probes.size());
    frame.probeHits = _hits;
    frame.probeEventCount = _eventCount;
    std::copy(_events.begin(), _events.begin() + _eventCount, frame.probeEvents.begin());
    Clear();
}

void HapticProbes::OnTap(const BreakpointTapHit& hit)
{
    const uint32_t probe = hit.cookie;
    const uint16_t address = hit.address;
    const int value = hit.value;
    if (probe >= _probes.size())
        return;
    if (_hits[probe] != 0xFFFF)
        _hits[probe]++;
    const HapticProbeKind kind = _probes[probe].kind;
    if (kind != HapticProbeKind::Write && kind != HapticProbeKind::PortOut)
        return;
    HapticProbeEvent event;
    event.probe = static_cast<uint8_t>(probe);
    if (kind == HapticProbeKind::Write)
    {
        // The breakpoint is told after the write: memory holds the new byte, _last the one it replaced
        const uint8_t now = _context->pMemory != nullptr ? _context->pMemory->DirectReadFromZ80Memory(address) : 0;
        event.before = _last[probe];
        event.value = now;
        _last[probe] = now;
    }
    else
    {
        event.value = static_cast<uint8_t>(value < 0 ? 0 : value);
    }
    if (_eventCount < _events.size())
        _events[_eventCount++] = event;
}

}  // namespace controllers
