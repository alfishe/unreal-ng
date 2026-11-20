#!/usr/bin/env python3
"""POC 052: capture the window recorder's real picture streams and time the stepped (encoder-paced) mode.

Run against an offscreen unreal-qt started with PATH=<raw>/bin:$PATH and POC052_CAPTURE=<raw>/streams/<name>
(the ffmpeg wrapper there tees the recorder's Matroska stream). One run per (workload, size) so each stream file is
named by the environment of the instance. Usage:

  capture_streams.py PORT RAW_DIR WORKLOAD SIZE FRAMES

WORKLOAD: demo (eyeache1.sna) | game (Dizzy X.sna); SIZE: native | fhd | uhd | kbd
Prints one JSON line: the stepped rate (frames / s) and the recorder's status.
"""
import json
import os
import sys
import time
import urllib.request

PORT, RAW, WORKLOAD, SIZE, FRAMES = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5])
B = f"http://127.0.0.1:{PORT}/api/v1"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../.."))
SNAPSHOTS = {"demo": "testdata/loaders/sna/eyeache1.sna", "game": "testdata/loaders/sna/Dizzy X.sna"}
SIZES = {
    "native": {},                                                             # the window's picture
    "fhd": {"width": 1920, "height": 1080, "fit": True, "picture_height": 1080},
    "uhd": {"width": 3840, "height": 2160, "fit": True, "picture_height": 2160},
    "kbd": {"keyboard": "pentagon2plus", "scale": 2},                         # the recipe's demo layout
}
session = None


def call(method, path, body=None, timeout=600):
    data = json.dumps(body).encode() if body is not None else None
    headers = {"Content-Type": "application/json"}
    if session:
        headers["X-Unreal-Session"] = session
    req = urllib.request.Request(B + path, method=method, data=data, headers=headers)
    try:
        return json.loads(urllib.request.urlopen(req, timeout=timeout).read() or b"{}")
    except urllib.error.HTTPError as e:
        return {"http": e.code, **json.loads(e.read() or b"{}")}


emulators = call("GET", "/emulator")["emulators"]
eid = emulators[0]["id"] if emulators else call("POST", "/emulator/start", {"model": "PENTAGON"})["id"]
E = f"/emulator/{eid}"
if not emulators:
    time.sleep(3)  # the window attaches to the new instance
session = call("POST", E + "/control/session", {"client": "poc052"})["session"]
try:
    call("PUT", E + "/audio/mixer/master", {"muted": True})
    call("PUT", E + "/video/window", {"crt": True, "crt_profile": "crt-lottes"})
    call("POST", E + "/snapshot/load", {"path": os.path.join(REPO, SNAPSHOTS[WORKLOAD])})
    call("POST", E + "/resume")
    time.sleep(float(os.environ.get("WARMUP", "2")))  # past the snapshot's first frames (a demo's title)
    call("POST", E + "/pause")
    # The encoder: CODEC (h264 / h265 / prores), HARDWARE=1 (VideoToolbox), ENGINE (ffmpeg / native)
    codec = os.environ.get("CODEC", "h264")
    encoder = {"codec": codec, "engine": os.environ.get("ENGINE", "ffmpeg"), "hardware": os.environ.get("HARDWARE") == "1"}
    out = os.path.join(RAW, "out", f"{WORKLOAD}-{SIZE}-{codec}.{'mov' if codec == 'prores' else 'mp4'}")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    start = call("POST", E + "/video/window/record",
                 {"path": out, **encoder, "audio": True, **SIZES[SIZE]})
    if "error" in start:
        print(json.dumps({"workload": WORKLOAD, "size": SIZE, "error": start}))
        sys.exit(1)
    if os.environ.get("WAIT_READY") == "1":  # the first stepped frame only once the encoder is up
        ready_by = time.monotonic() + 10
        while call("GET", E + "/video/window/record").get("encoder_ready_ms", 0) <= 0 and time.monotonic() < ready_by:
            time.sleep(0.05)
    t0 = time.monotonic()
    ran = call("POST", E + "/video/window/record/frames", {"frames": FRAMES})
    stepped = time.monotonic() - t0
    t1 = time.monotonic()
    stop = call("POST", E + "/video/window/record/stop")
    print(json.dumps({"workload": WORKLOAD, "size": SIZE, **encoder, "frames": FRAMES, "stepped_s": round(stepped, 2),
                      "stepped_fps": round(FRAMES / stepped, 1), "stop_s": round(time.monotonic() - t1, 2),
                      "width": start.get("width"), "height": start.get("height"),
                      "ran": ran.get("ran"), "error": ran.get("error") or stop.get("error"),
                      "load": os.getloadavg()[0]}))
finally:
    call("POST", E + "/resume")
    call("DELETE", E + "/control/session")
