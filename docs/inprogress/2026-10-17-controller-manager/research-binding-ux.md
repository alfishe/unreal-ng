# Research: binding UX in other emulators

| | |
|---|---|
| **Date** | 2026-10-18 |
| **Status** | Research; feeds the binding editor redesign ([ui-ux.md §3.1](ui-ux.md#31-binding-editor)) |
| **Why** | Owner 2026-10-18 on the first binding editor: "not intuitive at all, cryptic and inconvenient" |

Two surveys: the source checkouts in `/Volumes/TB4-4Tb/Projects/emulators/github/` (Mesen2, BizHawk, RetroArch, MAME,
Xpeccy, ZEsarUX, DOSBox-X, WinUAE, amiberry, vAmiga, fs-uae, ZX-M8XXX, zxsp, Zero-Emulator) and the web (Dolphin,
PCSX2, DuckStation, OpenEmu, Steam Input, PPSSPP, yuzu / Ryujinx, Fuse, Spectaculator, XAG 107, Game Accessibility
Guidelines). Paths below are relative to the checkouts.

## 1. The finding

Every well-regarded binding UI starts from the **emulated** input and shows the host binding next to it: Mesen2
(`UI/Views/NesControllerView.axaml:24-63`), BizHawk, RetroArch "Port N Controls", MAME "Input Assignments"
(`src/frontend/mame/ui/inputmap.cpp:623-640`), DOSBox-X, vAmiga, the WinUAE Remap dialog, Dolphin, PCSX2, DuckStation,
OpenEmu, PPSSPP, yuzu, Spectaculator, ZX-M8XXX. Host-first appears only in Xpeccy (`opt_gamepad.cpp:36-121`), the
expert pages (WinUAE Input, amiberry Custom, fs-uae config) and Steam Input. Our first editor was host-first with a
target-kind cascade and the raw spec in the main form: the Xpeccy pattern, the weakest studied.

## 2. Patterns to take

1. **Rows are what the game sees**, in plain words: Joystick (Up, Down, Left, Right, Fire, Fire 2), ZX keys, Mouse,
   Emulator (Pause, Quick save, On-screen keyboard, Fast forward). Each row holds the host inputs bound to it.
2. **The joystick type is one combo, not a per-binding field**, each choice explained: Kempston (port #1F),
   Sinclair 1 (keys 6 7 8 9 0), Sinclair 2 (1 2 3 4 5), Cursor / Protek (5 6 7 8 + 0), QAOP + Space (Fuse,
   Spectaculator, ZEsarUX `src/joystick.c:217-235`, ZX-M8XXX `core/joystick.js:53-59`).
3. **Presets that spell out their keys** (WinUAE `winuae.rc:2038`, amiberry `input.cpp:41-52`, Mesen2 "Select
   Preset…"), plus one-click "Automatic" that says what it picked (fs-uae "Auto (X)", PCSX2 Automatic Mapping, which
   reports when it cannot map).
4. **Click, then press**: the slot itself shows it listens, with a countdown ("Push Button/Axis… [5]", PCSX2;
   RetroArch "(Timeout 3.0 seconds)"); the prompt names the target and the ways out ("Esc cancels"). Wait for release
   first (RetroArch `menu_driver.c:4885`), compare axes with a resting snapshot (ZX-M8XXX `ui/gamepad.js`). A
   timeout or a close cancels, never clears (the Mesen2 bug, `KeyBindingButton.axaml.cs:62`). Pressing a pad button
   while the window is open selects its row (Spectaculator).
5. **Bind all** walks the rows with Step N of M, Back, Skip, Cancel (amiberry `controller_map.cpp:846-959`, OpenEmu
   auto-advance, Dolphin Iterative Input Mapping, PPSSPP Bind All). Not RetroArch's abort-on-timeout with no skip.
6. **Alternates are chips** with a "+" on the row and a remove on each chip (PPSSPP `UI/ControlMappingScreen.cpp`);
   not hidden modes (MAME append, Mesen2 key-set tabs, BizHawk Auto Tab).
7. **Clear and reset**: right-click or the chip's × removes one; "Restore defaults" for all, with a confirm.
   Defaults drawn plain, the user's overrides marked (MAME de-emphasis, `inputmap.cpp:331-339`).
8. **Conflicts said inline**: "A is already Fire: Move here / Keep both / Cancel" (zxsp marks duplicates red,
   amiberry "In-Use (X)"; none of the others handles it well).
9. **Advanced per binding in a context menu**: Autofire (with its rate), Toggle, Tap / hold, Invert, Threshold, Edit
   as text (yuzu right-click menu, Dolphin's expression editor behind right-click, vAmiga's autofire checkbox that
   reveals the rate). Deadzone and sensitivity per pad, not in the binding dialog.
10. **Scope once, in plain words, in the header**: "For this game / For every DualSense / For every pad" (zxsp "Default
    for all files / For file pattern / Use just now"), not per binding.
11. **Layers as tabs named by purpose** (Playing / Debugging), only in Advanced (Steam action sets).
12. **The picture is the overview**: a selected row lights its pad parts (OpenEmu), a pressed control lights its row
    (yuzu, DOSBox-X `sdl_mapper.cpp:3128`, WinUAE Test).

## 3. To avoid

Host-first cascades and the raw spec in the basic flow (Xpeccy); internal names (`south`, `rt.click`, `auto:fire`);
a field that listens on focus with no prompt (BizHawk); capture without cancel, or close that clears (Mesen2); a
preset silently turned "Custom" (WinUAE); cursor grab with "F12 = Exit" (WinUAE); a wizard with no skip (RetroArch);
two remap layers the user must understand (RetroArch Binds vs Quick Menu Controls); middle-click-only clear
(Dolphin); Steam's activator depth up front; auto-mapping that fails silently; a file to create before the first
binding (Xpeccy `opt_gamepad.cpp:246`).

## 4. Sources (web)

Dolphin wiki Configuring_Controllers and `Source/Core/DolphinQt/Config/Mapping`; pcsx2.net/docs/configuration/controllers
and `pcsx2-qt/Settings/InputBindingWidget.cpp`; DuckStation `src/duckstation-qt/controllerbindingwidget.ui`; OpenEmu
wiki "User guide: Preferences: Controls"; partner.steamgames.com Steam Input docs (activators, mode shifting, action
set layers); ppsspp.org/docs/settings/controls; yuzu `configure_input_player.cpp`; Fuse man page; spectaculator.com
docs 9.1 joystick; XAG 107 (devdocs.xbox.com); gameaccessibilityguidelines.com "allow controls to be remapped".
