#!/usr/bin/env python3
"""Recording colors: are the colors a player shows the emulator's?

  colors.py UNREAL_QT OUTDIR [WAY ...]   record and check
  colors.py --analyze OUTDIR             check files recorded earlier (on a machine without ffmpeg: Windows)

Puts the 16 ZX colors on the screen as vertical bands (paper only, black border, the CPU parked in DI; HALT) and
records a few frames per way:

  window:ENGINE:CODEC:TRANSFER[:hw]  the window recorder (Record > Window...), e.g. window:native:h264:picture:hw,
                                     window:ffmpeg:h264:nv12; the reference is the window's picture (window.png)
  core:FORMAT:CONTAINER:ACCELERATION the emulator's frame recording (POST /video/record, profile 1080p), e.g.
                                     core:h264:mp4:hardware (VideoToolbox / NVENC / VA-API), core:h265:mkv:software;
                                     the reference is the emulator's frame (frame.png)

Default ways: the window recorder's native h264 (Mac), ffmpeg h264 picture and nv12, ffmpeg h265; core h264 mp4 auto.
Each file is read back as stored (Y'CbCr, no conversion). Per file: the color tags (matrix, range, primaries,
transfer), per color the stored Y'CbCr and the worst RGB error of four decodings:

  tags    by the file's tags (what a player that honors them shows; untagged = BT.709 limited, HD players' guess)
  601/709 as BT.601 / BT.709 limited range: which matrix the encoder really used
  full    the limited data as full range: what washed-out greys and colors would mean

The errors are before any display transfer function. On a Mac one more line shows what macOS puts on screen
(macshow.swift: AVFoundation decodes by the tags, ColorSync converts to sRGB, as QuickTime, Safari and previews do):
the worst error and the mean shift (> 0 = lighter); a BT.709 transfer tag lifts mid tones there.

Analysis needs ffmpeg / ffprobe on PATH and Pillow; without ffprobe the files are only recorded. Environment: PORT
(8731), QT_QPA_PLATFORM (offscreen).
"""
import glob, json, os, shutil, signal, subprocess, sys, time, urllib.request

from PIL import Image

PORT = int(os.environ.get("PORT", "8731"))
B = f"http://127.0.0.1:{PORT}/api/v1"
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
MACSHOW = os.path.join(REPO, "scratch", "recording-colors-tools", "macshow")
NAMES = ["black", "blue", "red", "magenta", "green", "cyan", "yellow", "white"]
NAMES = NAMES + ["bright " + n for n in NAMES]
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


def bands(row, distance):
    """The 16 bands' values along a row through the paper: runs of near-equal pixels; bands 1-15 are the first 15
    runs of one width (band 8 is black), band 0 is the same width left of band 1 (it joins the black border)"""
    runs, start = [], 0
    for x in range(1, len(row) + 1):
        if x == len(row) or distance(row[x], row[start]) > 12:
            runs.append((start, x))
            start = x
    runs = [(s, e) for s, e in runs if e - s >= max(6, len(row) // 150)]  # not the scalers' edge pixels
    for i in range(1, len(runs) - 14):
        width = runs[i][1] - runs[i][0]
        if all(abs((e - s) - width) <= max(2, width // 10) for s, e in runs[i:i + 15]):
            picked = [(runs[i][0] - width, runs[i][0])] + runs[i:i + 15]
            break
    else:
        raise RuntimeError(f"no 15 runs of one width among {[e - s for s, e in runs]}")
    out = []
    for s, e in picked:
        mid = row[s + (e - s) // 4: e - (e - s) // 4]
        out.append(tuple(sorted(p[i] for p in mid)[len(mid) // 2] for i in range(3)))
    return out


def rgb_distance(a, b):
    return max(abs(x - y) for x, y in zip(a, b))


MATRICES = {"601": (0.299, 0.114), "709": (0.2126, 0.0722), "2020": (0.2627, 0.0593)}


def to_rgb(yuv, matrix, full):
    kr, kb = MATRICES[matrix]
    kg = 1 - kr - kb
    y, u, v = yuv
    if full:
        yn, un, vn = y / 255, (u - 128) / 255, (v - 128) / 255
    else:
        yn, un, vn = (y - 16) / 219, (u - 128) / 224, (v - 128) / 224
    r = yn + 2 * (1 - kr) * vn
    b = yn + 2 * (1 - kb) * un
    g = (yn - kr * r - kb * b) / kg
    return tuple(max(0, min(255, round(c * 255))) for c in (r, g, b))


def probe(f):
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v", "-show_entries",
                          "stream=codec_name,pix_fmt,width,height,color_range,color_space,color_primaries,color_transfer",
                          "-of", "json", f], capture_output=True, text=True).stdout
    return json.loads(out)["streams"][0]


def stored_frame(f, info):
    """The first frame as stored (an unchanged picture is not sent again: the file may hold one): Y'CbCr 4:4:4, the
    values untouched (the range kept, no matrix)"""
    rng = "full" if info.get("color_range") == "pc" else "limited"
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", f, "-vf", f"scale=in_range={rng}:out_range={rng}",
                          "-frames:v", "1", "-pix_fmt", "yuv444p", "-f", "rawvideo", "-"], capture_output=True).stdout
    w, h = info["width"], info["height"]
    if len(raw) != w * h * 3:
        raise RuntimeError(f"decoded {len(raw)} bytes, {w * h * 3} expected")
    row = h // 2
    return [(raw[row * w + x], raw[w * h + row * w + x], raw[2 * w * h + row * w + x]) for x in range(w)]


def mac_view(f):
    """The middle row as macOS shows the file (sRGB), or None off a Mac / without swiftc"""
    if sys.platform != "darwin":
        return None
    source = os.path.join(os.path.dirname(os.path.abspath(__file__)), "macshow.swift")
    if not os.path.exists(MACSHOW) or os.path.getmtime(MACSHOW) < os.path.getmtime(source):
        os.makedirs(os.path.dirname(MACSHOW), exist_ok=True)
        if subprocess.run(["swiftc", "-O", source, "-o", MACSHOW], capture_output=True).returncode:
            return None
    out = subprocess.run([MACSHOW, f], capture_output=True, text=True).stdout.split()
    if out[1:2] == ["no"]:  # AVFoundation does not open it (Matroska): no Mac player shows it either
        return None
    return [tuple(int(p[i:i + 2], 16) for i in (0, 2, 4)) for p in out[1:]] or None


def reference_of(path):
    image = Image.open(path).convert("RGB")
    return bands([image.getpixel((x, image.height // 2)) for x in range(image.width)], rgb_distance)


def analyze(f, reference, note=""):
    info = probe(f)
    tags = {k: info.get(k, "unknown") for k in ("color_space", "color_range", "color_primaries", "color_transfer")}
    print(f"\n{os.path.basename(f)}{note}: {info['codec_name']} {info['pix_fmt']} {info['width']}x{info['height']} "
          + " ".join(f"{k.split('_')[1]}={v}" for k, v in tags.items()), flush=True)
    try:
        stored = bands(stored_frame(f, info), rgb_distance)
    except RuntimeError as e:
        print("  ", e, flush=True)
        return
    full = tags["color_range"] == "pc"
    tagged = {"bt470bg": "601", "smpte170m": "601", "bt709": "709", "bt2020nc": "2020"}.get(tags["color_space"], "709")
    decodings = {"tags": (tagged, full), "601": ("601", False), "709": ("709", False), "full": (tagged, True)}
    worst = {k: 0 for k in decodings}
    print("   color           ref     stored Y'CbCr  " + "  ".join(f"{k:>6s}" for k in decodings), flush=True)
    for name, ref, yuv in zip(NAMES, reference, stored):
        cells = []
        for k, (matrix, rng) in decodings.items():
            d = rgb_distance(to_rgb(yuv, matrix, rng), ref)
            worst[k] = max(worst[k], d)
            cells.append(f"{d:6d}")
        print(f"   {name:15s} {'%02X%02X%02X' % ref}  {yuv[0]:3d} {yuv[1]:3d} {yuv[2]:3d}    " + "  ".join(cells), flush=True)
    print("   worst RGB error: " + ", ".join(f"{k} {v}" for k, v in worst.items()), flush=True)
    shown = mac_view(f)
    if shown:
        try:
            mac = bands(shown, rgb_distance)
        except RuntimeError as e:
            print("   macOS shows:", e, flush=True)
            return
        bias = sum(sum(m) - sum(r) for m, r in zip(mac, reference)) / (len(mac) * 3)
        print(f"   macOS shows: worst {max(rgb_distance(m, r) for m, r in zip(mac, reference))}, mean shift {bias:+.1f}"
              f" ({' '.join('%02X%02X%02X' % c for c in mac[1:8])})", flush=True)


def analyze_dir(out):
    refs = {"window": reference_of(os.path.join(out, "window.png")), "core": reference_of(os.path.join(out, "frame.png"))}
    for kind, ref in refs.items():
        print(f"{kind} reference:", " ".join("%02X%02X%02X" % c for c in ref), flush=True)
    for f in sorted(glob.glob(os.path.join(out, "colors-*"))):
        analyze(f, refs["core" if os.path.basename(f).startswith("colors-core-") else "window"])


def wait_window_record(E):
    for _ in range(600):
        if not call("GET", E + "/video/window/record").get("finalizing"):
            break
        time.sleep(0.1)


def record(binary, out, ways):
    global S
    env = dict(os.environ, UNREAL_WEBAPI_PORT=str(PORT), UNREAL_QT_VIEW_SETTINGS="off")
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    app = subprocess.Popen([binary], env=env, stdout=subprocess.DEVNULL, stderr=open(os.path.join(out, "app.log"), "w"))
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
        S = call("POST", E + "/control/session", {"client": "recording-colors", "surface": "webapi", "lease_seconds": 900}).get("session")
        call("PUT", E + "/audio/mixer/master", {"muted": True})
        call("POST", E + "/pause", {})
        call("POST", E + "/run_frames", {"frames": 100})
        # Paper only, color = column / 2 (8..15 bright); the CPU parked so nothing redraws; black border
        attributes = [((c // 2) & 7) << 3 | (0x40 if c >= 16 else 0) for _ in range(24) for c in range(32)]
        call("POST", E + "/memory/write", {"address": "0x4000", "data": [0] * 6144 + attributes})
        call("POST", E + "/memory/write", {"address": "0x8000", "data": [0xF3, 0x76]})
        call("PUT", E + "/registers/pc", {"value": 0x8000})
        call("POST", E + "/ports/out", {"port": "0xFE", "value": "0x00"})
        call("POST", E + "/run_frames", {"frames": 3})
        call("GET", E + f"/capture/screen?source=window&path={os.path.join(out, 'window.png')}")
        call("GET", E + f"/capture/screen?format=png&path={os.path.join(out, 'frame.png')}")
        for way in ways:
            kind, *rest = way.split(":")
            if kind == "core":
                fmt, container, acceleration = rest
                f = os.path.join(out, f"colors-core-{fmt}-{acceleration}.{container}")
                st = call("POST", E + "/video/record", {"action": "start", "format": fmt, "profile": "1080p",
                                                        "acceleration": acceleration, "filename": f})
                if "http" in st:
                    print(way, "start failed", st, flush=True)
                    continue
                call("POST", E + "/run_frames", {"frames": 12})
                call("POST", E + "/video/record", {"action": "stop"})
                note = f" (core, {st.get('acceleration')})"
            else:
                engine, codec, transfer, *hw = rest
                f = os.path.join(out, f"colors-window-{engine}-{codec}-{transfer}{'-hw' if hw else ''}."
                                      f"{'mov' if codec == 'prores' else 'mp4'}")
                body = {"path": f, "codec": codec, "engine": engine, "audio": False, "width": 3840, "height": 2160,
                        "fit": True, "transfer": transfer, "hardware": bool(hw)}
                st = call("POST", E + "/video/window/record", body)
                if "http" in st:
                    print(way, "start failed", st, flush=True)
                    continue
                call("POST", E + "/video/window/record/frames", {"frames": 12})
                call("POST", E + "/video/window/record/stop", {})
                wait_window_record(E)
                note = f" (window, written by {st.get('transfer')})"
            if not os.path.exists(f) or os.path.getsize(f) < 1000:
                print(f"\n{way}: no file, see app.log", flush=True)
                continue
            print(f"recorded {os.path.basename(f)}{note}", flush=True)
    finally:
        app.send_signal(signal.SIGTERM)
        try:
            app.wait(15)
        except Exception:
            app.kill()


if sys.argv[1] == "--analyze":
    analyze_dir(os.path.abspath(sys.argv[2]))
else:
    OUT = os.path.abspath(sys.argv[2])
    os.makedirs(OUT, exist_ok=True)
    for old in glob.glob(os.path.join(OUT, "colors-*")):
        os.remove(old)
    WAYS = sys.argv[3:] or (["window:native:h264:picture:hw"] if sys.platform == "darwin" else []) + [
        "window:ffmpeg:h264:picture", "window:ffmpeg:h264:nv12", "window:ffmpeg:h265:picture", "core:h264:mp4:auto"]
    record(sys.argv[1], OUT, WAYS)
    if shutil.which("ffprobe"):
        analyze_dir(OUT)
    else:
        print(f"no ffprobe here: copy {OUT} to a machine with ffmpeg, colors.py --analyze DIR", flush=True)
