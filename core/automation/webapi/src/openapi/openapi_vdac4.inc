    // TS-Conf VDAC4 board (Tang Nano 20K FT812-compatible core + ESP32-S3 running apps): read-only state
    // Design: docs/inprogress/2026-11-15-tsconf-vdac4/design.md §10.2 (every surface reads Vdac4Control::GetState)
    {
        const std::string statePath = "/api/v1/emulator/{id}/vdac4/state";
        Json::Value& get = paths[statePath]["get"];
        get["summary"] = "VDAC4 board state (Tang, ESP, app, link, audio, log)";
        get["tags"].append("VDAC4");
        get["description"] =
            "The TS-Conf VDAC4 board as the automation planes see it (CLI `vdac4 state`, Lua / Python "
            "`vdac4_state()`, MCP `vdac4_state`, WebSocket a call of this route). The Tang, ESP and app blocks are the "
            "board's copy from the last frame end; the log, packets and audio are read under their locks. Never "
            "parks the emulation thread, so it is safe to poll.";
        get["parameters"][0]["name"] = "id";
        get["parameters"][0]["in"] = "path";
        get["parameters"][0]["required"] = true;
        get["parameters"][0]["schema"]["type"] = "string";
        get["parameters"][1]["name"] = "log";
        get["parameters"][1]["in"] = "query";
        get["parameters"][1]["schema"]["type"] = "integer";
        get["parameters"][1]["schema"]["minimum"] = 0;
        get["parameters"][1]["schema"]["maximum"] = 256;
        get["parameters"][1]["description"] = "How many recent ESP log lines to include (default 50, at most 256, 0 = none)";

        get["responses"]["200"]["description"] = "The board's state";
        Json::Value& schema = get["responses"]["200"]["content"]["application/json"]["schema"];
        schema["type"] = "object";
        auto field = [](Json::Value& object, const char* name, const char* type, const char* description) {
            object["properties"][name]["type"] = type;
            object["properties"][name]["description"] = description;
        };
        auto block = [&schema](const char* name, const char* description) -> Json::Value& {
            Json::Value& object = schema["properties"][name];
            object["type"] = "object";
            object["description"] = description;
            return object;
        };

        Json::Value& tang = block("tang", "The Tang FPGA (FT812-compatible core)");
        field(tang, "personality", "string", "ts (TS-Conf video), loading (switching), ft (the FT812 core drives the picture)");
        field(tang, "armed", "boolean", "The ESP armed the Tang: the TS personality switches on Evo FT traffic");
        field(tang, "esp_host", "boolean", "FT_CFG ESP_HOST: the ESP drives the FT812");
        field(tang, "ram_g_3mb", "boolean", "FT_CFG RAM_G 3 MB mode");
        field(tang, "ft_cfg", "integer", "The FT_CFG register as written");

        Json::Value& esp = block("esp", "The ESP32-S3 and its firmware");
        field(esp, "phase", "string", "off, booting, running, flushing, restart_wait");
        field(esp, "ms", "integer", "ESP time since power-on (millis())");
        field(esp, "boots", "integer", "Boots since power-on");
        field(esp, "boot_nonce", "integer", "The nonce of the current boot");
        field(esp, "last_step", "integer", "The last command the firmware handled (SYS_INFO step)");
        field(esp, "last_error", "string", "The firmware's last error text (empty: none)");
        field(esp, "resources", "integer", "Resources held in PSRAM");
        field(esp, "clock_restarts", "integer", "Machine resets the ESP's clock stepped over (the ESP keeps running)");
        field(esp, "clock_offset_us", "integer", "How far the ESP's time is ahead of the machine's emulated time, microseconds");

        Json::Value& app = block("app", "The app the ESP runs (for example DOOM)");
        field(app, "running", "boolean", "An app runs");
        field(app, "module", "string", "The module file name");
        field(app, "name", "string", "The app's name (zm_app_t)");
        field(app, "version", "string", "The app's version (zm_app_t)");
        field(app, "command_first", "integer", "First command of the app's range (first > last: none)");
        field(app, "command_last", "integer", "Last command of the app's range");
        field(app, "error", "string", "The runtime's last error: a fault, what and where (empty: none)");
        Json::Value& sections = app["properties"]["sections"];
        sections["type"] = "array";
        sections["description"] = "The running app's loaded sections (empty: no app): what `vdac4/esp/memory?app=1` maps";
        sections["items"]["type"] = "object";
        field(sections["items"], "elf_address", "integer", "The section's address in the app's ELF file");
        field(sections["items"], "size", "integer", "Bytes");
        field(sections["items"], "loaded_at", "integer", "The ESP D-bus address it was loaded at");

        Json::Value& link = block("link", "The Z80 <-> ESP link and its frame transport");
        field(link, "pending_from_z80", "integer", "Bytes received from the Z80, not yet parsed");
        field(link, "pending_to_z80", "integer", "Bytes queued for the Z80");
        field(link, "send_blocked", "boolean", "The firmware waits inside send() for ring room");
        field(link, "bad_checksums", "integer", "Frames dropped for a bad checksum");
        field(link, "resyncs", "integer", "Times the frame parser resynchronised");
        field(link, "dropped_frames", "integer", "Frames dropped");

        Json::Value& packets = block("packets", "Packets the Z80 sent");
        field(packets, "count", "integer", "Packets since power-on");
        Json::Value& recent = packets["properties"]["recent"];
        recent["type"] = "array";
        recent["description"] = "The last packets (up to 64), oldest first";
        recent["items"]["type"] = "object";
        field(recent["items"], "command", "integer", "The command byte");
        field(recent["items"], "length", "integer", "Payload length, bytes");
        field(recent["items"], "ms", "integer", "ESP time it was handled");

        Json::Value& audio = block("audio", "The ESP's I2S player (mixer row \"VDAC4 ESP\")");
        field(audio, "input_rate", "integer", "The app's PCM rate, Hz (0: never opened)");
        field(audio, "output_rate", "integer", "The rate fed to the mixer, Hz");
        field(audio, "queued", "integer", "Output frames ahead of the mixer");
        field(audio, "underruns", "integer", "Times the mixer found the queue empty");
        field(audio, "dropped", "integer", "Frames dropped on overflow");
        field(audio, "sound", "boolean", "The last frame was not silent");

        Json::Value& threads = block("threads", "The ESP app's jobs and the Tang / FT812 work they hand over (host time)");
        field(threads, "esp_thread", "boolean", "Jobs on the runtime's worker thread ([VDAC4] EspThread)");
        field(threads, "ft_thread", "boolean", "The Tang's work for the jobs on its own thread ([VDAC4] FtThread)");
        field(threads, "esp_latency_ms", "integer", "ms the app's outputs reach the firmware after its job ([VDAC4] EspLatencyMs)");
        field(threads, "esp_jobs", "integer", "Advances run as jobs since power-on");
        field(threads, "esp_threaded_jobs", "integer", "Of them on the worker thread");
        field(threads, "esp_waits", "integer", "Times the emulation thread waited for a job");
        field(threads, "esp_wait_ms", "number", "Host time the emulation thread waited for jobs, ms");
        field(threads, "esp_job_ms", "number", "Host time the worker ran jobs, ms");
        field(threads, "ft_ops", "integer", "LINK writes and job ends the FT thread applied");
        field(threads, "ft_reads", "integer", "LINK reads of the jobs (each waits for the FT thread)");
        field(threads, "ft_apply_ms", "number", "Host time the FT thread applied ops, ms");
        field(threads, "ft_producer_wait_ms", "number", "Host time the jobs waited for the FT thread, ms");
        field(threads, "ft_owner_waits", "integer", "Times the emulation thread waited for the FT thread");
        field(threads, "ft_owner_wait_ms", "number", "Host time the emulation thread waited for the FT thread, ms");

        Json::Value& logLines = schema["properties"]["log"];
        logLines["type"] = "array";
        logLines["items"]["type"] = "string";
        logLines["description"] = "The ESP's recent log lines, oldest first (`log` query parameter)";

        get["responses"]["404"]["description"] = "No emulator with this id";
        get["responses"]["409"]["description"] =
            "No VDAC4 board (not TS-Conf, [MISC] TS_VDAC4=0, or a build without VDAC2 / VDAC4 support); the "
            "message says which";
    }

    // ESP memory (read-only): the app's data for haptic probes `cpu: esp` and for study
    {
        const std::string memoryPath = "/api/v1/emulator/{id}/vdac4/esp/memory";
        Json::Value& get = paths[memoryPath]["get"];
        get["summary"] = "Read the VDAC4 ESP's memory";
        get["tags"].append("VDAC4");
        get["description"] =
            "Bytes of the ESP32-S3's memory (CLI `vdac4 esp-memory`, Lua / Python `vdac4_esp_memory()`, MCP "
            "`vdac4_esp_memory`, WebSocket a call of this route). With `app=1` the address is one of the running app's "
            "ELF image (the address a symbol has in the file, as a haptic probe `cpu: esp` takes it), mapped through "
            "the sections of the last frame end; else a D-bus / I-bus / SRAM address. No pause: the bytes of the moment.";
        Json::Value& params = get["parameters"];
        params[0]["name"] = "id";
        params[0]["in"] = "path";
        params[0]["required"] = true;
        params[0]["schema"]["type"] = "string";
        params[1]["name"] = "address";
        params[1]["in"] = "query";
        params[1]["required"] = true;
        params[1]["schema"]["type"] = "string";
        params[1]["description"] = "32-bit address, decimal or 0x hex";
        params[2]["name"] = "size";
        params[2]["in"] = "query";
        params[2]["schema"]["type"] = "integer";
        params[2]["schema"]["minimum"] = 1;
        params[2]["schema"]["maximum"] = 4096;
        params[2]["description"] = "Bytes (default 16, at most 4096)";
        params[3]["name"] = "app";
        params[3]["in"] = "query";
        params[3]["schema"]["type"] = "boolean";
        params[3]["description"] = "The address is one of the running app's ELF image";
        get["responses"]["200"]["description"] = "{address, size, app, hex}: the bytes as lowercase hex";
        get["responses"]["400"]["description"] = "A bad address or size, no app running (app=1), or unmapped memory";
        get["responses"]["404"]["description"] = "No emulator with this id";
        get["responses"]["409"]["description"] = "No VDAC4 board";
    }
