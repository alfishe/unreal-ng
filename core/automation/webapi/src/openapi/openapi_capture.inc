    // Capture Commands endpoints
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["summary"] = "OCR text from screen";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["description"] =
        "Extract text from screen using ROM font bitmap matching (OCR). "
        "Returns 24 lines x 32 characters. Uses ZX Spectrum ROM font patterns.";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["description"] = "Screen OCR result";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["rows"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["cols"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["lines"]["type"] = "array";
    paths["/api/v1/emulator/{id}/capture/ocr"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["text"]["type"] = "string";

    // Capture screen endpoint
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["summary"] = "Capture screen as image";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["description"] =
        "Capture screen as GIF or PNG image. Returns base64-encoded data.";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["name"] = "format";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["schema"]["enum"].append("gif");
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][1]["schema"]["enum"].append("png");
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["name"] = "mode";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["in"] = "query";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["required"] = false;
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["schema"]["enum"].append("screen");
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["parameters"][2]["schema"]["enum"].append("full");
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["description"] =
        "Screen captured successfully";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["format"]["type"] = "string";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["width"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["height"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["size"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/capture/screen"]["get"]["responses"]["200"]["content"]["application/json"]["schema"]
         ["properties"]["data"]["type"] = "string";

    // Audio capture endpoints (sound core sample capture + DSP analysis)
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["summary"] = "Control audio capture session";
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["description"] =
        "Arm/stop/clear a stereo sample capture from the sound core. start arms a target of "
        "`seconds` of emulated audio (0.01-30.0); the emulator must run to collect samples.";
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("start");
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("stop");
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("clear");
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["seconds"]["type"] = "number";
    paths["/api/v1/emulator/{id}/audio/capture"]["post"]["responses"]["200"]["description"] =
        "Capture session updated (armed/target_samples/sample_rate/channels)";

    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["summary"] = "Audio capture progress";
    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["description"] =
        "Captured vs target sample counts and completion flag. Poll until complete=true.";
    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/audio/capture/status"]["get"]["responses"]["200"]["description"] =
        "Progress (captured_samples/target_samples/complete)";

    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["summary"] = "Audio capture result";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["description"] =
        "DSP analysis of the captured buffer: per-channel RMS/peak, zero-crossing rate, "
        "dominant frequency. wav=true additionally exports a WAV file into the scratch "
        "directory and returns its path.";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][1]["name"] = "wav";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][1]["in"] = "query";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][1]["required"] = false;
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][1]["description"] =
        "Export the captured samples as a WAV file (returns wav_path)";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["parameters"][1]["schema"]["type"] = "boolean";
    paths["/api/v1/emulator/{id}/audio/capture/result"]["get"]["responses"]["200"]["description"] =
        "Analysis result (sample_rate/channels/dominant_hz/rms/peak per side)";

    // Video recording endpoints (RecordingManager orchestration)
    paths["/api/v1/emulator/{id}/video/record"]["post"]["summary"] = "Control video recording";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["description"] =
        "start/stop/pause/resume a recording of the emulated screen. gif is native "
        "(dependency-free); other codecs map to containers as supported by the build. "
        "start pauses the emulator while applying fps/scale/region configuration, then "
        "resumes it. The recording feature is auto-enabled if disabled.";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/video/record"]["post"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("start");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("stop");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("pause");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["action"]["enum"].append("resume");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["format"]["type"] = "string";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["fps"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["scale"]["type"] = "integer";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["region"]["enum"].append("full");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["region"]["enum"].append("screen");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["region"]["enum"].append("main");
    paths["/api/v1/emulator/{id}/video/record"]["post"]["requestBody"]["content"]["application/json"]["schema"]
         ["properties"]["filename"]["type"] = "string";
    paths["/api/v1/emulator/{id}/video/record"]["post"]["responses"]["200"]["description"] =
        "Recording session updated (start: output/stats; stop: stats/output file); "
        "409 when already recording or nothing to stop";

    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["summary"] = "Video recording status";
    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["tags"].append("Capture");
    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["description"] =
        "Recording/paused flags, live RecordingStats (frames/durations/output size), "
        "feature state and last error.";
    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["parameters"][0]["name"] = "id";
    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["parameters"][0]["in"] = "path";
    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["parameters"][0]["required"] = true;
    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["parameters"][0]["schema"]["type"] = "string";
    paths["/api/v1/emulator/{id}/video/record/status"]["get"]["responses"]["200"]["description"] =
        "Recording status (recording/paused/stats/output)";

