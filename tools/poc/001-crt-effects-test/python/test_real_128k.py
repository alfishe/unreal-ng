#!/usr/bin/env python3
"""
CRT Effects Test with REAL 128K Menu
Tests all profiles at 1080p and 4K output resolutions.
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

# Source is 352x288
SRC_WIDTH = 352
SRC_HEIGHT = 288

# Output resolutions (maintaining 11:9 aspect ratio)
OUTPUTS = [
    ("1x", 352, 288),
    ("2x", 704, 576),
    ("3x", 1056, 864),
    ("1080p", 1320, 1080),  # 11:9 aspect at 1080 height
    ("4K", 2640, 2160),      # 11:9 aspect at 4K height
]

PROFILES = {
    "None": {"mask_type": 0, "mask_strength": 0.0, "scanline": 0.0, "bright": 1.0, "contrast": 1.0, "sat": 1.0},
    "Basic": {"mask_type": 0, "mask_strength": 0.0, "scanline": 0.3, "bright": 1.0, "contrast": 1.0, "sat": 1.0},
    "Aperture": {"mask_type": 1, "mask_strength": 0.5, "scanline": 0.25, "bright": 1.0, "contrast": 1.0, "sat": 1.1},
    "ShadowMask": {"mask_type": 2, "mask_strength": 0.6, "scanline": 0.2, "bright": 1.0, "contrast": 1.0, "sat": 1.05},
    "SlotMask": {"mask_type": 3, "mask_strength": 0.55, "scanline": 0.35, "bright": 1.1, "contrast": 1.0, "sat": 1.15},
    "Megatron": {"mask_type": 1, "mask_strength": 0.7, "scanline": 0.0, "bright": 1.05, "contrast": 1.1, "sat": 1.2},
}


def apply_scanlines(arr, weight):
    if weight < 0.001:
        return arr
    result = arr.astype(np.float32)
    result[1::2, :, :3] *= (1.0 - weight)
    return np.clip(result, 0, 255).astype(np.uint8)


def apply_aperture_mask(arr, strength, pixel_scale):
    if strength < 0.001:
        return arr
    result = arr.astype(np.float32)
    h, w = result.shape[:2]
    pitch = max(2.0, w / 640.0)
    period = pitch * 3
    inv = 1.0 - strength

    for x in range(w):
        phase = (x % period) / pitch
        if phase < 1.0:
            result[:, x, 0] *= 1.0
            result[:, x, 1] *= inv
            result[:, x, 2] *= inv
        elif phase < 2.0:
            result[:, x, 0] *= inv
            result[:, x, 1] *= 1.0
            result[:, x, 2] *= inv
        else:
            result[:, x, 0] *= inv
            result[:, x, 1] *= inv
            result[:, x, 2] *= 1.0
    return np.clip(result, 0, 255).astype(np.uint8)


def apply_uniform_darkening(arr, mask_strength):
    if mask_strength < 0.001:
        return arr
    result = arr.astype(np.float32)
    factor = 1.0 - mask_strength * 0.55
    result[:, :, :3] *= factor
    return np.clip(result, 0, 255).astype(np.uint8)


def apply_brightness_saturation(arr, brightness, contrast, saturation):
    result = arr.astype(np.float32)

    # Saturation
    if abs(saturation - 1.0) > 0.001:
        luma = 0.299 * result[:,:,0] + 0.587 * result[:,:,1] + 0.114 * result[:,:,2]
        for c in range(3):
            result[:,:,c] = luma + (result[:,:,c] - luma) * saturation

    # Contrast and brightness
    result[:,:,:3] = (result[:,:,:3] - 128) * contrast + 128
    result[:,:,:3] *= brightness

    return np.clip(result, 0, 255).astype(np.uint8)


def apply_crt(img, profile, out_w, out_h):
    """Apply CRT effects - mimics the CPU filter logic."""
    # Scale to output
    scaled = img.resize((out_w, out_h), Image.Resampling.NEAREST)
    arr = np.array(scaled)

    pixel_scale = out_w / SRC_WIDTH
    p = profile

    # Scanlines
    if p["scanline"] > 0:
        arr = apply_scanlines(arr, p["scanline"])

    # Mask (pattern at >= 3.5x, uniform darkening below)
    if p["mask_strength"] > 0 and p["mask_type"] > 0:
        if pixel_scale >= 3.5:
            arr = apply_aperture_mask(arr, p["mask_strength"], pixel_scale)
        else:
            arr = apply_uniform_darkening(arr, p["mask_strength"])

    # Color adjustments
    arr = apply_brightness_saturation(arr, p["bright"], p["contrast"], p["sat"])

    return Image.fromarray(arr)


def calc_stats(img):
    arr = np.array(img).astype(np.float32)
    lum = 0.299 * arr[:,:,0] + 0.587 * arr[:,:,1] + 0.114 * arr[:,:,2]
    return {"avg": float(np.mean(lum)), "min": float(np.min(lum)), "max": float(np.max(lum))}


def main():
    source_path = Path("/Users/dev/Downloads/128k.png")
    if not source_path.exists():
        print(f"Source not found: {source_path}")
        return 1

    scratch = Path("/Volumes/TB4-4Tb/Projects/Test/unreal-ng/scratch")
    scratch.mkdir(exist_ok=True)

    print("=" * 70)
    print("CRT Effects Test - REAL 128K Menu")
    print("=" * 70)

    src = Image.open(source_path).convert("RGBA")
    print(f"Source: {src.size[0]}x{src.size[1]}")

    # Save source
    src.save(scratch / "128k_source.png")

    results = []

    for profile_name, profile in PROFILES.items():
        print(f"\n--- {profile_name} ---")
        for out_name, out_w, out_h in OUTPUTS:
            result = apply_crt(src, profile, out_w, out_h)

            fname = f"cpu_{profile_name}_{out_name}.png"
            result.save(scratch / fname)

            stats = calc_stats(result)
            scale = out_w / SRC_WIDTH

            # Check for burning white
            status = "OK"
            if profile_name != "None" and stats["avg"] > 220:
                status = "FAIL: Burning white!"
            elif profile_name == "Megatron" and scale < 3.5 and stats["avg"] > 150:
                status = "FAIL: Megatron low-res too bright!"

            print(f"  {out_name:6} ({out_w}x{out_h}): avg={stats['avg']:.1f} {status}")
            results.append({
                "profile": profile_name, "output": out_name,
                "resolution": f"{out_w}x{out_h}", "scale": scale,
                "avg": stats["avg"], "status": status, "file": fname
            })

    # Generate HTML report
    html = f"""<!DOCTYPE html>
<html>
<head>
    <title>CRT Test - Real 128K Menu</title>
    <style>
        body {{ font-family: Arial; margin: 20px; background: #222; color: #eee; }}
        h1 {{ color: #fff; }}
        table {{ border-collapse: collapse; margin: 20px 0; }}
        th, td {{ border: 1px solid #555; padding: 8px; text-align: center; }}
        th {{ background: #444; }}
        .ok {{ background: #2a4; }}
        .fail {{ background: #a33; }}
        img {{ max-width: 200px; cursor: pointer; }}
        img:hover {{ max-width: 400px; }}
        .source {{ max-width: 352px; border: 2px solid #0ff; }}
    </style>
</head>
<body>
    <h1>CRT Effects Test Report</h1>
    <p>Generated: {datetime.now().strftime("%Y-%m-%d %H:%M:%S")}</p>
    <p>Source: 128K Menu (352x288)</p>

    <h2>Source Image</h2>
    <img src="128k_source.png" class="source">

    <h2>Results Matrix</h2>
    <table>
        <tr><th>Profile</th>"""

    for out_name, _, _ in OUTPUTS:
        html += f"<th>{out_name}</th>"
    html += "</tr>\n"

    for profile_name in PROFILES:
        html += f"<tr><th>{profile_name}</th>"
        for out_name, out_w, out_h in OUTPUTS:
            r = next((x for x in results if x["profile"] == profile_name and x["output"] == out_name), None)
            if r:
                cls = "ok" if "OK" in r["status"] else "fail"
                html += f'<td class="{cls}"><img src="{r["file"]}" title="{r["resolution"]}"><br>avg={r["avg"]:.1f}</td>'
            else:
                html += "<td>-</td>"
        html += "</tr>\n"

    html += """</table>

    <h2>Key Validation</h2>
    <ul>
        <li><b>Megatron at 1x/2x/3x:</b> Should show ~100-120 avg brightness (NOT burning white >200)</li>
        <li><b>Megatron at 4K:</b> Should show mask pattern with ~90-100 avg brightness</li>
        <li><b>None profile:</b> Should match source brightness</li>
    </ul>
</body>
</html>"""

    with open(scratch / "report_128k.html", "w") as f:
        f.write(html)

    print("\n" + "=" * 70)
    print(f"Report: file://{scratch}/report_128k.html")
    print("=" * 70)

    fails = [r for r in results if "FAIL" in r["status"]]
    if fails:
        print(f"\nFAILURES: {len(fails)}")
        for f in fails:
            print(f"  - {f['profile']} @ {f['output']}: {f['status']}")
        return 1

    print("\nAll tests PASSED!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
