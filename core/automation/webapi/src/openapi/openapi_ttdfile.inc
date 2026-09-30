    // GET /api/v1/ttd/file-info - a .ttd file described without loading it (api/ttd_api.cpp)
    {
        const std::string fileInfo = "/api/v1/ttd/file-info";
        paths[fileInfo]["get"]["summary"] = "Describe a .ttd file without loading it";
        paths[fileInfo]["get"]["tags"].append("Time-Travel Debug");
        paths[fileInfo]["get"]["description"] =
            "Reads a .ttd session file's headers only - no emulator instance is involved and nothing is "
            "decompressed - and returns its frame range, sections and the machine it was recorded on: model, "
            "ROM signature, fitted devices and the General Sound / TurboSound slot devices. A session loads only "
            "into a matching machine (ttd/load refuses another model, ROM set or slot card), so a client "
            "provisions the instance from this answer first: create the recorded model, fit the recorded "
            "General Sound card (POST control/audio/gs, action switch_personality), then ttd/load. Files written before the header's "
            "device mask are walked to their first checkpoint for the device set (peripherals_from_header false).";
        paths[fileInfo]["get"]["parameters"][0]["name"] = "path";
        paths[fileInfo]["get"]["parameters"][0]["in"] = "query";
        paths[fileInfo]["get"]["parameters"][0]["required"] = true;
        paths[fileInfo]["get"]["parameters"][0]["description"] = "Path of the .ttd file (UTF-8, as the server sees it)";
        paths[fileInfo]["get"]["parameters"][0]["schema"]["type"] = "string";
        paths[fileInfo]["get"]["responses"]["200"]["description"] = "The file's info";
        paths[fileInfo]["get"]["responses"]["200"]["content"]["application/json"]["schema"]["$ref"] =
            "#/components/schemas/TTDFileInfoResponse";
        paths[fileInfo]["get"]["responses"]["400"]["description"] =
            "No path given, or not a readable .ttd file (ok false, error says why)";
        paths[fileInfo]["get"]["responses"]["404"]["description"] = "The file cannot be opened (ok false, error)";
    }
