    // Media: the drive collection (media-control-design.md). The verbs and their
    // option lists come from MediaControl at run time, so this text cannot drift
    // from what the server accepts.
    {
        auto idParameter = [](Json::Value& operation, int index) {
            operation["parameters"][index]["name"] = "id";
            operation["parameters"][index]["in"] = "path";
            operation["parameters"][index]["required"] = true;
            operation["parameters"][index]["schema"]["type"] = "string";
            operation["parameters"][index]["description"] = "Emulator UUID or symbolic id";
        };
        auto slotParameter = [](Json::Value& operation, int index) {
            operation["parameters"][index]["name"] = "slot";
            operation["parameters"][index]["in"] = "path";
            operation["parameters"][index]["required"] = true;
            operation["parameters"][index]["schema"]["type"] = "string";
            operation["parameters"][index]["description"] =
                "A slot selector: the id (fdd.b, sd.zc), an alias (B, b:, sd, tape, hd), kind:index (floppy:1) "
                "or a tag query (tag:sd+neogs). 'auto' for insert: the slot is chosen from the file's content. "
                "Unknown or ambiguous selectors answer 404 unknown-slot / 400 ambiguous-slot with the candidates";
        };
        auto replyRef = [](Json::Value& operation) {
            operation["responses"]["200"]["description"] = "Done (sync) or queued (async: pending = true)";
            operation["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] = "#/components/schemas/MediaReply";
            operation["responses"]["400"]["description"] = "bad-request, kind-mismatch, unreadable-source, unknown-format, does-not-fit, ambiguous-slot";
            operation["responses"]["404"]["description"] = "unknown-slot, unknown-emulator";
            operation["responses"]["409"]["description"] =
                "dirty (say save, export or discard), recording (TTD v1 records; end_recording; on the engine a change "
                "while recording lands at the next frame boundary), in-use";
            operation["responses"]["501"]["description"] = "not-supported";
        };

        Json::Value& get = paths["/api/v1/emulator/{id}/media"]["get"];
        get["summary"] = "List the machine's media slots";
        get["description"] =
            "Every slot (floppy drives, SD cards, later tape / IDE / CD) with its tags, aliases, state and medium, "
            "and the detached media (a medium whose slot went away, e.g. an add-on card removed). "
            "'revision' increases with every change: poll it to know when to reload";
        get["tags"].append("Media");
        idParameter(get, 0);
        replyRef(get);

        Json::Value& info = paths["/api/v1/emulator/{id}/media/{slot}"]["get"];
        info["summary"] = "One slot; 'formats' lists the accepted formats, 'targets' the slots that take a file";
        info["description"] =
            "GET /media/{slot}: the slot (reply field 'info') and the medium's report (skipped folder entries, notes). "
            "GET /media/formats[?kind=floppy|tape|block|optical]: extensions per kind (reply field 'formats'). "
            "GET /media/targets?path=<file>: what the file is (reply field 'file': kinds, format, evidence) and where "
            "it can go on this machine (field 'targets', in the order a chooser lists them; 'default': the index used "
            "without asking, null when the caller should ask; 'refusal': why nothing takes it). A refusal is an answer: "
            "ok stays true. "
            "GET /media/compose?path=<descriptor>[&fs=fat16|fat32&codepage=&free=]: build a composition descriptor "
            "(a *.ucompose.yaml / .json file, or its JSON text) without inserting it (reply field 'compose': fs, sectors, "
            "clusters, contentId, files, layers; 'report': shadowed, whited-out and skipped entries). Insert takes the "
            "descriptor like any file";
        info["tags"].append("Media");
        idParameter(info, 0);
        slotParameter(info, 1);
        info["parameters"][2]["name"] = "path";
        info["parameters"][2]["in"] = "query";
        info["parameters"][2]["required"] = false;
        info["parameters"][2]["schema"]["type"] = "string";
        info["parameters"][2]["description"] = "targets: the file or folder to place; compose: the descriptor";
        replyRef(info);

        Json::Value& files = paths["/api/v1/emulator/{id}/media/{slot}/{verb}"]["get"];
        files["summary"] = "The files on a slot's disk: volumes / files / read (read only)";
        files["description"] =
            "The file systems on the disk in a slot, one way for all of them: TR-DOS, +3DOS and iS-DOS / TASiS floppies, "
            "FAT12 floppies (Sprinter DSS, MS-DOS), the FAT partitions of hard disks and SD cards (Estex DSS, NedoOS) and "
            "the iS-DOS / TASiS devices of an ATM-Turbo hard disk (a descriptor at a cylinder boundary). "
            "volumes: reply field 'volumes' (id, fs, system, label, offset, bytes, freeBytes). "
            "files?path=<dir>[&volume=<id>][&recursive=true]: field 'entries' (path, name, directory, size, start, "
            "attributes, mtime, error, and the file system's own fields), 'notes' (loops cut, stale entries). "
            "read?path=<file>[&volume=<id>][&out=<host file>]: field 'size' and 'data' (base64, at most 16 MiB) or "
            "'savedPath'; raw=true answers the bytes themselves (application/octet-stream). The same verbs are POST "
            "with a JSON body. The emulator is parked while the disk is read";
        files["tags"].append("Media");
        idParameter(files, 0);
        slotParameter(files, 1);
        files["parameters"][2]["name"] = "verb";
        files["parameters"][2]["in"] = "path";
        files["parameters"][2]["required"] = true;
        files["parameters"][2]["schema"]["type"] = "string";
        for (const char* v : {"volumes", "files", "read"})
            files["parameters"][2]["schema"]["enum"].append(v);
        const char* fileParameters[][2] = {
            {"path", "files: a directory on the disk (empty: the root); read: a file (SHELL/HELP/exit.hlp, BOOT.B)"},
            {"volume", "the volume id from volumes (trdos, plus3, isdos, fat, p1-p4, isdos-<cylinder>); left out: the first"},
            {"recursive", "files: the whole tree under the directory"},
            {"out", "read: write the bytes to this host file"},
            {"raw", "read: answer the bytes as application/octet-stream"},
        };
        for (const auto& [name, description] : fileParameters)
        {
            Json::Value parameter;
            parameter["name"] = name;
            parameter["in"] = "query";
            parameter["required"] = false;
            parameter["schema"]["type"] = std::string(name) == "recursive" || std::string(name) == "raw" ? "boolean" : "string";
            parameter["description"] = description;
            files["parameters"].append(parameter);
        }
        replyRef(files);

        std::string verbs;
        for (const std::string& v : MediaControl::Verbs())
        {
            if (v == "list" || v == "info" || v == "formats" || v == "targets" || v == "compose")
                continue;
            std::string options;
            for (const std::string& o : MediaControl::OptionsFor(v))
                options += (options.empty() ? "" : ", ") + o;
            verbs += "\n- **" + v + "**: " + (options.empty() ? "no options" : options);
        }
        Json::Value& post = paths["/api/v1/emulator/{id}/media/{slot}/{verb}"]["post"];
        post["summary"] =
            "insert / swap / eject / save / export / discard / rescan / create / protect / layers / changes / flatten / "
            "volumes / files / read / write / delete / rename / mkdir";
        post["description"] =
            "Operations are synchronous: the reply comes when the medium is in or out of the slot (swap delay "
            "included, at most 5 s, then pending = true). async: true replies at once with pending = true. "
            "A dirty medium leaves its slot only with a disposition: save (into its own file), export: <path> "
            "(into a new file) or discard. insert / swap take 'path' (a file or a folder) or the file itself "
            "(multipart/form-data 'file', or a raw body with X-Filename; up to 1 MB, deleted when ejected). "
            "save / export take 'path' as the target. write puts a file onto the disk in the slot (TR-DOS NAME.T, +3DOS "
            "NAME.TYP, iS-DOS, FAT), an older file of that name replaced: the bytes as base64 'data', a host file "
            "'in', or the body itself (application/octet-stream, ?path=NAME.T). delete removes a file or an empty "
            "iS-DOS / FAT directory (recursive: a directory with its contents); rename takes 'to': a bare name stays "
            "in the directory, a path from the root (/name: the root) moves the file or directory (iS-DOS, FAT); "
            "mkdir makes a directory (parents: the missing ones on the way too). The reply's access says where the change went: writethrough is in the "
            "disk's file, session is dirty until saved. "
            "Options go in the JSON body or the query string:" + verbs;
        post["tags"].append("Media");
        idParameter(post, 0);
        slotParameter(post, 1);
        post["parameters"][2]["name"] = "verb";
        post["parameters"][2]["in"] = "path";
        post["parameters"][2]["required"] = true;
        post["parameters"][2]["schema"]["type"] = "string";
        for (const std::string& v : MediaControl::Verbs())
        {
            if (v != "list" && v != "info" && v != "formats" && v != "targets" && v != "compose")
                post["parameters"][2]["schema"]["enum"].append(v);
        }
        Json::Value& body = post["requestBody"]["content"]["application/json"]["schema"];
        body["type"] = "object";
        body["properties"]["path"]["type"] = "string";
        body["properties"]["path"]["description"] =
            "insert / swap: the source; save / export: the target; files / read: a directory or file on the disk; "
            "write / delete / rename: the file or directory; mkdir: the directory";
        body["properties"]["volume"]["type"] = "string";
        body["properties"]["volume"]["description"] = "files / read / write / delete / rename / mkdir: the volume id from volumes; left out: the first";
        body["properties"]["to"]["type"] = "string";
        body["properties"]["to"]["description"] =
            "rename: the new name; a bare name stays in the file's directory, a path from the root moves it (iS-DOS, FAT)";
        body["properties"]["recursive"]["type"] = "boolean";
        body["properties"]["recursive"]["description"] = "files: the whole tree; delete: a directory with everything in it";
        body["properties"]["parents"]["type"] = "boolean";
        body["properties"]["parents"]["description"] = "mkdir: the missing directories on the way too";
        body["properties"]["out"]["type"] = "string";
        body["properties"]["out"]["description"] = "read: write the file's bytes to this host file";
        body["properties"]["data"]["type"] = "string";
        body["properties"]["data"]["description"] = "write: the file's bytes in base64";
        body["properties"]["in"]["type"] = "string";
        body["properties"]["in"]["description"] = "write: the file's bytes from this host file";
        body["properties"]["start"]["type"] = "string";
        body["properties"]["start"]["description"] = "write: the TR-DOS start / +3DOS CODE / iS-DOS load address: 32768, 0x8000 or #8000";
        body["properties"]["header"]["type"] = "boolean";
        body["properties"]["header"]["description"] = "write: +3DOS: false leaves out the 128-byte +3DOS header";
        body["properties"]["descriptor"]["type"] = "object";
        body["properties"]["descriptor"]["description"] =
            "insert / swap: a composition descriptor inline (the *.ucompose.json body) instead of a path";
        body["properties"]["access"]["type"] = "string";
        body["properties"]["access"]["enum"].append("readonly");
        body["properties"]["access"]["enum"].append("session");
        body["properties"]["access"]["enum"].append("writethrough");
        body["properties"]["fs"]["type"] = "string";
        body["properties"]["fs"]["enum"].append("fat16");
        body["properties"]["fs"]["enum"].append("fat32");
        body["properties"]["codepage"]["type"] = "string";
        body["properties"]["codepage"]["enum"].append("cp866");
        body["properties"]["codepage"]["enum"].append("cp1251");
        body["properties"]["free"]["type"] = "integer";
        body["properties"]["free"]["description"] = "Folder volumes: bytes of room for guest writes (default 256 MiB)";
        body["properties"]["kind"]["type"] = "string";
        body["properties"]["kind"]["description"] = "insert auto: the kind a folder becomes (floppy or block)";
        body["properties"]["format"]["type"] = "string";
        body["properties"]["format"]["description"] =
            "insert / swap: audio-cd - a folder of MP3 / FLAC / WAV files into a CD-ROM drive as a Red Book audio CD (a folder "
            "in a CD slot is one anyway; the report lists the tracks and every file not taken, info.medium.disc the tracks); "
            "create: auto, trdos (formatted: an empty TR-DOS catalogue), plus3 or unformatted (raw) (floppies)";
        body["properties"]["cylinders"]["type"] = "integer";
        body["properties"]["sides"]["type"] = "integer";
        body["properties"]["size"]["type"] = "integer";
        body["properties"]["size"]["description"] = "create on a block slot: bytes, a multiple of 512, up to 128 GiB (memory holds at most [MEDIA] SessionMemoryLimit of the guest's writes)";
        body["properties"]["save"]["type"] = "boolean";
        body["properties"]["export"]["type"] = "string";
        body["properties"]["export"]["description"] = "Disposition: write a dirty medium to this new file first";
        body["properties"]["discard"]["type"] = "boolean";
        body["properties"]["strategy"]["type"] = "string";
        body["properties"]["strategy"]["description"] =
            "save of a composite: delta (the session's writes into <descriptor>.delta, restored at the next insert), "
            "flat (a new image at path; the default with one), commit or write-back; left out: the descriptor's "
            "writes.save. With save on insert / swap / eject / create / rescan also discard (drop the writes) or ask "
            "(a delta here; the GUI asks)";
        body["properties"]["commit"]["type"] = "string";
        body["properties"]["commit"]["description"] =
            "save / flatten with strategy commit, where the descriptor's writes.commit allows it: all (everything or "
            "nothing) or partial (what has a source; the rest stays in the session). writes.commit: none (the default) "
            "refuses commit and write-back";
        body["properties"]["strict"]["type"] = "boolean";
        body["properties"]["strict"]["description"] =
            "save of a composite leaving its slot: a commit / write-back that fails refuses (the medium stays) instead of "
            "keeping the writes as a session delta";
        body["properties"]["plan"]["type"] = "boolean";
        body["properties"]["plan"]["description"] = "save / flatten with strategy commit or write-back: report what would be written, write nothing";
        body["properties"]["onConflict"]["type"] = "string";
        body["properties"]["onConflict"]["description"] = "write-back: refuse (default) or keep-both when a host file changed since the build";
        body["properties"]["force"]["type"] = "boolean";
        body["properties"]["force"]["description"] = "save as delta over a delta written over other sources";
        body["properties"]["vhd"]["type"] = "string";
        body["properties"]["vhd"]["description"] =
            "save / export / flatten of a block medium to a new .vhd: fixed (default, the most compatible) or dynamic "
            "(sparse: only 2 MiB blocks holding data are stored)";
        body["properties"]["compact"]["type"] = "boolean";
        body["properties"]["compact"]["description"] =
            "save / export of a FAT disk or card: a re-synthesized volume (every file contiguous; with fs: fat16 / fat32 "
            "converts, with size resizes)";
        body["properties"]["wp"]["type"] = "boolean";
        body["properties"]["on"]["type"] = "boolean";
        body["properties"]["on"]["description"] = "protect: the slot's write-protect switch";
        body["properties"]["retarget"]["type"] = "boolean";
        body["properties"]["retarget"]["description"] = "save: write <stem>.udi when the format cannot hold the disk (default true)";
        body["properties"]["compression"]["type"] = "string";
        body["properties"]["compression"]["description"] =
            "save / export of a block medium to a .chd: none, default (lzma,zlib,huff,flac), or up to four of zlib, lzma, huff, "
            "flac, zstd (default: the source CHD's codecs, else chdman's)";
        body["properties"]["parent"]["type"] = "string";
        body["properties"]["parent"]["description"] = "export to a .chd: write a child of this parent CHD (only differing hunks are stored)";
        body["properties"]["end_recording"]["type"] = "boolean";
        body["properties"]["async"]["type"] = "boolean";
        body["properties"]["device"]["type"] = "string";
        body["properties"]["device"]["description"] = "insert / swap on an IDE unit: disk, cdrom or cf - a CompactFlash card on an IDE adapter, a disk that identifies as CFA (swap the unit's drive first; the unit must be empty)";
        body["properties"]["journal"]["type"] = "string";
        body["properties"]["journal"]["description"] =
            "insert / swap of a medium written in session access: replay a session journal left by a crash "
            "(<source>.usession) and keep one, discard it unread and start a new one, or off (no journal next to the "
            "medium). Left out: [MEDIA] SessionJournal (off by default)";
        body["properties"]["immediate"]["type"] = "boolean";
        body["properties"]["immediate"]["description"] = "insert: no swap delay";
        Json::Value& upload = post["requestBody"]["content"]["multipart/form-data"]["schema"];
        upload["properties"]["file"]["type"] = "string";
        upload["properties"]["file"]["format"] = "binary";
        // write: the file's bytes as the body (path in the query)
        Json::Value& rawBody = post["requestBody"]["content"]["application/octet-stream"]["schema"];
        rawBody["type"] = "string";
        rawBody["format"] = "binary";
        replyRef(post);
    }
