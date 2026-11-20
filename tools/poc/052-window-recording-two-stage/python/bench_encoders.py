#!/usr/bin/env python3
"""POC 052: encode the captured recorder streams with every candidate, interleaved rounds, one JSON line per run.

  bench_encoders.py RAW_DIR ROUNDS [stream-glob] [variant,...]

Stage 1 (s1-*): the stream as the pipe carries it (the picture's own size), lossless or near-lossless.
Live (live-*): today's encode, the recorder's own arguments (its -vf scale / pad), as a running recording does.
Stage 2 (s2-*): the ffv1 intermediate of stage 1 into the final file, with the recorder's filter.
A run's speed is the stream's length over the wall time (x real time) and its frames per second.
"""
import glob
import json
import os
import subprocess
import sys
import time

FFMPEG = os.environ.get("FFMPEG", "ffmpeg")
OUT = None  # RAW_DIR/enc, set by main

STAGE1 = {
    "s1-raw": ["-c:v", "rawvideo"],
    "s1-ffv1": ["-c:v", "ffv1", "-level", "3", "-slices", "16", "-slicecrc", "0", "-g", "50", "-threads", "8"],
    "s1-ffv1-g1": ["-c:v", "ffv1", "-level", "3", "-slices", "16", "-slicecrc", "0", "-g", "1", "-threads", "8"],
    "s1-utvideo": ["-c:v", "utvideo", "-pix_fmt", "gbrp"],
    "s1-ffvhuff": ["-c:v", "ffvhuff", "-pix_fmt", "bgra"],
    "s1-magicyuv": ["-c:v", "magicyuv", "-pix_fmt", "gbrp"],
    "s1-x264rgb-qp0": ["-c:v", "libx264rgb", "-preset", "ultrafast", "-qp", "0", "-pix_fmt", "bgr0"],
    "s1-prores-vt": ["-c:v", "prores_videotoolbox", "-profile:v", "4444"],
}
FINAL = {
    "x264": ["-c:v", "libx264", "-preset", "@x264", "-crf", "12", "-pix_fmt", "yuv444p"],
    "x264-medium": ["-c:v", "libx264", "-preset", "medium", "-crf", "12", "-pix_fmt", "yuv444p"],
    "x265": ["-c:v", "libx265", "-preset", "ultrafast", "-crf", "14", "-pix_fmt", "yuv444p", "-x265-params",
             "log-level=error"],
    "x265-medium": ["-c:v", "libx265", "-preset", "medium", "-crf", "14", "-pix_fmt", "yuv444p", "-x265-params",
                    "log-level=error"],
    "h264-vt": ["-c:v", "h264_videotoolbox", "-q:v", "85", "-pix_fmt", "yuv420p"],
    "hevc-vt": ["-c:v", "hevc_videotoolbox", "-profile:v", "rext", "-q:v", "85", "-pix_fmt", "yuv422p10le",
                "-tag:v", "hvc1"],
}
LIVE = {"live-" + k: v for k, v in FINAL.items() if k in ("x264", "x265", "h264-vt", "hevc-vt")}
STAGE2 = {"s2-" + k: v for k, v in FINAL.items()}


def recorder_args(stream):
    """The recorder's -vf and VFR switches for this stream (from the wrapper's .args file), the x264 preset."""
    lines = open(stream[:-4] + ".args").read().split("\n")
    vf, vfr = [], []
    for i, a in enumerate(lines):
        if a == "-vf":
            vf = ["-vf", lines[i + 1]]
        if a in ("-fps_mode", "-enc_time_base"):
            vfr += [a, lines[i + 1]]
        if a == "-preset" and lines[i - 2] == "libx264":
            preset = lines[i + 1]
    return vf, vfr, locals().get("preset", "veryfast")


_probed = {}


def probe(path):
    """Frames, length (the last frame's time + one 50 Hz frame: the stream has no duration), width, height."""
    if path not in _probed:
        r = subprocess.run([os.environ.get("FFPROBE", "ffprobe"), "-v", "error", "-select_streams", "v:0",
                            "-show_entries", "packet=pts_time:stream=width,height", "-of", "json", path],
                           capture_output=True, text=True)
        j = json.loads(r.stdout)
        times = [float(p["pts_time"]) for p in j["packets"]]
        s = j["streams"][0]
        _probed[path] = (len(times), max(times) - min(times) + 0.02, s["width"], s["height"])
    return _probed[path]


def run(variant, stream, src, args, ext):
    out = os.path.join(OUT, f"{os.path.basename(stream)[:-4]}.{variant}.{ext}")
    t0 = time.monotonic()
    p = subprocess.run([FFMPEG, "-v", "error", "-y", "-i", src, "-an", *args, out], capture_output=True, text=True)
    wall = time.monotonic() - t0
    frames, duration, w, h = probe(stream)
    rec = {"variant": variant, "stream": os.path.basename(stream), "w": w, "h": h, "frames": frames,
           "duration_s": round(duration, 2), "wall_s": round(wall, 2), "fps": round(frames / wall, 1),
           "x_realtime": round(duration / wall, 2), "mb": round(os.path.getsize(out) / 1e6, 1) if p.returncode == 0
           else None, "mb_per_min": round(os.path.getsize(out) / 1e6 / duration * 60) if p.returncode == 0 else None,
           "load": round(os.getloadavg()[0], 1), "error": p.stderr.strip()[-300:] if p.returncode else None}
    print(json.dumps(rec), flush=True)
    return out


def main():
    global OUT
    raw, rounds = sys.argv[1], int(sys.argv[2])
    pattern = sys.argv[3] if len(sys.argv) > 3 else "*.mkv"
    only = set(sys.argv[4].split(",")) if len(sys.argv) > 4 else None
    OUT = os.path.join(raw, "enc")
    os.makedirs(OUT, exist_ok=True)
    streams = sorted(s for s in glob.glob(os.path.join(raw, "streams", pattern)) if not s.endswith((".args",)))
    for rnd in range(rounds):
        for stream in streams:
            vf, vfr, preset = recorder_args(stream)
            for name, args in STAGE1.items():
                if only is None or name in only:
                    run(name, stream, stream, args, "mov" if "prores" in name else "nut" if name == "s1-raw" else "mkv")
            for name, args in LIVE.items():
                if only is None or name in only:
                    run(name, stream, stream, vf + vfr + [preset if a == "@x264" else a for a in args], "mp4")
            inter = os.path.join(OUT, f"{os.path.basename(stream)[:-4]}.s1-ffv1.mkv")
            for name, args in STAGE2.items():
                if (only is None or name in only) and os.path.exists(inter):
                    run(name, stream, inter, vf + vfr + [preset if a == "@x264" else a for a in args], "mp4")


if __name__ == "__main__":
    main()
