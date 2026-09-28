#include "editormonitor.h"

#include "base/featuremanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

using ROMControlPoints::PointDef;
using ROMControlPoints::RomKind;

namespace
{
constexpr int MAX_ROM_PAGES_SCANNED = 64;
}

EditorMonitor::EditorMonitor(EmulatorContext* context) : _context(context)
{
    _events.reserve(256);
}

void EditorMonitor::Arm()
{
    if (_armed || _context == nullptr || _context->pDebugManager == nullptr)
        return;

    AnalyzerManager* analyzers = _context->pDebugManager->GetAnalyzerManager();
    FeatureManager* features = _context->pFeatureManager;
    if (analyzers == nullptr)
        return;

    // The first analyzer breakpoint switches breakpoints (and debug mode) on;
    // remember what was off so Disarm() leaves the machine as it found it
    _restoreBreakpoints = features != nullptr && !features->isEnabled(Features::kBreakpoints);
    _restoreDebugMode = features != nullptr && !features->isEnabled(Features::kDebugMode);

    _armed = analyzers->activate(ANALYZER_ID);
}

void EditorMonitor::Disarm()
{
    if (!_armed)
        return;

    AnalyzerManager* analyzers = _context->pDebugManager ? _context->pDebugManager->GetAnalyzerManager() : nullptr;
    if (analyzers != nullptr)
        analyzers->deactivate(ANALYZER_ID);
    _armed = false;

    FeatureManager* features = _context->pFeatureManager;
    if (features != nullptr)
    {
        if (_restoreBreakpoints)
            features->setFeature(Features::kBreakpoints, false);
        if (_restoreDebugMode)
            features->setFeature(Features::kDebugMode, false);
    }
    _restoreBreakpoints = false;
    _restoreDebugMode = false;
}

void EditorMonitor::ClearEvents()
{
    _events.clear();
    _overflowed = false;
}

void EditorMonitor::IdentifyRomPages()
{
    if (!_romPages.empty())
        return;

    Memory* memory = _context->pMemory;
    for (int page = 0; page < MAX_ROM_PAGES_SCANNED; page++)
    {
        const uint8_t* bytes = memory->ROMPageHostAddress(static_cast<uint8_t>(page));
        _romPages.push_back(ROMControlPoints::Identify(bytes));
    }
}

int EditorMonitor::CurrentRomPage() const
{
    Memory* memory = _context ? _context->pMemory : nullptr;
    if (memory == nullptr || !memory->IsBank0ROM())
        return -1;
    return memory->GetROMPageFromAddress(memory->GetPhysicalAddressForZ80Page(0));
}

RomKind EditorMonitor::CurrentRom()
{
    IdentifyRomPages();
    const int page = CurrentRomPage();
    if (page < 0 || page >= static_cast<int>(_romPages.size()))
        return RomKind::Unknown;
    return _romPages[static_cast<size_t>(page)];
}

void EditorMonitor::onActivate(AnalyzerManager* manager)
{
    _manager = manager;
    IdentifyRomPages();
    _points.clear();

    for (size_t page = 0; page < _romPages.size(); page++)
    {
        const RomKind rom = _romPages[page];
        if (rom == RomKind::Unknown)
            continue;

        for (const PointDef& point : ROMControlPoints::All())
        {
            if (point.rom != rom)
                continue;
            manager->requestExecutionBreakpointInPage(point.address, static_cast<uint8_t>(page), BANK_ROM,
                                                      _registrationId);
            _points[Key(static_cast<uint8_t>(page), point.address)] = &point;
        }
    }
}

void EditorMonitor::onDeactivate()
{
    // AnalyzerManager releases the breakpoints after this returns
    _points.clear();
}

void EditorMonitor::onBreakpointHit(uint16_t address, Z80* cpu)
{
    const int page = CurrentRomPage();
    if (page < 0)
        return;

    const auto it = _points.find(Key(static_cast<uint8_t>(page), address));
    if (it == _points.end())
        return;
    const PointDef* point = it->second;

    if (!_events.empty() && _events.back().point == point->point && _events.back().rom == point->rom)
    {
        Event& last = _events.back();
        last.repeats++;
        last.lastFrame = _context->emulatorState.frame_counter;
        return;
    }

    if (_events.size() >= MAX_EVENTS)
    {
        _overflowed = true;
        return;
    }

    Memory& memory = *_context->pMemory;

    Event event;
    event.frame = _context->emulatorState.frame_counter;
    event.tstate = cpu->t;
    event.point = point->point;
    event.rom = point->rom;
    event.pc = address;
    event.a = cpu->a;
    event.l = cpu->l;
    event.lastK = memory.DirectReadFromZ80Memory(0x5C08);
    event.flags = memory.DirectReadFromZ80Memory(0x5C3B);
    event.errNr = memory.DirectReadFromZ80Memory(0x5C3A);
    event.lastFrame = event.frame;
    _events.push_back(event);
}
