#include "emulator/actions/catalogue.h"

#include <map>

#include "base/featuremanager.h"
#include "debugger/ttd/timetravelhooks.h"
#include "emulator/actions/actionparams.h"
#include "emulator/actions/hostactions.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audiomixer.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"
#include "emulator/video/windowview.h"

namespace actions
{

namespace
{
using Descriptions = std::map<std::string, const char*, std::less<>>;

const Descriptions& DescriptionTable()
{
    static const Descriptions table = {
        // Run
        {"run.pause_toggle", "Pause the machine, or continue it when it is paused."},
        {"run.pause", "Pause the machine (nothing happens when it is paused already)."},
        {"run.resume", "Continue a paused machine."},
        {"run.step_in", "Run one instruction (into a call); needs a pause."},
        {"run.step_over", "Run one instruction, over a call; needs a pause."},
        {"run.step_out", "Run until the current routine returns; needs a pause."},
        {"run.frame_step", "Run one video frame and pause again."},
        {"run.wait_int", "Run to the next interrupt and pause there."},
        {"run.to_scanline", "Run to a scanline (param line) and pause there."},
        {"run.n_scanlines", "Run a number of scanlines (param count) and pause."},
        {"run.step_by", "Step by a dial's delta in a unit (param unit: instructions, frames, scanlines, tstates; count): forward runs, backward steps back through the history."},
        {"run.turbo_hold", "Run at the turbo rate while the control is held."},
        {"run.steps", "Run a number of instructions (param count) and pause."},
        {"run.frames", "Run a number of frames (param count) and pause."},
        {"run.tstates", "Run a number of t-states (param count) and pause."},
        // Time travel
        {"ttd.record_toggle", "Start recording the time travel history, or stop it."},
        {"ttd.step_back_frame", "Go back one frame in the history."},
        {"ttd.step_forward_frame", "Go forward one frame in the history."},
        {"ttd.step_back_instruction", "Go back one instruction in the history."},
        {"ttd.step_forward_instruction", "Go forward one instruction in the history."},
        {"ttd.reverse_step", "Step back over the last instruction, as a debugger's reverse step."},
        {"ttd.reverse_continue", "Run backwards to the previous breakpoint in the history."},
        {"ttd.bookmark_add", "Bookmark the current position of the history."},
        {"ttd.resume_live", "Leave the history and continue the live machine."},
        {"ttd.jog_frames", "Move through the history by a number of frames (a delta)."},
        {"ttd.jog_instructions", "Move through the history by a number of instructions (a delta)."},
        {"ttd.seek_by", "Seek through the history by a dial's delta in a unit (param unit, else ttd.scrub_unit's): frames and seconds on an acceleration curve, batch, instructions, tstates, scanlines, bookmarks; answers the marks crossed."},
        {"ttd.find_last_write", "Find the last write to an address (param addr, else the memory / disassembly cursor) before now, and go there."},
        {"ttd.bookmark_prev", "Go to the previous bookmark of the history."},
        {"ttd.bookmark_next", "Go to the next bookmark of the history."},
        {"ttd.step_back_scanline", "Go back one scanline (param count: N) in the history."},
        {"ttd.start", "Start recording the time travel history."},
        {"ttd.stop", "Stop recording the time travel history."},
        {"ttd.seek", "Go to a frame (param frame, tinframe) or a bookmark (param bookmark) of the history."},
        {"ttd.bookmark_delete", "Delete a bookmark of the history (param label)."},
        {"ttd.invalidate", "Drop the recorded history."},
        {"ttd.history_limit", "Set how much history is kept (params frames, bytes)."},
        {"ttd.journal", "Record the input journal with the history, or stop it (param enabled)."},
        {"ttd.dump", "Save the time travel session to a file (param path)."},
        {"ttd.load", "Open a saved time travel session (param path)."},
        // Run / step / time travel units (steppingactions.cpp: the pace, runs to a place, TTD units)
        {"run.pace_set", "Choose the pace of the paced run (param preset: full, 25fps, 5fps, 1fps, frame, 100lines, 10lines, 1line, 1op; interval_ms 1..5000)."},
        {"run.pace_next", "A finer pace: one step down the ladder (25 fps .. 1 instruction)."},
        {"run.pace_prev", "A coarser pace: one step up the ladder (towards full speed)."},
        {"run.pace_by", "Walk the pace ladder by a dial's delta."},
        {"run.pace_toggle", "Run at the pace, or stop it; at full speed pause or continue the machine."},
        {"run.pace_stop", "Stop the paced run; the machine stays paused."},
        {"run.pace_step", "Run one step of the chosen pace (a frame, N scanlines, an instruction)."},
        {"run.to_pixel", "Run to the first paper pixel of the next frame and pause."},
        {"run.until_pc", "Run until PC reaches an address (param pc; max_tstates; breakpoints) and pause."},
        {"run.to_cursor", "Run to the disassembly's cursor (or param addr); a breakpoint on the way stops it."},
        {"run.step_line", "Run to the next source line of the loaded listing."},
        {"run.to_line", "Run to a source line of the loaded listing (param line)."},
        {"ttd.step_forward_scanline", "Go forward one scanline (param count: N) in the history."},
        {"ttd.bookmarks", "List the bookmarks of the history."},
        {"ttd.find_last", "Find the last access (param access: write, read, execute, io) to an address before now, and go there."},
        {"ttd.seek_start", "Go to the start of the history."},
        {"ttd.seek_end", "Go to the end of the history."},
        {"ttd.unit_back", "Go back one step of the scrub unit."},
        {"ttd.unit_forward", "Go forward one step of the scrub unit."},
        {"ttd.scrub_unit", "Choose the unit a dial scrubs in: frames, seconds, batch, instructions, tstates, scanlines, bookmarks, or next / prev (param count: a step)."},
        {"ttd.journal_build", "Build the write journal over the history (params from_frame, to_frame)."},
        {"ttd.journal_build_cancel", "Cancel a running write-journal build."},
        {"ttd.export_clip", "Export a range of the history as a lossless clip (params path, from, to)."},
        {"dbg.follow_pc", "Show the disassembly at PC again."},
        {"dbg.goto", "Show the disassembly at an address (param addr)."},
        {"dbg.scroll_mode_toggle", "Scroll the disassembly by bytes or by instructions."},
        // Context
        {"ctx.set", "Set the instance's surface context (play, ttd, ttd.timecode, debug.disasm); \"\" returns to the default."},
        // Machine and capture
        {"mach.reset", "Reset the machine; hold the control for a second."},
        {"mach.nmi", "Send a non-maskable interrupt (the Magic button)."},
        {"mach.turbo_toggle", "Switch the turbo rate on or off."},
        {"mach.start", "Start a machine of a model (param model); with replace it takes the place of the target."},
        {"mach.cold_reset", "Restart the same model from power on."},
        {"cap.screenshot_save", "Save a screenshot to a file (param path)."},
        {"cap.screenshot", "Copy a screenshot of the window to the clipboard."},
        {"cap.record_toggle", "Start or stop a video recording (not available yet)."},
        {"cap.clip_last", "Save the last seconds as a clip (not available yet)."},
        // Input
        {"input.key", "Press a key of the machine (param key), as a tap or held while the control is."},
        {"input.type", "Type a text on the machine's keyboard (param text)."},
        {"input.macro", "Run a key macro on the machine's keyboard."},
        {"input.joystick", "Move or fire the machine's joystick (params direction, fire)."},
        {"input.release_all", "Release every key and button held on the machine."},
        // BASIC
        {"basic.run", "Type and run a BASIC command (param command)."},
        {"basic.inject", "Write a BASIC program into memory (param program)."},
        {"basic.clear", "Clear the BASIC program (NEW)."},
        {"basic.mode", "Enter a BASIC mode from the 128K menu (param mode)."},
        // Features and settings
        {"feature.toggle", "Switch a feature on or off (param name; on to set it)."},
        {"set.speed", "Set the emulation speed (param value: 1, 2, 4, 8, 16, or next)."},
        {"set.toggle", "Switch a bool setting on or off (param name)."},
        {"set.value", "Set a setting to a value (params name, value)."},
        // Audio
        {"audio.mute_toggle", "Mute the sound, or unmute it."},
        {"audio.source_mute", "Mute or unmute one sound source or channel (param source: ay1, or ay1.a for a channel)."},
        {"audio.source_solo", "Solo a sound source (only soloed sources are heard) or a channel within its source."},
        {"audio.source_volume", "Set the volume of a source or channel (param volume 0..1, or gain_db)."},
        {"audio.volume_by", "Change the volume of a source or channel by a delta."},
        // Media and snapshots
        {"media.open", "Open software on the target (param path): a snapshot, a tape or a disk, with the fast-load settings."},
        {"media.tape", "Play, stop, rewind or eject the tape (param op)."},
        {"media.eject", "Eject a medium (param slot or drive)."},
        {"media.protect", "Write-protect a disk, or allow writes (param drive)."},
        {"media.save", "Save a changed medium back to its file."},
        {"media.create", "Insert a new blank disk."},
        {"snapshot.quick", "Save or load the quick snapshot (param op)."},
        {"snapshot.save", "Save a snapshot to a file (param path)."},
        // Video
        {"video.overscan_toggle", "Show the border's overscan, or hide it."},
        {"video.temporal", "Switch the de-flicker (ZX DLSS) on or off."},
        // Debugger
        {"dbg.mode_toggle", "Switch debug mode on or off (breakpoints and the debug features)."},
        {"dbg.breakpoints_clear", "Remove every breakpoint."},
        {"dbg.breakpoint_hits_reset", "Reset the hit counts of the breakpoints."},
        // The Qt client's GUI actions
        {"dbg.open", "Open the debugger window."},
        {"dbg.focus_next_pane", "Move the focus to the next debugger pane."},
        {"dbg.focus_prev_pane", "Move the focus to the previous debugger pane."},
        {"dbg.cursor_up", "Move the cursor of the debugger pane up."},
        {"dbg.cursor_down", "Move the cursor of the debugger pane down."},
        {"dbg.cursor_left", "Move the cursor of the debugger pane left."},
        {"dbg.cursor_right", "Move the cursor of the debugger pane right."},
        {"dbg.page_up", "Page the debugger pane up."},
        {"dbg.page_down", "Page the debugger pane down."},
        {"dbg.memory_follow", "Show the memory at the value under the debugger's cursor."},
        {"dbg.breakpoint_toggle_pc", "Toggle a breakpoint at the cursor (not available yet)."},
        {"dbg.labels_toggle", "Show or hide the labels in the disassembly."},
        {"osd.keyboard", "Show the on-screen keyboard, or hide it (the GUI's window of the machine's own keyboard)."},
        {"osd.quick_menu", "Show the quick menu (not available yet)."},
    };
    return table;
}

using Toggles = std::map<std::string, DefaultToggle, std::less<>>;

const Toggles& ToggleTable()
{
    static const Toggles table = [] {
        Toggles t;
        t["run.pause_toggle"].state = [](Emulator& emulator, const ActionValue&) -> std::optional<bool> {
            return emulator.IsPaused();
        };
        t["ttd.record_toggle"].state = [](Emulator& emulator, const ActionValue&) -> std::optional<bool> {
            EmulatorContext* context = emulator.GetContext();
            if (context == nullptr || context->pTimeTravelHooks == nullptr)
                return std::nullopt;
            return context->pTimeTravelHooks->IsRecording();
        };
        t["mach.turbo_toggle"].state = [](Emulator& emulator, const ActionValue&) -> std::optional<bool> {
            return emulator.IsTurboMode();
        };
        t["audio.mute_toggle"].state = [](Emulator& emulator, const ActionValue&) -> std::optional<bool> {
            EmulatorContext* context = emulator.GetContext();
            if (context == nullptr || context->pSoundManager == nullptr)
                return std::nullopt;
            return context->pSoundManager->isMuted();
        };
        // A mixer source or channel (param source: ay1, ay1.a): its mute / solo; the readout toggle.audio.source_mute.ay1.a
        for (const char* id : {"audio.source_mute", "audio.source_solo"})
        {
            DefaultToggle& source = t[id];
            source.param = "source";
            const bool solo = std::string_view(id) == "audio.source_solo";
            source.state = [solo](Emulator& emulator, const ActionValue& value) -> std::optional<bool> {
                const std::optional<std::string> key = TextParam(value, "source");
                if (!key)
                    return std::nullopt;
                return AudioMixer::Flag(emulator.GetContext(), *key, solo ? "solo" : "muted");
            };
        }
        t["video.overscan_toggle"].state = [](Emulator& emulator, const ActionValue&) -> std::optional<bool> {
            return emulator.IsOverscanMode();
        };
        t["video.temporal"].state = [](Emulator& emulator, const ActionValue&) -> std::optional<bool> {
            EmulatorContext* context = emulator.GetContext();
            if (context == nullptr || context->pScreen == nullptr)
                return std::nullopt;
            return !context->pScreen->GetTemporalAlgorithm().empty();
        };
        t["dbg.mode_toggle"].state = [](Emulator& emulator, const ActionValue&) -> std::optional<bool> {
            FeatureManager* features = emulator.GetFeatureManager();
            if (features == nullptr)
                return std::nullopt;
            return features->isEnabled(Features::kDebugMode);
        };
        // The GUI's on-screen keyboard: shown as the window published it (pictureactions.h)
        t["osd.keyboard"].state = [](Emulator&, const ActionValue&) -> std::optional<bool> {
            const std::optional<WindowDisplaySettings> settings = WindowView::PublishedSettings();
            if (!settings)
                return std::nullopt;
            return settings->osdKeyboard;
        };
        DefaultToggle& feature = t["feature.toggle"];
        feature.stem = "feature";
        feature.param = "name";
        feature.state = [](Emulator& emulator, const ActionValue& value) -> std::optional<bool> {
            FeatureManager* features = emulator.GetFeatureManager();
            const std::optional<std::string> name = TextParam(value, "name");
            if (features == nullptr || !name || !features->hasFeature(*name))
                return std::nullopt;
            return features->isEnabled(*name);
        };
        DefaultToggle& setting = t["set.toggle"];
        setting.stem = "set";
        setting.param = "name";
        setting.state = [](Emulator& emulator, const ActionValue& value) -> std::optional<bool> {
            const std::optional<std::string> name = TextParam(value, "name");
            if (!name)
                return std::nullopt;
            return HostSettingIsOn(emulator, *name);
        };
        return t;
    }();
    return table;
}
}  // namespace

std::string DefaultDescription(std::string_view id)
{
    const Descriptions& table = DescriptionTable();
    auto it = table.find(id);
    return it != table.end() ? std::string(it->second) : std::string();
}

const DefaultToggle* DefaultToggleOf(std::string_view id)
{
    const Toggles& table = ToggleTable();
    auto it = table.find(id);
    return it != table.end() ? &it->second : nullptr;
}

}  // namespace actions
