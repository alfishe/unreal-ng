#!/usr/bin/env python3
"""Demo videos through the window recorder: demo.py UNREAL_QT OUTDIR SECONDS SNAPSHOT[,SNAPSHOT..] [WAY ...]

For every snapshot and way (default ffmpeg:h264:picture): loads the snapshot, runs SKIP frames (100), records SECONDS
of stepped frames (50 a second, sound on) at WIDTH x HEIGHT (3840 x 2160) fit into
OUTDIR/<snapshot>-<engine>-<codec>-<transfer>[-hw][-TAG].mp4, prints the file and its color tags. Stepped frames make
the files the same frames on every run and machine, whatever the machine's speed.

Environment: SKIP, WIDTH, HEIGHT, PH (picture_height, 0 = the window's), TAG (a suffix for the names, e.g. `before`),
PORT (8731), QT_QPA_PLATFORM (offscreen; on Linux xcb under xvfb-run). The sound reaches no speaker: a windowless
session plays zeros.
"""
import json, os, signal, subprocess, sys, time, urllib.request

BIN, OUT, SECONDS, SNAS = sys.argv[1], os.path.abspath(sys.argv[2]), float(sys.argv[3]), sys.argv[4].split(",")
WAYS = sys.argv[5:] or ["ffmpeg:h264:picture"]
PORT = int(os.environ.get("PORT", "8731"))
B = f"http://127.0.0.1:{PORT}/api/v1"
os.makedirs(OUT, exist_ok=True)
env = dict(os.environ, UNREAL_WEBAPI_PORT=str(PORT), UNREAL_QT_VIEW_SETTINGS="off")
env.setdefault("QT_QPA_PLATFORM", "offscreen")
app = subprocess.Popen([BIN], env=env, stdout=subprocess.DEVNULL, stderr=open(os.path.join(OUT, "app.log"), "w"))
S = None


def call(method, path, body=None, timeout=1800):
    req = urllib.request.Request(B + path, method=method, data=None if body is None else json.dumps(body).encode())
    req.add_header("Content-Type", "application/json")
    if S:
        req.add_header("X-Unreal-Session", S)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        return {"http": e.code, "body": e.read().decode()[:300]}


try:
    for _ in range(150):
        try:
            urllib.request.urlopen(B + "/emulator", timeout=1)
            break
        except Exception:
            time.sleep(0.2)
    if not call("GET", "/emulator").get("emulators"):
        call("POST", "/emulator/start", {"model": "PENTAGON"})
        time.sleep(1)
    eid = call("GET", "/emulator")["emulators"][0]["id"]
    E = f"/emulator/{eid}"
    S = call("POST", E + "/control/session", {"client": "recording-demo", "surface": "webapi", "lease_seconds": 3600}).get("session")
    call("POST", E + "/pause", {})
    tag = os.environ.get("TAG", "")
    for sna in SNAS:
        name = os.path.splitext(os.path.basename(sna))[0].replace(" ", "-")
        for way in WAYS:
            engine, codec, transfer, *hw = way.split(":")
            f = os.path.join(OUT, f"{name}-{engine}-{codec}-{transfer}{'-hw' if hw else ''}{'-' + tag if tag else ''}.mp4")
            loaded = call("POST", E + "/snapshot/load", {"path": os.path.abspath(sna)})
            if "http" in loaded or loaded.get("success") is False:
                print(name, "load failed", loaded, flush=True)
                break
            call("POST", E + "/pause", {})
            call("POST", E + "/run_frames", {"frames": int(os.environ.get("SKIP", "100"))})
            body = {"path": f, "codec": codec, "engine": engine, "audio": True, "fit": True,
                    "width": int(os.environ.get("WIDTH", "3840")), "height": int(os.environ.get("HEIGHT", "2160")),
                    "picture_height": int(os.environ.get("PH", "0")), "transfer": transfer, "hardware": bool(hw)}
            st = call("POST", E + "/video/window/record", body)
            if "http" in st:
                print(way, "start failed", st, flush=True)
                continue
            t0 = time.time()
            call("POST", E + "/video/window/record/frames", {"frames": int(SECONDS * 50)})
            call("POST", E + "/video/window/record/stop", {})
            for _ in range(3000):
                g = call("GET", E + "/video/window/record")
                if not g.get("finalizing"):
                    break
                time.sleep(0.1)
            probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v", "-show_entries",
                                    "stream=codec_name,pix_fmt,width,height,color_space,color_range,color_primaries,color_transfer",
                                    "-of", "compact=p=0:nk=0", f], capture_output=True, text=True).stdout.strip()
            print(f"{os.path.basename(f)}: {time.time() - t0:.0f} s, written by {st.get('transfer')}, [{probe}]"
                  f" err={g.get('error')!r}", flush=True)
finally:
    app.send_signal(signal.SIGTERM)
    try:
        app.wait(15)
    except Exception:
        app.kill()
