#pragma once

#include <QString>
#include <optional>
#include <string>

class EmulatorBinding;

/// The Machine menu's "Host keyboard" state of the instance the UI is bound to
struct HostKeyboardMenuState
{
    QString route;               ///< the requested route (Keyboard::HostRouteName)
    QString effective;           ///< the route in effect
    bool ps2Controller = false;  ///< the machine has a PS/2 keyboard controller
    QString controller;          ///< its name when it has one ("PROFI-XT firmware 1.27"), else empty
};

/// Reads and changes the host keyboard route from the UI thread (MainWindow,
/// Machine menu aboutToShow and the route actions). The binding may still be
/// bound to an instance that automation is removing on another thread - the UI
/// unbinds one queued event later - so every access goes through a context
/// lease (Emulator::LeaseContext): it keeps the keyboard alive for the call, and
/// is refused once the removal has begun.
namespace HostKeyboardMenu
{
/// nullopt: nothing bound, the instance is being removed (or released), or it has no keyboard
std::optional<HostKeyboardMenuState> Read(const EmulatorBinding* binding);

/// Requests a route by name. error is set when the keyboard refuses it; the
/// result is the state after the request (nullopt as for Read, error untouched)
std::optional<HostKeyboardMenuState> Request(const EmulatorBinding* binding, const QString& route, std::string& error);
}  // namespace HostKeyboardMenu
