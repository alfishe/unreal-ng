#!/usr/bin/env python3
"""
GPU vs CPU CRT Parity Test
Compares GPU shader logic vs CPU filter logic pixel-by-pixel.
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


def apply_gpu(src, p, out_w, out_h):
    """GPU shader logic - matches devicescreen_gl.cpp"""
    img = Image.fromarray(src).resize((out_w, out_h), Image.Resampling.NEAREST)
    arr = np.array(img).astype(np.float32) / 255.0

    scale = out_w / SRC_W
    h, w = arr.shape[:2]

    # Scanlines: sin(y * PI / scale) * 0.5 + 0.5
    if p["sl"] > 0.001:
        y = np.arange(h).reshape(-1, 1).astype(np.float32)
        scanline = np.sin(y * np.pi / scale) * 0.5 + 0.5
        arr[:, :, :3] *= (1.0 - p["sl"] * (1.0 - scanline))[:, :, np.newaxis]

    # Mask with smoothstep transition
    if p["mask"] > 0 and p["ms"] > 0.001:
        fade = float(smoothstep(2.5, 3.5, scale))
        avg_effect = 1.0 - p["ms"] * 0.55

        if fade > 0.01:
            pitch = max(2.0, w / 640.0)
            x_arr = np.arange(w).astype(np.float32)
            phase = (x_arr % (pitch * 3)) / pitch

            # GPU uses mix(1.0, 0.2, s) = 1 - 0.8*s for aperture
            dim = 1.0 - 0.8 * p["ms"]

            mask = np.ones((h, w, 3), dtype=np.float32)
            r_stripe = phase < 1
            g_stripe = (phase >= 1) & (phase < 2)
            b_stripe = phase >= 2

            mask[:, r_stripe, 1] = dim
            mask[:, r_stripe, 2] = dim
            mask[:, g_stripe, 0] = dim
            mask[:, g_stripe, 2] = dim
            mask[:, b_stripe, 0] = dim
            mask[:, b_stripe, 1] = dim

            patterned = arr[:, :, :3] * mask
            uniform = arr[:, :, :3] * avg_effect
            arr[:, :, :3] = uniform + (patterned - uniform) * fade
        else:
            arr[:, :, :3] *= avg_effect

    # Saturation
    if abs(p["st"] - 1.0) > 0.001:
        luma = 0.299 * arr[:,:,0] + 0.587 * arr[:,:,1] + 0.114 * arr[:,:,2]
        arr[:, :, :3] = luma[:, :, np.newaxis] + (arr[:, :, :3] - luma[:, :, np.newaxis]) * p["st"]

    # Contrast then brightness (GPU style: 0-1 range)
    arr[:, :, :3] = (arr[:, :, :3] - 0.5) * p["ct"] + 0.5
    arr[:, :, :3] *= p["br"]

    return np.clip(arr * 255, 0, 255).astype(np.uint8)


def apply_cpu(src, p, out_w, out_h):
    """CPU filter logic - matches crtfilter.cpp"""
    img = Image.fromarray(src).resize((out_w, out_h), Image.Resampling.NEAREST)
    arr = np.array(img).astype(np.float32)

    scale = out_w / SRC_W
    h, w = arr.shape[:2]

    # Scanlines: sin(y * PI / scale) * 0.5 + 0.5
    if p["sl"] > 0.001:
        y = np.arange(h).reshape(-1, 1).astype(np.float32)
        scanline = np.sin(y * np.pi / scale) * 0.5 + 0.5
        factor = 1.0 - p["sl"] * (1.0 - scanline)
        arr[:, :, :3] *= factor[:, :, np.newaxis]

    # Mask with smoothstep transition (must match GPU blending exactly)
    if p["mask"] > 0 and p["ms"] > 0.001:
        t = np.clip((scale - 2.5) / 1.0, 0, 1)
        fade = t * t * (3 - 2 * t)
        avg_effect = 1.0 - p["ms"] * 0.55

        if fade > 0.01:
            pitch = max(2.0, w / 640.0)
            x_arr = np.arange(w).astype(np.float32)
            phase = (x_arr % (pitch * 3)) / pitch

            # GPU uses mix(1.0, 0.2, s) = 1 - 0.8*s for aperture
            dim = 1.0 - 0.8 * p["ms"]

            mask = np.ones((h, w, 3), dtype=np.float32)
            r_stripe = phase < 1
            g_stripe = (phase >= 1) & (phase < 2)
            b_stripe = phase >= 2

            mask[:, r_stripe, 1] = dim
            mask[:, r_stripe, 2] = dim
            mask[:, g_stripe, 0] = dim
            mask[:, g_stripe, 2] = dim
            mask[:, b_stripe, 0] = dim
            mask[:, b_stripe, 1] = dim

            # Blend between uniform and patterned (same as GPU)
            patterned = arr[:, :, :3] * mask
            uniform = arr[:, :, :3] * avg_effect
            arr[:, :, :3] = uniform + (patterned - uniform) * fade
        else:
            arr[:, :, :3] *= avg_effect

    # Saturation
    if abs(p["st"] - 1.0) > 0.001:
        luma = 0.299 * arr[:,:,0] + 0.587 * arr[:,:,1] + 0.114 * arr[:,:,2]
        arr[:, :, :3] = luma[:, :, np.newaxis] + (arr[:, :, :3] - luma[:, :, np.newaxis]) * p["st"]

    # Contrast then brightness (CPU style: 0-255 range)
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

    scratch.mkdir(exist_ok=True)
    src = np.array(Image.open(src_path).convert("RGBA"))

    print("=" * 80)
    print("GPU vs CPU CRT PARITY TEST")
    print("=" * 80)

    results = []
    all_pass = True

    for pname, p in PROFILES.items():
        print(f"\n--- {pname} ---")
        for oname, ow, oh in OUTPUTS:
            scale = ow / SRC_W

            gpu = apply_gpu(src, p, ow, oh)
            cpu = apply_cpu(src, p, ow, oh)

            gpu_avg = calc_avg(gpu)
            cpu_avg = calc_avg(cpu)
            diff = np.abs(gpu.astype(float) - cpu.astype(float))
            max_diff = float(np.max(diff))
            avg_diff = float(np.mean(diff))

            ok = True
            status = "PASS"

            if pname != "None" and (gpu_avg > 220 or cpu_avg > 220):
                status = "FAIL: Burning white!"
                ok = False
            elif max_diff > 30:
                status = f"FAIL: Diff={max_diff:.0f}"
                ok = False

            if not ok:
                all_pass = False

            print(f"  {oname:6} GPU={gpu_avg:.0f} CPU={cpu_avg:.0f} maxDiff={max_diff:.0f} avgDiff={avg_diff:.1f} {status}")

            gf = f"gpu_{pname}_{oname}.png"
            cf = f"cpu_{pname}_{oname}.png"
            Image.fromarray(gpu).save(scratch / gf)
            Image.fromarray(cpu).save(scratch / cf)

            results.append({
                "profile": pname, "output": oname, "res": f"{ow}x{oh}",
                "gpu_avg": gpu_avg, "cpu_avg": cpu_avg,
                "max_diff": max_diff, "avg_diff": avg_diff,
                "status": status, "gpu_file": gf, "cpu_file": cf
            })

    # HTML Report with lightbox
    html = f"""<!DOCTYPE html>
<html><head><title>GPU vs CPU CRT Parity</title>
<style>
*{{box-sizing:border-box}}
body{{font-family:monospace;background:#111;color:#eee;margin:20px}}
table{{border-collapse:collapse;margin:20px 0}}
th,td{{border:1px solid #444;padding:6px;text-align:center;vertical-align:top}}
th{{background:#333}}
.pass{{background:#1a4}}
.fail{{background:#a22}}
.thumb{{width:100px;height:auto;cursor:pointer;display:block;margin:2px auto}}
.pair{{display:flex;gap:4px;justify-content:center}}
.label{{font-size:10px;color:#888}}
#lightbox{{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.95);z-index:1000;justify-content:center;align-items:center;flex-direction:column}}
#lightbox.active{{display:flex}}
#lightbox img{{max-width:90%;max-height:85%;object-fit:contain}}
#lightbox .close{{position:absolute;top:15px;right:25px;color:#fff;font-size:36px;cursor:pointer}}
#lightbox .caption{{color:#fff;font-size:14px;margin-top:10px}}
</style></head><body>
<h1>GPU vs CPU CRT Parity Test</h1>
<p>Generated: {datetime.now().strftime("%Y-%m-%d %H:%M:%S")}</p>
<p>Source: 128K Menu ({SRC_W}x{SRC_H})</p>

<h2>Results</h2>
<table>
<tr><th>Profile</th><th>Res</th><th>GPU Avg</th><th>CPU Avg</th><th>Max Diff</th><th>Status</th><th>GPU</th><th>CPU</th></tr>
"""

    for r in results:
        cls = "pass" if "PASS" in r["status"] else "fail"
        html += f'''<tr class="{cls}">
<td>{r["profile"]}</td><td>{r["output"]}</td>
<td>{r["gpu_avg"]:.0f}</td><td>{r["cpu_avg"]:.0f}</td>
<td>{r["max_diff"]:.0f}</td><td>{r["status"]}</td>
<td><img class="thumb" src="{r["gpu_file"]}" onclick="openLB(this,'{r["profile"]} {r["output"]} GPU')"></td>
<td><img class="thumb" src="{r["cpu_file"]}" onclick="openLB(this,'{r["profile"]} {r["output"]} CPU')"></td>
</tr>
'''

    html += """</table>

<div id="lightbox" onclick="closeLB()">
<span class="close">&times;</span>
<img id="lb-img" src="">
<div class="caption" id="lb-cap"></div>
</div>

<script>
function openLB(img,cap){
  document.getElementById('lb-img').src=img.src;
  document.getElementById('lb-cap').textContent=cap;
  document.getElementById('lightbox').classList.add('active');
}
function closeLB(){document.getElementById('lightbox').classList.remove('active');}
document.addEventListener('keydown',e=>{if(e.key==='Escape')closeLB();});
</script>
</body></html>"""

    with open(scratch / "gpu_vs_cpu_report.html", "w") as f:
        f.write(html)

    print("\n" + "=" * 80)
    print(f"Report: file://{scratch}/gpu_vs_cpu_report.html")
    print(f"RESULT: {'ALL PASS' if all_pass else 'FAILURES DETECTED'}")
    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
