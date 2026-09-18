#!/usr/bin/env python3
"""
Fast GPU vs CPU CRT Test - Vectorized numpy implementation
"""

import sys
from pathlib import Path
from datetime import datetime

try:
    from PIL import Image
    import numpy as np
except ImportError:
    print("pip install pillow numpy")
    sys.exit(1)

SRC_W, SRC_H = 352, 288

OUTPUTS = [("1x", 352, 288), ("2x", 704, 576), ("3x", 1056, 864), ("1080p", 1320, 1080), ("4K", 2640, 2160)]

PROFILES = {
    "None":       {"mask": 0, "ms": 0.0,  "sl": 0.0,  "br": 1.0,  "ct": 1.0, "st": 1.0},
    "Basic":      {"mask": 0, "ms": 0.0,  "sl": 0.3,  "br": 1.0,  "ct": 1.0, "st": 1.0},
    "Aperture":   {"mask": 1, "ms": 0.5,  "sl": 0.25, "br": 1.0,  "ct": 1.0, "st": 1.1},
    "ShadowMask": {"mask": 2, "ms": 0.6,  "sl": 0.2,  "br": 1.0,  "ct": 1.0, "st": 1.05},
    "SlotMask":   {"mask": 3, "ms": 0.55, "sl": 0.35, "br": 1.1,  "ct": 1.0, "st": 1.15},
    "Megatron":   {"mask": 1, "ms": 0.7,  "sl": 0.0,  "br": 1.05, "ct": 1.1, "st": 1.2},
}


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def apply_gpu_crt(src, p, out_w, out_h):
    """GPU shader logic - vectorized"""
    # Scale
    img = Image.fromarray(src).resize((out_w, out_h), Image.Resampling.NEAREST)
    arr = np.array(img).astype(np.float32) / 255.0

    scale = out_w / SRC_W
    h, w = arr.shape[:2]

    # Scanlines (sine wave)
    if p["sl"] > 0.001:
        y = np.arange(h).reshape(-1, 1)
        scanline = np.sin(y * np.pi / (h / SRC_H)) * 0.5 + 0.5
        arr[:, :, :3] *= (1.0 - p["sl"] * (1.0 - scanline))[:, :, np.newaxis]

    # Mask
    if p["mask"] > 0 and p["ms"] > 0.001:
        fade = smoothstep(2.5, 3.5, scale)
        avg_effect = 1.0 - p["ms"] * 0.55

        if fade > 0.01:
            pitch = max(2.0, w / 640.0)
            x = np.arange(w)
            phase = (x % (pitch * 3)) / pitch

            mask = np.ones((h, w, 3), dtype=np.float32)
            inv = 1.0 - p["ms"]

            r_stripe = phase < 1
            g_stripe = (phase >= 1) & (phase < 2)
            b_stripe = phase >= 2

            mask[:, r_stripe, 1] = inv
            mask[:, r_stripe, 2] = inv
            mask[:, g_stripe, 0] = inv
            mask[:, g_stripe, 2] = inv
            mask[:, b_stripe, 0] = inv
            mask[:, b_stripe, 1] = inv

            patterned = arr[:, :, :3] * mask
            uniform = arr[:, :, :3] * avg_effect
            arr[:, :, :3] = uniform + (patterned - uniform) * fade
        else:
            arr[:, :, :3] *= avg_effect

    # Saturation
    if abs(p["st"] - 1.0) > 0.001:
        luma = 0.299 * arr[:,:,0] + 0.587 * arr[:,:,1] + 0.114 * arr[:,:,2]
        arr[:, :, :3] = luma[:, :, np.newaxis] + (arr[:, :, :3] - luma[:, :, np.newaxis]) * p["st"]

    # Brightness/contrast
    arr[:, :, :3] = (arr[:, :, :3] - 0.5) * p["ct"] + 0.5
    arr[:, :, :3] *= p["br"]

    return np.clip(arr * 255, 0, 255).astype(np.uint8)


def apply_cpu_crt(src, p, out_w, out_h):
    """CPU filter logic - vectorized"""
    img = Image.fromarray(src).resize((out_w, out_h), Image.Resampling.NEAREST)
    arr = np.array(img).astype(np.float32)

    scale = out_w / SRC_W
    h, w = arr.shape[:2]

    # Scanlines (every other line)
    if p["sl"] > 0.001:
        arr[1::2, :, :3] *= (1.0 - p["sl"])

    # Mask
    if p["mask"] > 0 and p["ms"] > 0.001:
        avg_effect = 1.0 - p["ms"] * 0.55

        if scale >= 3.5:
            pitch = max(2.0, w / 640.0)
            x = np.arange(w)
            phase = (x % (pitch * 3)) / pitch

            eff = p["ms"] * min(1.0, max(0.0, scale - 2.0))
            inv = 1.0 - eff

            r_stripe = phase < 1
            g_stripe = (phase >= 1) & (phase < 2)
            b_stripe = phase >= 2

            arr[:, r_stripe, 1] *= inv
            arr[:, r_stripe, 2] *= inv
            arr[:, g_stripe, 0] *= inv
            arr[:, g_stripe, 2] *= inv
            arr[:, b_stripe, 0] *= inv
            arr[:, b_stripe, 1] *= inv
        else:
            arr[:, :, :3] *= avg_effect

    # Saturation
    if abs(p["st"] - 1.0) > 0.001:
        luma = 0.299 * arr[:,:,0] + 0.587 * arr[:,:,1] + 0.114 * arr[:,:,2]
        arr[:, :, :3] = luma[:, :, np.newaxis] + (arr[:, :, :3] - luma[:, :, np.newaxis]) * p["st"]

    # Brightness/contrast
    if abs(p["br"] - 1.0) > 0.001 or abs(p["ct"] - 1.0) > 0.001:
        arr[:, :, :3] = (arr[:, :, :3] - 128) * p["ct"] + 128
        arr[:, :, :3] *= p["br"]

    return np.clip(arr, 0, 255).astype(np.uint8)


def calc_avg(arr):
    lum = 0.299 * arr[:,:,0] + 0.587 * arr[:,:,1] + 0.114 * arr[:,:,2]
    return float(np.mean(lum))


def main():
    scratch = Path("/Volumes/TB4-4Tb/Projects/Test/unreal-ng/scratch")
    src_path = Path("/Users/dev/Downloads/128k.png")

    if not src_path.exists():
        print(f"Source not found: {src_path}")
        return 1

    src = np.array(Image.open(src_path).convert("RGBA"))
    print("=" * 80)
    print("GPU vs CPU CRT PARITY TEST (Fast)")
    print("=" * 80)

    results = []
    all_pass = True

    for pname, p in PROFILES.items():
        print(f"\n--- {pname} ---")
        for oname, ow, oh in OUTPUTS:
            scale = ow / SRC_W

            gpu = apply_gpu_crt(src, p, ow, oh)
            cpu = apply_cpu_crt(src, p, ow, oh)

            gpu_avg = calc_avg(gpu)
            cpu_avg = calc_avg(cpu)
            diff = np.abs(gpu.astype(float) - cpu.astype(float))
            max_diff = float(np.max(diff))
            avg_diff = float(np.mean(diff))

            # Status
            ok = True
            status = "PASS"

            if pname != "None" and (gpu_avg > 220 or cpu_avg > 220):
                status = "FAIL: Burning white!"
                ok = False
            elif max_diff > 50:
                status = f"FAIL: Diff too high ({max_diff:.0f})"
                ok = False

            if not ok:
                all_pass = False

            print(f"  {oname:6} GPU={gpu_avg:.0f} CPU={cpu_avg:.0f} diff={avg_diff:.1f} {status}")

            # Save
            Image.fromarray(gpu).save(scratch / f"gpu_{pname}_{oname}.png")
            Image.fromarray(cpu).save(scratch / f"cpu_{pname}_{oname}.png")

            results.append({"profile": pname, "output": oname, "gpu": gpu_avg, "cpu": cpu_avg,
                           "diff": avg_diff, "max_diff": max_diff, "status": status})

    # HTML Report
    html = f"""<!DOCTYPE html>
<html><head><title>GPU vs CPU Test</title>
<style>
body{{font-family:monospace;background:#111;color:#eee;margin:20px}}
table{{border-collapse:collapse;margin:20px 0}}
th,td{{border:1px solid #444;padding:8px;text-align:center}}
th{{background:#333}}
.pass{{background:#1a4}}
.fail{{background:#a22}}
img{{max-height:150px;cursor:pointer}}
img:hover{{max-height:300px}}
</style></head><body>
<h1>GPU vs CPU CRT Parity Test</h1>
<p>Generated: {datetime.now()}</p>
<h2>Results</h2>
<table>
<tr><th>Profile</th><th>Res</th><th>GPU Avg</th><th>CPU Avg</th><th>Diff</th><th>Status</th><th>GPU</th><th>CPU</th></tr>
"""
    for r in results:
        cls = "pass" if "PASS" in r["status"] else "fail"
        gf = f"gpu_{r['profile']}_{r['output']}.png"
        cf = f"cpu_{r['profile']}_{r['output']}.png"
        html += f'<tr class="{cls}"><td>{r["profile"]}</td><td>{r["output"]}</td>'
        html += f'<td>{r["gpu"]:.0f}</td><td>{r["cpu"]:.0f}</td><td>{r["diff"]:.1f}</td>'
        html += f'<td>{r["status"]}</td>'
        html += f'<td><img src="{gf}"></td><td><img src="{cf}"></td></tr>\n'

    html += "</table></body></html>"

    with open(scratch / "gpu_vs_cpu_report.html", "w") as f:
        f.write(html)

    print("\n" + "=" * 80)
    print(f"Report: file://{scratch}/gpu_vs_cpu_report.html")
    print(f"RESULT: {'ALL PASS' if all_pass else 'FAILURES DETECTED'}")
    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
