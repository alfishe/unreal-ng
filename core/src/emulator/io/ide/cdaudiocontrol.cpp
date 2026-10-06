#include "stdafx.h"

#include "cdaudiocontrol.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "common/stringhelper.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/ide/ata/atapicdrom.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/storage/cd/cdimage.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/statenodejson.h"

namespace
{
    std::string MsfText(const cd::Msf& msf)
    {
        char text[16];
        std::snprintf(text, sizeof(text), "%02u:%02u:%02u", msf.m, msf.s, msf.f);
        return text;
    }

    const char* StatusName(CdAudioStatus status)
    {
        switch (status)
        {
            case CdAudioStatus::Playing: return "playing";
            case CdAudioStatus::Paused: return "paused";
            case CdAudioStatus::Completed: return "completed";
            case CdAudioStatus::Error: return "error";
            case CdAudioStatus::Idle: break;
        }
        return "idle";
    }

    const char* RouteName(uint8_t select)
    {
        switch (select & 3)
        {
            case 1: return "left";
            case 2: return "right";
            case 3: return "mono";
            default: return "mute";
        }
    }

    AtapiCdrom* CdOf(EmulatorContext* context, int unit)
    {
        IdeController* ide = context ? context->pIdeController : nullptr;
        if (!ide || unit < 0 || unit >= ide->ChannelCount() * AtaChannel::kUnits)
            return nullptr;
        AtaDevice* device = ide->Channel(unit / AtaChannel::kUnits).Unit(unit % AtaChannel::kUnits);
        return device && device->Kind() == AtaDeviceKind::Cdrom ? static_cast<AtapiCdrom*>(device) : nullptr;
    }

    CdAudioReply Fail(const std::string& error, const std::string& message)
    {
        CdAudioReply reply;
        reply.ok = false;
        reply.error = error;
        reply.message = message;
        return reply;
    }

    bool ParseNumber(const std::string& text, long& value)
    {
        if (text.empty())
            return false;
        char* end = nullptr;
        value = std::strtol(text.c_str(), &end, 0);
        return end && *end == '\0';
    }

    bool ParseBool(const std::string& text, bool& value)
    {
        const std::string t = StringHelper::ToLower(text);
        if (t.empty() || t == "true" || t == "1" || t == "on" || t == "yes")
            value = true;
        else if (t == "false" || t == "0" || t == "off" || t == "no")
            value = false;
        else
            return false;
        return true;
    }

    bool ParseMsf(const std::string& text, int32_t& lba)
    {
        unsigned m = 0, s = 0, f = 0;
        char tail = 0;
        if (std::sscanf(text.c_str(), "%u:%u:%u%c", &m, &s, &f, &tail) != 3 || s >= 60 || f >= 75 || m > 99)
            return false;
        lba = cd::MsfToLba(static_cast<uint8_t>(m), static_cast<uint8_t>(s), static_cast<uint8_t>(f));
        return true;
    }

    /// Run a machine-side change at an instruction boundary: a running emulator is parked for it
    template <typename F>
    void Parked(EmulatorContext* context, F&& change)
    {
        Emulator* emulator = context ? context->pEmulator : nullptr;
        const bool park = emulator && emulator->IsRunning() && !emulator->IsPaused();
        if (park)
        {
            emulator->Pause(false);
            emulator->WaitForPauseConfirmation(1000);
        }
        change();
        if (park)
            emulator->Resume(false);
    }
}  // namespace

/// region <CdAudioReply>

StateNode CdAudioReply::ToValue() const
{
    StateNode value = StateNode::Object();
    value["ok"] = ok;
    if (!ok)
    {
        value["error"] = error;
        value["message"] = message;
    }
    for (const auto& [key, field] : body.members)
        value[key] = field;
    return value;
}

std::string CdAudioReply::ToJson() const
{
    return StateNodeToJsonText(ToValue());
}

int CdAudioReply::HttpStatus() const
{
    if (ok)
        return 200;
    if (error == "no-cd-drive")
        return 404;
    if (error == "recording" || error == "not-playing" || error == "no-disc")
        return 409;
    return 400;
}

/// endregion </CdAudioReply>

const std::vector<std::string>& CdAudioControl::Verbs()
{
    static const std::vector<std::string> verbs = {"status", "play", "pause", "resume", "stop", "volume", "mixer"};
    return verbs;
}

StateNode CdAudioControl::DriveState(EmulatorContext* context, int unit)
{
    StateNode drive = StateNode::Object();
    AtapiCdrom* cd = CdOf(context, unit);
    if (!cd)
        return drive;
    drive["unit"] = unit;
    drive["slot"] = IdeUnitSlot::IdFor(unit / AtaChannel::kUnits, unit % AtaChannel::kUnits);
    drive["tray_open"] = cd->TrayOpen();

    CdAudioPlayer& audio = cd->Audio();
    const CdImage* disc = cd->HasDisc() ? cd->Disc() : nullptr;
    if (disc)
    {
        StateNode d = StateNode::Object();
        d["format"] = disc->Format();
        d["source"] = disc->Describe();
        d["first_track"] = int(disc->FirstTrackNumber());
        d["last_track"] = int(disc->LastTrackNumber());
        d["lead_out_lba"] = static_cast<unsigned>(disc->LeadOutLba());
        d["lead_out_msf"] = MsfText(cd::LbaToMsf(disc->LeadOutLba()));
        d["audio_tracks"] = disc->HasAudio();
        d["sessions"] = int(disc->SessionCount());
        StateNode tracks = StateNode::Array();
        for (size_t i = 0; i < disc->TrackCount(); i++)
        {
            const cd::Track& t = disc->TrackAt(i);
            StateNode track = StateNode::Object();
            track["number"] = int(t.number);
            track["session"] = int(t.session);
            track["type"] = cd::TrackModeName(t.mode);
            if (!disc->TrackTitle(i).empty())
                track["title"] = disc->TrackTitle(i);  // an audio CD built from a folder: the file it came from
            track["start_lba"] = static_cast<unsigned>(t.startLba);
            track["start_msf"] = MsfText(cd::LbaToMsf(t.startLba));
            track["pregap_lba"] = static_cast<unsigned>(t.pregapLba);
            track["end_lba"] = static_cast<unsigned>(t.endLba);
            track["length_msf"] = MsfText(cd::FramesToMsf(t.Frames()));
            tracks.push(track);
        }
        d["tracks"] = tracks;
        drive["disc"] = d;
    }
    else
    {
        drive["disc"] = StateNode();
    }

    const CdAudioState& s = audio.State();
    const CdAudioStatus status = audio.PeekStatus();
    StateNode a = StateNode::Object();
    a["status"] = StatusName(status);
    const uint8_t codes[] = {0x15, 0x11, 0x12, 0x13, 0x14};
    a["status_code"] = int(codes[static_cast<int>(status)]);
    const uint64_t sample = audio.PeekHeadSample();
    const uint32_t lba = static_cast<uint32_t>(sample / cd::kSamplesPerFrame);
    a["lba"] = static_cast<unsigned>(lba);
    a["sample"] = sample;
    a["msf"] = MsfText(cd::LbaToMsf(lba));
    if (disc)
    {
        const int index = disc->TrackIndexAt(lba);
        if (index >= 0)
        {
            const cd::Track& t = disc->TrackAt(static_cast<size_t>(index));
            a["track"] = int(t.number);
            a["index"] = lba < t.startLba ? 0 : 1;
            a["relative_msf"] = (lba < t.startLba ? "-" : "") + MsfText(cd::FramesToMsf(lba < t.startLba ? t.startLba - lba : lba - t.startLba));
        }
    }
    a["play_start_lba"] = static_cast<unsigned>(s.playStartLba);
    a["play_end_lba"] = static_cast<unsigned>(s.endLba);
    a["sotc"] = s.sotc != 0;
    drive["audio"] = a;

    StateNode volume = StateNode::Object();
    volume["left"] = int(s.portVolume[0]);
    volume["right"] = int(s.portVolume[1]);
    volume["left_channel"] = RouteName(s.portSelect[0]);
    volume["right_channel"] = RouteName(s.portSelect[1]);
    drive["drive_volume"] = volume;

    StateNode mixer = StateNode::Object();
    mixer["row"] = IdeController::CdAudioName(unit);
    if (const AudioDeviceInfo* row = context->pSoundManager ? context->pSoundManager->device(CdAudioSourceFor(unit)) : nullptr)
    {
        mixer["volume"] = static_cast<double>(row->volume);
        mixer["mute"] = row->mute;
        mixer["solo"] = row->solo;
        mixer["active"] = row->activeRecently;
        mixer["peak"] = static_cast<double>(row->peak);
    }
    drive["mixer"] = mixer;
    return drive;
}

StateNode CdAudioControl::State(EmulatorContext* context)
{
    StateNode ret = StateNode::Object();
    IdeController* ide = context ? context->pIdeController : nullptr;
    const uint8_t mask = ide && ide->Enabled() ? ide->CdUnitMask() : 0;
    ret["available"] = mask != 0;
    if (!mask)
    {
        ret["reason"] = !ide || !ide->Enabled() ? "No IDE board on this machine (configure [HDD] Scheme)"
                                                : "No CD drive on the IDE board (a unit becomes one with CDn=1 or device=cdrom)";
        return ret;
    }
    StateNode drives = StateNode::Array();
    for (int unit = 0; unit < IdeController::kMaxUnits; unit++)
    {
        if ((mask >> unit) & 1)
            drives.push(DriveState(context, unit));
    }
    ret["drives"] = drives;
    return ret;
}

int CdAudioControl::ResolveDrive(const std::string& drive, CdAudioReply& reply) const
{
    IdeController* ide = _context ? _context->pIdeController : nullptr;
    const uint8_t mask = ide && ide->Enabled() ? ide->CdUnitMask() : 0;
    if (!mask)
    {
        reply = Fail("no-cd-drive", "this machine has no CD drive (an IDE unit becomes one with CDn=1 or device=cdrom)");
        return -1;
    }
    if (drive.empty() || drive == "cd" || drive == "auto")
    {
        for (int unit = 0; unit < IdeController::kMaxUnits; unit++)
        {
            if ((mask >> unit) & 1)
                return unit;
        }
    }
    long number = -1;
    int unit = ParseNumber(drive, number) ? static_cast<int>(number) : IdeController::UnitForSlot(drive);
    if (unit < 0 || unit >= IdeController::kMaxUnits || !((mask >> unit) & 1))
    {
        std::string list;
        for (int u = 0; u < IdeController::kMaxUnits; u++)
        {
            if ((mask >> u) & 1)
                list += (list.empty() ? "" : ", ") + IdeUnitSlot::IdFor(u / AtaChannel::kUnits, u % AtaChannel::kUnits);
        }
        reply = Fail("no-cd-drive", "'" + drive + "' is no CD drive; the CD drives: " + list);
        return -1;
    }
    return unit;
}

CdAudioReply CdAudioControl::Execute(const CdAudioRequest& request)
{
    CdAudioReply reply;
    const std::string verb = StringHelper::ToLower(request.verb.empty() ? "status" : request.verb);
    bool known = false;
    for (const std::string& v : Verbs())
        known = known || v == verb;
    if (!known)
        return Fail("bad-request", "unknown verb '" + request.verb + "' (status, play, pause, resume, stop, volume, mixer)");

    if (verb == "status" && request.drive.empty())
    {
        reply.body = State(_context);
        return reply;
    }
    const int unit = ResolveDrive(request.drive, reply);
    if (unit < 0)
        return reply;
    if (verb == "status")
    {
        reply.body["drive"] = DriveState(_context, unit);
        return reply;
    }
    if (verb == "mixer")
        return Mixer(unit, request.options);

    // The machine-side verbs: the guest's own commands would do the same, but from outside a replay cannot repeat them
    if (_context->pTimeTravelHooks)
    {
        const std::string guard = _context->pTimeTravelHooks->RecordingGuard(ttd::TTDGuardedAction::CdFrontPanel);
        if (!guard.empty())
            return Fail("recording", guard);
    }
    AtapiCdrom* cd = CdOf(_context, unit);
    if (verb == "volume")
        return Volume(unit, request.options);
    if (!cd->HasDisc() || !cd->Disc())
        return Fail("no-disc", "no disc in " + IdeUnitSlot::IdFor(unit / AtaChannel::kUnits, unit % AtaChannel::kUnits));
    if (cd->TrayOpen())
        return Fail("no-disc", "the guest ejected the disc of " + IdeUnitSlot::IdFor(unit / AtaChannel::kUnits, unit % AtaChannel::kUnits) +
                                   " (tray open): START STOP UNIT load, or insert the disc again");
    if (verb == "play")
        return Play(unit, request.options);

    bool done = true;
    Parked(_context, [&] {
        if (verb == "pause")
            done = cd->Audio().Pause();
        else if (verb == "resume")
            done = cd->Audio().Resume();
        else
            cd->Audio().Stop();
    });
    if (!done)
        return Fail("not-playing", "no play in progress to " + verb);
    reply.body["drive"] = DriveState(_context, unit);
    return reply;
}

CdAudioReply CdAudioControl::Play(int unit, const std::map<std::string, std::string>& options)
{
    AtapiCdrom* cd = CdOf(_context, unit);
    const CdImage& disc = *cd->Disc();
    auto option = [&options](const char* name) -> const std::string* {
        auto it = options.find(name);
        return it == options.end() ? nullptr : &it->second;
    };
    int64_t start = -1;
    int64_t end = -1;
    long number = 0;
    if (const std::string* track = option("track"))
    {
        if (!ParseNumber(*track, number) || disc.TrackIndexForNumber(static_cast<uint8_t>(number)) < 0)
            return Fail("bad-request", "track '" + *track + "' is not on the disc (tracks " + std::to_string(disc.FirstTrackNumber()) +
                                           "-" + std::to_string(disc.LastTrackNumber()) + ")");
        start = disc.TrackAt(static_cast<size_t>(disc.TrackIndexForNumber(static_cast<uint8_t>(number)))).startLba;
        // Without to=: through the last audio track of the run (same session, no data track between)
        size_t lastIndex = static_cast<size_t>(disc.TrackIndexForNumber(static_cast<uint8_t>(number)));
        const uint8_t session = disc.TrackAt(lastIndex).session;
        while (lastIndex + 1 < disc.TrackCount() && disc.TrackAt(lastIndex + 1).IsAudio() && disc.TrackAt(lastIndex + 1).session == session)
            lastIndex++;
        long to = disc.TrackAt(lastIndex).number;
        if (const std::string* last = option("to"))
        {
            if (!ParseNumber(*last, to) || disc.TrackIndexForNumber(static_cast<uint8_t>(to)) < 0 || to < number)
                return Fail("bad-request", "to='" + *last + "': no such track after track " + std::to_string(number));
        }
        end = disc.TrackAt(static_cast<size_t>(disc.TrackIndexForNumber(static_cast<uint8_t>(to)))).endLba;
    }
    else if (const std::string* lba = option("lba"))
    {
        long frames = 0;
        const std::string* length = option("frames");
        if (!ParseNumber(*lba, number) || number < 0 || !length || !ParseNumber(*length, frames) || frames <= 0)
            return Fail("bad-request", "lba= needs frames= (both whole numbers)");
        start = number;
        end = number + frames;
    }
    else if (const std::string* msf = option("msf"))
    {
        int32_t from = 0;
        int32_t to = 0;
        const std::string* last = option("end");
        if (!ParseMsf(*msf, from) || !last || !ParseMsf(*last, to) || from < 0 || to <= from)
            return Fail("bad-request", "msf= and end= are MM:SS:FF, end after msf");
        start = from;
        end = to;
    }
    else
    {
        return Fail("bad-request", "play needs track=N (to=M), lba=X frames=N, or msf=MM:SS:FF end=MM:SS:FF");
    }
    if (start >= disc.LeadOutLba())
        return Fail("bad-request", "LBA " + std::to_string(start) + " is past the lead-out (LBA " + std::to_string(disc.LeadOutLba()) + ")");
    const int index = disc.TrackIndexAt(static_cast<uint32_t>(start));
    if (index < 0 || !disc.TrackAt(static_cast<size_t>(index)).IsAudio())
        return Fail("bad-request", "LBA " + std::to_string(start) + " is no audio frame");
    // The drive's rules (PLAY AUDIO): the play ends at the session's lead-out; a range that runs
    // into a data track is refused
    end = std::min<int64_t>(end, disc.SessionLeadOutLba(disc.TrackAt(static_cast<size_t>(index)).session));
    for (size_t i = static_cast<size_t>(index); i < disc.TrackCount() && disc.TrackAt(i).pregapLba < end; i++)
    {
        if (!disc.TrackAt(i).IsAudio())
            return Fail("bad-request", "the range runs into data track " + std::to_string(disc.TrackAt(i).number) +
                                           " (a drive refuses it: END OF USER AREA ENCOUNTERED ON THIS TRACK)");
    }
    Parked(_context, [&] { cd->Audio().Play(static_cast<uint32_t>(start), static_cast<uint32_t>(end)); });
    CdAudioReply reply;
    reply.body["drive"] = DriveState(_context, unit);
    return reply;
}

CdAudioReply CdAudioControl::Volume(int unit, const std::map<std::string, std::string>& options)
{
    AtapiCdrom* cd = CdOf(_context, unit);
    CdAudioState next = cd->Audio().State();
    for (const auto& [name, value] : options)
    {
        long number = 0;
        if (name == "left" || name == "right")
        {
            if (!ParseNumber(value, number) || number < 0 || number > 255)
                return Fail("bad-request", name + "='" + value + "': a volume is 0-255");
            next.portVolume[name == "left" ? 0 : 1] = static_cast<uint8_t>(number);
        }
        else if (name == "route")
        {
            const std::string route = StringHelper::ToLower(value);
            if (route == "stereo")
                next.portSelect[0] = 1, next.portSelect[1] = 2;
            else if (route == "swap")
                next.portSelect[0] = 2, next.portSelect[1] = 1;
            else if (route == "mono")
                next.portSelect[0] = 3, next.portSelect[1] = 3;
            else if (route == "left")
                next.portSelect[0] = 1, next.portSelect[1] = 1;
            else if (route == "right")
                next.portSelect[0] = 2, next.portSelect[1] = 2;
            else if (route == "mute")
                next.portSelect[0] = 0, next.portSelect[1] = 0;
            else
                return Fail("bad-request", "route='" + value + "': stereo, swap, mono, left, right or mute");
        }
        else if (name == "sotc")
        {
            bool on = false;
            if (!ParseBool(value, on))
                return Fail("bad-request", "sotc='" + value + "': true or false");
            next.sotc = on ? 1 : 0;
        }
        else
        {
            return Fail("bad-request", "unknown option '" + name + "' for volume (left, right, route, sotc)");
        }
    }
    Parked(_context, [&] {
        CdAudioState& state = cd->Audio().MutableState();
        std::copy(std::begin(next.portVolume), std::end(next.portVolume), std::begin(state.portVolume));
        std::copy(std::begin(next.portSelect), std::end(next.portSelect), std::begin(state.portSelect));
        state.sotc = next.sotc;
    });
    CdAudioReply reply;
    reply.body["drive"] = DriveState(_context, unit);
    return reply;
}

CdAudioReply CdAudioControl::Mixer(int unit, const std::map<std::string, std::string>& options)
{
    SoundManager* sound = _context->pSoundManager;
    const AudioSourceType type = CdAudioSourceFor(unit);
    if (!sound || !sound->device(type))
        return Fail("no-cd-drive", "the mixer has no row for this drive yet (it appears with the first frame)");
    for (const auto& [name, value] : options)
    {
        bool on = false;
        if (name == "volume")
        {
            char* tail = nullptr;
            const double volume = std::strtod(value.c_str(), &tail);
            if (value.empty() || !tail || *tail || volume < 0.0 || volume > 4.0)
                return Fail("bad-request", "volume='" + value + "': 0.0-4.0 (1.0 = as the drive outputs it)");
            sound->setDeviceVolume(type, static_cast<float>(volume));
        }
        else if (name == "mute" || name == "solo")
        {
            if (!ParseBool(value, on))
                return Fail("bad-request", name + "='" + value + "': true or false");
            if (name == "mute")
                sound->setDeviceMute(type, on);
            else
                sound->setDeviceSolo(type, on);
        }
        else
        {
            return Fail("bad-request", "unknown option '" + name + "' for mixer (volume, mute, solo)");
        }
    }
    CdAudioReply reply;
    reply.body["drive"] = DriveState(_context, unit);
    return reply;
}
