#pragma once

// The Embedded debugger's processors (gs-debugger design §7.3; TTD branches debuggers.md §8): the main Z80 and each
// sound card CPU fitted (the General Sound or NeoGS in the GS slot, the MultiSound's GS), one tab each above the
// panes. The tab in front is the CPU the registers, the disassembly, the memory view (that CPU's 64 KB), the
// breakpoints toggled in the disassembly, the steps and the time group act on. With the main CPU alone the bar is
// hidden and the window looks as it always did.
//
// What a pane reads of a card goes through its debug target (GSCardTarget: peeks with no side effects, the card's
// registers, labels and breakpoints) on the GUI thread of a paused machine, as eCat's card tab reads it; nothing here
// runs on the emulation thread.
//
// A stop brings its CPU's tab to the front once (Emulator::LastStop().serial): a card's breakpoint or step the card's,
// a main CPU's breakpoint or step the main CPU's. A pause, a frame watch and every move in time leave the tab in front
// alone, so a card's Step Back keeps the card's tab.

#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "debugger/disassembler/disassemblywindow.h"
#include "debugger/targets/debugcpuid.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"

class DebugManager;
class EmulatorContext;
class GeneralSoundCard;
class QTabBar;

namespace debuggercpu
{
/// The CPUs the window can show now: the main CPU first, then each card CPU fitted (GS, then MultiSound GS)
std::vector<DebugCpuId> Fitted(EmulatorContext* context);
/// A tab's title: "Main CPU", "GS", "NeoGS" (the GS slot's NeoGS), "MultiSound GS"
QString Title(EmulatorContext* context, DebugCpuId cpu);
/// The card whose CPU `cpu` is (null: the main CPU, or no such card now)
GeneralSoundCard* Card(EmulatorContext* context, DebugCpuId cpu);
/// The CPU's registers now; a card's as its debug target reads them (T 0: the card counts its own clock). False
/// without the CPU
bool Registers(EmulatorContext* context, DebugCpuId cpu, Z80Registers& out);
/// A byte as the CPU sees it now, no side effects; #FF without the CPU
uint8_t Peek(EmulatorContext* context, DebugCpuId cpu, uint16_t address);
/// What a card CPU's 16 KB window `window` (0..3) maps now: "ROM 1", "RAM 3", "none"; empty without the card
QString CardWindowName(EmulatorContext* context, DebugCpuId cpu, uint8_t window);
/// The CPU a stop names: a breakpoint's or a step's CPU; none for a pause, a frame watch or no stop yet
std::optional<DebugCpuId> StopCpu(const Emulator::DebugStop& stop);

/// A card CPU's 64 KB for the disassembly window: bytes by peeks, anchors from the card's PC, its code labels and
/// its execution breakpoints, no markup (the memory markup is the main CPU's); the runtime hints read the card's
/// registers as they were when the source was made
class CardSource : public disasm::Source
{
public:
    CardSource(EmulatorContext* context, DebugCpuId cpu);
    /// The card is there (its bytes, registers and marks are real)
    bool Valid() const { return _card != nullptr; }
    uint8_t Read(uint16_t address) const override;
    void MarksOf(uint16_t from, uint16_t to, disasm::Marks& out) const override;
    Z80Registers* Registers() const override { return _hasRegisters ? &_registers : nullptr; }

private:
    GeneralSoundCard* _card = nullptr;
    DebugManager* _debug = nullptr;
    DebugCpuId _cpu = DebugCpuId::Main;
    mutable Z80Registers _registers{};
    bool _hasRegisters = false;
};
}  // namespace debuggercpu

/// The processor tabs: a QTabBar the window places above its panes, kept to the CPUs fitted, and the CPU in front
class DebuggerCpuTabs : public QObject
{
    Q_OBJECT

public:
    explicit DebuggerCpuTabs(QTabBar* bar, QObject* parent = nullptr);

    /// The machine shown (null: none). Another machine: its last stop taken as seen (a stop before the window looked
    /// brings nothing forward), the tabs its CPUs, the main CPU in front; the same machine: the tabs follow its CPUs
    void setEmulator(std::shared_ptr<Emulator> emulator);
    /// The tabs follow the CPUs fitted now (a card plugged or removed); a CPU gone puts the main CPU in front.
    /// True when the CPU in front changed
    bool update();
    /// A new stop (LastStop().serial moved) of a breakpoint or a step brings its CPU's tab to the front, once per
    /// stop. True when the CPU in front changed
    bool followStop();

    DebugCpuId current() const { return _current; }
    const std::vector<DebugCpuId>& cpus() const { return _cpus; }
    /// Put `cpu`'s tab in front; false when it has none
    bool select(DebugCpuId cpu);
    /// The window shown or hidden: shown, the card in front is the machine's selected CPU (DebugManager::SelectCpu:
    /// it follows the main CPU's steps of the paused machine); hidden or gone, the main CPU is again
    void setActive(bool active);
    QTabBar* bar() const { return _bar; }

signals:
    /// Another CPU is in front: the panes read it
    void cpuChanged(DebugCpuId cpu);

private:
    void rebuild();
    void applySelection();
    void release();

    QTabBar* _bar = nullptr;
    std::shared_ptr<Emulator> _emulator;
    std::vector<DebugCpuId> _cpus{DebugCpuId::Main};
    std::vector<QString> _titles;
    DebugCpuId _current = DebugCpuId::Main;
    uint64_t _stopSerial = 0;
    bool _active = true;
    bool _building = false;
};
