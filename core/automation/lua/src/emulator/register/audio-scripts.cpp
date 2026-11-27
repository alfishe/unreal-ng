// LuaEmulator bindings: audio captures, speed, recording, labels, taps and script callbacks.
// One group of registerType (lua_emulator.h): each group in a file of its own, so they compile in parallel
#include "emulator/lua_emulator.h"

void LuaEmulator::RegisterAudioAndScripts(sol::state& lua)
{
    // Capture stats (peak/RMS per channel) + optional WAV export
    lua.set_function("audio_capture_result", [this](sol::optional<std::string> pathOpt) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        if (!context || !context->pDebugManager) { result["error"] = "debug manager not available"; return result; }

        AnalyzerManager* manager = context->pDebugManager->GetAnalyzerManager();
        AudioCaptureAnalyzer* capture =
            manager ? manager->getAnalyzer<AudioCaptureAnalyzer>("audiocapture") : nullptr;
        if (!capture || !manager) { result["error"] = "audio capture analyzer not available"; return result; }

        if (capture->isStreaming())
        {
            // The audio is in the WAV, not in memory: nothing to analyze
            if (!capture->isStreamClosed())
            {
                result["error"] = "the capture file is still being written - poll audio_capture_status";
                return result;
            }
            const size_t streamRate = context->pSoundManager ? context->pSoundManager->getCoreRate() : 44100;
            const size_t streamFrames = capture->getCapturedSamples() / 2;
            result["frames"] = static_cast<uint64_t>(streamFrames);
            result["sample_rate"] = static_cast<uint64_t>(streamRate);
            result["duration_seconds"] = static_cast<double>(streamFrames) / streamRate;
            result["complete"] = capture->isCaptureComplete();
            result["wav_path"] = capture->getStreamPath();
            result["file_failed"] = capture->hasStreamFailed();
            return result;
        }

        const auto& buffer = capture->getBuffer();
        const size_t frames = std::min(buffer.size(), capture->getCapturedSamples()) / 2;
        if (frames == 0)
        {
            result["error"] = "no captured audio — call audio_capture_start first";
            return result;
        }

        const size_t rate = context->pSoundManager ? context->pSoundManager->getCoreRate() : 44100;

        double peak[2] = {0.0, 0.0};
        double sumSquares[2] = {0.0, 0.0};
        for (size_t frame = 0; frame < frames; frame++)
        {
            for (int channel = 0; channel < 2; channel++)
            {
                const double normalized = static_cast<double>(buffer[frame * 2 + channel]) / 32768.0;
                const double magnitude = std::fabs(normalized);
                if (magnitude > peak[channel])
                    peak[channel] = magnitude;
                sumSquares[channel] += normalized * normalized;
            }
        }

        result["frames"] = static_cast<uint64_t>(frames);
        result["sample_rate"] = static_cast<uint64_t>(rate);
        result["duration_seconds"] = static_cast<double>(frames) / rate;
        result["complete"] = capture->isCaptureComplete();
        result["left_peak"] = peak[0];
        result["left_rms"] = std::sqrt(sumSquares[0] / frames);
        result["right_peak"] = peak[1];
        result["right_rms"] = std::sqrt(sumSquares[1] / frames);

        // Optional WAV export to the given path
        if (pathOpt.has_value())
        {
            TinyWav wav{};
            if (tinywav_open_write(&wav, 2, static_cast<int32_t>(rate), TW_INT16, TW_INTERLEAVED,
                                   pathOpt->c_str()) == 0)
            {
                tinywav_write_i(&wav, const_cast<void*>(static_cast<const void*>(buffer.data())),
                                static_cast<int>(frames));
                tinywav_close_write(&wav);
                result["saved"] = *pathOpt;
            }
            else
            {
                result["save_error"] = "failed to open wav";
            }
        }
        return result;
    });

    // Core audio rate control (same switch as CLI 'setting audio_rate' and
    // WebAPI PUT settings/audio_rate). Pin the rate (44100..192000) for
    // this run - never persisted to the ini; 0 = auto (follow the
    // priority chain: device > [SOUND] CoreRate > 44100). Applied at the
    // next frame boundary; deferred while a recording is in progress.
    // Host speed multiplier 1/2/4/8/16 (same switch as CLI 'setting speed N'
    // and WebAPI setting 'speed'). Refused (false) while TTD records.
    lua.set_function("set_speed", [this](int multiplier) -> bool {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return false;
        if (multiplier != 1 && multiplier != 2 && multiplier != 4 && multiplier != 8 && multiplier != 16)
            return false;
        return emulator->SetSpeedMultiplier(static_cast<uint8_t>(multiplier));
    });

    lua.set_function("get_speed", [this]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        if (!context || !context->pCore) { result["error"] = "core not available"; return result; }
        FeatureManager* fm = emulator->GetFeatureManager();
        result["multiplier"]   = context->pCore->GetHostSpeedMultiplier();
        result["effective"]    = context->pCore->GetSpeedMultiplier();  // with the machine's hardware turbo
        result["turbo_mode"]   = fm && fm->isEnabled(Features::kTurboMode);
        result["turbo_active"] = context->config.turbo_mode;
        result["turbo_audio"]  = context->config.turbo_mode_audio;
        return result;
    });

    lua.set_function("set_audio_rate", [this](int rate) -> bool {
        Emulator* emulator = effectiveEmulator();
        if (!emulator) return false;
        auto* context = emulator->GetContext();
        if (!context || !context->pSoundManager) return false;
        context->pSoundManager->setCoreRatePin(static_cast<uint32_t>(rate));
        return context->pSoundManager->getCoreRatePin() == static_cast<uint32_t>(rate);
    });

    lua.set_function("get_audio_rate", [this]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        if (!context || !context->pSoundManager) { result["error"] = "sound manager not available"; return result; }
        SoundManager* sound = context->pSoundManager;
        result["pin"] = sound->getCoreRatePin();  // 0 = auto
        result["core_rate"] = static_cast<uint64_t>(sound->getCoreRate());
        result["target_rate"] = static_cast<uint64_t>(sound->getTargetCoreRate());
        return result;
    });

    // Sound character (same source as CLI 'setting ay_voicing|ay_punch|
    // ay_room|beeper_punch' and WebAPI PUT settings/<name>): runtime only,
    // applied at the next frame boundary. get_sound_character() returns
    // all four as text; set_sound_character(name, value) returns
    // {ok=true, value=...} or {ok=false, error=...}
    lua.set_function("get_sound_character", [this]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        if (!context || !context->pSoundManager) { result["error"] = "sound manager not available"; return result; }
        for (const SoundCharacterSettings::Descriptor& d : SoundCharacterSettings::Descriptors())
            result[d.name] = SoundCharacterSettings::Get(*context->pSoundManager, d.name);
        return result;
    });

    lua.set_function("set_sound_character", [this](const std::string& name, const std::string& value) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        result["ok"] = false;
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* context = emulator->GetContext();
        if (!context || !context->pSoundManager) { result["error"] = "sound manager not available"; return result; }
        std::string error;
        if (!SoundCharacterSettings::Set(*context->pSoundManager, name, value, error))
        {
            result["error"] = error;
            return result;
        }
        result["ok"] = true;
        result["value"] = SoundCharacterSettings::Get(*context->pSoundManager, name);
        return result;
    });

    // Video recording control over the RecordingManager (mirrors POST /video/record)
#ifdef ENABLE_RECORDING
    lua.set_function("video_record", [this](sol::this_state ts, const std::string& action,
                                            sol::optional<sol::table> optsOpt) -> sol::object {
        sol::state_view lua_view(ts);
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
        {
            sol::table result = lua_view.create_table();
            result["error"] = "no emulator";
            return result;
        }
        // capture::RecordingControl: the presets, modes and option rules every plane shares
        std::map<std::string, std::string> options;
        if (optsOpt.has_value() && optsOpt->valid())
        {
            for (const auto& [key, value] : *optsOpt)
            {
                if (!key.is<std::string>())
                    continue;
                const std::string name = key.as<std::string>();
                if (value.get_type() == sol::type::boolean)
                {
                    // audio = true / false: aac or none, as before; other flags as text
                    const bool on = value.as<bool>();
                    options[name] = name == "audio" ? (on ? "aac" : "none") : (on ? "true" : "false");
                }
                else if (value.get_type() == sol::type::number)
                {
                    const double number = value.as<double>();
                    options[name] = number == std::floor(number) ? std::to_string(static_cast<long long>(number))
                                                                 : std::to_string(number);
                }
                else if (value.get_type() == sol::type::string)
                    options[name] = value.as<std::string>();
            }
        }
        // audio_rate (a number or "auto", as the CLI's --audio-rate): the core rate pinned before the start, so
        // the recording is stamped with it; applied at the next frame boundary
        SoundManager* sound = emulator->GetContext() ? emulator->GetContext()->pSoundManager : nullptr;
        if (auto rate = options.find("audio_rate"); rate != options.end())
        {
            const std::string text = rate->second;
            options.erase(rate);
            if (action == "start" || action == "toggle")
            {
                uint32_t audioRate = 0;
                if (text != "auto")
                {
                    char* end = nullptr;
                    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
                    if (text.empty() || end == nullptr || *end != '\0')
                    {
                        sol::table result = lua_view.create_table();
                        result["error"] = "audio_rate must be a number or 'auto'";
                        return result;
                    }
                    audioRate = static_cast<uint32_t>(parsed);
                }
                if (sound == nullptr)
                {
                    sol::table result = lua_view.create_table();
                    result["error"] = "sound manager not available";
                    return result;
                }
                sound->setCoreRatePin(audioRate);
                for (int attempt = 0; attempt < 100 && sound->getCoreRate() != sound->getTargetCoreRate(); attempt++)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (sound->getCoreRate() != sound->getTargetCoreRate())
                {
                    sol::table result = lua_view.create_table();
                    result["error"] = "audio_rate not applied within 1s (emulator paused?) - "
                                      "resume it or call set_audio_rate before pausing";
                    return result;
                }
            }
        }
        std::error_code ec;
        const std::string folder = (std::filesystem::temp_directory_path(ec) / "unreal-lua").string();
        const capture::RecordingReply reply = capture::RecordingControl(*emulator, folder).Execute(action, options);
        StateNode body = reply.body;
        if (reply.ok && sound != nullptr && (action == "start" || action == "toggle"))
            body["audio_rate"] = static_cast<uint64_t>(sound->getCoreRate());
        if (!reply.ok)
        {
            body["error"] = reply.message.empty() ? reply.error : reply.message;
            body["code"] = reply.error;
        }
        return StateNodeToLua(ts, body);
    });

    // Current recording state + live statistics (mirrors GET /video/record/status)
    lua.set_function("video_record_status", [this]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }

        auto* context = emulator->GetContext();
        RecordingManager* rm = context ? context->pRecordingManager : nullptr;
        if (!rm) { result["error"] = "recording manager not available"; return result; }

        result["recording"] = rm->IsRecording();
        result["paused"] = rm->IsPaused();
        result["feature_enabled"] = rm->isFeatureEnabled();
        result["realtime_capable"] = rm->IsRealtimeCapable();
        if (!rm->GetLastRecordingError().empty())
            result["last_error"] = rm->GetLastRecordingError();

        const RecordingManager::RecordingStats stats = rm->GetStats();
        result["frames_recorded"] = static_cast<uint64_t>(stats.framesRecorded);
        result["recorded_duration"] = stats.recordedDuration;
        result["emulated_duration"] = stats.emulatedDuration;
        result["output_file_size"] = static_cast<uint64_t>(stats.outputFileSize);
        result["average_frame_time_ms"] = stats.averageFrameTime;
        result["recent_fps"] = stats.recentFps;
        result["audio_samples_recorded"] = static_cast<uint64_t>(stats.audioSamplesRecorded);
        result["video_codec"] = rm->GetVideoCodec();
        result["profile"] = rm->GetOutputProfile();
        result["acceleration"] = EncoderAccelerationName(rm->GetEncoderAcceleration());
        result["audio"] = rm->HasAudio();
        result["audio_codec"] = rm->HasAudio() ? rm->GetAudioCodec() : std::string();
        result["audio_sample_rate"] = static_cast<uint64_t>(rm->HasAudio() ? rm->GetAudioSampleRate() : 0);
        result["audio_channels"] = static_cast<uint64_t>(rm->HasAudio() ? rm->GetAudioChannels() : 0);
        result["audio_duration"] = rm->HasAudio() ? rm->GetAudioDuration() : 0.0;
        result["output"] = rm->GetOutputFilename();
        return result;
    });
#else
    lua.set_function("video_record", [this](const std::string& action, sol::optional<sol::table>) -> sol::table {
        (void)action;
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        result["error"] = "recording support is disabled in this build (ENABLE_RECORDING=OFF)";
        return result;
    });

    lua.set_function("video_record_status", [this]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        result["error"] = "recording support is disabled in this build (ENABLE_RECORDING=OFF)";
        return result;
    });
#endif

    // Assemble Z80 source text; optionally write the bytes into RAM
    lua.set_function("assemble", [this](const std::string& code, sol::object addressValue,
                                        sol::optional<bool> writeOpt) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }

        uint16_t address = 0;
        if (addressValue.is<std::string>())
        {
            try { address = static_cast<uint16_t>(std::stoul(addressValue.as<std::string>(), nullptr, 0)); }
            catch (...) { result["error"] = "invalid address"; return result; }
        }
        else if (addressValue.is<int>())
        {
            address = static_cast<uint16_t>(addressValue.as<int>() & 0xFFFF);
        }

        Z80TextAssembler assembler;
        AsmResult asmResult = assembler.Assemble(code, address);

        result["ok"] = asmResult.ok;
        if (!asmResult.ok)
        {
            result["error"] = asmResult.error.message;
            result["error_line"] = asmResult.error.line;
            return result;
        }

        if (writeOpt.value_or(false))
        {
            Memory* memory = emulator->GetMemory();
            if (memory)
            {
                emulator->EditMemoryFromToolOrThrow("Lua assemble write", [&] {
                    uint32_t addr = asmResult.startAddress;
                    for (uint8_t b : asmResult.bytes)
                        memory->ToolWriteToZ80Memory(static_cast<uint16_t>((addr++) & 0xFFFF), b);
                });
                result["written"] = true;
            }
        }

        result["address"] = asmResult.startAddress;
        result["end_address"] = asmResult.endAddress;

        sol::table bytes = lua_view.create_table();
        for (size_t i = 0; i < asmResult.bytes.size(); i++)
            bytes[i + 1] = asmResult.bytes[i];
        result["bytes"] = bytes;

        sol::table symbols = lua_view.create_table();
        for (const auto& sym : asmResult.symbols)
            symbols[sym.first] = sym.second;
        if (!asmResult.symbols.empty())
            result["symbols"] = symbols;
        return result;
    });

    // Resolve a label name to its address, or an address to label(s)
    lua.set_function("label_resolve", [this](sol::object queryValue, sol::optional<std::string> cpu) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* ctx = emulator->GetContext();
        LabelManager* labelMgr = ctx ? ScriptLabels(cpu) : nullptr;
        if (!labelMgr) { result["error"] = "label manager not available"; return result; }

        std::string query;
        if (queryValue.is<std::string>())
            query = queryValue.as<std::string>();
        else if (queryValue.is<int>())
            query = std::to_string(queryValue.as<int>());
        else
        {
            result["error"] = "query must be a label name or address";
            return result;
        }

        // Name direction
        auto label = labelMgr->GetLabelByName(query);
        if (label)
        {
            result["found"] = true;
            result["query"] = "name";
            result["address"] = label->address;
            result["name"] = label->name;
            if (!label->type.empty())
                result["type"] = label->type;
            return result;
        }

        // Address direction: 0x / $ / decimal
        uint16_t address = 0;
        bool isAddress = false;
        try
        {
            if (query.rfind("0x", 0) == 0 || query.rfind("0X", 0) == 0)
            {
                address = static_cast<uint16_t>(std::stoul(query.substr(2), nullptr, 16));
                isAddress = true;
            }
            else if (query[0] == '$')
            {
                address = static_cast<uint16_t>(std::stoul(query.substr(1), nullptr, 16));
                isAddress = true;
            }
            else if (!query.empty() && query.find_first_not_of("0123456789") == std::string::npos)
            {
                address = static_cast<uint16_t>(std::stoul(query));
                isAddress = true;
            }
        }
        catch (...)
        {
        }

        if (!isAddress)
        {
            result["found"] = false;
            result["query"] = "name";
            if (labelMgr->GetLabelCount() == 0)
                result["hint"] = "no labels loaded — symbols_load first";
            return result;
        }

        result["query"] = "address";
        result["address"] = address;

        auto exact = labelMgr->GetLabelByZ80Address(address);
        result["found"] = exact != nullptr;
        if (exact)
            result["name"] = exact->name;

        auto atAddress = labelMgr->GetAllLabelsAtAddress(address);
        if (!atAddress.empty())
        {
            sol::table aliases = lua_view.create_table();
            size_t index = 0;
            for (const auto& l : atAddress)
            {
                sol::table item = lua_view.create_table();
                item["name"] = l->name;
                item["address"] = l->address;
                aliases[index + 1] = item;
                index++;
            }
            result["aliases"] = aliases;
        }

        const Label* bestBelow = nullptr;
        const Label* bestAbove = nullptr;
        for (const auto& l : labelMgr->GetAllLabels())
        {
            if (l->address < address && (!bestBelow || l->address > bestBelow->address))
                bestBelow = l.get();
            else if (l->address > address && (!bestAbove || l->address < bestAbove->address))
                bestAbove = l.get();
        }
        if (bestBelow)
        {
            result["nearest_below"] = bestBelow->name;
            result["nearest_below_address"] = bestBelow->address;
        }
        if (bestAbove)
        {
            result["nearest_above"] = bestAbove->name;
            result["nearest_above_address"] = bestAbove->address;
        }
        return result;
    });

    // sjasmplus .lst source-listing navigation
    lua.set_function("listing_load", [this](const std::string& path) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* ctx = emulator->GetContext();
        ListingParser* parser = ctx && ctx->pDebugManager ? ctx->pDebugManager->GetListingParser() : nullptr;
        if (!parser) { result["error"] = "listing parser not available"; return result; }

        result["ok"] = parser->LoadListing(path);
        if (result["ok"])
        {
            result["lines"] = static_cast<uint64_t>(parser->GetLineCount());
            result["code_lines"] = static_cast<uint64_t>(parser->GetCodeLineCount());
            result["total_bytes"] = static_cast<uint64_t>(parser->GetTotalBytes());
            result["min_address"] = parser->GetMinAddress();
            result["max_address"] = parser->GetMaxAddress();
        }
        return result;
    });

    lua.set_function("listing_source_at", [this](sol::optional<unsigned> addressOpt) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* ctx = emulator->GetContext();
        ListingParser* parser = ctx && ctx->pDebugManager ? ctx->pDebugManager->GetListingParser() : nullptr;
        if (!parser) { result["error"] = "listing parser not available"; return result; }
        if (!parser->IsLoaded()) { result["error"] = "no listing loaded"; return result; }

        Z80State* z80 = emulator->GetZ80State();
        if (!z80) { result["error"] = "Z80 state not available"; return result; }

        const uint16_t address = addressOpt.has_value() ? static_cast<uint16_t>(addressOpt.value()) : z80->pc;
        const ListingLine* line = parser->FindLineByAddress(address);
        result["found"] = line != nullptr;
        if (line)
        {
            result["line"] = line->lineNumber;
            result["source"] = line->source;
            result["has_code"] = line->hasCode;
            if (line->hasCode)
            {
                result["address"] = line->addressStart;
                result["address_end"] = line->addressEnd;
            }
        }
        return result;
    });

    lua.set_function("listing_step_line", [this]() -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pDebugManager) { result["error"] = "debug manager not available"; return result; }
        ListingParser* parser = ctx->pDebugManager->GetListingParser();
        if (!parser) { result["error"] = "listing parser not available"; return result; }
        if (!parser->IsLoaded()) { result["error"] = "no listing loaded"; return result; }

        Z80State* z80 = emulator->GetZ80State();
        if (!z80) { result["error"] = "Z80 state not available"; return result; }

        const ListingLine* startLine = parser->FindLineByAddress(z80->pc);
        const int startLineNumber = startLine ? startLine->lineNumber : -1;

        const unsigned maxTStates = ctx->config.frame * 100;
        emulator->RunUntilCondition(
            [parser, startLineNumber](const Z80State& state) {
                const ListingLine* line = parser->FindLineByAddress(state.pc);
                return line != nullptr && line->lineNumber != startLineNumber;
            },
            maxTStates);

        z80 = emulator->GetZ80State();
        const ListingLine* endLine = z80 ? parser->FindLineByAddress(z80->pc) : nullptr;
        result["line_changed"] = endLine != nullptr && endLine->lineNumber != startLineNumber;
        if (z80)
            result["pc"] = z80->pc;
        if (endLine)
        {
            result["line"] = endLine->lineNumber;
            result["source"] = endLine->source;
        }
        return result;
    });

    lua.set_function("listing_run_to_line", [this](int lineNumber) -> sol::table {
        sol::state_view lua_view(*_lua);
        sol::table result = lua_view.create_table();
        Emulator* emulator = effectiveEmulator();
        if (!emulator) { result["error"] = "no emulator"; return result; }
        auto* ctx = emulator->GetContext();
        if (!ctx || !ctx->pDebugManager) { result["error"] = "debug manager not available"; return result; }
        ListingParser* parser = ctx->pDebugManager->GetListingParser();
        if (!parser) { result["error"] = "listing parser not available"; return result; }
        if (!parser->IsLoaded()) { result["error"] = "no listing loaded"; return result; }

        Z80State* z80 = emulator->GetZ80State();
        if (!z80) { result["error"] = "Z80 state not available"; return result; }

        const ListingLine* target = parser->FindNextCodeLine(lineNumber);
        if (!target)
        {
            result["error"] = "no code line at or after line " + std::to_string(lineNumber);
            return result;
        }

        const uint16_t targetAddress = target->addressStart;
        const bool alreadyAt = z80->pc == targetAddress;
        if (!alreadyAt)
        {
            const unsigned maxTStates = ctx->config.frame * 500;
            emulator->RunUntilCondition(
                [targetAddress](const Z80State& state) { return state.pc == targetAddress; }, maxTStates);
        }

        z80 = emulator->GetZ80State();
        const bool reached = z80 && z80->pc == targetAddress;
        result["reached"] = reached;
        result["already_at"] = alreadyAt;
        if (z80)
            result["pc"] = z80->pc;
        result["line"] = target->lineNumber;
        result["source"] = target->source;
        return result;
    });

    // Port trace (PDR) bindings — runtime feature "porttrace"
    LuaPortTrace::registerBindings(lua, [this]() -> Emulator* { return effectiveEmulator(); });

    // TS-Conf VDAC2 card (FT812): bus capture
    LuaVdac2::registerBindings(lua, [this]() -> Emulator* { return effectiveEmulator(); });

    // TS-Conf VDAC4 board (Tang + ESP32-S3): state
    LuaVdac4::registerBindings(lua, [this]() -> Emulator* { return effectiveEmulator(); });

    // Script callbacks (docs/features/script-callbacks.md): a function runs deferred, on this Lua thread, with a
    // snapshot of the hit; a string is a chunk for the hook state (frame or sync mode, on the emulation thread).
    // on_tap({exec|read|write|port_in|port_out = addr, to=, page=, mask=, ["if"]=, dialect=, cpu=} |
    // {event=, value=, vmask=, cpu=} | {chip=, reg=, to=, unit=, value=, vmask=, ["if"]=, dialect=}, fn_or_chunk
    // [, options]). cpu "gs": the sound card CPU's; a sync chunk on its read / port_in may return the byte the card
    // sees. An event is the card's (cpu "gs" by default) or the machine's (the main CPU's); chip a sound chip's
    // register written by the main CPU
    lua.set_function("on_tap", [this](const sol::table& where, const sol::object& fn,
                                      sol::optional<sol::table> options) -> std::tuple<int, sol::optional<std::string>> {
        CallbackSpec spec;
        spec.trigger = CallbackTrigger::Tap;
        static const std::pair<const char*, std::pair<BreakpointTypeEnum, uint8_t>> kKinds[] = {
            {"exec", {BRK_MEMORY, BRK_MEM_EXECUTE}}, {"read", {BRK_MEMORY, BRK_MEM_READ}},
            {"write", {BRK_MEMORY, BRK_MEM_WRITE}},  {"port_in", {BRK_IO, BRK_IO_IN}},
            {"port_out", {BRK_IO, BRK_IO_OUT}}};
        const std::string cpu = where.get<sol::optional<std::string>>("cpu").value_or("");
        if (!DebugManager::ParseCpuName(cpu, spec.cpu))
            return {-1, std::string("cpu must be 'main', 'gs' or 'ms'")};
        // An event's or a register's byte: value= the bits to match, vmask= which (0xFF with value alone)
        const sol::optional<int32_t> value = where["value"];
        const sol::optional<int32_t> vmask = where["vmask"];
        if (value)
        {
            spec.tap.nrValue = static_cast<uint8_t>(*value);
            spec.tap.nrMask = 0xFF;
        }
        if (vmask)
            spec.tap.nrMask = static_cast<uint8_t>(*vmask);
        const sol::optional<std::string> event = where["event"];
        const sol::optional<std::string> chip = where["chip"];
        if ((value || vmask) && !event && !chip)
            return {-1, std::string("value and vmask apply to event and chip taps")};
        // An event: no address. The machine's are the main CPU's, the card's its CPU's
        if (event)
        {
            GSCardEvent kind = GSCardEvent::Count;
            if (!ParseGSCardEvent(event->c_str(), kind))
                return {-1, "unknown event '" + *event + "'"};
            if (cpu.empty())
                spec.cpu = GSCardEventIsHostSide(kind) ? DebugCpuId::Main : DebugCpuId::GS;
            spec.tap.type = BRK_EVENT;
            spec.tap.access = 0;
            spec.tap.address = static_cast<uint16_t>(kind);
            return RegisterCallback(spec, fn, options);
        }
        // A sound chip's register the main CPU wrote
        if (chip)
        {
            SoundChipKind chipKind = SoundChipKind::Count;
            if (!ParseSoundChipKind(chip->c_str(), chipKind))
                return {-1, "chip must be ay, fm, saa, opl4, opl4wave, dac or cbl, not '" + *chip + "'"};
            spec.tap.type = BRK_CHIPREG;
            spec.tap.access = 0;
            spec.tap.chip = static_cast<uint8_t>(chipKind);
            spec.tap.address = static_cast<uint16_t>(where.get<sol::optional<int32_t>>("reg").value_or(0));
            if (auto to = where.get<sol::optional<int32_t>>("to"))
            {
                spec.tap.hasEnd = true;
                spec.tap.addressEnd = static_cast<uint16_t>(*to);
            }
            if (auto unit = where.get<sol::optional<int32_t>>("unit"))
                spec.tap.chipUnit = static_cast<uint8_t>(*unit);
            spec.tap.condition = where.get<sol::optional<std::string>>("if").value_or(
                where.get<sol::optional<std::string>>("condition").value_or(""));
            if (!BreakpointConditionProgram::ParseDialect(
                    where.get<sol::optional<std::string>>("dialect").value_or(""), spec.tap.conditionDialect))
                return {-1, std::string("dialect must be 'unreal' or 'c'")};
            return RegisterCallback(spec, fn, options);
        }
        int kinds = 0;
        for (const auto& [name, kind] : kKinds)
            if (auto address = where.get<sol::optional<int64_t>>(name))
            {
                kinds++;
                spec.tap.type = kind.first;
                spec.tap.access = kind.second;
                spec.tap.address = static_cast<uint16_t>(*address);
            }
        if (kinds != 1)
            return {-1, std::string("one of exec, read, write, port_in, port_out names the address (or event an "
                                    "event, chip a sound chip)")};
        spec.tap.condition = where.get<sol::optional<std::string>>("if").value_or(
            where.get<sol::optional<std::string>>("condition").value_or(""));
        if (!BreakpointConditionProgram::ParseDialect(where.get<sol::optional<std::string>>("dialect").value_or(""),
                                                      spec.tap.conditionDialect))
            return {-1, std::string("dialect must be 'unreal' or 'c'")};
        std::string error;
        if (!BreakpointManager::ApplyScriptOptions(spec.tap, where.get<sol::optional<std::string>>("page").value_or(""),
                                                   where.get<sol::optional<int32_t>>("to").value_or(-1), false,
                                                   where.get<sol::optional<int32_t>>("mask").value_or(-1), "", error))
            return {-1, error};
        return RegisterCallback(spec, fn, options);
    });
    lua.set_function("on_breakpoint", [this](uint16_t id, const sol::object& fn, sol::optional<sol::table> options) {
        CallbackSpec spec;
        spec.trigger = CallbackTrigger::Breakpoint;
        spec.breakpointId = id;
        return RegisterCallback(spec, fn, options);
    });
    lua.set_function("on_game_event", [this](const std::string& event, const sol::object& fn,
                                             sol::optional<sol::table> options) {
        CallbackSpec spec;
        spec.trigger = CallbackTrigger::GameEvent;
        spec.event = event;
        return RegisterCallback(spec, fn, options);
    });
    // The card's events by name (gs-debugger requirements X13): on_tap({event = ...}) on the sound card CPU;
    // options.cpu "ms" picks the MultiSound's General Sound (default "gs")
    auto cardCpu = [](const sol::optional<sol::table>& options, DebugCpuId& cpu) -> bool {
        cpu = DebugCpuId::GS;
        if (!options)
            return true;
        const sol::optional<std::string> name = (*options)["cpu"];
        return !name || (DebugManager::ParseCpuName(*name, cpu) && IsCardCpu(cpu));
    };
    auto cardEvent = [this, cardCpu](GSCardEvent kind) {
        return [this, kind, cardCpu](const sol::object& fn, sol::optional<sol::table> options)
                   -> std::tuple<int, sol::optional<std::string>> {
            CallbackSpec spec;
            spec.trigger = CallbackTrigger::Tap;
            if (!cardCpu(options, spec.cpu))
                return {-1, std::string("cpu must be a card's: 'gs' or 'ms'")};
            spec.tap.type = BRK_EVENT;
            spec.tap.access = 0;
            spec.tap.address = static_cast<uint16_t>(kind);
            return RegisterCallback(spec, fn, options);
        };
    };
    lua.set_function("gs_on_command", cardEvent(GSCardEvent::CardCommandRead));
    lua.set_function("gs_on_data", cardEvent(GSCardEvent::CardDataRead));
    lua.set_function("gs_on_reply", cardEvent(GSCardEvent::CardReplyWrite));
    lua.set_function("gs_on_int", cardEvent(GSCardEvent::Int));
    lua.set_function("gs_on_nmi", cardEvent(GSCardEvent::Nmi));
    lua.set_function("gs_on_page", cardEvent(GSCardEvent::PageSwitch));
    lua.set_function("gs_on_dac_fetch", cardEvent(GSCardEvent::DacFetch));
    lua.set_function("gs_on_clock_change", cardEvent(GSCardEvent::ClockChange));
    lua.set_function("gs_on_dma_start", cardEvent(GSCardEvent::DmaStart));
    lua.set_function("gs_on_dma_done", cardEvent(GSCardEvent::DmaDone));
    lua.set_function("gs_on_gscfg0", cardEvent(GSCardEvent::ConfigWrite));
    // A card port (its 8-bit decode): gs_on_port_in(port, fn [, options]), gs_on_port_out
    auto cardPort = [this, cardCpu](uint8_t access) {
        return [this, access, cardCpu](int port, const sol::object& fn, sol::optional<sol::table> options)
                   -> std::tuple<int, sol::optional<std::string>> {
            CallbackSpec spec;
            spec.trigger = CallbackTrigger::Tap;
            if (!cardCpu(options, spec.cpu))
                return {-1, std::string("cpu must be a card's: 'gs' or 'ms'")};
            spec.tap.type = BRK_IO;
            spec.tap.access = access;
            spec.tap.address = static_cast<uint16_t>(port & 0xFF);
            spec.tap.portMask = 0x00FF;
            return RegisterCallback(spec, fn, options);
        };
    };
    lua.set_function("gs_on_port_in", cardPort(BRK_IO_IN));
    lua.set_function("gs_on_port_out", cardPort(BRK_IO_OUT));
    lua.set_function("callbacks", [this](sol::this_state state) -> sol::table {
        sol::state_view lua(state);
        sol::table list = lua.create_table();
        if (ScriptCallbackManager* callbacks = CallbacksOf(effectiveEmulator()))
            for (const CallbackInfo& info : callbacks->List())
                list.add(CallbackInfoTable(lua, info));
        return list;
    });
    lua.set_function("callback_resume", [this](uint32_t id) -> bool {
        ScriptCallbackManager* callbacks = CallbacksOf(effectiveEmulator());
        return callbacks && callbacks->Resume(id);
    });
    lua.set_function("callback_remove", [this](uint32_t id) -> bool {
        Emulator* emulator = effectiveEmulator();
        ScriptCallbackManager* callbacks = CallbacksOf(emulator);
        if (!callbacks || !callbacks->Remove(id))
            return false;
        _callbacks.erase({emulator->GetId(), id});
        return true;
    });
    lua.set_function("callback_policy", [this](uint32_t id, const std::string& policy) -> std::tuple<bool, sol::optional<std::string>> {
        ScriptCallbackManager* callbacks = CallbacksOf(effectiveEmulator());
        CallbackPolicy parsed;
        if (!ParseCallbackPolicy(policy, parsed))
            return {false, "policy must be suspend, disable, ignore or unpaced, not '" + policy + "'"};
        if (!callbacks || !callbacks->SetPolicy(id, parsed))
            return {false, parsed == CallbackPolicy::Unpaced
                               ? "no frame or sync callback " + std::to_string(id) + " (unpaced is for those)"
                               : std::string("no callback ") + std::to_string(id)};
        return {true, sol::nullopt};
    });
    // The hooks' time and the unpaced state: {unpaced, unpaced_spans, last_frame_us, max_frame_us, frames_over_share}
    lua.set_function("callback_hooks", [this](sol::this_state state) -> sol::table {
        sol::state_view lua(state);
        sol::table t = lua.create_table();
        if (ScriptCallbackManager* callbacks = CallbacksOf(effectiveEmulator()))
        {
            const CallbackHookStatus status = callbacks->HookStatus();
            t["unpaced"] = status.unpaced;
            t["unpaced_spans"] = status.unpacedSpans;
            t["last_frame_us"] = status.lastFrameUs;
            t["max_frame_us"] = status.maxFrameUs;
            t["frames_over_share"] = status.framesOverShare;
        }
        return t;
    });
    // Values the script and its hooks share: nil, a boolean, a number or a string
    lua.set_function("shared_get", [this](sol::this_state state, const std::string& key) -> sol::object {
        ScriptCallbackManager* callbacks = CallbacksOf(effectiveEmulator());
        return LuaFromCallbackValue(state, callbacks ? callbacks->SharedGet(key) : CallbackValue{});
    });
    lua.set_function("shared_set", [this](const std::string& key, const sol::object& value) {
        CallbackValue shared;
        if (!LuaToCallbackValue(value, shared))
            throw std::invalid_argument("shared_set: '" + key + "' takes nil, a boolean, a number or a string");
        if (ScriptCallbackManager* callbacks = CallbacksOf(effectiveEmulator()))
            callbacks->SharedSet(key, shared);
    });
    // Runs the callbacks with hits now (a script that loops on its own thread calls it; the idle loop does it itself)
    lua.set_function("callbacks_run", [this]() {
        while (DrainCallbacks())
        {
        }
    });
}
