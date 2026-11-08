#include "guicontrol/interfaces.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace guicontrol
{

namespace
{
const ParamSpec kDir{"dir", ParamKind::String, false, "up, down, left or right"};
const ParamSpec kPages{"n", ParamKind::Int, true, "pages, signed: below 0 up (default 1)"};
const ParamSpec kAddr{"addr", ParamKind::Address, false, "an address: decimal, 0x.., #.., $.. or ..h"};

std::vector<InterfaceSpec> Declared()
{
    return {
        {"debugger",
         Level::Host,
         {
             {"debugger.open", "Open the debugger", {}, "Show the debugger window and raise it."},
             {"debugger.close", "Close the debugger", {}, "Hide the debugger window."},
             {"debugger.focus",
              "Focus a pane",
              {{"pane", ParamKind::String, false, "disassembly, registers, memory or stack"}},
              "Give a pane of the debugger the focus (a closed debugger opens)."},
             {"debugger.focus_next", "Next pane", {}, "The next pane of the debugger takes the focus."},
             {"debugger.focus_prev", "Previous pane", {}, "The previous pane of the debugger takes the focus."},
             {"debugger.cursor_move",
              "Cursor in the focused pane",
              {kDir},
              "The arrow key into the pane in focus: the disassembly's row, the memory's byte, a register or stack cell.",
              true},
             {"debugger.page", "Page in the focused pane", {kPages}, "Page Up / Page Down into the pane in focus.", true},
             {"debugger.activate",
              "Enter in the focused pane",
              {},
              "Enter into the pane in focus: the disassembly follows the operand, the memory view follows a register or "
              "stack value, or the word at its cursor."},
         },
         {"open", "pane"},
         "The program's debugger: there even while its window is closed."},
        {"metadata",
         Level::Host,
         {
             {"metadata.set",
              "Metadata switch",
              {{"switch", ParamKind::String, false, "metadata, regions, colours, notes or labels"},
               {"on", ParamKind::Bool, true, "absent: flip it"}},
              "A switch of Debug > Metadata, for every debugger of the program (kept in the settings)."},
         },
         {"metadata", "regions", "colours", "notes", "labels"},
         "The memory markup and labels the program's views show."},
        {"disassembly",
         Level::View,
         {
             {"disassembly.goto", "Go to an address", {kAddr}, "Show the address at the top, the cursor on it."},
             {"disassembly.follow_pc", "Show PC", {}, "Show the disassembly at PC again."},
             {"disassembly.cursor",
              "Put the cursor",
              {kAddr},
              "The cursor row on the address; the view scrolls only when it is not shown."},
             {"disassembly.cursor_move",
              "Move the cursor",
              {{"dir", ParamKind::String, false, "up or down"}},
              "The cursor one row (one byte in byte scroll) up or down.",
              true},
             {"disassembly.page", "Page", {kPages}, "The cursor a page (16 rows) up or down.", true},
             {"disassembly.scroll_mode",
              "Scroll mode",
              {{"mode", ParamKind::String, true, "byte or command; absent: flip it"}},
              "Scroll by whole instructions or by bytes."},
             {"disassembly.run_to_cursor",
              "Run to the cursor",
              {},
              "Run the machine to the cursor row (run.to_cursor with its address); a breakpoint on the way stops it."},
             {"disassembly.breakpoint_toggle_cursor",
              "Breakpoint at the cursor",
              {},
              "Toggle an execution breakpoint at the cursor row."},
         },
         {"top", "cursor", "pc", "scroll_mode"},
         "A disassembly view."},
        {"memory",
         Level::View,
         {
             {"memory.goto", "Go to an address", {kAddr}, "Show the address's page, the cursor on it."},
             {"memory.cursor",
              "Put the cursor",
              {kAddr},
              "The cursor on the address; the page changes only when the address is not on it."},
             {"memory.cursor_move",
              "Move the cursor",
              {kDir},
              "The cursor a byte left or right, a line up or down.",
              true},
             {"memory.page", "Page", {kPages}, "The cursor a screen of lines up or down.", true},
             {"memory.follow",
              "Follow a register",
              {{"register", ParamKind::String, false, "pc, sp, bc, de, hl, ix, iy, or off"}},
              "Where the view goes when the machine stops: the register's address, or nowhere (off)."},
             {"memory.select",
              "Select bytes",
              {{"from", ParamKind::Address, false, "the first byte"}, {"to", ParamKind::Address, false, "the last byte"}},
              "Select a range on the page shown."},
             {"memory.follow_cursor",
              "Follow the word at the cursor",
              {},
              "Go to the address the little-endian word at the cursor holds."},
             {"memory.find_last_write",
              "Last write to the cursor",
              {},
              "Find the last write to the cursor's byte before now and go there (ttd.find_last_write)."},
             {"memory.find_last",
              "Last access to the cursor",
              {{"access", ParamKind::String, true, "write, read, execute or io (write)"}},
              "Find the last access to the cursor's byte before now and go there (ttd.find_last)."},
         },
         {"top", "cursor", "selection", "follow"},
         "A memory (hex) view of the CPU's address space, one 16 KB page at a time."},
        {"tilegrid",
         Level::View,
         {
             {"tilegrid.single_sync",
              "Single sync",
              {{"on", ParamKind::Bool, true, "absent: flip it"}},
              "Every tile shows the first tile's machine in step with it (the others are removed); off: each tile "
              "runs its own again."},
         },
         {"tiles", "single_sync"},
         "The video wall's grid of tiles, each showing a machine."},
        {"window",
         Level::Window,
         {
             {"window.raise", "Raise the window", {}, "Show the window, bring it to the front and give it focus."},
             {"window.show", "Show the window", {}, "Show the window without raising it."},
             {"window.hide", "Hide the window", {}, "Hide the window."},
             {"window.move",
              "Move the window",
              {{"x", ParamKind::Int, true, "left edge"},
               {"y", ParamKind::Int, true, "top edge"},
               {"w", ParamKind::Int, true, "width"},
               {"h", ParamKind::Int, true, "height"}},
              "Move or resize the window; the fields not given stay."},
             {"window.screenshot",
              "Screenshot of the window",
              {{"path", ParamKind::String, true, "a PNG file; absent: the clipboard"}},
              "The window's own pixels (not the emulated screen) to a file or the clipboard."},
         },
         {"title", "geometry", "visible", "active"},
         "A top-level window of the program."},
    };
}

std::vector<InterfaceSpec>& Registry()
{
    static std::vector<InterfaceSpec> interfaces = Declared();
    return interfaces;
}
}  // namespace

const std::vector<InterfaceSpec>& Interfaces()
{
    return Registry();
}

void AddProgramInterface(InterfaceSpec spec)
{
    std::vector<InterfaceSpec>& interfaces = Registry();
    for (InterfaceSpec& known : interfaces)
        if (QLatin1String(known.name) == QLatin1String(spec.name))
        {
            known = std::move(spec);
            return;
        }
    interfaces.push_back(std::move(spec));
}

const InterfaceSpec* FindInterface(QStringView name)
{
    for (const InterfaceSpec& spec : Interfaces())
        if (name == QLatin1String(spec.name))
            return &spec;
    return nullptr;
}

const CommandSpec* FindCommand(QStringView method)
{
    const InterfaceSpec* spec = FindInterface(InterfaceOf(method));
    if (spec == nullptr)
        return nullptr;
    for (const CommandSpec& command : spec->commands)
        if (method == QLatin1String(command.method))
            return &command;
    return nullptr;
}

QString InterfaceOf(QStringView method)
{
    const qsizetype dot = method.indexOf(u'.');
    return (dot < 0 ? method : method.left(dot)).toString();
}

const char* LevelName(Level level)
{
    switch (level)
    {
        case Level::Host:
            return "host";
        case Level::Window:
            return "window";
        case Level::View:
            return "view";
    }
    return "view";
}

const char* ParamKindName(ParamKind kind)
{
    switch (kind)
    {
        case ParamKind::String:
            return "string";
        case ParamKind::Int:
            return "int";
        case ParamKind::Bool:
            return "bool";
        case ParamKind::Address:
            return "address";
    }
    return "string";
}

QByteArray CatalogueJson()
{
    QJsonArray interfaces;
    for (const InterfaceSpec& spec : Interfaces())
    {
        QJsonArray commands;
        for (const CommandSpec& command : spec.commands)
        {
            QJsonArray params;
            for (const ParamSpec& param : command.params)
                params.append(QJsonObject{{"name", param.name},
                                          {"kind", ParamKindName(param.kind)},
                                          {"optional", param.optional},
                                          {"description", param.description}});
            commands.append(QJsonObject{{"method", command.method},
                                        {"label", command.label},
                                        {"description", command.description},
                                        {"repeats", command.repeats},
                                        {"params", params}});
        }
        QJsonArray state;
        for (const char* field : spec.state)
            state.append(QLatin1String(field));
        interfaces.append(QJsonObject{{"name", spec.name},
                                      {"level", LevelName(spec.level)},
                                      {"description", spec.description},
                                      {"state", state},
                                      {"commands", commands}});
    }
    return QJsonDocument(QJsonObject{{"format", "unreal-ng-gui-catalogue"}, {"version", 1}, {"interfaces", interfaces}})
        .toJson(QJsonDocument::Indented);
}

}  // namespace guicontrol
