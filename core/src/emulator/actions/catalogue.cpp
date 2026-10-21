#include "emulator/actions/catalogue.h"

#include <map>

#include "base/featuremanager.h"
#include "debugger/ttd/timetravelhooks.h"
#include "emulator/actions/actionparams.h"
#include "emulator/actions/hostactions.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"

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
        {"run.step_by", "Step by a dial's delta: forward runs instructions, backward steps back through the history."},
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
        {"ttd.seek_by", "Seek through the history with a dial's acceleration curve; answers the marks crossed."},
        {"ttd.find_last_write", "Find the last write to an address in the history (not available yet)."},
        {"ttd.bookmark_prev", "Go to the previous bookmark (not available yet)."},
        {"ttd.bookmark_next", "Go to the next bookmark (not available yet)."},
        {"ttd.step_back_scanline", "Go back one scanline in the history (not available yet)."},
        {"ttd.start", "Start recording the time travel history."},
        {"ttd.stop", "Stop recording the time travel history."},
        {"ttd.seek", "Go to a frame (param frame, tinframe) or a bookmark (param bookmark) of the history."},
        {"ttd.bookmark_delete", "Delete a bookmark of the history (param label)."},
        {"ttd.invalidate", "Drop the recorded history."},
        {"ttd.history_limit", "Set how much history is kept (params frames, bytes)."},
        {"ttd.journal", "Record the input journal with the history, or stop it (param enabled)."},
        {"ttd.dump", "Save the time travel session to a file (param path)."},
        {"ttd.load", "Open a saved time travel session (param path)."},
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
        {"audio.source_mute", "Mute or unmute one sound source (param source)."},
        {"audio.volume_by", "Change the volume of a source by a delta."},
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
        {"dbg.breakpoint_toggle_pc", "Set an execution breakpoint at PC (or at address), or remove the one there."},
        {"dbg.breakpoint_toggle_cursor", "Set an execution breakpoint at the debugger's disassembly cursor, or remove it."},
        {"dbg.labels_toggle", "Show or hide labels in the disassembly (not available yet)."},
        {"osd.keyboard", "Show the on-screen keyboard (not available yet)."},
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
