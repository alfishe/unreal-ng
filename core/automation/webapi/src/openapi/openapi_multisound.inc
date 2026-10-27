    // ZX-MultiSound state and MIDI control (ZX-MultiSound tdd-integration.md §5) - DeviceState::MultiSound / Midi and
    // MidiControl, the same trees and actions as CLI `multisound` / `midi`, Lua / Python multisound_state(),
    // midi_state(), midi_panic() and the MCP aspects audio_multisound / audio_midi. Peeked: reading changes nothing.
    {
        auto idParameter = [](Json::Value& operation) {
            operation["parameters"][0]["name"] = "id";
            operation["parameters"][0]["in"] = "path";
            operation["parameters"][0]["required"] = true;
            operation["parameters"][0]["schema"]["type"] = "string";
        };

        Json::Value& state = paths["/api/v1/emulator/{id}/state/audio/multisound"]["get"];
        state["summary"] = "Get the ZX-MultiSound card";
        state["tags"].append("Audio State");
        state["description"] =
            "card, slot, options (text, dip {ym, saa, gs, sd}, gs_ram, ctrl_mask with ctrl_mask_note for the unofficial "
            "classic mask), fit, adapter, source, shadowed_devices (the built-ins the slot shadows or took out: id, name, "
            "state), logic (the CPLD: ym_chip, ym_read_status, fm_muted, saa_clock, rom_lock, the GS mailbox latches and "
            "flags, dac[4]: the DAC registers as latched), ym (master_clock_hz, selected_chip, ratio_phase, chips[2] "
            "{index, part, address_latch, status, ssg: "
            "the AY report of /state/audio/ay/{chip}, fm: the YM2203 report of /state/audio/fm/{chip}}), saa "
            "(registers_hex, address_latch, sound_enabled, sync, clock_enabled, chip_clock_hz, voices[6], noise[2], "
            "envelopes[2], output levels), gs (the /state/audio/gs report plus firmware_ready, cpu_steps, dac_fetches), "
            "dac[4] (the output stage: channel, side, sample, volume), midi (line, bank, bytes_received, framing_errors, active_voices), time";
        idParameter(state);
        state["responses"]["200"]["description"] = "The card's report";
        state["responses"]["404"]["description"] = "Emulator not found, or no ZX-MultiSound fitted (POST slots/zxbus.next/plug {card: multisound})";

        Json::Value& midi = paths["/api/v1/emulator/{id}/state/audio/midi"]["get"];
        midi["summary"] = "Get the MIDI line and synthesizer (SAM2695)";
        midi["tags"].append("Audio State");
        midi["description"] =
            "slot, synthesizer, line (source, port, bit, connected, level, edges, last_change_time), bank (status, name, "
            "source, error), parts[16] (channel 1-16, program 1-128, bank_msb, rhythm, preset name, volume, pan, "
            "expression, pitch_bend, active_voices, keys and notes sounding, muted), polyphony_limit, active_voices, "
            "fading_voices, master_volume, effects, counters (bytes_received, framing_errors, dropped_busy, "
            "dropped_queue_full, sysex_received, sysex_overflows, voices_stolen, notes_dropped, stream_overruns)";
        idParameter(midi);
        midi["responses"]["200"]["description"] = "The MIDI report";
        midi["responses"]["404"]["description"] = "Emulator not found, or no MIDI synthesizer fitted";

        Json::Value& control = paths["/api/v1/emulator/{id}/control/audio/midi"]["post"];
        control["summary"] = "MIDI control: panic";
        control["tags"].append("Audio State");
        control["description"] =
            "panic: every voice of the synthesizer stops (All Sound Off on all 16 parts); the controllers, programs and "
            "the MIDI stream in progress stay. Applied at the next instruction boundary as a TTD live input (journaled "
            "while recording, replayed). The bank is configuration ([MIDI] Bank=): changed through the slots, a restart";
        idParameter(control);
        control["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        control["requestBody"]["content"]["application/json"]["schema"]["properties"]["action"]["type"] = "string";
        control["requestBody"]["content"]["application/json"]["schema"]["properties"]["action"]["enum"].append("panic");
        control["responses"]["200"]["description"] = "{ok, action, message}";
        control["responses"]["400"]["description"] = "Unknown action";
        control["responses"]["404"]["description"] = "No MIDI synthesizer fitted";
        control["responses"]["409"]["description"] = "A TTD replay owns the input";
    }
