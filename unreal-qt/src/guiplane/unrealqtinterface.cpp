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
             {"unrealqt.osd_keyboard_nav",
              "On-screen keyboard: move",
              {{"dir", ParamKind::String, false, "up, down, left, right"}},
              "Move the pad's cursor to the nearest key in a direction (the first move: the middle key).",
              true},
             {"unrealqt.osd_keyboard_tap",
              "On-screen keyboard: press",
              {},
              "Tap the key under the cursor on the machine; a modifier (CS, SS, Shift) latches for the next key."},
             {"unrealqt.osd_keyboard_force",
              "On-screen keyboard: which",
              {{"keyboard", ParamKind::String, true,
                "\"\" the machine's own, minimal (the bare ZX matrix), steam (Steam's system keyboard), or a "
                "picture id of the same matrix (zx48k, pentagon, ...)"}},
              "The keyboard shown whatever the model, kept in the settings."},
             {"unrealqt.controllers_open", "Controllers window", {}, "Open Tools > Controllers."},
             {"unrealqt.controllers_learn",
              "Learn a controller",
              {{"device", ParamKind::String, false, "a host device id (controllers devices lists them)"}},
              "The learn wizard for a device the controller database does not know."},
         },
         {"osd_keyboard", "osd_keyboard_state"},
         "unreal-qt's own windows."});
    guicontrol::AddProgramInterface(
        {"handheld",
         guicontrol::Level::Host,
         {
             {"handheld.menu",
              "Handheld menu",
              {{"on", ParamKind::Bool, true, "absent: flip it"}},
              "Open or close the controller menu over the picture (handheld mode: Steam Deck Game Mode, "
              "UNREAL_HANDHELD=1). The machine pauses while it is open."},
             {"handheld.nav",
              "Menu: move",
              {{"dir", ParamKind::String, false, "up, down, left, right"}},
              "Move the menu's focus: sections, rows, slots, a picker's choices.",
              true},
             {"handheld.accept", "Menu: select", {}, "Open the section, activate the row, load the slot, choose."},
             {"handheld.back", "Menu: back", {}, "Close a picker, leave the page; on the sections it closes the menu."},
             {"handheld.option",
              "Menu: page button",
              {{"button", ParamKind::String, false, "x (save into the slot) or y (delete the slot)"}},
              "A page's own X / Y button."},
             {"handheld.section",
              "Menu: section",
              {{"dir", ParamKind::String, false, "prev or next"}},
              "The previous / next section; a page up / down in a picker."},
         },
         {"handheld_menu"},
         "The handheld menu (Steam Deck): the verbs the pad drives it with, for the automation."});
}
