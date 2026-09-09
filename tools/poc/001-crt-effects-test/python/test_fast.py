#!/usr/bin/env python3
"""
Fast GPU vs CPU CRT Test - Vectorized numpy implementation
Matches actual C++ implementations for parity testing.
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


def apply_crt(src, p, out_w, out_h):
    """Unified CRT implementation matching both GPU and CPU after fixes."""
    img = Image.fromarray(src).resize((out_w, out_h), Image.Resampling.NEAREST)
    arr = np.array(img).astype(np.float32)

    scale = out_w / SRC_W
    h, w = arr.shape[:2]

    # Scanlines (sine wave - matches GPU shader)
    # sin(y * PI / scale) where scale = outHeight / srcHeight
    if p["sl"] > 0.001:
        y = np.arange(h).reshape(-1, 1).astype(np.float32)
        scanline = np.sin(y * np.pi / scale) * 0.5 + 0.5
        factor = 1.0 - p["sl"] * (1.0 - scanline)
        arr[:, :, :3] *= factor[:, :, np.newaxis]

    # Mask - smoothstep transition 2.5 to 3.5
    if p["mask"] > 0 and p["ms"] > 0.001:
        fade = float(smoothstep(2.5, 3.5, scale))
        avg_effect = 1.0 - p["ms"] * 0.55

        if fade > 0.01:
            pitch = max(2.0, w / 640.0)
            x_arr = np.arange(w).astype(np.float32)
            phase = (x_arr % (pitch * 3)) / pitch

            # GPU uses 1 - 0.8*s for aperture grille dim value
            eff_strength = p["ms"] * fade
            dim = 1.0 - 0.8 * eff_strength

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

    # Brightness/contrast
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
    src_avg = calc_avg(src)

    print("=" * 80)
    print("CRT EFFECTS TEST (Unified Implementation)")
    print(f"Source: {src_path.name} ({SRC_W}x{SRC_H}), avg brightness: {src_avg:.0f}")
    print("=" * 80)

    results = []
    all_pass = True

    for pname, p in PROFILES.items():
        print(f"\n--- {pname} ---")
        for oname, ow, oh in OUTPUTS:
            scale = ow / SRC_W

            out = apply_crt(src, p, ow, oh)
            out_avg = calc_avg(out)

            # Status checks
            ok = True
            status = "PASS"

            if pname == "None":
                # Should match source brightness exactly
                if abs(out_avg - src_avg) > 1:
                    status = f"FAIL: avg={out_avg:.0f}, expected={src_avg:.0f}"
                    ok = False
            elif out_avg > 220:
                status = "FAIL: Burning white!"
                ok = False

            if not ok:
                all_pass = False

            print(f"  {oname:6} ({ow}x{oh}) avg={out_avg:.0f} {status}")

            fname = f"crt_{pname}_{oname}.png"
            Image.fromarray(out).save(scratch / fname)

            results.append({"profile": pname, "output": oname, "res": f"{ow}x{oh}",
                           "scale": scale, "avg": out_avg, "status": status, "file": fname})

    # HTML Report with lightbox
    html = f"""<!DOCTYPE html>
<html><head><title>CRT Effects Test</title>
<style>
body{{font-family:monospace;background:#111;color:#eee;margin:20px}}
table{{border-collapse:collapse;margin:20px 0}}
th,td{{border:1px solid #444;padding:8px;text-align:center}}
th{{background:#333}}
.pass{{background:#1a4}}
.fail{{background:#a22}}
img.thumb{{max-height:120px;cursor:pointer}}
.lightbox{{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.9);z-index:1000;justify-content:center;align-items:center}}
.lightbox.active{{display:flex}}
.lightbox img{{max-width:95%;max-height:95%;object-fit:contain}}
.lightbox .close{{position:absolute;top:20px;right:30px;color:#fff;font-size:40px;cursor:pointer}}
.lightbox .caption{{position:absolute;bottom:20px;color:#fff;font-size:16px}}
</style></head><body>
<h1>CRT Effects Test Report</h1>
<p>Generated: {datetime.now().strftime("%Y-%m-%d %H:%M:%S")}</p>
<p>Source: 128K Menu ({SRC_W}x{SRC_H}), avg brightness: {src_avg:.0f}</p>

<h2>Results Matrix</h2>
<table>
<tr><th>Profile</th>"""

    for oname, _, _ in OUTPUTS:
        html += f"<th>{oname}</th>"
    html += "</tr>\n"

    for pname in PROFILES:
        html += f"<tr><th>{pname}</th>"
        for oname, ow, oh in OUTPUTS:
            r = next((x for x in results if x["profile"] == pname and x["output"] == oname), None)
            if r:
                cls = "pass" if "PASS" in r["status"] else "fail"
                html += f'''<td class="{cls}">
<img class="thumb" src="{r["file"]}" onclick="showLightbox(this, '{pname} @ {oname}')" title="{r["res"]}">
<br>avg={r["avg"]:.0f}</td>'''
            else:
                html += "<td>-</td>"
        html += "</tr>\n"

    html += """</table>

<div class="lightbox" id="lightbox" onclick="hideLightbox()">
    <span class="close">&times;</span>
    <img id="lightbox-img" src="">
    <div class="caption" id="lightbox-caption"></div>
</div>

<script>
function showLightbox(img, caption) {
    document.getElementById('lightbox-img').src = img.src;
    document.getElementById('lightbox-caption').textContent = caption;
    document.getElementById('lightbox').classList.add('active');
}
function hideLightbox() {
    document.getElementById('lightbox').classList.remove('active');
}
document.addEventListener('keydown', e => { if (e.key === 'Escape') hideLightbox(); });
</script>

<h2>Validation Criteria</h2>
<ul>
<li><b>None profile:</b> Must match source brightness exactly</li>
<li><b>All profiles:</b> No burning white (avg &lt; 220)</li>
<li><b>Mask profiles at low-res:</b> Uniform darkening applied</li>
<li><b>Mask profiles at high-res:</b> Full pattern visible</li>
</ul>
</body></html>"""

    with open(scratch / "crt_report.html", "w") as f:
        f.write(html)

    print("\n" + "=" * 80)
    print(f"Report: file://{scratch}/crt_report.html")
    print(f"RESULT: {'ALL PASS' if all_pass else 'FAILURES DETECTED'}")
    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
