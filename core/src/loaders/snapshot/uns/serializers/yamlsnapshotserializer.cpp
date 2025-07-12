#include "yamlsnapshotserializer.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include "common/stringhelper.h"

// @brief Load snapshot from YAML file and populate DTO
bool YamlSnapshotSerializer::load(const std::string& filePath, SnapshotDTO& out)
{
    _lastError.clear();
    try
    {
        // Read file
        std::ifstream in(filePath);
        if (!in)
        {
            _lastError = "Failed to open YAML file: " + filePath;
            return false;
        }

        // Read YAML file content
        std::string yamlContent((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        // Convert to immutable view and parse
        ryml::csubstr content = ryml::to_csubstr(yamlContent);
        ryml::Tree tree = ryml::parse_in_arena(content);
        ryml::NodeRef root = tree.rootref();

        // Debug dump of the parsed tree
#ifdef _DEBUG
        std::cout << "Parsed YAML tree:\n" << tree << std::endl;
#endif

        // --- Metadata ---
        if (!root.has_child("metadata"))
        {
            _lastError = "YAML missing required 'metadata' section";
            return false;
        }
        loadMetadata(root["metadata"], out.metadata);

        // --- Machine ---
        loadMachine(root["machine"], out.machine);

        // --- Memory ---
        loadMemory(root["memory"], out.memory);

        // --- Z80 ---
        loadZ80(root["z80"], out.z80);

        // --- Peripherals ---
        loadPeripherals(root["peripherals"], out.peripherals);

        // --- Media ---
        loadMedia(root["media"], out.media);

        // --- Debug ---
        loadDebug(root["debug"], out.debug);

        // --- Emulator Config ---
        loadEmulatorConfig(root["emulator_config"], out.emulator_config);
        return true;
    }
    catch (const std::exception& ex)
    {
        _lastError = std::string("YAML load error: ") + ex.what();
        return false;
    }
}

// @brief Save snapshot DTO to YAML file
bool YamlSnapshotSerializer::save(const std::string& filePath, const SnapshotDTO& in)
{
    _lastError.clear();
    try
    {
        ryml::Tree tree;
        auto root = tree.rootref();

        // --- Metadata ---
        auto meta = root.append_child();
        meta.set_key("metadata");
        meta |= ryml::MAP;
        if (!saveMetadata(meta, in.metadata))
        {
            return false;
        }

        // --- Machine ---
        auto mach = root.append_child();
        mach.set_key("machine");
        mach |= ryml::MAP;
        if (!saveMachine(mach, in.machine))
        {
            return false;
        }

        // --- Memory ---
        auto mem = root.append_child();
        mem.set_key("memory");
        mem |= ryml::MAP;
        if (!saveMemory(mem, in.memory))
        {
            return false;
        }

        // --- Z80 ---
        auto z = root.append_child();
        z.set_key("z80");
        z |= ryml::MAP;
        if (!saveZ80(z, in.z80))
        {
            return false;
        }

        // --- Peripherals ---
        auto periph = root.append_child();
        periph.set_key("peripherals");
        periph |= ryml::MAP;
        if (!savePeripherals(periph, in.peripherals))
        {
            return false;
        }

        // --- Media ---
        auto media = root.append_child();
        media.set_key("media");
        media |= ryml::MAP;
        if (!saveMedia(media, in.media))
        {
            return false;
        }

        // --- Debug ---
        auto debug = root.append_child();
        debug.set_key("debug");
        debug |= ryml::MAP;
        if (!saveDebug(debug, in.debug))
        {
            return false;
        }

        // --- Emulator Config ---
        auto ec = root.append_child();
        ec.set_key("emulator_config");
        ec |= ryml::MAP;
        if (!saveEmulatorConfig(ec, in.emulator_config))
        {
            return false;
        }

        // --- Save to file ---
        std::ofstream outFile(filePath);
        if (!outFile)
        {
            _lastError = "Failed to open YAML file for writing: " + filePath;
            return false;
        }

        outFile << tree;
        return true;
    }
    catch (const std::exception& ex)
    {
        _lastError = std::string("YAML save error: ") + ex.what();
        return false;
    }
}

// --- Sectional load helpers ---
bool YamlSnapshotSerializer::loadMetadata(const ryml::ConstNodeRef& meta, MetadataDTO& dto)
{
    safeGet(meta["manifest_version"], dto.manifest_version, std::string{});
    safeGet(meta["emulator_id"], dto.emulator_id, std::string{});
    safeGet(meta["emulator_version"], dto.emulator_version, std::string{});
    safeGet(meta["host_platform"], dto.host_platform, std::string{});
    safeGet(meta["emulated_platform"], dto.emulated_platform, std::string{});
    safeGet(meta["save_time"], dto.save_time, std::string{});
    safeGet(meta["description"], dto.description, std::string{});
    return true;
}

bool YamlSnapshotSerializer::loadMachine(const ryml::ConstNodeRef& mach, MachineDTO& dto)
{
    if (!mach.readable())
        return false;
    safeGet(mach["model"], dto.model, std::string{});
    safeGet(mach["ram_size_kb"], dto.ram_size_kb, 0u);
    if (const auto t = mach["timing"]; t.readable())
    {
        safeGet(t["preset"], dto.timing.preset, std::string{});
        safeGet(t["frame"], dto.timing.frame, 0u);
        safeGet(t["line"], dto.timing.line, 0u);
        safeGet(t["int"], dto.timing.int_period, 0u);
        safeGet(t["intstart"], dto.timing.intstart, 0u);
        safeGet(t["intlen"], dto.timing.intlen, 0u);
        safeGet(t["total_t_states"], dto.timing.total_t_states, 0ull);
    }
    if (const auto c = mach["config"]; c.readable())
    {
        safeGet(c["even_m1"], dto.config.even_m1, 0);
        safeGet(c["border_4t"], dto.config.border_4t, 0);
        safeGet(c["floatbus"], dto.config.floatbus, 0);
        safeGet(c["floatdos"], dto.config.floatdos, 0);
        safeGet(c["portff"], dto.config.portff, 0);
    }
    if (const auto u = mach["ula"]; u.readable())
    {
        safeGet(u["border_color"], dto.ula.border_color, 0);
        safeGet(u["frame_counter"], dto.ula.frame_counter, 0u);
        safeGet(u["flash_state"], dto.ula.flash_state, false);
        safeGet(u["screen_mode"], dto.ula.screen_mode, std::string{});
        safeGet(u["frame_t_states"], dto.ula.frame_t_states, 0u);
    }
    return true;
}

bool YamlSnapshotSerializer::loadMemory(const ryml::ConstNodeRef& mem, MemoryDTO& dto)
{
    if (!mem.readable())
        return false;

    if (const auto pages = mem["pages"]; pages.readable() && pages.is_map())
    {
        for (const auto page : pages)
        {
            // Only insert if this is a map and has a 'file' child
            if (!page.is_map() || !page.has_child("file"))
                continue;

            std::string key = page.key().str;
            MemoryPageDTO pdto;
            safeGet(page["file"], pdto.file, std::string{});
            if (const auto cs = page["checksum"]; cs.readable())
            {
                safeGet(cs["algorithm"], pdto.checksum.algorithm, std::string{});
                safeGet(cs["value"], pdto.checksum.value, std::string{});
            }
            dto.pages[key] = pdto;
        }
    }

    if (const auto mmap = mem["memory_map"]; mmap.readable())
    {
        for (const auto region : mmap)
        {
            std::string addr = region.key().str;
            auto& mdto = dto.memory_map[addr];
            safeGet(region["type"], mdto.type, std::string{});
            safeGet(region["bank"], mdto.bank, 0);
        }
    }

    if (const auto ports = mem["ports"]; ports.readable())
    {
        for (const auto port : ports)
        {
            std::string portnum = port.key().str;
            safeGet(port, dto.ports[portnum], uint8_t(0));
        }
    }
    return true;
}

bool YamlSnapshotSerializer::loadZ80(const ryml::ConstNodeRef& z, Z80DTO& dto)
{
    if (!z.readable())
        return false;
    if (const auto r = z["registers"]; r.readable())
    {
        auto& reg = dto.registers;
        safeGet(r["af"], reg.af, uint16_t(0));
        safeGet(r["bc"], reg.bc, uint16_t(0));
        safeGet(r["de"], reg.de, uint16_t(0));
        safeGet(r["hl"], reg.hl, uint16_t(0));
        safeGet(r["af_"], reg.af_, uint16_t(0));
        safeGet(r["bc_"], reg.bc_, uint16_t(0));
        safeGet(r["de_"], reg.de_, uint16_t(0));
        safeGet(r["hl_"], reg.hl_, uint16_t(0));
        safeGet(r["ix"], reg.ix, uint16_t(0));
        safeGet(r["iy"], reg.iy, uint16_t(0));
        safeGet(r["pc"], reg.pc, uint16_t(0));
        safeGet(r["sp"], reg.sp, uint16_t(0));
        safeGet(r["i"], reg.i, uint8_t(0));
        safeGet(r["r"], reg.r, uint8_t(0));
    }
    if (const auto i = z["interrupts"]; i.readable())
    {
        auto& intr = dto.interrupts;
        safeGet(i["im"], intr.im, 0);
        safeGet(i["iff1"], intr.iff1, false);
        safeGet(i["iff2"], intr.iff2, false);
        safeGet(i["halted"], intr.halted, false);
    }
    return true;
}

bool YamlSnapshotSerializer::loadPeripherals(const ryml::ConstNodeRef& peripherals, PeripheralsDTO& dto)
{
    if (!peripherals.readable())
        return false;
    if (const auto psg = peripherals["psg0"]; psg.readable())
    {
        loadPsg(psg, dto.psg0);
    }
    if (const auto psg = peripherals["psg1"]; psg.readable())
    {
        loadPsg(psg, dto.psg1);
    }
    if (const auto wd = peripherals["wd1793"]; wd.readable())
    {
        loadWd1793(wd, dto.wd1793);
    }
    return true;
}

bool YamlSnapshotSerializer::loadMedia(const ryml::ConstNodeRef& media, MediaDTO& dto)
{
    if (!media.readable())
        return false;
    if (const auto drives = media["floppy_drives"]; drives.readable())
    {
        for (const auto drive : drives)
        {
            FloppyDriveDTO d;
            safeGet(drive["drive_id"], d.drive_id, std::string{});
            safeGet(drive["file"], d.file, std::string{});
            safeGet(drive["type"], d.type, std::string{});
            if (const auto cs = drive["checksum"]; cs.readable())
            {
                safeGet(cs["algorithm"], d.checksum.algorithm, std::string{});
                safeGet(cs["value"], d.checksum.value, std::string{});
            }
            safeGet(drive["write_protected"], d.write_protected, false);
            safeGet(drive["motor_on"], d.motor_on, false);
            safeGet(drive["track_head_position"], d.track_head_position, 0);
            dto.floppy_drives.push_back(d);
        }
    }
    if (const auto tape = media["tape_player"]; tape.readable())
    {
        TapePlayerDTO t;
        safeGet(tape["file"], t.file, std::string{});
        if (const auto cs = tape["checksum"]; cs.readable())
        {
            safeGet(cs["algorithm"], t.checksum.algorithm, std::string{});
            safeGet(cs["value"], t.checksum.value, std::string{});
        }
        safeGet(tape["position"], t.position, 0);
        safeGet(tape["playing"], t.playing, false);
        safeGet(tape["auto_start"], t.auto_start, false);
        safeGet(tape["traps_enabled"], t.traps_enabled, false);
        dto.tape_player = t;
    }
    if (const auto hds = media["hard_disks"]; hds.readable())
    {
        for (const auto hd : hds)
        {
            HardDiskDTO d;
            safeGet(hd["drive_id"], d.drive_id, 0);
            safeGet(hd["file"], d.file, std::string{});
            safeGet(hd["type"], d.type, std::string{});
            if (const auto cs = hd["checksum"]; cs.readable())
            {
                safeGet(cs["algorithm"], d.checksum.algorithm, std::string{});
                safeGet(cs["value"], d.checksum.value, std::string{});
            }
            safeGet(hd["size_mb"], d.size_mb, 0);
            safeGet(hd["read_only"], d.read_only, false);
            dto.hard_disks.push_back(d);
        }
    }
    return true;
}

bool YamlSnapshotSerializer::loadDebug(const ryml::ConstNodeRef& d, DebugDTO& dto)
{
    if (!d.readable())
        return false;

    if (d.has_child("label_files"))
    {
        for (auto lf : d["label_files"])
            dto.label_files.push_back(std::string(lf.val().data(), lf.val().size()));
    }

    if (d.has_child("embedded_labels"))
    {
        for (auto el : d["embedded_labels"])
        {
            DebugLabelDTO l;
            safeGet(el["type"], l.type, std::string{});
            safeGet(el["value"], l.value, 0u);
            safeGet(el["name"], l.name, std::string{});
            safeGet(el["mem_type"], l.mem_type, std::string{});
            safeGet(el["page"], l.page, 0);
            safeGet(el["offset"], l.offset, 0);
            dto.embedded_labels.push_back(l);
        }
    }

    if (d.has_child("breakpoint_files"))
    {
        for (auto bf : d["breakpoint_files"])
            dto.breakpoint_files.push_back(std::string(bf.val().data(), bf.val().size()));
    }

    if (d.has_child("breakpoints"))
    {
        for (auto bp : d["breakpoints"])
        {
            BreakpointDTO b;
            safeGet(bp["type"], b.type, std::string{});
            safeGet(bp["value"], b.value, 0u);
            safeGet(bp["mem_type"], b.mem_type, std::string{});
            safeGet(bp["page"], b.page, 0);
            safeGet(bp["offset"], b.offset, 0);
            safeGet(bp["enabled"], b.enabled, false);
            safeGet(bp["condition"], b.condition, std::string{});
            dto.breakpoints.push_back(b);
        }
    }

    if (d.has_child("watchpoints"))
    {
        for (auto wp : d["watchpoints"])
        {
            WatchpointDTO w;
            safeGet(wp["type"], w.type, std::string{});
            safeGet(wp["value"], w.value, 0u);
            safeGet(wp["mem_type"], w.mem_type, std::string{});
            safeGet(wp["page"], w.page, 0);
            safeGet(wp["offset"], w.offset, 0);
            safeGet(wp["size"], w.size, 0);
            safeGet(wp["read"], w.read, false);
            safeGet(wp["write"], w.write, false);
            dto.watchpoints.push_back(w);
        }
    }
    
    if (d.has_child("call_stack"))
    {
        for (auto cs : d["call_stack"])
            safeGet(cs, dto.call_stack.emplace_back(), 0u);
    }
    return true;
}

bool YamlSnapshotSerializer::loadEmulatorConfig(const ryml::ConstNodeRef& node, EmulatorConfigDTO& dto)
{
    if (!node.readable())
        return false;
    if (const auto f = node["features"]; f.readable())
    {
        safeGet(f["turbo_mode"], dto.features.turbo_mode, false);
        safeGet(f["magic_button_enabled"], dto.features.magic_button_enabled, false);
    }
    if (const auto d = node["debug_features"]; d.readable())
    {
        safeGet(d["debug_mode"], dto.debug_features.debug_mode, false);
        safeGet(d["memory_counters"], dto.debug_features.memory_counters, false);
        safeGet(d["call_trace"], dto.debug_features.call_trace, false);
    }
    if (const auto v = node["video"]; v.readable())
    {
        safeGet(v["filter"], dto.video.filter, std::string{});
        safeGet(v["scanlines"], dto.video.scanlines, 0);
        safeGet(v["border_size"], dto.video.border_size, 0);
    }
    if (const auto s = node["sound"]; s.readable())
    {
        safeGet(s["enabled"], dto.sound.enabled, false);
        safeGet(s["sample_rate"], dto.sound.sample_rate, 0);
        safeGet(s["buffer_size"], dto.sound.buffer_size, 0);
    }
    if (const auto i = node["input"]; i.readable())
    {
        safeGet(i["mouse_type"], dto.input.mouse_type, std::string{});
        safeGet(i["joystick_type"], dto.input.joystick_type, std::string{});
        safeGet(i["keyboard_layout"], dto.input.keyboard_layout, std::string{});
    }
    return true;
}

// --- Sectional save helpers ---
bool YamlSnapshotSerializer::saveMetadata(ryml::NodeRef& meta, const MetadataDTO& dto)
{
    meta.append_child() << ryml::key("manifest_version") << dto.manifest_version;
    meta.append_child() << ryml::key("emulator_id") << dto.emulator_id;
    meta.append_child() << ryml::key("emulator_version") << dto.emulator_version;
    meta.append_child() << ryml::key("host_platform") << dto.host_platform;
    meta.append_child() << ryml::key("emulated_platform") << dto.emulated_platform;
    meta.append_child() << ryml::key("save_time") << dto.save_time;
    meta.append_child() << ryml::key("description") << dto.description;
    return true;
}

bool YamlSnapshotSerializer::saveMachine(ryml::NodeRef& mach, const MachineDTO& dto)
{
    mach["model"] << dto.model;
    mach["ram_size_kb"] << dto.ram_size_kb;

    // Timing
    auto timing = mach["timing"];
    timing |= ryml::MAP;
    timing["preset"] << dto.timing.preset;
    timing["frame"] << dto.timing.frame;
    timing["line"] << dto.timing.line;
    timing["int"] << dto.timing.int_period;
    timing["intstart"] << dto.timing.intstart;
    timing["intlen"] << dto.timing.intlen;
    timing["total_t_states"] << dto.timing.total_t_states;

    // Config
    auto config = mach["config"];
    config |= ryml::MAP;
    config["even_m1"] << dto.config.even_m1;
    config["border_4t"] << dto.config.border_4t;
    config["floatbus"] << dto.config.floatbus;
    config["floatdos"] << dto.config.floatdos;
    config["portff"] << dto.config.portff;

    // ULA
    auto ula = mach["ula"];
    ula |= ryml::MAP;
    ula["border_color"] << dto.ula.border_color;
    ula["frame_counter"] << dto.ula.frame_counter;
    ula["flash_state"] << (dto.ula.flash_state ? "true" : "false");
    ula["screen_mode"] << dto.ula.screen_mode;
    ula["frame_t_states"] << dto.ula.frame_t_states;
    return true;
}

bool YamlSnapshotSerializer::saveMemory(ryml::NodeRef& mem, const MemoryDTO& dto)
{
    auto pages = mem["pages"];
    pages |= ryml::MAP;
    for (const auto& [key, val] : dto.pages)
    {
        auto page = pages[key.c_str()];
        page |= ryml::MAP;
        page["file"] << val.file;
        auto cs = page["checksum"];
        cs |= ryml::MAP;
        cs["algorithm"] << val.checksum.algorithm;
        cs["value"] << val.checksum.value;
    }

    auto mmap = mem["memory_map"];
    mmap |= ryml::MAP;
    for (const auto& [addr, val] : dto.memory_map)
    {
        auto region = mmap[addr.c_str()];
        region |= ryml::MAP;
        region["type"] << val.type;
        region["bank"] << val.bank;
    }

    auto ports = mem["ports"];
    ports |= ryml::MAP;
    for (const auto& [p, v] : dto.ports)
    {
        ports[p.c_str()] << v;
    }
    return true;
}

bool YamlSnapshotSerializer::saveZ80(ryml::NodeRef& z, const Z80DTO& dto)
{
    auto r = z["registers"];
    r |= ryml::MAP;
    r["af"] << StringHelper::Format("0x%04X", dto.registers.af);
    r["bc"] << StringHelper::Format("0x%04X", dto.registers.bc);
    r["de"] << StringHelper::Format("0x%04X", dto.registers.de);
    r["hl"] << StringHelper::Format("0x%04X", dto.registers.hl);
    r["af_"] << StringHelper::Format("0x%04X", dto.registers.af_);
    r["bc_"] << StringHelper::Format("0x%04X", dto.registers.bc_);
    r["de_"] << StringHelper::Format("0x%04X", dto.registers.de_);
    r["hl_"] << StringHelper::Format("0x%04X", dto.registers.hl_);
    r["ix"] << StringHelper::Format("0x%04X", dto.registers.ix);
    r["iy"] << StringHelper::Format("0x%04X", dto.registers.iy);
    r["pc"] << StringHelper::Format("0x%04X", dto.registers.pc);
    r["sp"] << StringHelper::Format("0x%04X", dto.registers.sp);
    r["i"] << StringHelper::Format("0x%02X", dto.registers.i);
    r["r"] << StringHelper::Format("0x%02X", dto.registers.r);

    // Interrupts
    auto ints = z["interrupts"];
    ints |= ryml::MAP;
    ints["im"] << dto.interrupts.im;
    ints["iff1"] << (dto.interrupts.iff1 ? "true" : "false");
    ints["iff2"] << (dto.interrupts.iff2 ? "true" : "false");
    ints["halted"] << (dto.interrupts.halted ? "true" : "false");
    return true;
}

bool YamlSnapshotSerializer::savePeripherals(ryml::NodeRef& periph, const PeripheralsDTO& dto)
{
    auto write_psg = [](ryml::NodeRef& parent, const char* key, const PSGDTO& dto) {
        auto psg = parent[key];
        psg |= ryml::MAP;
        psg["chip_type"] << dto.chip_type;

        auto regs = psg["registers"];
        regs |= ryml::MAP;
        for (const auto& [rk, rv] : dto.registers)
        {
            regs[rk.c_str()] << rv;
        }
        psg["selected_register"] << dto.selected_register;

        auto env = psg["envelope"];
        env |= ryml::MAP;
        env["phase"] << dto.envelope.phase;
        env["step_counter"] << dto.envelope.step_counter;
        env["current_volume"] << dto.envelope.current_volume;
        env["direction"] << dto.envelope.direction;

        auto tone = psg["tone"];
        tone |= ryml::MAP;
        tone["counter_a"] << dto.tone.counter_a;
        tone["counter_b"] << dto.tone.counter_b;
        tone["counter_c"] << dto.tone.counter_c;
        tone["output_a"] << dto.tone.output_a;
        tone["output_b"] << dto.tone.output_b;
        tone["output_c"] << dto.tone.output_c;

        auto noise = psg["noise"];
        noise |= ryml::MAP;
        noise["shift_register"] << dto.noise.shift_register;
        noise["counter"] << dto.noise.counter;
    };

    write_psg(periph, "psg0", dto.psg0);
    write_psg(periph, "psg1", dto.psg1);

    // WD1793
    auto wd = periph["wd1793"];
    wd |= ryml::MAP;
    wd["command"] << dto.wd1793.command;
    wd["track"] << dto.wd1793.track;
    wd["sector"] << dto.wd1793.sector;
    wd["data"] << dto.wd1793.data;
    wd["status"] << dto.wd1793.status;
    wd["motor_on"] << dto.wd1793.motor_on;
    wd["head_loaded"] << dto.wd1793.head_loaded;
    wd["write_protect"] << dto.wd1793.write_protect;
    wd["motor_timeout"] << dto.wd1793.motor_timeout;
    wd["index_counter"] << dto.wd1793.index_counter;
    auto sm = wd["state_machine"];
    sm |= ryml::MAP;
    sm["phase"] << dto.wd1793.state_machine.phase;
    sm["timer"] << dto.wd1793.state_machine.timer;
    sm["busy_counter"] << dto.wd1793.state_machine.busy_counter;
    return true;
}

bool YamlSnapshotSerializer::saveMedia(ryml::NodeRef& media, const MediaDTO& dto)
{
    auto drives = media["floppy_drives"];
    drives |= ryml::SEQ;
    for (const auto& d : dto.floppy_drives)
    {
        auto drive = drives.append_child();
        drive |= ryml::MAP;
        drive["drive_id"] << d.drive_id;
        drive["file"] << d.file;
        drive["type"] << d.type;
        auto cs = drive["checksum"];
        cs |= ryml::MAP;
        cs["algorithm"] << d.checksum.algorithm;
        cs["value"] << d.checksum.value;
        drive["write_protected"] << d.write_protected;
        drive["motor_on"] << d.motor_on;
        drive["track_head_position"] << d.track_head_position;
    }

    if (dto.tape_player)
    {
        auto t = media["tape_player"];
        t |= ryml::MAP;
        t["file"] << dto.tape_player->file;
        auto cs = t["checksum"];
        cs |= ryml::MAP;
        cs["algorithm"] << dto.tape_player->checksum.algorithm;
        cs["value"] << dto.tape_player->checksum.value;
        t["position"] << dto.tape_player->position;
        t["playing"] << dto.tape_player->playing;
        t["auto_start"] << dto.tape_player->auto_start;
        t["traps_enabled"] << dto.tape_player->traps_enabled;
    }

    auto hds = media["hard_disks"];
    hds |= ryml::SEQ;
    for (const auto& hd : dto.hard_disks)
    {
        auto h = hds.append_child();
        h |= ryml::MAP;
        h["drive_id"] << hd.drive_id;
        h["file"] << hd.file;
        h["type"] << hd.type;
        auto cs = h["checksum"];
        cs |= ryml::MAP;
        cs["algorithm"] << hd.checksum.algorithm;
        cs["value"] << hd.checksum.value;
        h["size_mb"] << hd.size_mb;
        h["read_only"] << hd.read_only;
    }
    return true;
}

bool YamlSnapshotSerializer::saveDebug(ryml::NodeRef& debug, const DebugDTO& dto)
{
    auto lfs = debug["label_files"];
    lfs |= ryml::SEQ;
    for (const auto& lf : dto.label_files)
    {
        lfs.append_child() << lf;
    }

    // Embedded Labels
    auto els = debug["embedded_labels"];
    els |= ryml::SEQ;
    for (const auto& el : dto.embedded_labels)
    {
        auto e = els.append_child();
        e |= ryml::MAP;
        e["type"] << el.type;
        e["value"] << el.value;
        e["name"] << el.name;
        e["mem_type"] << el.mem_type;
        e["page"] << el.page;
        e["offset"] << el.offset;
    }

    // Breakpoint Files
    auto bfs = debug["breakpoint_files"];
    bfs |= ryml::SEQ;
    for (const auto& bf : dto.breakpoint_files)
    {
        bfs.append_child() << bf;
    }

    // Breakpoints
    auto bps = debug["breakpoints"];
    bps |= ryml::SEQ;
    for (const auto& bp : dto.breakpoints)
    {
        auto b = bps.append_child();
        b |= ryml::MAP;
        b["type"] << bp.type;
        b["value"] << bp.value;
        b["mem_type"] << bp.mem_type;
        b["page"] << bp.page;
        b["offset"] << bp.offset;
        b["enabled"] << bp.enabled;
        b["condition"] << bp.condition;
    }

    // Watchpoints
    auto wps = debug["watchpoints"];
    wps |= ryml::SEQ;
    for (const auto& wp : dto.watchpoints)
    {
        auto w = wps.append_child();
        w |= ryml::MAP;
        w["type"] << wp.type;
        w["value"] << wp.value;
        w["mem_type"] << wp.mem_type;
        w["page"] << wp.page;
        w["offset"] << wp.offset;
        w["size"] << wp.size;
        w["read"] << wp.read;
        w["write"] << wp.write;
    }

    // Call stack
    auto cs = debug["call_stack"];
    cs |= ryml::SEQ;
    for (const auto& addr : dto.call_stack)
    {
        cs.append_child() << addr;
    }
    return true;
}

bool YamlSnapshotSerializer::saveEmulatorConfig(ryml::NodeRef& ec, const EmulatorConfigDTO& dto)
{
    // Features
    auto f = ec.append_child();
    f.set_key("features");
    f.append_child() << ryml::key("turbo_mode") << dto.features.turbo_mode;
    f.append_child() << ryml::key("magic_button_enabled") << dto.features.magic_button_enabled;

    // Debug Features
    auto df = ec.append_child();
    df.set_key("debug_features");
    df.append_child() << ryml::key("debug_mode") << dto.debug_features.debug_mode;
    df.append_child() << ryml::key("memory_counters") << dto.debug_features.memory_counters;
    df.append_child() << ryml::key("call_trace") << dto.debug_features.call_trace;

    // Video
    auto vid = ec.append_child();
    vid.set_key("video");
    vid.append_child() << ryml::key("filter") << dto.video.filter;
    vid.append_child() << ryml::key("scanlines") << dto.video.scanlines;
    vid.append_child() << ryml::key("border_size") << dto.video.border_size;

    // Sound
    auto snd = ec.append_child();
    snd.set_key("sound");
    snd.append_child() << ryml::key("enabled") << dto.sound.enabled;
    snd.append_child() << ryml::key("sample_rate") << dto.sound.sample_rate;
    snd.append_child() << ryml::key("buffer_size") << dto.sound.buffer_size;

    // Input
    auto inp = ec.append_child();
    inp.set_key("input");
    inp.append_child() << ryml::key("mouse_type") << dto.input.mouse_type;
    inp.append_child() << ryml::key("joystick_type") << dto.input.joystick_type;
    inp.append_child() << ryml::key("keyboard_layout") << dto.input.keyboard_layout;
    return true;
}

// --- Component load helpers ---
bool YamlSnapshotSerializer::loadPsg(const ryml::ConstNodeRef& node, PSGDTO& dto)
{
    if (!node.readable())
        return false;

    safeGet(node["chip_type"], dto.chip_type, std::string{});
    if (const auto regs = node["registers"]; regs.readable())
    {
        for (const auto reg : regs)
        {
            std::string key = reg.key().str;
            safeGet(reg, dto.registers[key], uint8_t(0));
        }
    }

    safeGet(node["selected_register"], dto.selected_register, std::string{});
    if (const auto env = node["envelope"]; env.readable())
    {
        safeGet(env["phase"], dto.envelope.phase, 0);
        safeGet(env["step_counter"], dto.envelope.step_counter, 0);
        safeGet(env["current_volume"], dto.envelope.current_volume, 0);
        safeGet(env["direction"], dto.envelope.direction, std::string{});
    }

    if (const auto tone = node["tone"]; tone.readable())
    {
        safeGet(tone["counter_a"], dto.tone.counter_a, 0);
        safeGet(tone["counter_b"], dto.tone.counter_b, 0);
        safeGet(tone["counter_c"], dto.tone.counter_c, 0);
        safeGet(tone["output_a"], dto.tone.output_a, 0);
        safeGet(tone["output_b"], dto.tone.output_b, 0);
        safeGet(tone["output_c"], dto.tone.output_c, 0);
    }

    if (const auto noise = node["noise"]; noise.readable())
    {
        safeGet(noise["shift_register"], dto.noise.shift_register, 0u);
        safeGet(noise["counter"], dto.noise.counter, 0);
    }

    return true;
}

bool YamlSnapshotSerializer::loadWd1793(const ryml::ConstNodeRef& node, WD1793DTO& dto)
{
    if (!node.readable())
        return false;
    safeGet(node["command"], dto.command, uint8_t(0));
    safeGet(node["track"], dto.track, uint8_t(0));
    safeGet(node["sector"], dto.sector, uint8_t(0));
    safeGet(node["data"], dto.data, uint8_t(0));
    safeGet(node["status"], dto.status, uint8_t(0));
    safeGet(node["motor_on"], dto.motor_on, false);
    safeGet(node["head_loaded"], dto.head_loaded, false);
    safeGet(node["write_protect"], dto.write_protect, false);
    safeGet(node["motor_timeout"], dto.motor_timeout, 0u);
    safeGet(node["index_counter"], dto.index_counter, 0u);

    if (const auto sm = node["state_machine"]; sm.readable())
    {
        safeGet(sm["phase"], dto.state_machine.phase, 0);
        safeGet(sm["timer"], dto.state_machine.timer, 0);
        safeGet(sm["busy_counter"], dto.state_machine.busy_counter, 0);
    }

    return true;
}

// @brief Get last error message
std::string YamlSnapshotSerializer::lastError() const
{
    return _lastError;
}
