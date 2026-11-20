#include "debugger/debuggercpu.h"

#include <QSignalBlocker>
#include <QTabBar>

#include <algorithm>

#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/targets/gscardtarget.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"

namespace debuggercpu
{
namespace
{
GSCardTarget* Target(EmulatorContext* context, DebugCpuId cpu)
{
    DebugManager* debug = context ? context->pDebugManager : nullptr;
    GSCardTarget* target = debug && IsCardCpu(cpu) ? debug->GetCardTarget(cpu) : nullptr;
    return target && target->Available() ? target : nullptr;
}
}  // namespace

std::vector<DebugCpuId> Fitted(EmulatorContext* context)
{
    std::vector<DebugCpuId> cpus{DebugCpuId::Main};
    for (const DebugCpuId cpu : {DebugCpuId::GS, DebugCpuId::MultiSoundGS})
        if (Target(context, cpu))
            cpus.push_back(cpu);
    return cpus;
}

QString Title(EmulatorContext* context, DebugCpuId cpu)
{
    switch (cpu)
    {
        case DebugCpuId::GS:
        {
            GeneralSoundCard* card = Card(context, cpu);
            return card && card->implementation() == GSCardImplementation::NGS ? QStringLiteral("NeoGS")
                                                                                : QStringLiteral("GS");
        }
        case DebugCpuId::MultiSoundGS:
            return QStringLiteral("MultiSound GS");
        default:
            return QStringLiteral("Main CPU");
    }
}

GeneralSoundCard* Card(EmulatorContext* context, DebugCpuId cpu)
{
    GSCardTarget* target = Target(context, cpu);
    return target ? target->Card() : nullptr;
}

bool Registers(EmulatorContext* context, DebugCpuId cpu, Z80Registers& out)
{
    if (!IsCardCpu(cpu))
    {
        Z80* z80 = context && context->pCore ? context->pCore->GetZ80() : nullptr;
        if (!z80)
            return false;
        out = static_cast<const Z80Registers&>(*z80);
        return true;
    }
    GSCardTarget* target = Target(context, cpu);
    return target && target->Registers(out);
}

uint8_t Peek(EmulatorContext* context, DebugCpuId cpu, uint16_t address)
{
    if (!IsCardCpu(cpu))
        return context && context->pMemory ? context->pMemory->DirectReadFromZ80Memory(address) : 0xFF;
    uint8_t value = 0xFF;
    if (GSCardTarget* target = Target(context, cpu))
        target->Peek(address, value);
    return value;
}

QString CardWindowName(EmulatorContext* context, DebugCpuId cpu, uint8_t window)
{
    GeneralSoundCard* card = Card(context, cpu);
    if (!card || window > 3)
        return {};
    const GeneralSoundCard::CardPage page = card->mapCardAddress(static_cast<uint16_t>(window * 0x4000));
    switch (page.kind)
    {
        case GeneralSoundCard::CardPage::Rom:
            return QStringLiteral("ROM %1").arg(static_cast<int>(page.page));
        case GeneralSoundCard::CardPage::Ram:
            return QStringLiteral("RAM %1").arg(static_cast<int>(page.page));
        default:
            return QStringLiteral("none");
    }
}

std::optional<DebugCpuId> StopCpu(const Emulator::DebugStop& stop)
{
    using Reason = Emulator::DebugStop::Reason;
    if (stop.reason != Reason::Breakpoint && stop.reason != Reason::Step)
        return std::nullopt;
    return stop.breakpoint.cpu;
}

CardSource::CardSource(EmulatorContext* context, DebugCpuId cpu)
    : _card(Card(context, cpu)), _debug(context ? context->pDebugManager : nullptr), _cpu(cpu)
{
    if (_card)
        _hasRegisters = debuggercpu::Registers(context, cpu, _registers);
}

uint8_t CardSource::Read(uint16_t address) const
{
    uint8_t value = 0xFF;
    if (_card)
        _card->peekCardMemory(address, value);
    return value;
}

void CardSource::MarksOf(uint16_t from, uint16_t to, disasm::Marks& out) const
{
    out = {};
    if (!_card || !_debug)
        return;
    const auto inStretch = [&](uint32_t address) { return address >= from && address <= to; };
    if (_hasRegisters && inStretch(_registers.pc))
        out.anchors.push_back(_registers.pc);
    if (LabelManager* labels = _debug->GetLabelManager(_cpu))
        for (const auto& label : labels->GetLabelsInRange(from, to))
            if (label && label->type != "data" && label->type != "const" &&
                labels->GetLabelByZ80Address(label->address) == label)
                out.anchors.push_back(label->address);
    if (BreakpointManager* breakpoints = _debug->GetBreakpointsManager(_cpu))
        for (const auto& pair : breakpoints->GetAllBreakpoints())
        {
            const BreakpointDescriptor* bp = pair.second.get();
            if (bp && bp->type == BRK_MEMORY && (bp->memoryType & BRK_MEM_EXECUTE) && bp->active && !bp->isRange &&
                inStretch(bp->z80address))
                out.anchors.push_back(bp->z80address);
        }
    std::sort(out.anchors.begin(), out.anchors.end());
    out.anchors.erase(std::unique(out.anchors.begin(), out.anchors.end()), out.anchors.end());
}
}  // namespace debuggercpu

DebuggerCpuTabs::DebuggerCpuTabs(QTabBar* bar, QObject* parent) : QObject(parent), _bar(bar)
{
    _bar->setObjectName(QStringLiteral("debuggerCpuTabs"));
    _bar->setDocumentMode(true);
    _bar->setExpanding(false);
    _bar->setDrawBase(false);
    _bar->setToolTip(tr("The processor the registers, disassembly, memory, steps and time controls act on"));
    connect(_bar, &QTabBar::currentChanged, this, [this](int index) {
        if (_building || index < 0 || static_cast<size_t>(index) >= _cpus.size() || _cpus[index] == _current)
            return;
        _current = _cpus[index];
        applySelection();
        emit cpuChanged(_current);
    });
    rebuild();
}

void DebuggerCpuTabs::setEmulator(std::shared_ptr<Emulator> emulator)
{
    if (emulator == _emulator)
    {
        update();
        return;
    }
    release();
    _emulator = std::move(emulator);
    _stopSerial = _emulator ? _emulator->LastStop().serial : 0;
    const DebugCpuId before = _current;
    _current = DebugCpuId::Main;
    _cpus.clear();
    update();
    applySelection();
    if (_current != before)
        emit cpuChanged(_current);
}

bool DebuggerCpuTabs::update()
{
    // Under the context's lease: a machine being removed has nothing to show
    std::vector<DebugCpuId> cpus{DebugCpuId::Main};
    std::vector<QString> titles;
    {
        const Emulator::ContextLease lease = _emulator ? _emulator->LeaseContext() : Emulator::ContextLease{};
        cpus = debuggercpu::Fitted(lease.get());
        for (const DebugCpuId cpu : cpus)
            titles.push_back(debuggercpu::Title(lease.get(), cpu));
    }
    if (cpus == _cpus && titles == _titles)
        return false;
    _cpus = std::move(cpus);
    _titles = std::move(titles);
    const DebugCpuId before = _current;
    if (std::find(_cpus.begin(), _cpus.end(), _current) == _cpus.end())
        _current = DebugCpuId::Main;
    rebuild();
    if (_current == before)
        return false;
    applySelection();
    emit cpuChanged(_current);
    return true;
}

void DebuggerCpuTabs::rebuild()
{
    _building = true;
    {
        const QSignalBlocker blocker(_bar);
        while (_bar->count() > 0)
            _bar->removeTab(0);
        if (_titles.size() != _cpus.size())
        {
            _titles.clear();
            for (const DebugCpuId cpu : _cpus)
                _titles.push_back(debuggercpu::Title(nullptr, cpu));
        }
        for (size_t i = 0; i < _cpus.size(); ++i)
        {
            const int index = _bar->addTab(_titles[i]);
            _bar->setTabToolTip(index, IsCardCpu(_cpus[i])
                                           ? tr("The %1 card's Z80 (%2): its registers, code, 64 KB, steps and "
                                                "time controls")
                                                 .arg(_titles[i], QString::fromLatin1(DebugCpuName(_cpus[i])))
                                           : tr("The machine's Z80"));
        }
        const auto at = std::find(_cpus.begin(), _cpus.end(), _current);
        _bar->setCurrentIndex(static_cast<int>(at - _cpus.begin()));
    }
    // The main CPU alone: no bar, the window as it always was
    _bar->setVisible(_cpus.size() > 1);
    _building = false;
}

bool DebuggerCpuTabs::select(DebugCpuId cpu)
{
    const auto at = std::find(_cpus.begin(), _cpus.end(), cpu);
    if (at == _cpus.end())
        return false;
    if (cpu == _current)
        return true;
    _bar->setCurrentIndex(static_cast<int>(at - _cpus.begin()));  // currentChanged: applies and tells
    return true;
}

bool DebuggerCpuTabs::followStop()
{
    if (!_emulator)
        return false;
    const Emulator::DebugStop stop = _emulator->LastStop();
    if (stop.serial == _stopSerial)
        return false;
    _stopSerial = stop.serial;
    const std::optional<DebugCpuId> cpu = debuggercpu::StopCpu(stop);
    if (!cpu || *cpu == _current)
        return false;
    update();  // a card's stop may come before the window saw the card
    return select(*cpu) && _current == *cpu;
}

void DebuggerCpuTabs::setActive(bool active)
{
    if (_active == active)
        return;
    _active = active;
    if (active)
        applySelection();
    else
        release();
}

void DebuggerCpuTabs::applySelection()
{
    if (!_active || !_emulator)
        return;
    const Emulator::ContextLease lease = _emulator->LeaseContext();
    if (DebugManager* debug = lease ? lease.get()->pDebugManager : nullptr)
        if (debug->SelectedCpu() != _current)
            debug->SelectCpu(_current);
}

void DebuggerCpuTabs::release()
{
    if (!_emulator)
        return;
    // Only this window's choice is undone (another front-end may have picked a card since)
    const Emulator::ContextLease lease = _emulator->LeaseContext();
    if (DebugManager* debug = lease ? lease.get()->pDebugManager : nullptr)
        if (debug->SelectedCpu() != DebugCpuId::Main && debug->SelectedCpu() == _current)
            debug->SelectCpu(DebugCpuId::Main);
}
