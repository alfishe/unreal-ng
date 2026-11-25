#!/usr/bin/env python3
"""Window recording through the WebAPI: live.py UNREAL_QT OUTDIR ROUNDS [hardware]

Starts unreal-qt (its own WebAPI port, muted), loads the same snapshot before every file and records 200 stepped
frames per transfer mode into OUTDIR/live-<mode>-<round>.mp4 (h264, 3840x2160 fit, sound on). Per file it prints the
wall time of the 200 frames, ffmpeg's CPU per frame (Linux / macOS), the writer thread's time per sent picture (median,
p90 and the paths taken, from UNREAL_WINDOW_RECORDER_TIMING lines in OUTDIR/app.log) and ffprobe's view of the file.

Environment:
  SNA      snapshot reloaded before every file (default testdata/loaders/sna/7threality.sna)
  SKIP     frames run after the load before recording (300)
  PH       picture_height (0: the window's own height)
  MODES    comma list of transfer modes (picture,nv12)
  ENGINE   ffmpeg or native (ffmpeg)
  PORT     WebAPI port (8731)
  QT_QPA_PLATFORM  default offscreen (Linux without a display: xcb under xvfb-run)
"""
import json, os, re, signal, statistics, subprocess, sys, time, urllib.request

BIN, OUT, ROUNDS = sys.argv[1], os.path.abspath(sys.argv[2]), int(sys.argv[3])
HW = len(sys.argv) > 4 and sys.argv[4] == "hardware"
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
SNA = os.path.abspath(os.environ.get("SNA", os.path.join(REPO, "testdata", "loaders", "sna", "7threality.sna")))
PORT = int(os.environ.get("PORT", "8731"))
B = f"http://127.0.0.1:{PORT}/api/v1"
os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(OUT, "app.log")
env = dict(os.environ, UNREAL_WEBAPI_PORT=str(PORT), UNREAL_WINDOW_RECORDER_TIMING="1")
env.setdefault("QT_QPA_PLATFORM", "offscreen")
app = subprocess.Popen([BIN], env=env, stdout=subprocess.DEVNULL, stderr=open(LOG, "w"))
S = None


def call(method, path, body=None, timeout=600):
    req = urllib.request.Request(B + path, method=method, data=None if body is None else json.dumps(body).encode())
    req.add_header("Content-Type", "application/json")
    if S:
        req.add_header("X-Unreal-Session", S)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        return {"http": e.code, "body": e.read().decode()[:300]}


def cpu(pid):
    if os.path.exists(f"/proc/{pid}/stat"):
        f = open(f"/proc/{pid}/stat").read().rsplit(")", 1)[1].split()
        return (int(f[11]) + int(f[12])) / os.sysconf("SC_CLK_TCK")
    t = subprocess.run(["ps", "-o", "time=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
    sec = 0.0
    for x in t.replace("-", ":").split(":"):
        sec = sec * 60 + float(x)
    return sec


def ffmpeg_pid():
    try:
        out = subprocess.run(["pgrep", "-P", str(app.pid), "ffmpeg"], capture_output=True, text=True).stdout.split()
    except OSError:  # Windows: no pgrep, no ffmpeg CPU figure
        return None
    return int(out[-1]) if out else None


def writer_times(since):
    """The writer's lines written to app.log after offset `since`: (sorted ms of sent pictures, paths seen)"""
    with open(LOG, errors="replace") as f:
        f.seek(since)
        lines = re.findall(r"window-recorder frame (\w+) ([0-9.]+) ms", f.read())
    sent = sorted(float(ms) for how, ms in lines if how != "same")
    return sent, sorted({how for how, _ in lines})


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
    S = call("POST", E + "/control/session", {"client": "recording-live", "surface": "webapi", "lease_seconds": 900}).get("session")
    call("PUT", E + "/audio/mixer/master", {"muted": True})
    call("POST", E + "/pause", {})
    for r in range(ROUNDS):
        for transfer in os.environ.get("MODES", "picture,nv12").split(","):
            call("POST", E + "/snapshot/load", {"path": SNA})
            call("POST", E + "/pause", {})
            call("POST", E + "/run_frames", {"frames": int(os.environ.get("SKIP", "300"))})
            f = os.path.join(OUT, f"live-{transfer}-{r}.mp4")
            body = {"path": f, "codec": "h264", "engine": os.environ.get("ENGINE", "ffmpeg"), "audio": True,
                    "width": 3840, "height": 2160, "fit": True, "picture_height": int(os.environ.get("PH", "0")),
                    "transfer": transfer, "hardware": HW}
            st = call("POST", E + "/video/window/record", body)
            if "http" in st:
                print(transfer, "start failed", st, flush=True)
                continue
            call("POST", E + "/video/window/record/frames", {"frames": 10})
            since = os.path.getsize(LOG)
            pid = ffmpeg_pid()
            c0 = cpu(pid) if pid else None
            t0 = time.time()
            res = call("POST", E + "/video/window/record/frames", {"frames": 200})
            dt = time.time() - t0
            c1 = cpu(pid) if pid else None
            call("POST", E + "/video/window/record/stop", {})
            for _ in range(600):
                g = call("GET", E + "/video/window/record")
                if not g.get("finalizing"):
                    break
                time.sleep(0.1)
            sent, paths = writer_times(since)
            med = f"{statistics.median(sent):6.2f}" if sent else "     ?"
            p90 = f"{sent[int(len(sent) * 0.9)]:6.2f}" if sent else "     ?"
            probe = subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v", "-show_entries",
                                    "stream=width,height,pix_fmt,nb_read_frames", "-of", "csv=p=0", f],
                                   capture_output=True, text=True).stdout.strip()
            ff = f"{(c1 - c0) / 200 * 1000:5.1f}" if c0 is not None and c1 is not None else "  ?"
            print(f"r{r} {transfer:7s} {st.get('width')}x{st.get('height')}: 200 frames {dt:5.2f} s, "
                  f"writer n={len(sent)} med {med} p90 {p90} ms path={','.join(paths) or '?'}, "
                  f"ffmpeg CPU {ff} ms/frame, slowest {res.get('slowest_frame_ms')} ms, file [{probe}] "
                  f"err={g.get('error')!r}", flush=True)
finally:
    app.send_signal(signal.SIGTERM)
    try:
        app.wait(15)
    except Exception:
        app.kill()
