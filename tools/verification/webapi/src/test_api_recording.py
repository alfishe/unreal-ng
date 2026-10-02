"""Video recording with an optional sound track on every scripted surface.

POST /video/record takes "audio":"aac" (plus video_bitrate / audio_bitrate); without it the file is video
only, as before. The embedded Lua (video_record global) and Python (emu.video_record) take the same options.
A GIF has no audio track: every surface refuses gif + audio with the same message. The status endpoint
reports the session's audio codec, rate, channels and the emulated seconds of sound recorded.

The H.264 + AAC cases need the native macOS encoder (or ffmpeg elsewhere); they skip when the server cannot
start such a recording. Output files go to the system temp directory.
"""
import os
import tempfile
import time

import pytest


def _out(name):
    return os.path.join(tempfile.gettempdir(), f"unreal-webapi-rec-{os.getpid()}-{name}")


def _start_or_skip(api_client, emu, body):
    try:
        return api_client.video_record(emu, body)
    except Exception as exc:  # 500 = this build/platform cannot encode it
        if "500" in str(exc):
            pytest.skip(f"recording backend unavailable: {exc}")
        raise


class TestVideoRecordAudio:

    def test_gif_with_audio_is_refused(self, api_client, active_emulator):
        resp = api_client.video_record(active_emulator, {"action": "start", "format": "gif", "audio": "aac",
                                                         "filename": _out("refused.gif")}, expected_status=400)
        assert "GIF has no audio" in resp["message"], resp
        status = api_client.video_record_status(active_emulator)
        assert status["recording"] is False

    def test_codec_that_does_not_fit_the_container_is_refused(self, api_client, active_emulator):
        resp = api_client.video_record(active_emulator, {"action": "start", "format": "vp9", "audio": "aac",
                                                         "filename": _out("refused.webm")}, expected_status=400)
        assert ".webm" in resp["message"], resp

    def test_video_only_stays_the_default(self, api_client, active_emulator):
        path = _out("video-only.gif")
        resp = api_client.video_record(active_emulator, {"action": "start", "format": "gif", "filename": path})
        assert resp["audio"] is False and resp["audio_codec"] == ""
        time.sleep(0.5)
        stop = api_client.video_record(active_emulator, {"action": "stop"})
        assert stop["frames_recorded"] > 0
        assert stop["audio"] is False and stop["audio_samples_recorded"] == 0
        os.remove(path)

    def test_h264_with_aac_records_the_sound(self, api_client, active_emulator):
        path = _out("aac.mp4")
        resp = _start_or_skip(api_client, active_emulator, {"action": "start", "format": "h264", "audio": "aac",
                                                            "audio_bitrate": 192, "scale": 2, "filename": path})
        assert resp["audio"] is True and resp["audio_codec"] == "aac", resp
        assert resp["audio_channels"] == 2 and resp["audio_sample_rate"] >= 44100, resp
        time.sleep(1.0)
        status = api_client.video_record_status(active_emulator)
        assert status["recording"] is True and status["audio"] is True
        assert status["audio_samples_recorded"] > 0 and status["audio_duration"] > 0.0, status
        stop = api_client.video_record(active_emulator, {"action": "stop"})
        # The sound track is as long as the picture, within a frame or two
        video_seconds = stop["emulated_duration"]
        assert abs(stop["audio_duration"] - video_seconds) < 0.05, stop
        assert stop["file_size"] > 0
        os.remove(path)

    def test_lua_video_record_audio(self, api_client, active_emulator):
        if not api_client.get_lua_status().get("available", False):
            pytest.skip("Lua interpreter not available")
        code = """
local r = video_record("start", {format = "gif", audio = "aac", filename = "__GIF__"})
assert(r.error and string.find(r.error, "GIF has no audio", 1, true), "gif+audio: " .. tostring(r.error))
r = video_record("start", {format = "h264", audio = "aac", filename = "__MP4__"})
if r.error then print("LUA_REC_SKIP " .. tostring(r.message or r.error)) return end
assert(r.audio == true and r.audio_codec == "aac" and r.audio_channels == 2, "start audio fields")
local s = video_record_status()
assert(s.audio == true and s.audio_codec == "aac", "status audio fields")
print("LUA_REC_OK")
""".replace("__GIF__", _out("lua.gif")).replace("__MP4__", _out("lua.mp4"))
        resp = api_client.exec_lua(code)
        assert resp["success"] is True, resp
        if "LUA_REC_SKIP" in resp["output"]:
            pytest.skip(resp["output"])
        assert "LUA_REC_OK" in resp["output"], resp
        time.sleep(0.5)
        stop = api_client.video_record(active_emulator, {"action": "stop"})
        assert stop["audio"] is True and stop["audio_samples_recorded"] > 0, stop
        os.remove(_out("lua.mp4"))

    def test_python_video_record_audio(self, api_client, active_emulator):
        if not api_client.get_python_status().get("available", False):
            pytest.skip("Python interpreter not available")
        code = """
import unreal_emulator as ue
emu = ue.emu_get('__EMU_ID__')
r = emu.video_record('start', {'format': 'gif', 'audio': 'aac', 'filename': '__GIF__'})
assert 'GIF has no audio' in r.get('error', ''), r
r = emu.video_record('start', {'format': 'h264', 'audio': True, 'audio_bitrate': 160, 'filename': '__MP4__'})
if 'error' in r:
    print('PY_REC_SKIP', r.get('message', r['error']))
else:
    assert r['audio'] is True and r['audio_codec'] == 'aac' and r['audio_channels'] == 2, r
    s = emu.video_record_status()
    assert s['audio'] is True and s['audio_codec'] == 'aac', s
    print('PY_REC_OK')
""".replace("__EMU_ID__", active_emulator).replace("__GIF__", _out("py.gif")).replace("__MP4__", _out("py.mp4"))
        resp = api_client.exec_python(code)
        assert resp["success"] is True, resp
        if "PY_REC_SKIP" in resp["output"]:
            pytest.skip(resp["output"])
        assert "PY_REC_OK" in resp["output"], resp
        time.sleep(0.5)
        stop = api_client.video_record(active_emulator, {"action": "stop"})
        assert stop["audio"] is True and stop["audio_samples_recorded"] > 0, stop
        os.remove(_out("py.mp4"))
