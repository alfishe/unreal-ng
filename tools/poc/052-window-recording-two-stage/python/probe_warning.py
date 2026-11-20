#!/usr/bin/env python3
"""POC 052, goal 3: can a short probe before the start predict the steady encode rate?

  probe_warning.py RAW_DIR ROUNDS [stream-glob] [variant,...]

For each captured stream and live setting, three numbers per round, interleaved:
  steady  - the whole stream encoded with the recorder's arguments (frames / wall s, ffmpeg start included)
  first   - the first PROBE_FRAMES of the same stream (what a calibration at the start would see)
  synth   - PROBE_FRAMES of a synthetic moving picture at the stream's size (no real frames needed before the start)
Each probe is reported with and without ffmpeg's start-up (a second run of 1 frame measures it).
"""
import glob
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import bench_encoders as bench  # noqa: E402  (its module body runs nothing when RAW/ROUNDS come from here)

RAW, ROUNDS = sys.argv[1], int(sys.argv[2])
PATTERN = sys.argv[3] if len(sys.argv) > 3 else "*.mkv"
ONLY = set(sys.argv[4].split(",")) if len(sys.argv) > 4 else None
PROBE_FRAMES = int(os.environ.get("PROBE_FRAMES", "25"))
FFMPEG = os.environ.get("FFMPEG", "ffmpeg")


def timed(args):
    t0 = time.monotonic()
    p = subprocess.run([FFMPEG, "-v", "error", "-y", *args, "-f", "null", "-"], capture_output=True, text=True)
    return time.monotonic() - t0, p.stderr.strip()[-200:] if p.returncode else None


for rnd in range(ROUNDS):
    for stream in sorted(glob.glob(os.path.join(RAW, "streams", PATTERN))):
        vf, vfr, preset = bench.recorder_args(stream)
        frames, duration, w, h = bench.probe(stream)
        for name, args in bench.LIVE.items():
            if ONLY and name not in ONLY:
                continue
            enc = vf + vfr + [preset if a == "@x264" else a for a in args]
            steady, err = timed(["-i", stream, "-an", *enc])
            first, _ = timed(["-i", stream, "-an", "-frames:v", str(PROBE_FRAMES), *enc])
            synth_src = ["-f", "lavfi", "-i", f"testsrc2=size={w}x{h}:rate=50,format=bgra"]
            synth, _ = timed([*synth_src, "-frames:v", str(PROBE_FRAMES), *enc])
            startup, _ = timed([*synth_src, "-frames:v", "1", *enc])
            print(json.dumps({
                "variant": name, "stream": os.path.basename(stream), "w": w, "h": h, "frames": frames,
                "steady_fps": round(frames / steady, 1),
                "first_fps": round(PROBE_FRAMES / first, 1),
                "first_fps_net": round((PROBE_FRAMES - 1) / max(first - startup, 1e-3), 1),
                "synth_fps": round(PROBE_FRAMES / synth, 1),
                "synth_fps_net": round((PROBE_FRAMES - 1) / max(synth - startup, 1e-3), 1),
                "probe_s": round(synth, 2), "startup_s": round(startup, 2),
                "load": round(os.getloadavg()[0], 1), "error": err}), flush=True)
