#include "hostkeyboardmenu.h"

#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"

namespace
{
Emulator::ContextLease LeaseBound(const EmulatorBinding* binding)
{
    Emulator* emulator = binding && binding->isBound() ? binding->emulator() : nullptr;
    return emulator ? emulator->LeaseContext() : Emulator::ContextLease{};
}

HostKeyboardMenuState StateOf(const Keyboard& keyboard)
{
    HostKeyboardMenuState state;
    state.route = QString::fromLatin1(Keyboard::HostRouteName(keyboard.GetHostRoute()));
    state.effective = QString::fromLatin1(Keyboard::HostRouteName(keyboard.EffectiveHostRoute()));
    state.ps2Controller = keyboard.HasPs2Sink();
    return state;
}
}  // namespace

std::optional<HostKeyboardMenuState> HostKeyboardMenu::Read(const EmulatorBinding* binding)
{
    const Emulator::ContextLease lease = LeaseBound(binding);
    if (!lease || !lease->pKeyboard)
        return std::nullopt;
    return StateOf(*lease->pKeyboard);
}

std::optional<HostKeyboardMenuState> HostKeyboardMenu::Request(const EmulatorBinding* binding, const QString& route,
                                                               std::string& error)
{
    const Emulator::ContextLease lease = LeaseBound(binding);
    if (!lease || !lease->pKeyboard)
        return std::nullopt;
    lease->pKeyboard->RequestHostRoute(route.toStdString(), error);
    return StateOf(*lease->pKeyboard);
}
