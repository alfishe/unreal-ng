/// @file builtinprofiles.cpp
/// @brief The out-of-the-box profiles (mapping-and-profiles.md §8, debug-and-ttd.md §4). Control names are positional
/// (`south` is A on Xbox, Cross on PlayStation, B on Switch), so one global pad profile serves every pad family.

#include "profiles/profilestore.h"

namespace controllers
{

namespace
{
const char* const kPadPlay = R"json({
  "schema": 1, "kind": "play", "id": "builtin/play/pad-default", "label": "Game pad",
  "layers": { "play": {
    "dpad.*":   { "retro": "auto:dpad" },
    "lstick":   { "retro": "auto:dpad" },
    "south":    { "retro": "auto:fire" },
    "east":     { "retro": "auto:fire2" },
    "west":     { "retro": "auto:fire3" },
    "north":    { "key": "SPACE" },
    "menu":     { "key": "ENTER" },
    "view":     { "action": "osd.keyboard" },
    "rt.click": { "retro": "auto:fire", "turbo": "classic", "on": 2, "off": 2, "label": "Fire (auto)" },
    "rstick.x": { "mouse": "dx" },
    "rstick.y": { "mouse": "dy" }
  } }
})json";

/// A generic HID pad's hat and X / Y already arrive as the d-pad and the left stick (HidPad): only its buttons here
const char* const kGenericPlay = R"json({
  "schema": 1, "kind": "play", "id": "builtin/play/generic-default", "label": "DB9 adapter",
  "applies_to": { "families": ["generic"] },
  "layers": { "play": {
    "button.0":    { "retro": "auto:fire" },
    "button.1":    { "retro": "auto:fire2" },
    "button.2":    { "retro": "auto:fire3" }
  } }
})json";

const char* const kKeyboardPlay = R"json({
  "schema": 1, "kind": "play", "id": "builtin/play/keyboard-numpad", "label": "Numpad joystick",
  "applies_to": { "families": ["keyboard"] },
  "layers": { "play": {
    "key.kp8": { "retro": "auto:up" },
    "key.kp2": { "retro": "auto:down" },
    "key.kp4": { "retro": "auto:left" },
    "key.kp6": { "retro": "auto:right" },
    "key.kp0": { "retro": "auto:fire" },
    "key.kp5": { "retro": "auto:fire2" }
  } }
})json";

/// debug-and-ttd.md §4.2: the layer key on the view+menu chord, bookmarks on lb / rb with the d-pad
const char* const kPadDebug = R"json({
  "schema": 1, "kind": "debug", "id": "builtin/debug/pad-default", "label": "Debugger on a pad",
  "layer_key": { "control": "view+menu", "mode": "hold" },
  "layers": { "debug": {
    "lt":            { "action": "ttd.scrub_back", "curve": "scrub" },
    "rt":            { "action": "ttd.scrub_forward", "curve": "scrub" },
    "south":         { "action": "run.step_in" },
    "rb":            { "action": "run.step_over" },
    "lb":            { "action": "run.step_out" },
    "lb+dpad.left":  { "action": "ttd.bookmark_prev" },
    "rb+dpad.right": { "action": "ttd.bookmark_next" },
    "dpad.left":     { "action": "ttd.step_back_instruction", "repeat": { "delay": 15, "rate": 4 } },
    "dpad.right":    { "action": "run.step_in", "repeat": { "delay": 15, "rate": 4 } },
    "dpad.up":       { "action": "run.frame_step" },
    "dpad.down":     { "action": "ttd.step_back_frame" },
    "north":         { "action": "ttd.reverse_continue" },
    "west":          { "action": "ttd.bookmark_add" },
    "east":          { "action": "ttd.resume_live" },
    "menu":          { "action": "run.pause_toggle" },
    "view":          { "action": "dbg.focus_next_pane" },
    "lstick.up":     { "action": "dbg.cursor_up" },
    "lstick.down":   { "action": "dbg.cursor_down" },
    "lstick.left":   { "action": "dbg.cursor_left" },
    "lstick.right":  { "action": "dbg.cursor_right" },
    "rstick.click":  { "action": "dbg.breakpoint_toggle_pc" },
    "lstick.click":  { "action": "dbg.memory_follow" }
  } }
})json";

/// The TS-Config Kempston is 8 bits wide: a Sega pad on the Evo reads R L D U B C A START (retro-devices.md §2). The
/// buttons sit as RetroArch's Genesis core has them: west A, south B, east C, menu START
const char* const kTsConfSegaPlay = R"json({
  "schema": 1, "kind": "play", "id": "builtin/play/tsconf-sega-pad", "label": "Sega pad (TS-Config)",
  "match": { "machines": ["TSL"] },
  "layers": { "play": {
    "west":  { "retro": "auto:a" },
    "south": { "retro": "auto:b" },
    "east":  { "retro": "auto:c" },
    "menu":  { "retro": "auto:start" }
  } }
})json";

/// The Next's DB9 holds a Mega Drive 6-button pad (retro-devices.md §2): B C A START on #1F / #37, X Y Z MODE in NR #B2.
/// The buttons sit as RetroArch's Genesis core has them: south B (the Kempston fire), east C, west A, north Y, lb X, rb Z,
/// menu START, view MODE
const char* const kNextMdPlay = R"json({
  "schema": 1, "kind": "play", "id": "builtin/play/next-md-pad", "label": "Mega Drive pad (ZX Next)",
  "match": { "machines": ["NEXT"] },
  "layers": { "play": {
    "south": { "retro": "auto:b" },
    "east":  { "retro": "auto:c" },
    "west":  { "retro": "auto:a" },
    "north": { "retro": "auto:y" },
    "lb":    { "retro": "auto:x" },
    "rb":    { "retro": "auto:z" },
    "menu":  { "retro": "auto:start" },
    "view":  { "retro": "auto:mode" }
  } }
})json";

/// Vradark's Revenge (ZX Next, Sanchez Crew): its own Settings screen - W A S D move, Enter fires, Space casts,
/// F / C drink life / mana, 1 / 2 the next bullets / spell, R autofire, M map, P pause; the Kempston mouse
/// aims and its left button fires. The right stick stays the mouse (the pad default)
const char* const kVradarkPlay = R"json({
  "schema": 1, "kind": "play", "id": "builtin/game/vradarks-revenge", "label": "Vradark's Revenge",
  "match": { "machines": ["NEXT"],
             "file_sha256": ["32ad0b5fa7cbb749841ecf26bdd6fce7563d8ffbcc736c15689b1fa4abcbcd9e"] },
  "layers": { "play": {
    "dpad.*":       { "none": true },
    "lstick":       { "none": true },
    "dpad.up":      { "key": "W" },
    "dpad.down":    { "key": "S" },
    "dpad.left":    { "key": "A" },
    "dpad.right":   { "key": "D" },
    "lstick.up":    { "key": "W" },
    "lstick.down":  { "key": "S" },
    "lstick.left":  { "key": "A" },
    "lstick.right": { "key": "D" },
    "south":        { "key": "ENTER", "label": "Fire" },
    "north":        { "key": "SPACE", "label": "Spell" },
    "west":         { "key": "F", "label": "Life" },
    "east":         { "key": "C", "label": "Mana" },
    "lb":           { "key": "1", "label": "Next bullets" },
    "rb":           { "key": "2", "label": "Next spell" },
    "rt.click":     { "mouse": "left", "label": "Fire (mouse)" },
    "lt.click":     { "mouse": "right" },
    "rstick.click": { "key": "R", "label": "Autofire" },
    "view":         { "key": "M", "label": "Map" },
    "menu":         { "key": "P", "label": "Pause" }
  } }
})json";

/// debug-and-ttd.md §4.1: the Elite's paddles carry the layer key and the bookmarks
const char* const kEliteDebug = R"json({
  "schema": 1, "kind": "debug", "id": "builtin/debug/elite-paddles", "label": "Debugger on an Elite",
  "applies_to": { "families": ["xbox-elite"] },
  "layer_key": { "control": "paddle.p4", "mode": "hold" },
  "layers": { "debug": {
    "paddle.p1": { "action": "ttd.bookmark_next" },
    "paddle.p3": { "action": "ttd.bookmark_prev" },
    "paddle.p2": { "action": "ttd.find_last_write" }
  } }
})json";
}  // namespace

const std::vector<const char*>& BuiltinProfileTexts()
{
    static const std::vector<const char*> texts = {kPadPlay, kGenericPlay, kKeyboardPlay, kTsConfSegaPlay,
                                                         kNextMdPlay, kVradarkPlay, kPadDebug, kEliteDebug};
    return texts;
}

}  // namespace controllers
