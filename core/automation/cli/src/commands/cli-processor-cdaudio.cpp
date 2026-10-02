#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/io/ide/cdaudiocontrol.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "cli-processor.h"

/// region <CD audio commands>

// cdaudio [verb] [drive] [name=value ...] [--json]
// A thin adapter over CdAudioControl (core/src/emulator/io/ide/cdaudiocontrol.h): the same
// verbs, options and errors as the WebAPI, MCP, Lua and Python (PLAN #83)

namespace
{
    constexpr const char* NEWLINE = CLIProcessor::NEWLINE;

    std::string Text(const StateNode* node)
    {
        if (!node)
            return "";
        switch (node->kind)
        {
            case StateNode::Kind::String: return node->s;
            case StateNode::Kind::Int: return std::to_string(node->i);
            case StateNode::Kind::Bool: return node->b ? "yes" : "no";
            case StateNode::Kind::Double:
            {
                std::ostringstream out;
                out << std::fixed << std::setprecision(2) << node->d;
                return out.str();
            }
            default: return "";
        }
    }

    void DriveText(std::ostringstream& out, const StateNode& drive)
    {
        out << Text(drive.find("slot")) << " (unit " << Text(drive.find("unit")) << ")" << NEWLINE;
        const StateNode* disc = drive.find("disc");
        if (!disc || disc->kind == StateNode::Kind::Null)
        {
            out << "  disc: none" << NEWLINE;
        }
        else
        {
            out << "  disc: " << Text(disc->find("format")) << "  " << Text(disc->find("source")) << NEWLINE;
            out << "  tracks " << Text(disc->find("first_track")) << "-" << Text(disc->find("last_track"));
            if (const StateNode* sessions = disc->find("sessions"); sessions && sessions->i > 1)
                out << " in " << sessions->i << " sessions";
            out << ", lead-out " << Text(disc->find("lead_out_msf")) << " (LBA " << Text(disc->find("lead_out_lba")) << ")" << NEWLINE;
            if (const StateNode* tracks = disc->find("tracks"))
            {
                for (const StateNode& t : tracks->items)
                {
                    out << "    " << std::setw(2) << Text(t.find("number")) << "  " << std::left << std::setw(6) << Text(t.find("type"))
                        << std::right << " " << Text(t.find("start_msf")) << "  LBA " << std::setw(6) << Text(t.find("start_lba"))
                        << "  length " << Text(t.find("length_msf"));
                    if (const StateNode* session = t.find("session"); session && session->i > 1)
                        out << "  session " << session->i;
                    if (const StateNode* title = t.find("title"))
                        out << "  " << title->s;
                    out << NEWLINE;
                }
            }
        }
        if (const StateNode* a = drive.find("audio"))
        {
            out << "  audio: " << Text(a->find("status")) << " (0x" << std::hex << a->find("status_code")->i << std::dec << ")";
            if (a->find("track"))
                out << "  track " << Text(a->find("track")) << " index " << Text(a->find("index")) << "  "
                    << Text(a->find("msf")) << " (track " << Text(a->find("relative_msf")) << ")";
            out << "  LBA " << Text(a->find("lba")) << NEWLINE;
            out << "  play range: LBA " << Text(a->find("play_start_lba")) << "-" << Text(a->find("play_end_lba"))
                << (a->find("sotc")->b ? ", stop on track crossing" : "") << NEWLINE;
        }
        if (const StateNode* v = drive.find("drive_volume"))
            out << "  drive volume (page 0Eh): left " << Text(v->find("left")) << " plays " << Text(v->find("left_channel"))
                << ", right " << Text(v->find("right")) << " plays " << Text(v->find("right_channel")) << NEWLINE;
        if (const StateNode* m = drive.find("mixer"))
        {
            out << "  mixer row '" << Text(m->find("row")) << "'";
            if (m->find("volume"))
                out << ": volume " << Text(m->find("volume")) << (m->find("mute")->b ? ", muted" : "")
                    << (m->find("solo")->b ? ", solo" : "") << (m->find("active")->b ? ", sound" : "");
            out << NEWLINE;
        }
    }
}  // namespace

std::string CLIProcessor::CdAudioStateText(const StateNode& state)
{
    std::ostringstream out;
    if (const StateNode* available = state.find("available"); available && !available->b)
    {
        out << "No CD drive: " << Text(state.find("reason")) << NEWLINE;
        return out.str();
    }
    if (const StateNode* drives = state.find("drives"))
    {
        for (const StateNode& drive : drives->items)
            DriveText(out, drive);
    }
    if (const StateNode* drive = state.find("drive"))
        DriveText(out, *drive);
    return out.str();
}

void CLIProcessor::HandleCdAudio(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    CdAudioRequest request;
    request.verb = args.empty() ? "status" : args[0];
    std::transform(request.verb.begin(), request.verb.end(), request.verb.begin(), ::tolower);
    if (request.verb == "help")
    {
        ShowCdAudioHelp(session);
        return;
    }
    bool json = false;
    for (size_t i = 1; i < args.size(); i++)
    {
        const std::string& arg = args[i];
        if (arg == "--json")
        {
            json = true;
            continue;
        }
        const size_t equals = arg.find('=');
        if (equals == std::string::npos)
        {
            if (!request.drive.empty())
            {
                session.SendResponse("Error: unexpected argument '" + arg + "'. Use 'cdaudio help' for the syntax." + NEWLINE);
                return;
            }
            request.drive = arg;
            continue;
        }
        const std::string name = arg.substr(0, equals);
        if (name == "drive")
            request.drive = arg.substr(equals + 1);
        else
            request.options[name] = arg.substr(equals + 1);
    }

    const CdAudioReply reply = CdAudioControl(emulator->GetContext()).Execute(request);
    if (json)
    {
        session.SendResponse(reply.ToJson() + NEWLINE);
        return;
    }
    if (!reply.ok)
    {
        session.SendResponse("Error [" + reply.error + "]: " + reply.message + NEWLINE);
        return;
    }
    session.SendResponse(CdAudioStateText(reply.body));
}

void CLIProcessor::ShowCdAudioHelp(const ClientSession& session)
{
    std::ostringstream out;
    out << "CD audio of the ATAPI CD drives (the same verbs on WebAPI, MCP, Lua, Python):" << NEWLINE;
    out << "  cdaudio [status] [drive]                - disc, tracks, audio status, head, volume, mixer row" << NEWLINE;
    out << "  cdaudio play [drive] track=N [to=M]     - play track N through track M (default: the last)" << NEWLINE;
    out << "  cdaudio play [drive] lba=X frames=N     - play N frames (75 a second) from LBA X" << NEWLINE;
    out << "  cdaudio play [drive] msf=MM:SS:FF end=MM:SS:FF" << NEWLINE;
    out << "  cdaudio pause | resume | stop [drive]   - as PAUSE / RESUME and STOP PLAY / SCAN" << NEWLINE;
    out << "  cdaudio volume [drive] left=0-255 right=0-255 route=stereo|swap|mono|left|right|mute sotc=on|off" << NEWLINE;
    out << "                                          - page 0Eh, as MODE SELECT sets it" << NEWLINE;
    out << "  cdaudio mixer [drive] volume=0-4 mute=on|off solo=on|off - the drive's mixer row" << NEWLINE;
    out << "  drive: ide0.slave, a unit 0-3, or none for the first CD drive; --json for the raw reply" << NEWLINE;
    out << "  play, pause, resume, stop and volume are refused while TTD records (a replay would not repeat them)" << NEWLINE;
    session.SendResponse(out.str());
}

/// endregion </CD audio commands>
