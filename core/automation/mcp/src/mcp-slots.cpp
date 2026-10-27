#include "mcp-slots.h"

#include <cctype>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include "mcp-tool-utils.h"
#include "target-resolver.h"

namespace mcp
{

const std::vector<std::string>& TapeExtensions()
{
    static const std::vector<std::string> extensions = {"tap", "spc", "sta", "ltp", "zxt", "tzx"};
    return extensions;
}

const std::vector<std::pair<std::string, std::vector<std::string>>>& MediaToolActions()
{
    static const std::vector<std::string> insertOptions = {"access", "format", "fs", "codepage", "free", "wp", "kind", "device",
                                                           "save", "export", "discard", "strategy", "onConflict", "strict",
                                                           "end_recording", "async", "immediate", "journal"};
    static const std::vector<std::pair<std::string, std::vector<std::string>>> actions = {
        {"list", {}},
        {"info", {}},
        {"formats", {"kind"}},
        {"targets", {}},
        {"insert", insertOptions},
        {"eject", {"save", "export", "discard", "strategy", "onConflict", "strict", "end_recording", "async"}},
        {"swap", insertOptions},
        {"save", {"retarget", "compression", "compact", "fs", "size", "vhd", "strategy", "force", "plan", "onConflict", "commit"}},
        {"export", {"compression", "parent", "compact", "fs", "size", "vhd"}},
        {"discard", {"async"}},
        {"rescan", {"save", "export", "discard", "strategy", "onConflict", "strict", "async"}},
        {"create", {"format", "cylinders", "sides", "size", "save", "export", "discard", "strategy", "onConflict", "strict",
                    "end_recording", "async"}},
        {"protect", {"on"}},
        {"compose", {"fs", "codepage", "free"}},
        {"layers", {}},
        {"changes", {}},
        {"flatten", {"strategy", "plan", "force", "onConflict", "commit", "compression", "compact", "fs", "size", "vhd"}},
        {"volumes", {}},
        {"files", {"volume", "recursive"}},
        {"read", {"volume", "out"}},
        {"write", {"volume", "in", "data", "start", "header"}},
        {"delete", {"volume", "recursive"}},
        {"rename", {"volume", "to"}},
        {"mkdir", {"volume", "parents"}},
        {"attrib", {"volume", "attributes", "time"}},
        {"label", {"volume", "label"}},
    };
    return actions;
}

namespace
{
    /// Percent-encode a selector for the URL path ("tag:sd+neogs", "b:")
    std::string EncodeSegment(const std::string& text)
    {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        for (unsigned char c : text)
        {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                out.push_back(static_cast<char>(c));
            else
            {
                out.push_back('%');
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 0x0F]);
            }
        }
        return out;
    }

    std::string Summary(const std::string& action, const Json::Value& reply)
    {
        std::ostringstream out;
        if (action == "list")
        {
            for (const Json::Value& slot : reply["slots"])
            {
                out << slot["id"].asString();
                if (slot["aliases"].size() > 0)
                    out << " (" << slot["aliases"][0].asString() << ")";
                out << ": " << slot["state"].asString();
                if (slot["medium"].isObject())
                {
                    out << " " << slot["medium"]["source"].asString() << " [" << slot["medium"]["format"].asString() << ", "
                        << slot["medium"]["access"].asString();
                    if (slot["medium"]["dirty"].asBool())
                        out << ", unsaved " << slot["medium"]["changes"].asString();
                    out << "]";
                }
                out << "\n";
            }
            for (const Json::Value& slot : reply["detached"])
                out << "detached " << slot["id"].asString() << ": " << slot["medium"]["source"].asString() << "\n";
            out << "revision " << reply["revision"].asUInt64();
            return out.str();
        }
        if (action == "info")
        {
            const Json::Value& slot = reply["info"];
            out << slot["id"].asString() << ": " << slot["state"].asString();
            if (slot["medium"].isObject())
                out << " " << slot["medium"]["source"].asString() << " [" << slot["medium"]["format"].asString() << "]";
            return out.str();
        }
        if (action == "changes")
        {
            out << reply["changes"].size() << " change(s) in " << reply["changedSectors"].asUInt64() << " changed sector(s)";
            for (const Json::Value& c : reply["changes"])
            {
                out << "\n" << c["op"].asString() << " " << c["path"].asString();
                if (c.isMember("oldPath"))
                    out << " (was " << c["oldPath"].asString() << ")";
                if (!c["layer"].asString().empty())
                    out << " [" << c["layer"].asString() << "]";
            }
            for (const Json::Value& w : reply["warnings"])
                out << "\nwarning: " << w.asString();
            return out.str();
        }
        if (action == "volumes")
        {
            out << reply["volumes"].size() << " volume(s) on " << reply["slot"].asString();
            for (const Json::Value& v : reply["volumes"])
            {
                out << "\n" << v["id"].asString() << ": " << v["system"].asString() << " \"" << v["label"].asString() << "\", "
                    << v["bytes"].asUInt64() << " bytes";
                if (v.isMember("freeBytes"))
                    out << ", " << v["freeBytes"].asInt64() << " free";
                if (v["offset"].asUInt64())
                    out << ", at byte " << v["offset"].asUInt64();
                if (v.isMember("writable") && !v["writable"].asBool())
                    out << ", read only";
                if (!v["attributes"].asString().empty())
                    out << ", attributes " << v["attributes"].asString();
                if (v["times"].asBool())
                    out << ", times";
                if (v["labels"].asBool())
                    out << ", label";
            }
            for (const Json::Value& n : reply["notes"])
                out << "\nnote: " << n.asString();
            return out.str();
        }
        if (action == "files")
        {
            out << reply["entries"].size() << " entries in " << reply["slot"].asString() << " volume "
                << reply["volume"].asString() << " /" << reply["path"].asString();
            for (const Json::Value& e : reply["entries"])
            {
                out << "\n" << e["path"].asString();
                if (e["directory"].asBool())
                    out << "/";
                else
                    out << " " << e["size"].asUInt64() << " bytes";
                if (e["start"].asUInt())
                    out << ", start " << e["start"].asUInt();
                if (e.isMember("attributes"))
                    out << " [" << e["attributes"].asString() << "]";
                if (e.isMember("error"))
                    out << " error: " << e["error"].asString();
            }
            for (const Json::Value& n : reply["notes"])
                out << "\nnote: " << n.asString();
            return out.str();
        }
        if (action == "read")
        {
            out << reply["path"].asString() << ": " << reply["size"].asUInt64() << " bytes";
            if (reply.isMember("savedPath"))
                return out.str() + " written to " + reply["savedPath"].asString();
            // The bytes for an agent that sees only the text: base64 up to 16 KiB, else name 'out'
            const std::string data = reply["data"].asString();
            if (data.size() <= 16384 * 4 / 3 + 4)
                out << "\nbase64: " << data;
            else
                out << "\n(too long for the text: the bytes are in structuredContent.data, or give 'out' to write a host file)";
            return out.str();
        }
        if (action == "write" || action == "delete" || action == "rename" || action == "mkdir" || action == "attrib" || action == "label")
        {
            std::string changed = "changed " + reply["path"].asString();
            if (reply.isMember("attributes"))
                changed += ": attributes " + (reply["attributes"].asString().empty() ? std::string("none") : reply["attributes"].asString());
            if (reply.isMember("mtime"))
                changed += ", mtime " + std::to_string(reply["mtime"].asInt64());
            out << (action == "write"    ? "wrote " + reply["path"].asString() + " (" + std::to_string(reply["size"].asUInt64()) + " bytes)"
                    : action == "delete" ? "deleted " + reply["path"].asString()
                    : action == "mkdir"  ? "made " + reply["path"].asString()
                    : action == "attrib" ? changed
                    : action == "label"  ? "labelled '" + reply["label"].asString() + "'"
                                         : "renamed " + reply["path"].asString() + " to " + reply["to"].asString())
                << " on " << reply["slot"].asString() << " volume " << reply["volume"].asString();
            if (reply["access"].asString() == "writethrough")
                out << "; written through to the disk's file";
            else
                out << "; unsaved: " << reply["changes"].asString() << " (save:true on eject, or the save action, keeps them)";
            return out.str();
        }
        if (action == "formats")
            return "Accepted formats per kind\n" + FlatFields(reply["formats"]);
        if (action == "compose" || action == "layers")
        {
            const Json::Value& value = reply[action];
            out << value["descriptor"].asString() << ": " << value["fs"].asString() << ", " << value["sectors"].asUInt64()
                << " sectors, " << value["files"].asUInt64() << " files, content " << value["contentId"].asString();
            for (const Json::Value& layer : value["layers"])
                out << "\n" << layer["name"].asString() << " (" << layer["kind"].asString() << " " << layer["path"].asString()
                    << ") -> " << layer["mount"].asString() << ": " << layer["files"].asUInt64() << " files";
            for (const Json::Value& line : reply["report"])
                out << "\nnote: " << line.asString();
            return out.str();
        }
        if (action == "targets")
        {
            const Json::Value& file = reply["file"];
            out << "file: ";
            if (file["kinds"].empty())
                out << "unknown";
            for (Json::ArrayIndex i = 0; i < file["kinds"].size(); i++)
                out << (i ? " " : "") << file["kinds"][i].asString();
            if (!file["format"].asString().empty())
                out << " (" << file["format"].asString() << ")";
            if (file["evidence"].size() > 0)
                out << " - " << file["evidence"][0].asString();
            if (reply["refusal"].isString())
                out << "\nrefused: " << reply["refusal"].asString();
            const int chosen = reply["default"].isInt() ? reply["default"].asInt() : -1;
            for (Json::ArrayIndex i = 0; i < reply["targets"].size(); i++)
            {
                const Json::Value& target = reply["targets"][i];
                out << "\n" << (static_cast<int>(i) == chosen ? "* " : "  ")
                    << (target["slot"].isString() ? target["slot"].asString() : target["action"].asString()) << " - "
                    << target["label"].asString();
                if (target["occupiedBy"].isString())
                    out << ", replaces " << target["occupiedBy"].asString();
                if (target["dirty"].asBool())
                    out << " (unsaved writes!)";
                if (target["autostart"].asBool())
                    out << ", autostart";
            }
            if (chosen < 0 && reply["targets"].size() > 1)
                out << "\nseveral targets: ask the user, then insert with the chosen slot";
            return out.str();
        }
        out << action << " " << reply["slot"].asString() << ": ok";
        if (reply["pending"].asBool())
            out << " (pending: applied at the next frame boundary)";
        for (const Json::Value& line : reply["report"])
            out << "\nnote: " << line.asString();
        return out.str();
    }
}  // namespace

void RegisterMediaSlots(ToolRegistry& registry)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["target"]["type"] = "string";
    schema["properties"]["target"]["description"] = "Emulator id, or 'auto'";
    schema["properties"]["action"]["type"] = "string";
    for (const auto& [verb, verbOptions] : MediaToolActions())
        schema["properties"]["action"]["enum"].append(verb);
    // Not MediaControl verbs: the files opened lately (RecentFiles, GET / DELETE /api/v1/recent)
    schema["properties"]["action"]["enum"].append("recent");
    schema["properties"]["action"]["enum"].append("recent_clear");
    schema["properties"]["slot"]["type"] = "string";
    schema["properties"]["slot"]["description"] =
        "Slot selector: id (fdd.b, sd.zc), alias (A, B, b:, sd, tape, hd), kind:index (floppy:1), tag query "
        "(tag:sd+neogs); 'auto' for insert (chosen from the file's content)";
    schema["properties"]["path"]["type"] = "string";
    schema["properties"]["path"]["description"] =
        "insert / swap: a file or a folder on the emulator host; save / export: the target file; targets: the file "
        "or folder to place; compose: a *.ucompose.yaml descriptor; files: a directory on the disk (empty: the root); "
        "read: a file on the disk (SHELL/HELP/exit.hlp, BOOT.B, games/klad.exe); write / delete / rename: the file "
        "(TR-DOS NAME.T, +3DOS NAME.TYP, FAT and iS-DOS dir/name.ext) or directory; mkdir: the directory to make; attrib: "
        "the file or directory to change";
    schema["properties"]["descriptor"]["type"] = "object";
    schema["properties"]["descriptor"]["description"] =
        "insert / swap / compose: a composition descriptor inline (version, target, layers) instead of a path";

    // Every option any verb takes, typed; MediaControl checks which verb takes which
    const std::set<std::string> booleans = {"save", "discard", "wp", "on", "retarget", "end_recording", "async", "immediate", "compact", "force", "plan", "strict", "recursive", "header", "parents"};
    const std::set<std::string> integers = {"free", "cylinders", "sides", "size"};
    std::set<std::string> options;
    std::string perVerb;
    for (const auto& [verb, verbOptions] : MediaToolActions())
    {
        std::string list;
        for (const std::string& option : verbOptions)
        {
            options.insert(option);
            list += (list.empty() ? "" : ", ") + option;
        }
        perVerb += "\n" + verb + ": " + (list.empty() ? "-" : list);
    }
    for (const std::string& option : options)
        schema["properties"][option]["type"] = booleans.count(option) ? "boolean" : integers.count(option) ? "integer" : "string";
    schema["properties"]["access"]["enum"].append("readonly");
    schema["properties"]["access"]["enum"].append("session");
    schema["properties"]["access"]["enum"].append("writethrough");
    schema["properties"]["export"]["description"] = "Disposition: write a dirty medium to this new file before it leaves";
    schema["properties"]["kind"]["description"] = "formats: filter; insert auto: the kind a folder becomes (floppy, block)";
    schema["properties"]["compression"]["description"] =
        "save / export of a hard disk or SD card to a .chd: none, default (lzma,zlib,huff,flac) or up to four of zlib, lzma, "
        "huff, flac, zstd";
    schema["properties"]["parent"]["description"] = "export to a .chd: write a child of this parent CHD";
    schema["properties"]["strategy"]["description"] =
        "save of a composite (*.ucompose.yaml): delta (the session's writes into <descriptor>.delta, restored at the "
        "next insert), flat (a new image at path; the default with one), commit, write-back; left out: the "
        "descriptor's writes.save. With save:true on insert / swap / eject / create / rescan also discard (drop the "
        "writes) or ask (a delta here; the GUI asks)";
    schema["properties"]["commit"]["description"] =
        "save / flatten with strategy commit of a composite whose writes.commit allows it: all (every change or none: a "
        "partition with no source to go to refuses the commit) or partial (what has a source; the rest stays in the "
        "session). A descriptor's writes.commit: none (the default) refuses commit and write-back";
    schema["properties"]["strict"]["description"] =
        "save:true of a composite leaving its slot: a commit / write-back that fails refuses (the medium stays) instead "
        "of keeping the writes as a session delta";
    schema["properties"]["force"]["description"] =
        "save as delta over a delta that was written over other sources; commit although the guest's file system has "
        "lost clusters or cross-links";
    schema["properties"]["plan"]["description"] =
        "save / flatten with strategy commit or write-back: report what would be written, write nothing";
    schema["properties"]["onConflict"]["description"] =
        "write-back: refuse (default) when a host file changed since the build, or keep-both (the guest's version as "
        "'name (guest).ext')";
    schema["properties"]["journal"]["description"] =
        "insert / swap of a medium written in session access: replay a session journal left by a crash "
        "(<source>.usession) and keep one, discard it unread and start a new one, or off (no journal next to the "
        "medium). Left out: [MEDIA] SessionJournal (off by default)";
    schema["properties"]["vhd"]["description"] =
        "save / export / flatten of a block medium to a new .vhd: fixed (default) or dynamic (only the 2 MiB blocks "
        "holding data are stored)";
    schema["properties"]["compact"]["description"] =
        "save / export of a FAT disk or card: write a re-synthesized volume (every file contiguous; fs converts, size "
        "resizes) instead of the layout as it is";
    schema["properties"]["format"]["description"] =
        "insert / swap: 'audio-cd' - a folder of MP3 / FLAC / WAV files into a CD-ROM drive as a Red Book audio CD (a "
        "folder in a CD slot is one anyway; the reply's report lists the tracks and every file not taken); create: auto, "
        "unformatted (raw), plus3 or trdos (floppies; trdos is formatted with an empty catalogue); create on an IDE / SD slot: size in bytes";
    schema["properties"]["volume"]["description"] =
        "files / read / write / delete / rename / mkdir / attrib / label: the volume id from volumes (trdos, plus3, isdos, fat, p1-p4 MBR partitions, isdos-<cylinder> "
        "iS-DOS devices of an ATM hard disk); left out: the first";
    schema["properties"]["out"]["description"] = "read: write the file's bytes to this host file instead of answering them";
    schema["properties"]["in"]["description"] = "write: the bytes from this host file";
    schema["properties"]["data"]["description"] = "write: the bytes in base64 (instead of 'in')";
    schema["properties"]["start"]["description"] =
        "write: the TR-DOS start field / the +3DOS CODE / the iS-DOS load address: 32768, 0x8000 or #8000";
    schema["properties"]["to"]["description"] =
        "rename: the new name; a bare name keeps the file in its directory, FAT and iS-DOS take a path from the root "
        "(/name: the root), moving the file or the directory";
    schema["properties"]["recursive"]["description"] = "files: the whole tree under the directory; delete: a directory with everything in it";
    schema["properties"]["parents"]["description"] = "mkdir: make the missing directories on the way too (one there already is no error)";
    schema["properties"]["header"]["description"] = "write: +3DOS: false writes the file without its 128-byte header";
    schema["properties"]["attributes"]["description"] =
        "attrib: the letters of the volume's file system (volumes lists them: FAT RHSA read only / hidden / system / "
        "archive, iS-DOS RWHD read / write protected, hidden, delete protected, +3DOS RSA): '+R-H' sets and clears, 'RA' "
        "leaves exactly these set";
    schema["properties"]["time"]["description"] =
        "attrib: the date and time of the file or directory (FAT, iS-DOS): '2026-10-26 14:30[:05]', 'now' or UTC seconds";
    schema["properties"]["label"]["description"] =
        "label: the volume's name - the FAT volume label (empty removes it), the TR-DOS disk title (8), the iS-DOS device "
        "name (8, 11 on a Chic disk)";
    schema["properties"]["device"]["type"] = "string";
    schema["properties"]["device"]["description"] =
        "insert / swap on an IDE unit: disk, cdrom or cf (a CompactFlash card on an IDE adapter) - swap the unit's drive first (the unit must be empty)";
    schema["required"].append("action");

    registry.Register(
        "media",
        "The machine's media slots (floppy drives, tape, IDE hard disks and CD-ROM, SD cards). A CD-ROM drive takes "
        "ISO / CUE / raw BIN / CD CHD images (multisession too) and folders of MP3 / FLAC / WAV files (an audio CD; "
        "info shows the disc's tracks). Actions: list, info, "
        "formats, targets (where a file can go: what it is, the slots that take it in order, the default, or why "
        "nothing does), insert, swap, eject, save, export, discard, rescan, create, protect, compose (build a "
        "*.ucompose.yaml - several folders layered into one FAT disk - without inserting it; insert takes the "
        "descriptor like a file), layers (a composite medium's layers), changes (the guest's unsaved writes as file "
        "operations - create, modify, delete, rename, mkdir, rmdir - with the layer each touched), flatten (a composite by a "
        "named strategy: flat <path>, delta, commit - the graft base image takes everything, journaled - or write-back - "
        "the guest's file changes into the writable folder layers), volumes / files / read (the files on a disk in "
        "a slot: TR-DOS, +3DOS, iS-DOS / TASiS floppies and ATM hard disk devices, FAT of Sprinter DSS and "
        "NedoOS floppies, hard disks and SD cards), write / delete / rename / mkdir (a file on that disk: write takes path "
        "the name, data base64 or in a host file, start, header, and replaces an older file of that name; rename "
        "takes to and moves directories too; delete with recursive:true takes a directory with its contents; mkdir makes "
        "a directory, parents:true the missing ones on the way; attrib sets attributes and / or time of a file or "
        "directory; label names the volume; a session disk turns dirty until saved, a write-through one takes it into its file). Operations are synchronous (the reply "
        "comes when the medium is in or out; async:true returns at once). A dirty medium leaves its slot only with "
        "save:true, export:'<path>' or discard:true; save:true follows the composite's writes.save (or strategy), and a "
        "commit / write-back that fails keeps the writes as a session delta. rescan leaves a medium whose sources did "
        "not change as it is. The reply's 'revision' increases with every change. "
        "recent lists the files opened lately, newest first (kind: all, snapshot, tape, disk, rzx, image, cd; open one "
        "with the actions tool: run media.open_recent {index, kind}); recent_clear forgets them. "
        "Options per action:" + perVerb,
        std::move(schema),
        [](const Json::Value& args, IApiCaller& caller, ToolCallback done, const ProgressFn&) {
            const std::string action = args["action"].asString();
            const std::string slot = args.isMember("slot") ? args["slot"].asString() : "";

            if (action == "recent" || action == "recent_clear")
            {
                const std::string kind = args.get("kind", "all").asString();
                const std::string path = "/api/v1/recent" + (kind.empty() || kind == "all" ? std::string()
                                                                                             : "?kind=" + EncodeSegment(kind));
                caller.Call(action == "recent" ? "GET" : "DELETE", path, nullptr, [action, done](int status, Json::Value reply) {
                    if (status < 200 || status >= 300)
                    {
                        done(ToolResult::Error("media " + action + ": " +
                                               (status == 0 ? std::string("WebAPI unreachable") : DescribeErrorBody(reply))));
                        return;
                    }
                    std::ostringstream out;
                    const Json::Value& files = reply.isMember("files") ? reply["files"] : reply["all"];
                    if (action == "recent_clear")
                        out << "Recent files cleared (" << reply.get("cleared", "all").asString() << ")";
                    else if (files.empty())
                        out << "No recent files";
                    else
                    {
                        const std::string kind = reply.get("kind", "all").asString();
                        out << files.size() << " recent file(s), newest first (open: actions run media.open_recent "
                            << (kind == "all" ? std::string("{index}") : "{index, kind: " + kind + "}") << "):";
                        for (const Json::Value& file : files)
                            out << "\n" << file["index"].asUInt() << " " << file["kind"].asString() << " "
                                << file["name"].asString() << " - " << file["path"].asString();
                    }
                    const std::string text = out.str();
                    done(ToolResult::Ok(text, std::move(reply)));
                });
                return;
            }

            auto body = std::make_shared<Json::Value>(Json::objectValue);
            for (const std::string& name : args.getMemberNames())
            {
                if (name != "action" && name != "slot" && name != "target")
                    (*body)[name] = args[name];
            }

            TargetResolver::ResolveFromArgs(args, caller, [action, slot, body, &caller, done](bool ok, const std::string& idOrError) {
                if (!ok)
                {
                    done(ToolResult::Error(idOrError));
                    return;
                }
                const std::string& id = idOrError;
                std::string method = "POST";
                std::string path;
                const Json::Value* payload = body.get();
                if (action == "list")
                {
                    method = "GET";
                    path = Endpoint(id, "/media");
                    payload = nullptr;
                }
                else if (action == "formats")
                {
                    method = "GET";
                    path = Endpoint(id, "/media/formats");
                    if (body->isMember("kind"))
                        path += "?kind=" + EncodeSegment((*body)["kind"].asString());
                    payload = nullptr;
                }
                else if (action == "targets")
                {
                    method = "GET";
                    path = Endpoint(id, "/media/targets?path=" + EncodeSegment((*body).get("path", "").asString()));
                    payload = nullptr;
                }
                else if (action == "compose")
                {
                    std::string descriptor = (*body).get("path", "").asString();
                    if ((*body)["descriptor"].isObject())
                    {
                        Json::StreamWriterBuilder writer;
                        writer["indentation"] = "";
                        descriptor = Json::writeString(writer, (*body)["descriptor"]);
                    }
                    method = "GET";
                    path = Endpoint(id, "/media/compose?path=" + EncodeSegment(descriptor));
                    for (const char* option : {"fs", "codepage", "free"})
                    {
                        if (body->isMember(option))
                            path += std::string("&") + option + "=" + EncodeSegment((*body)[option].asString());
                    }
                    payload = nullptr;
                }
                else if (action == "info")
                {
                    method = "GET";
                    path = Endpoint(id, "/media/" + EncodeSegment(slot));
                    payload = nullptr;
                }
                else
                {
                    path = Endpoint(id, "/media/" + EncodeSegment(slot) + "/" + action);
                }

                caller.Call(method, path, payload, [action, body, done](int status, Json::Value reply) {
                    if (status == 0)
                    {
                        done(ToolResult::Error("WebAPI unreachable — is the emulator running with WebAPI enabled (port 8090)?"));
                        return;
                    }
                    if (status < 200 || status >= 300 || !reply.get("ok", false).asBool())
                    {
                        done(ToolResult::Error("media " + action + ": [" + reply.get("error", "error").asString() + "] " +
                                               reply.get("message", DescribeErrorBody(reply)).asString()));
                        return;
                    }
                    // the text first: argument order is unspecified, and gcc moves `reply` out before reading it
                    const std::string text = Summary(action, reply);
                    done(ToolResult::Ok(text, std::move(reply)));
                });
            });
        });
}

} // namespace mcp
