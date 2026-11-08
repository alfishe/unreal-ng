#pragma once

/// @file interfaces.h
/// @brief The interfaces of the GUI control plane (design §4.3): each one declared once, with its commands, their
/// parameters and its state fields. A program implements an interface on the node where its subject lives (host,
/// window or view); clients read the declarations to map arguments and to write the catalogue without a GUI running.

#include <QString>
#include <QStringView>

#include <vector>

namespace guicontrol
{

enum class ParamKind
{
    String,
    Int,
    Bool,
    Address  ///< 0..#FFFF: decimal, 0x.., #.., $..; kept as text, the program parses it
};

struct ParamSpec
{
    const char* name;
    ParamKind kind;
    bool optional;
    const char* description;
};

struct CommandSpec
{
    const char* method;  ///< "window.move"
    const char* label;
    std::vector<ParamSpec> params;
    const char* description;
    bool repeats = false;  ///< a held binding repeats it (cursor moves, pages)
};

enum class Level
{
    Host,
    Window,
    View
};

struct InterfaceSpec
{
    const char* name;  ///< "window"
    Level level;
    std::vector<CommandSpec> commands;
    std::vector<const char*> state;  ///< the state fields it publishes
    const char* description;
};

/// Every declared interface: the plane's own, then the programs' extras added in this process
const std::vector<InterfaceSpec>& Interfaces();
/// A program's own commands (`unrealqt.*`): declared by the program from a table without widgets, so a catalogue
/// writer links it without the program (design §4.2). A name already declared is replaced
void AddProgramInterface(InterfaceSpec spec);
const InterfaceSpec* FindInterface(QStringView name);
/// A declared command; null for a program's own extra or an unknown one
const CommandSpec* FindCommand(QStringView method);
/// "window" of "window.move"; the method itself when it has no dot
QString InterfaceOf(QStringView method);
const char* LevelName(Level level);
const char* ParamKindName(ParamKind kind);

}  // namespace guicontrol
