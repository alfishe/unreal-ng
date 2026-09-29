    // MoonSound (ZXM-MoonSound, YMF278B OPL4) state reports - DeviceState::MoonSound / MoonSoundFm / MoonSoundPcm,
    // the same trees CLI `state audio moonsound [fm|pcm]`, Lua/Python audio_moonsound_state() and the MCP aspects
    // audio_moonsound / audio_opl4_fm / audio_opl4_pcm render. A snapshot as of the chip's last guest access or
    // frame run: reading it never advances the chip.
    paths["/api/v1/emulator/{id}/state/audio/moonsound"]["get"]["summary"] = "Get MoonSound (OPL4) overview";
    paths["/api/v1/emulator/{id}/state/audio/moonsound"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/moonsound"]["get"]["description"] =
        "new_mode / new2_mode (OPL3 and OPL4-wave enables), status, the guest address latches, the block mix "
        "latches #F8 (FM) and #F9 (PCM) with per-side level, attenuation_db and muted, wave_memory (rom_bytes, "
        "rom_loaded_bytes, ram_bytes, ram_dirty_pages), fm_keyed_channels and pcm_keyed_slots.";
    paths["/api/v1/emulator/{id}/state/audio/moonsound"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/moonsound"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/moonsound"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/moonsound"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/moonsound"]["get"]["responses"]["200"]["description"] = "MoonSound overview";
    paths["/api/v1/emulator/{id}/state/audio/moonsound"]["get"]["responses"]["404"]["description"] =
        "Emulator not found, or no MoonSound fitted ([SOUND] MoonSound=1)";

    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["summary"] = "Get the MoonSound FM or PCM half";
    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["tags"].append("Audio State");
    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["description"] =
        "part=fm: status, timers[2] (count, load, enabled, masked, flag), four_op_connections, rhythm, "
        "tremolo_depth_db, vibrato_depth_cents, note_select, channels[18] (channel, bank, fnum, block, frequency_hz, "
        "key_on, feedback, connection, output_left/right, four_op, four_op_role, algorithm, rhythm_part, sounding, peak, "
        "operators[2] {slot, register_offset, mult, multiplier, ksr, tremolo, vibrato, sustaining, ksl, "
        "ksl_db_per_octave, tl, total_level_db, ar, dr, sl, sustain_level_db, rr, waveform, waveform_name, key_on, "
        "envelope {phase, attenuation, attenuation_max}, sounding}) and registers_bank0_hex / registers_bank1_hex. "
        "part=pcm: wave_enabled, memory_address, slots[24] (slot, wave, octave, fnum, playback_rate_hz, key_on, "
        "total_level, total_level_db, level_direct, pan, pan_attenuation {left_db, right_db; null = off}, damp, "
        "sample_bits, start, loop, end, position, sample_address, memory (rom | ram), envelope {phase, attenuation, "
        "attenuation_max, ar, d1r, d2r, rr, rc, decay_level, decay_level_db}, lfo, lfo_hz, lfo_active, vibrato, am, "
        "sounding, peak) and registers_hex.";
    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["parameters"][1]["name"] = "part";
    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["parameters"][1]["in"] = "path";
    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["parameters"][1]["required"] = true;
    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["parameters"][1]["schema"]["enum"].append("fm");
    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["parameters"][1]["schema"]["enum"].append("pcm");
    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["responses"]["200"]["description"] = "FM or PCM half";
    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["responses"]["400"]["description"] =
        "part is not fm or pcm";
    paths["/api/v1/emulator/{id}/state/audio/moonsound/{part}"]["get"]["responses"]["404"]["description"] =
        "Emulator not found, or no MoonSound fitted";
