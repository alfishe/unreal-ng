#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/media/mediacontrol.h>

#include "common/base64.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <set>
#include <sstream>

#include "cli-processor.h"

/// region <Media Commands>

// media <verb> [slot] [path] [--option value] [--flag] [--json]
// A thin adapter over MediaControl (core/src/emulator/media/mediacontrol.h):
// the same verbs, options, selectors and error codes as the WebAPI, MCP, Lua
// and Python. Design: docs/inprogress/2026-09-28-storage-manager/media-control-design.md

namespace
{
    /// Options that take a value; every other option is a flag
    const std::set<std::string> kValueOptions = {"access", "fs",       "codepage", "free", "format", "kind",
                                                 "export", "cylinders", "sides",   "size", "on",     "retarget",
                                                 "compression", "parent", "volume", "out", "in", "data", "start", "to",
                                                 "attributes", "time", "label"};

    /// Verbs that take a path after the slot
    bool TakesPath(const std::string& verb)
    {
        return verb == "insert" || verb == "swap" || verb == "save" || verb == "export" || verb == "targets" ||
               verb == "compose" || verb == "files" || verb == "read" || verb == "write" || verb == "delete" ||
               verb == "rename" || verb == "mkdir" || verb == "attrib";
    }

    /// `media compose` / `media layers`: the layout and one line per layer
    void CompositeText(std::ostringstream& out, const StateNode& value)
    {
        constexpr const char* NEWLINE = CLIProcessor::NEWLINE;
        out << "  " << value.find("descriptor")->s << ": " << value.find("fs")->s << ", " << value.find("sectors")->i
            << " sectors (" << value.find("clusters")->i << " clusters of " << value.find("sectorsPerCluster")->i * 512
            << " bytes), " << value.find("files")->i << " files, " << value.find("fileBytes")->i << " bytes, content "
            << value.find("contentId")->s << NEWLINE;
        int index = 0;
        for (const StateNode& layer : value.find("layers")->items)
        {
            out << "  " << index++ << " " << std::left << std::setw(12) << layer.find("name")->s << std::setw(7)
                << layer.find("kind")->s << layer.find("path")->s;
            if (!layer.find("from")->s.empty() && layer.find("from")->s != "/")
                out << " from " << layer.find("from")->s;
            out << " -> " << layer.find("mount")->s << "  " << layer.find("files")->i << " files, "
                << layer.find("bytes")->i << " bytes" << NEWLINE;
        }
        if (const StateNode* partitions = value.find("partitions"))
        {
            for (const StateNode& p : partitions->items)
                out << "  partition " << std::left << std::setw(10) << p.find("name")->s << " " << p.find("kind")->s << " "
                    << p.find("fs")->s << " type #" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                    << p.find("type")->i << std::dec << std::setfill(' ') << " at " << p.find("start")->i << ", "
                    << p.find("sectors")->i << " sectors" << NEWLINE;
        }
    }

    std::string MediumText(const StateNode* medium)
    {
        if (!medium || medium->kind == StateNode::Kind::Null)
            return "(empty)";
        std::string text = medium->find("source")->s + "  " + medium->find("format")->s + "  " +
                           medium->find("access")->s;
        if (medium->find("dirty")->b)
            text += "  unsaved " + medium->find("changes")->s;
        return text;
    }

    std::string Join(const StateNode* items)
    {
        std::string text;
        if (items)
        {
            for (const StateNode& item : items->items)
                text += (text.empty() ? "" : " ") + item.s;
        }
        return text;
    }

    std::string SlotLine(const StateNode& slot)
    {
        std::ostringstream line;
        line << "  " << std::left << std::setw(12) << slot.find("id")->s;
        const StateNode* aliases = slot.find("aliases");
        line << std::setw(6) << (aliases ? Join(aliases) : "");
        line << std::setw(9) << slot.find("state")->s;
        line << MediumText(slot.find("medium"));
        return line.str();
    }
}  // namespace

void CLIProcessor::HandleMedia(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    std::string verb = args.empty() ? "list" : args[0];
    std::transform(verb.begin(), verb.end(), verb.begin(), ::tolower);
    if (verb == "help")
    {
        ShowMediaHelp(session);
        return;
    }

    // Positional arguments, then --options
    MediaRequest request;
    request.verb = verb;
    bool json = false;
    std::vector<std::string> positional;
    for (size_t i = 1; i < args.size(); i++)
    {
        const std::string& arg = args[i];
        if (arg.rfind("--", 0) != 0)
        {
            positional.push_back(arg);
            continue;
        }
        std::string name = arg.substr(2);
        std::string value;
        const size_t equals = name.find('=');
        if (equals != std::string::npos)
        {
            value = name.substr(equals + 1);
            name = name.substr(0, equals);
        }
        std::replace(name.begin(), name.end(), '-', '_');  // --end-recording
        if (name == "json")
        {
            json = true;
            continue;
        }
        if (equals == std::string::npos && kValueOptions.count(name) && i + 1 < args.size())
            value = args[++i];
        request.options[name] = value;
    }

    const bool slotless = verb == "list" || verb == "formats" || verb == "targets" || verb == "compose";
    size_t next = 0;
    if (!slotless && next < positional.size())
        request.selector = positional[next++];
    if (TakesPath(verb) && next < positional.size())
        request.path = positional[next++];
    if (verb == "rename" && !request.options.count("to") && next < positional.size())
        request.options["to"] = positional[next++];  // media rename A OLD.C NEW.C
    if (verb == "attrib" && !request.options.count("attributes") && next < positional.size())
        request.options["attributes"] = positional[next++];  // media attrib A GAME.C +R-H
    if (verb == "label" && !request.options.count("label") && next < positional.size())
        request.options["label"] = positional[next++];  // media label A "MY DISK"
    if (next < positional.size())
    {
        session.SendResponse("Error: unexpected argument '" + positional[next] + "'" + NEWLINE +
                             "Use 'media help' for the syntax." + NEWLINE);
        return;
    }

    const MediaReply reply = MediaControl(emulator->GetContext()).Execute(request);
    if (json)
    {
        session.SendResponse(reply.ToJson() + NEWLINE);
        return;
    }

    std::ostringstream out;
    if (!reply.result.Ok())
    {
        out << "Error [" << MediaErrorCode(reply.result.error) << "]: " << reply.result.message << NEWLINE;
        session.SendResponse(out.str());
        return;
    }

    const StateNode body = reply.ToValue();
    if (verb == "list")
    {
        out << "  " << std::left << std::setw(12) << "Slot" << std::setw(6) << "Alias" << std::setw(9) << "State"
            << "Medium" << NEWLINE;
        for (const StateNode& slot : body.find("slots")->items)
            out << SlotLine(slot) << NEWLINE;
        const StateNode* detached = body.find("detached");
        if (detached && !detached->items.empty())
        {
            out << "Detached (the slot went away; save, export or discard by slot id):" << NEWLINE;
            for (const StateNode& slot : detached->items)
                out << SlotLine(slot) << NEWLINE;
        }
    }
    else if (verb == "info")
    {
        const StateNode* slot = body.find("info");
        out << SlotLine(*slot) << NEWLINE;
        out << "  kind: " << slot->find("kind")->s << "  tags: " << Join(slot->find("tags")) << NEWLINE;
        if (const StateNode* guest = slot->find("guestName"))
            out << "  guest: " << guest->s << NEWLINE;
    }
    else if (verb == "formats")
    {
        for (const auto& [kind, extensions] : body.find("formats")->members)
            out << "  " << std::left << std::setw(9) << kind << Join(&extensions) << NEWLINE;
    }
    else if (verb == "targets")
    {
        const StateNode* file = body.find("file");
        out << "  file: " << (file->find("kinds")->items.empty() ? "unknown" : Join(file->find("kinds")));
        if (!file->find("format")->s.empty())
            out << " (" << file->find("format")->s << ")";
        const StateNode* evidence = file->find("evidence");
        if (evidence && !evidence->items.empty())
            out << " - " << evidence->items.front().s;
        out << NEWLINE;
        const StateNode* refusal = body.find("refusal");
        if (refusal && refusal->kind == StateNode::Kind::String)
            out << "  refused: " << refusal->s << NEWLINE;
        const StateNode* defaultTarget = body.find("default");
        const int64_t chosen = defaultTarget && defaultTarget->kind == StateNode::Kind::Int ? defaultTarget->i : -1;
        int64_t index = 0;
        for (const StateNode& target : body.find("targets")->items)
        {
            const StateNode* slot = target.find("slot");
            const std::string where = slot && slot->kind == StateNode::Kind::String ? slot->s : target.find("action")->s;
            out << "  " << (index == chosen ? "* " : "  ") << std::left << std::setw(12) << where << std::setw(26)
                << target.find("label")->s;
            const StateNode* occupied = target.find("occupiedBy");
            out << (occupied && occupied->kind == StateNode::Kind::String ? "replaces " + occupied->s : "empty");
            if (target.find("dirty")->b)
                out << "  unsaved writes!";
            if (target.find("autostart")->b)
                out << "  autostart";
            out << NEWLINE;
            index++;
        }
        if (chosen < 0 && index > 1)
            out << "  several targets: name the slot (media insert <slot> <path>)" << NEWLINE;
    }
    else if (verb == "compose")
    {
        CompositeText(out, *body.find("compose"));
    }
    else if (verb == "layers")
    {
        out << "  slot " << reply.slot << NEWLINE;
        CompositeText(out, *body.find("layers"));
    }
    else if (verb == "changes")
    {
        const StateNode* changes = body.find("changes");
        out << "  slot " << reply.slot << ": " << (changes ? changes->items.size() : 0) << " change(s) in "
            << body.find("changedSectors")->i << " changed sector(s)" << NEWLINE;
        if (changes)
        {
            for (const StateNode& c : changes->items)
            {
                out << "  " << c.find("op")->s << " " << c.find("path")->s;
                if (const StateNode* old = c.find("oldPath"))
                    out << " (was " << old->s << ")";
                if (!c.find("layer")->s.empty())
                    out << " [" << c.find("layer")->s << "]";
                out << NEWLINE;
            }
        }
        for (const StateNode& w : body.find("warnings")->items)
            out << "  warning: " << w.s << NEWLINE;
    }
    else if (verb == "volumes")
    {
        out << "  slot " << reply.slot << ": " << body.find("volumes")->items.size() << " volume(s)" << NEWLINE;
        for (const StateNode& v : body.find("volumes")->items)
        {
            out << "  " << std::left << std::setw(10) << v.find("id")->s << std::setw(16) << v.find("system")->s
                << std::setw(14) << ("\"" + v.find("label")->s + "\"") << v.find("bytes")->i << " bytes";
            if (const StateNode* free = v.find("freeBytes"))
                out << ", " << free->i << " free";
            if (v.find("offset")->i)
                out << ", at byte " << v.find("offset")->i;
            if (const StateNode* writable = v.find("writable"); writable && !writable->b)
                out << ", read only";
            if (const StateNode* letters = v.find("attributes"); letters && !letters->s.empty())
                out << ", attributes " << letters->s;
            if (const StateNode* times = v.find("times"); times && times->b)
                out << ", times";
            if (const StateNode* labels = v.find("labels"); labels && labels->b)
                out << ", label";
            out << NEWLINE;
        }
        for (const StateNode& n : body.find("notes")->items)
            out << "  note: " << n.s << NEWLINE;
    }
    else if (verb == "files")
    {
        const StateNode* entries = body.find("entries");
        out << "  slot " << reply.slot << ", volume " << body.find("volume")->s << ", /" << body.find("path")->s << ": "
            << entries->items.size() << " entries" << NEWLINE;
        for (const StateNode& e : entries->items)
        {
            out << "  " << std::left << std::setw(40) << (e.find("path")->s + (e.find("directory")->b ? "/" : ""))
                << std::right << std::setw(9) << (e.find("directory")->b ? std::string("<dir>") : std::to_string(e.find("size")->i));
            if (e.find("start")->i)
            {
                char start[8];
                std::snprintf(start, sizeof start, "  #%04X", static_cast<unsigned>(e.find("start")->i));
                out << start;
            }
            if (const StateNode* a = e.find("attributes"))
                out << "  " << a->s;
            if (const StateNode* error = e.find("error"))
                out << "  error: " << error->s;
            out << NEWLINE;
        }
        for (const StateNode& n : body.find("notes")->items)
            out << "  note: " << n.s << NEWLINE;
    }
    else if (verb == "write" || verb == "delete" || verb == "rename" || verb == "mkdir" || verb == "attrib" || verb == "label")
    {
        const std::string path = body.find("path") ? body.find("path")->s : std::string();
        if (verb == "label")
            out << "  labelled '" << body.find("label")->s << "'";
        else if (verb == "attrib")
        {
            out << "  changed " << path;
            if (const StateNode* a = body.find("attributes"))
                out << ": " << (a->s.empty() ? std::string("no attributes") : a->s);
            if (const StateNode* t = body.find("mtime"))
            {
                const std::time_t seconds = static_cast<std::time_t>(t->i);
                std::tm tm {};
#ifdef _WIN32
                gmtime_s(&tm, &seconds);
#else
                gmtime_r(&seconds, &tm);
#endif
                char when[32];
                std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
                out << ", " << when;
            }
        }
        else if (verb == "write")
            out << "  wrote " << path << " (" << body.find("size")->i << " bytes)";
        else if (verb == "delete")
            out << "  deleted " << path;
        else if (verb == "mkdir")
            out << "  made " << path;
        else
            out << "  renamed " << path << " to " << body.find("to")->s;
        out << " on slot " << reply.slot << ", volume " << body.find("volume")->s << NEWLINE;
        if (body.find("access")->s == "writethrough")
            out << "  written through to the disk's file" << NEWLINE;
        else
            out << "  unsaved: " << body.find("changes")->s << " (media save / eject --save keeps them)" << NEWLINE;
    }
    else if (verb == "read" && !body.find("savedPath"))
    {
        // A hex dump of the start; --out <file> takes the whole file
        constexpr size_t kShown = 4096;
        std::vector<uint8_t> data;
        base64::Decode(body.find("data")->s, data);
        out << "  " << body.find("path")->s << ": " << data.size() << " bytes" << NEWLINE;
        for (size_t row = 0; row < data.size() && row < kShown; row += 16)
        {
            char line[96];
            int n = std::snprintf(line, sizeof line, "  %06zX ", row);
            for (size_t k = row; k < row + 16; k++)
                n += k < data.size() ? std::snprintf(line + n, sizeof line - n, " %02X", data[k])
                                     : std::snprintf(line + n, sizeof line - n, "   ");
            out << line << "  ";
            for (size_t k = row; k < row + 16 && k < data.size(); k++)
                out << (data[k] >= 0x20 && data[k] < 0x7F ? static_cast<char>(data[k]) : '.');
            out << NEWLINE;
        }
        if (data.size() > kShown)
            out << "  ... (--out <file> writes all " << data.size() << " bytes)" << NEWLINE;
    }
    else
    {
        out << "ok: " << reply.slot;
        if (reply.pending)
            out << " (pending: applied at the next frame boundary)";
        if (const StateNode* saved = body.find("savedPath"))
            out << " -> " << saved->s;
        out << NEWLINE;
    }
    for (const std::string& line : reply.result.report)
        out << "  note: " << line << NEWLINE;
    session.SendResponse(out.str());
}

void CLIProcessor::ShowMediaHelp(const ClientSession& session)
{
    std::ostringstream out;
    out << "Usage: media <verb> [slot] [path] [--options] [--json]" << NEWLINE << NEWLINE;
    out << "Verbs:" << NEWLINE;
    out << "  list                         - every slot and the detached media" << NEWLINE;
    out << "  info <slot>                  - one slot, its tags and medium" << NEWLINE;
    out << "  formats [--kind floppy]      - accepted formats per kind" << NEWLINE;
    out << "  targets <path>               - the slots that take a file (* = used without asking)" << NEWLINE;
    out << "  insert <slot|auto> <path>    - a file or a folder; auto picks the slot" << NEWLINE;
    out << "                                 a folder of MP3 / FLAC / WAV files in a CD slot is an audio CD" << NEWLINE;
    out << "                                 (--format audio-cd); 'info <slot>' lists its tracks" << NEWLINE;
    out << "                                 --journal replay|discard|off: a session journal left by a crash" << NEWLINE;
    out << "  swap <slot> <path>           - eject + insert in one step" << NEWLINE;
    out << "  eject <slot>                 - take the medium out" << NEWLINE;
    out << "  save <slot> [path]           - floppies: write back (or to path)" << NEWLINE;
    out << "                                 composites: --strategy delta (default: <descriptor>.delta, restored" << NEWLINE;
    out << "                                 at the next insert) | flat <path>; --force over a foreign delta" << NEWLINE;
    out << "  export <slot> <path>         - a copy of the medium as it is now (.img, .vhd, .chd)" << NEWLINE;
    out << "                                 --compact [--fs fat32] [--size 64MiB]: a defragmented FAT volume" << NEWLINE;
    out << "                                 --vhd fixed|dynamic: a .vhd target's kind (dynamic: sparse)" << NEWLINE;
    out << "  discard <slot>               - drop the unsaved writes" << NEWLINE;
    out << "  rescan <slot>                - rebuild a folder medium" << NEWLINE;
    out << "  create <slot> [--size bytes] - a blank floppy, or a card / disk up to 128 GiB" << NEWLINE;
    out << "  protect <slot> --on true|false - the write-protect switch" << NEWLINE;
    out << "  compose <descriptor>         - build a *.ucompose.yaml without inserting it: layout and report" << NEWLINE;
    out << "                                 (insert takes the descriptor like any file)" << NEWLINE;
    out << "  layers <slot>                - a composite medium's layers" << NEWLINE;
    out << "  changes <slot>               - the guest's unsaved writes as file operations (and their layers)" << NEWLINE;
    out << "  volumes <slot>               - the file systems on the disk: TR-DOS, +3DOS, iS-DOS / TASiS, ATM CP/M, MB-02," << NEWLINE
        << "                                 MDOS, FAT (DSS, NedoOS): floppies, FAT partitions, ATM CP/M partitions and" << NEWLINE
        << "                                 iS-DOS devices of an ATM hard disk" << NEWLINE;
    out << "  files <slot> [dir] [--volume id] [--recursive] - a directory's entries (the first volume by default)" << NEWLINE;
    out << "  read <slot> <file> [--volume id] [--out host-file] - a file's bytes (a hex dump without --out)" << NEWLINE;
    out << "  write <slot> <file> --in host-file | --data base64 [--start n] [--header=false] [--volume id]" << NEWLINE
        << "                               - a file onto the disk (TR-DOS NAME.T, +3DOS NAME.TYP, MDOS name.T, FAT and" << NEWLINE
        << "                                 iS-DOS dir/name.ext), an older file of that name replaced; --start: the" << NEWLINE
        << "                                 TR-DOS / MDOS start, +3DOS CODE, iS-DOS load address (#8000, 0x8000, 32768);" << NEWLINE
        << "                                 --header=false: +3DOS without its header" << NEWLINE;
    out << "  delete <slot> <file> [--volume id] [--recursive] - delete a file or an empty directory (FAT, iS-DOS);" << NEWLINE
        << "                                 --recursive: a directory with everything in it" << NEWLINE;
    out << "  rename <slot> <file> <new-name> [--volume id] - a bare new name keeps it in its directory; FAT and" << NEWLINE
        << "                                 iS-DOS take a path from the root (/name: the root), moving the file or" << NEWLINE
        << "                                 the directory" << NEWLINE;
    out << "  mkdir <slot> <dir> [--volume id] [--parents] - a new directory (FAT, iS-DOS catalogue); --parents:" << NEWLINE
        << "                                 the missing ones on the way too" << NEWLINE;
    out << "  attrib <slot> <file> [+R-H | RA] [--time 2026-10-26T14:30[:05] | now] [--volume id]" << NEWLINE
        << "                               - the attributes (FAT R H S A, iS-DOS R W H D, +3DOS R S A; +R-H changes," << NEWLINE
        << "                                 RA sets exactly) and the date and time (FAT, iS-DOS) of a file or directory" << NEWLINE;
    out << "  label <slot> <name> [--volume id] - the FAT volume label (\"\" removes it), TR-DOS disk title, iS-DOS" << NEWLINE
        << "                                 device name" << NEWLINE;
    out << "  flatten <slot> [path] --strategy flat|delta|commit|write-back [--plan] [--force]" << NEWLINE
        << "                               [--onConflict refuse|keep-both]" << NEWLINE
        << "                               - a composite by a named strategy (commit: into the graft base image;" << NEWLINE
        << "                                 write-back: the guest's files into the writable folder layers)" << NEWLINE
        << NEWLINE;
    out << "Slot: id (fdd.b), alias (B, b:, sd, hd), kind:index (floppy:1), tag:a+b" << NEWLINE;
    out << "A dirty medium leaves only with --save, --export <path> or --discard" << NEWLINE;
    out << "Options per verb:" << NEWLINE;
    for (const std::string& v : MediaControl::Verbs())
    {
        std::string options;
        for (const std::string& o : MediaControl::OptionsFor(v))
            options += (options.empty() ? "" : " ") + std::string("--") + o;
        out << "  " << std::left << std::setw(9) << v << (options.empty() ? "-" : options) << NEWLINE;
    }
    out << "Sync by default: the reply comes when the medium is in or out; --async returns at once" << NEWLINE;
    session.SendResponse(out.str());
}

/// endregion </Media Commands>
