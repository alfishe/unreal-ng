#pragma once

/// @file cdaudiocontrol.h
/// @brief CD audio for every automation surface (PLAN #83): the one place that
/// reports the CD drives' audio state and carries out the control verbs, so the
/// WebAPI (and MCP through it), the CLI, Lua and Python return the same data.
/// Replies are StateNode trees, as every surface already converts them.
///
/// | Verb | Options | Does |
/// |---|---|---|
/// | `status` | - | every CD drive: disc and tracks, audio status, head (LBA, MSF, track, index), play range, page 0Eh, mixer row |
/// | `play` | `track=N` [`to=M`], or `lba=X` `frames=N`, or `msf=MM:SS:FF` `end=MM:SS:FF` | as PLAY AUDIO from the drive's front panel: track N to the end of track M (default: the disc's last audio track) |
/// | `pause` / `resume` / `stop` | - | as PAUSE / RESUME and STOP PLAY / SCAN |
/// | `volume` | `left=0..255` `right=0..255` `route=stereo|swap|mono|left|right|mute` `sotc=true|false` | page 0Eh, as MODE SELECT would set it |
/// | `mixer` | `volume=0..4` `mute=true|false` `solo=true|false` | the drive's SoundManager row (host side) |
///
/// `drive` picks the drive: a slot id (`ide0.slave`), a unit (`0`..`3`) or
/// empty for the first CD drive of the board. The machine-side verbs (play,
/// pause, resume, stop, volume) act at an instruction boundary (a running
/// emulator is parked for them) and are refused while TTD records: a replay
/// would not repeat them (TTDGuardedAction::CdFrontPanel). `mixer` is host
/// state and always allowed.
///
/// Example (what a surface does):
/// @code
///   CdAudioRequest request{"play", "ide0.slave", {{"track", "2"}}};
///   CdAudioReply reply = CdAudioControl(context).Execute(request);
///   http.status = reply.HttpStatus();  http.body = reply.ToValue();
/// @endcode

#include <map>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;
class CdAudioPlayer;

struct CdAudioRequest
{
    std::string verb = "status";
    std::string drive;  ///< slot id, unit 0-3, or empty: the first CD drive
    std::map<std::string, std::string> options;
};

struct CdAudioReply
{
    bool ok = true;
    std::string error;    ///< "no-cd-drive", "no-disc", "bad-request", "recording", "not-playing"
    std::string message;
    StateNode body = StateNode::Object();

    /// ok, error, message, then the body's fields
    StateNode ToValue() const;
    std::string ToJson() const;
    int HttpStatus() const;
};

class CdAudioControl
{
public:
    explicit CdAudioControl(EmulatorContext* context) : _context(context) {}

    CdAudioReply Execute(const CdAudioRequest& request);

    /// Every CD drive of the IDE board, as `status` reports them; `available` false without one
    static StateNode State(EmulatorContext* context);
    /// One drive's report (unit 0-3)
    static StateNode DriveState(EmulatorContext* context, int unit);

    static const std::vector<std::string>& Verbs();

private:
    /// The unit `drive` names; -1 with the reply's error set
    int ResolveDrive(const std::string& drive, CdAudioReply& reply) const;
    CdAudioReply Play(int unit, const std::map<std::string, std::string>& options);
    CdAudioReply Volume(int unit, const std::map<std::string, std::string>& options);
    CdAudioReply Mixer(int unit, const std::map<std::string, std::string>& options);

    EmulatorContext* _context = nullptr;
};
