#include "guicontrol/interfaces.h"

namespace guicontrol
{

const std::vector<InterfaceSpec>& Interfaces()
{
    static const std::vector<InterfaceSpec> interfaces = {
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
    return interfaces;
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

}  // namespace guicontrol
