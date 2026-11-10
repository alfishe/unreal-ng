#include "unrealqtinterface.h"

#include "guicontrol/interfaces.h"

void DeclareUnrealQtInterface()
{
    using guicontrol::ParamKind;
    guicontrol::AddProgramInterface(
        {"unrealqt",
         guicontrol::Level::Host,
         {
             {"unrealqt.osd_keyboard",
              "On-screen keyboard",
              {{"on", ParamKind::Bool, true, "absent: flip it"}},
              "Show or hide the on-screen keyboard window."},
             {"unrealqt.controllers_open", "Controllers window", {}, "Open Tools > Controllers."},
             {"unrealqt.controllers_learn",
              "Learn a controller",
              {{"device", ParamKind::String, false, "a host device id (controllers devices lists them)"}},
              "The learn wizard for a device the controller database does not know."},
         },
         {"osd_keyboard"},
         "unreal-qt's own windows."});
}
