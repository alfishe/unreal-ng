    // region CD audio (api/cdaudio_api.cpp; CdAudioControl, PLAN #83)
    {
        const std::string drive =
            "Each CD drive (an ATAPI unit of the IDE board: CDn=1 in [HDD], or device=cdrom on insert): unit and slot "
            "(ide0.master ... ide1.slave); the disc (format iso / cue / bin / chd, source, first / last track, lead-out "
            "LBA and MSF, and every track: number, type audio / mode1 / mode2, start LBA and MSF, pregap LBA, end LBA, "
            "length); the audio side (status idle / playing / paused / completed / error with the READ SUB-CHANNEL "
            "status code 0x11-0x15, the head as lba, sample and msf, its track, index (0 in a pregap) and "
            "relative_msf, the play range play_start_lba / play_end_lba, sotc); drive_volume (page 0Eh: left / right "
            "volume 0-255 and the channel each output plays: left, right, mono, mute); the mixer row (row name, volume, "
            "mute, solo, active, peak). The head moves 75 frames per second of emulated time.";

        Json::Value& state = paths["/api/v1/emulator/{id}/state/cdaudio"]["get"];
        state["summary"] = "Get the CD drives' audio state";
        state["tags"].append("Audio State");
        state["description"] = "available: false (with reason) without a CD drive; else drives: [...]. " + drive;
        state["parameters"][0]["name"] = "id";
        state["parameters"][0]["in"] = "path";
        state["parameters"][0]["required"] = true;
        state["parameters"][0]["schema"]["type"] = "string";
        state["responses"]["200"]["description"] = "CD audio state";

        Json::Value& active = paths["/api/v1/emulator/state/cdaudio"]["get"];
        active["summary"] = "Get the CD drives' audio state (active emulator)";
        active["tags"].append("Audio State (Active)");
        active["responses"]["200"]["description"] = "CD audio state";

        const std::string verbs =
            " status: one drive's report (drive given) or all. play: track=N (to=M: through track M, default the last "
            "track), or lba=X frames=N, or msf=MM:SS:FF end=MM:SS:FF - as PLAY AUDIO from the drive's front panel. "
            "pause / resume / stop: as PAUSE / RESUME and STOP PLAY / SCAN (409 not-playing when nothing plays). "
            "volume: left=0-255 right=0-255 route=stereo|swap|mono|left|right|mute sotc=true|false (page 0Eh, as "
            "MODE SELECT sets it). mixer: volume=0.0-4.0 mute=true|false solo=true|false (the SoundManager row). "
            "drive picks the drive: a slot id (ide0.slave), a unit 0-3, or empty for the first CD drive. play, pause, "
            "resume, stop and volume act at an instruction boundary and are refused while TTD records (409 "
            "recording: a replay would not repeat them; the guest's own CD player commands are recorded). Reply: ok, "
            "error (no-cd-drive 404, no-disc / recording / not-playing 409, bad-request 400), message, drive.";
        Json::Value& post = paths["/api/v1/emulator/{id}/cdaudio/{verb}"]["post"];
        post["summary"] = "CD audio control: play / pause / resume / stop / volume / mixer";
        post["description"] = "Options in the JSON body or the query string." + verbs;
        post["tags"].append("Audio State");
        post["parameters"][0]["name"] = "id";
        post["parameters"][0]["in"] = "path";
        post["parameters"][0]["required"] = true;
        post["parameters"][0]["schema"]["type"] = "string";
        post["parameters"][1]["name"] = "verb";
        post["parameters"][1]["in"] = "path";
        post["parameters"][1]["required"] = true;
        post["parameters"][1]["schema"]["type"] = "string";
        for (const std::string& v : CdAudioControl::Verbs())
            post["parameters"][1]["schema"]["enum"].append(v);
        Json::Value& body = post["requestBody"]["content"]["application/json"]["schema"];
        body["type"] = "object";
        body["properties"]["drive"]["type"] = "string";
        body["properties"]["drive"]["description"] = "ide0.slave, a unit 0-3, or empty: the first CD drive";
        body["properties"]["track"]["type"] = "integer";
        body["properties"]["to"]["type"] = "integer";
        body["properties"]["lba"]["type"] = "integer";
        body["properties"]["frames"]["type"] = "integer";
        body["properties"]["msf"]["type"] = "string";
        body["properties"]["end"]["type"] = "string";
        body["properties"]["left"]["type"] = "integer";
        body["properties"]["right"]["type"] = "integer";
        body["properties"]["route"]["type"] = "string";
        for (const char* r : {"stereo", "swap", "mono", "left", "right", "mute"})
            body["properties"]["route"]["enum"].append(r);
        body["properties"]["sotc"]["type"] = "boolean";
        body["properties"]["volume"]["type"] = "number";
        body["properties"]["mute"]["type"] = "boolean";
        body["properties"]["solo"]["type"] = "boolean";
        post["responses"]["200"]["description"] = "Done: the drive's report";
        post["responses"]["400"]["description"] = "bad-request";
        post["responses"]["404"]["description"] = "no-cd-drive";
        post["responses"]["409"]["description"] = "no-disc, recording or not-playing";

        Json::Value& postActive = paths["/api/v1/emulator/cdaudio/{verb}"]["post"];
        postActive["summary"] = "CD audio control (active emulator)";
        postActive["description"] = post["description"];
        postActive["tags"].append("Audio State (Active)");
        postActive["parameters"][0] = post["parameters"][1];
        postActive["requestBody"] = post["requestBody"];
        postActive["responses"] = post["responses"];
    }
    // endregion CD audio
