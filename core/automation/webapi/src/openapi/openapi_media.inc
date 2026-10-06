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
            operation["responses"]["409"]["description"] = "dirty (say save, export or discard), recording (TTD records; end_recording), in-use";
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
        post["summary"] = "insert / swap / eject / save / export / discard / rescan / create / protect / layers / changes / flatten";
        post["description"] =
            "Operations are synchronous: the reply comes when the medium is in or out of the slot (swap delay "
            "included, at most 5 s, then pending = true). async: true replies at once with pending = true. "
            "A dirty medium leaves its slot only with a disposition: save (into its own file), export: <path> "
            "(into a new file) or discard. insert / swap take 'path' (a file or a folder) or the file itself "
            "(multipart/form-data 'file', or a raw body with X-Filename; up to 1 MB, deleted when ejected). "
            "save / export take 'path' as the target. Options go in the JSON body or the query string:" + verbs;
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
        body["properties"]["path"]["description"] = "insert / swap: the source; save / export: the target";
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
            "create: auto, unformatted or plus3 (floppies)";
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
            "save of a composite: delta (the session's writes into <descriptor>.delta, restored at the next insert; "
            "the default without a path) or flat (a new image at path; the default with one)";
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
        body["properties"]["immediate"]["type"] = "boolean";
        body["properties"]["immediate"]["description"] = "insert: no swap delay";
        Json::Value& upload = post["requestBody"]["content"]["multipart/form-data"]["schema"];
        upload["properties"]["file"]["type"] = "string";
        upload["properties"]["file"]["format"] = "binary";
        replyRef(post);
    }
