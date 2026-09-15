#!/usr/bin/env python3
"""
CRT Effects Test Report Generator
Generates visual comparison matrix of all CRT profiles at multiple resolutions.
Outputs to scratch/ directory with HTML report.
"""

import os
import sys
import json
import math
from pathlib import Path
from dataclasses import dataclass
from typing import List, Tuple, Optional
from datetime import datetime

try:
    from PIL import Image, ImageDraw, ImageFont
    import numpy as np
    HAS_DEPS = True
except ImportError:
    HAS_DEPS = False
    print("ERROR: Required dependencies not found.")
    print("Install with: pip install pillow numpy")
    sys.exit(1)

# Configuration
SRC_WIDTH = 352
SRC_HEIGHT = 288

@dataclass
class ProfileConfig:
    name: str
    mask_type: int  # 0=None, 1=Aperture, 2=Shadow, 3=Slot
    mask_strength: float
    scanline_weight: float
    brightness: float
    contrast: float
    saturation: float
    bloom_strength: float

PROFILES = [
    ProfileConfig("None", 0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0),
    ProfileConfig("Basic", 0, 0.0, 0.3, 1.0, 1.0, 1.0, 0.1),
    ProfileConfig("Aperture", 1, 0.5, 0.25, 1.0, 1.0, 1.1, 0.15),
    ProfileConfig("ShadowMask", 2, 0.6, 0.2, 1.0, 1.0, 1.05, 0.12),
    ProfileConfig("SlotMask", 3, 0.55, 0.35, 1.1, 1.0, 1.15, 0.18),
    ProfileConfig("Megatron", 1, 0.7, 0.0, 1.05, 1.1, 1.2, 0.25),
]

SCALES = [
    ("1x", 1.0),
    ("2x", 2.0),
    ("3x", 3.0),
    ("4x", 4.0),
]


def generate_zx_screen() -> Image.Image:
    """Generate a ZX Spectrum-like test screen."""
    img = Image.new('RGB', (SRC_WIDTH, SRC_HEIGHT), (192, 192, 192))
    draw = ImageDraw.Draw(img)

    # Menu box
    box_x, box_y = 120, 70
    box_w, box_h = 160, 130

    # Black border
    draw.rectangle([box_x, box_y, box_x + box_w, box_y + box_h],
                   fill=(0, 0, 0))
    # Cyan interior
    draw.rectangle([box_x + 2, box_y + 2, box_x + box_w - 2, box_y + box_h - 2],
                   fill=(0, 255, 255))

    # Rainbow stripe
    colors = [(0,0,0), (255,0,0), (255,255,0), (0,255,0),
              (0,255,255), (0,0,255), (255,0,255), (255,255,255)]
    stripe_w = box_w // len(colors)
    for i, color in enumerate(colors):
        x1 = box_x + 2 + i * stripe_w
        draw.rectangle([x1, box_y + 6, x1 + stripe_w - 1, box_y + 14], fill=color)

    # Text simulation (simple rectangles for "text")
    text_y = box_y + 24
    for i in range(6):
        text_w = 80 + (i % 3) * 20
        draw.rectangle([box_x + 8, text_y, box_x + 8 + text_w, text_y + 10],
                       fill=(0, 0, 0))
        text_y += 16

    # Copyright at bottom
    draw.rectangle([70, SRC_HEIGHT - 30, 280, SRC_HEIGHT - 18], fill=(0, 0, 0))

    return img


def apply_scanlines(img: np.ndarray, weight: float) -> np.ndarray:
    """Apply scanline effect (darken every other line)."""
    if weight < 0.001:
        return img

    result = img.copy().astype(np.float32)
    dark_factor = 1.0 - weight

    # Darken odd lines
    result[1::2, :, :] *= dark_factor

    return np.clip(result, 0, 255).astype(np.uint8)


def apply_aperture_mask(img: np.ndarray, strength: float, pixel_scale: float) -> np.ndarray:
    """Apply aperture grille mask pattern."""
    if strength < 0.001:
        return img

    result = img.copy().astype(np.float32)
    h, w = result.shape[:2]

    # Calculate pitch based on output width
    pitch = max(2.0, w / 640.0)
    period = int(pitch * 3)

    inv_strength = 1.0 - strength

    for x in range(w):
        phase = int(x % period / pitch)
        if phase == 0:
            # Red stripe
            result[:, x, 0] *= 1.0
            result[:, x, 1] *= inv_strength
            result[:, x, 2] *= inv_strength
        elif phase == 1:
            # Green stripe
            result[:, x, 0] *= inv_strength
            result[:, x, 1] *= 1.0
            result[:, x, 2] *= inv_strength
        else:
            # Blue stripe
            result[:, x, 0] *= inv_strength
            result[:, x, 1] *= inv_strength
            result[:, x, 2] *= 1.0

    return np.clip(result, 0, 255).astype(np.uint8)


def apply_uniform_darkening(img: np.ndarray, mask_strength: float) -> np.ndarray:
    """Apply uniform darkening (compensation for missing mask at low res)."""
    if mask_strength < 0.001:
        return img

    result = img.copy().astype(np.float32)
    avg_mask_effect = 1.0 - mask_strength * 0.55
    result *= avg_mask_effect

    return np.clip(result, 0, 255).astype(np.uint8)


def apply_brightness_contrast(img: np.ndarray, brightness: float, contrast: float) -> np.ndarray:
    """Apply brightness and contrast adjustments."""
    if abs(brightness - 1.0) < 0.001 and abs(contrast - 1.0) < 0.001:
        return img

    result = img.copy().astype(np.float32)
    result = (result - 128) * contrast + 128
    result *= brightness

    return np.clip(result, 0, 255).astype(np.uint8)


def apply_saturation(img: np.ndarray, saturation: float) -> np.ndarray:
    """Apply saturation adjustment."""
    if abs(saturation - 1.0) < 0.001:
        return img

    result = img.copy().astype(np.float32)
    luma = 0.299 * result[:,:,0] + 0.587 * result[:,:,1] + 0.114 * result[:,:,2]
    luma = luma[:,:,np.newaxis]

    result = luma + (result - luma) * saturation

    return np.clip(result, 0, 255).astype(np.uint8)


def apply_crt_profile(img: Image.Image, profile: ProfileConfig,
                      out_width: int, out_height: int) -> Image.Image:
    """Apply CRT profile effects to an image."""
    # Scale to output size (nearest neighbor)
    scaled = img.resize((out_width, out_height), Image.Resampling.NEAREST)
    arr = np.array(scaled)

    pixel_scale = out_width / SRC_WIDTH

    # Apply scanlines
    if profile.scanline_weight > 0:
        arr = apply_scanlines(arr, profile.scanline_weight)

    # Apply mask or uniform darkening based on scale
    if profile.mask_strength > 0 and profile.mask_type > 0:
        if pixel_scale >= 3.5:
            # Full mask pattern at high resolution
            arr = apply_aperture_mask(arr, profile.mask_strength, pixel_scale)
        else:
            # Uniform darkening at low resolution
            arr = apply_uniform_darkening(arr, profile.mask_strength)

    # Apply color adjustments
    arr = apply_saturation(arr, profile.saturation)
    arr = apply_brightness_contrast(arr, profile.brightness, profile.contrast)

    return Image.fromarray(arr)


def calculate_stats(img: Image.Image) -> dict:
    """Calculate image statistics."""
    arr = np.array(img).astype(np.float32)
    lum = 0.299 * arr[:,:,0] + 0.587 * arr[:,:,1] + 0.114 * arr[:,:,2]

    return {
        "avg": float(np.mean(lum)),
        "min": float(np.min(lum)),
        "max": float(np.max(lum)),
        "std": float(np.std(lum)),
    }


def check_brightness(stats: dict, profile: ProfileConfig, scale: float) -> Tuple[bool, str]:
    """Check if brightness is within expected range."""
    avg = stats["avg"]

    # None profile should be unchanged
    if profile.name == "None":
        if 180 < avg < 200:
            return True, "OK"
        else:
            return False, f"FAIL: avg={avg:.1f}, expected ~192"

    # Check for burning white
    if avg > 230:
        return False, f"FAIL: Burning white (avg={avg:.1f})"

    # Check for too dark
    if avg < 60:
        return False, f"FAIL: Too dark (avg={avg:.1f})"

    # Check Megatron specifically
    if profile.name == "Megatron":
        # At low scale, should be around 105-145
        if scale < 3.5:
            if 90 < avg < 160:
                return True, f"OK (avg={avg:.1f})"
            else:
                return False, f"FAIL: Megatron low-res avg={avg:.1f}, expected 90-160"

    return True, f"OK (avg={avg:.1f})"


def generate_report(scratch_dir: Path, results: List[dict]) -> str:
    """Generate HTML report."""
    html = """<!DOCTYPE html>
<html>
<head>
    <title>CRT Effects Test Report</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }
        h1 { color: #333; }
        .matrix { border-collapse: collapse; margin: 20px 0; }
        .matrix th, .matrix td { border: 1px solid #ccc; padding: 8px; text-align: center; }
        .matrix th { background: #333; color: white; }
        .matrix td { background: white; }
        .pass { background: #d4edda !important; }
        .fail { background: #f8d7da !important; }
        .thumb { max-width: 150px; max-height: 120px; cursor: pointer; }
        .thumb:hover { transform: scale(1.5); transition: 0.2s; }
        .stats { font-size: 10px; color: #666; }
        .summary { background: #fff; padding: 20px; border-radius: 8px; margin: 20px 0; }
        .legend { margin: 20px 0; }
        .legend span { padding: 4px 12px; margin-right: 10px; border-radius: 4px; }
    </style>
</head>
<body>
    <h1>CRT Effects Test Report</h1>
    <p>Generated: """ + datetime.now().strftime("%Y-%m-%d %H:%M:%S") + """</p>

    <div class="legend">
        <span class="pass">PASS</span>
        <span class="fail">FAIL</span>
    </div>

    <h2>Test Matrix</h2>
    <table class="matrix">
        <tr>
            <th>Profile</th>
"""

    # Header row with scales
    for scale_name, _ in SCALES:
        html += f"            <th>{scale_name}</th>\n"
    html += "        </tr>\n"

    # Group results by profile
    by_profile = {}
    for r in results:
        pname = r["profile"]
        if pname not in by_profile:
            by_profile[pname] = []
        by_profile[pname].append(r)

    # Data rows
    for profile in PROFILES:
        html += f"        <tr>\n            <th>{profile.name}</th>\n"

        profile_results = by_profile.get(profile.name, [])
        for scale_name, scale in SCALES:
            matching = [r for r in profile_results if r["scale_name"] == scale_name]
            if matching:
                r = matching[0]
                status_class = "pass" if r["passed"] else "fail"
                img_path = r["image_path"]
                stats = r["stats"]
                html += f"""            <td class="{status_class}">
                <img src="{img_path}" class="thumb" title="{r['status']}">
                <div class="stats">avg: {stats['avg']:.1f}</div>
                <div class="stats">{r['status'][:20]}</div>
            </td>
"""
            else:
                html += "            <td>-</td>\n"

        html += "        </tr>\n"

    html += """    </table>

    <div class="summary">
        <h2>Summary</h2>
"""

    passed = sum(1 for r in results if r["passed"])
    failed = sum(1 for r in results if not r["passed"])
    total = len(results)

    html += f"""        <p><strong>Total Tests:</strong> {total}</p>
        <p><strong>Passed:</strong> {passed}</p>
        <p><strong>Failed:</strong> {failed}</p>
"""

    if failed > 0:
        html += "        <h3>Failures:</h3>\n        <ul>\n"
        for r in results:
            if not r["passed"]:
                html += f"            <li>{r['profile']} @ {r['scale_name']}: {r['status']}</li>\n"
        html += "        </ul>\n"

    html += """    </div>

    <h2>Profile Parameters</h2>
    <table class="matrix">
        <tr>
            <th>Profile</th>
            <th>Mask Type</th>
            <th>Mask Strength</th>
            <th>Scanlines</th>
            <th>Brightness</th>
            <th>Contrast</th>
            <th>Saturation</th>
            <th>Bloom</th>
        </tr>
"""

    mask_names = ["None", "Aperture", "Shadow", "Slot"]
    for p in PROFILES:
        html += f"""        <tr>
            <td>{p.name}</td>
            <td>{mask_names[p.mask_type]}</td>
            <td>{p.mask_strength}</td>
            <td>{p.scanline_weight}</td>
            <td>{p.brightness}</td>
            <td>{p.contrast}</td>
            <td>{p.saturation}</td>
            <td>{p.bloom_strength}</td>
        </tr>
"""

    html += """    </table>
</body>
</html>
"""
    return html


def main():
    script_dir = Path(__file__).parent
    scratch_dir = script_dir / "scratch"
    scratch_dir.mkdir(exist_ok=True)

    print("=" * 60)
    print("CRT Effects Test Report Generator")
    print("=" * 60)
    print(f"Output directory: {scratch_dir}")
    print()

    # Generate reference image
    print("Generating ZX Spectrum test screen...")
    ref_img = generate_zx_screen()
    ref_path = scratch_dir / "reference.png"
    ref_img.save(ref_path)
    print(f"  Saved: {ref_path}")

    # Run all tests
    results = []

    for profile in PROFILES:
        print(f"\nTesting profile: {profile.name}")
        print(f"  mask_type={profile.mask_type}, mask_strength={profile.mask_strength}")

        for scale_name, scale in SCALES:
            out_w = int(SRC_WIDTH * scale)
            out_h = int(SRC_HEIGHT * scale)

            # Apply CRT effects
            result_img = apply_crt_profile(ref_img, profile, out_w, out_h)

            # Save image
            img_filename = f"cpu_{profile.name}_{scale_name}.png"
            img_path = scratch_dir / img_filename
            result_img.save(img_path)

            # Calculate stats
            stats = calculate_stats(result_img)

            # Check brightness
            passed, status = check_brightness(stats, profile, scale)

            results.append({
                "profile": profile.name,
                "scale": scale,
                "scale_name": scale_name,
                "resolution": f"{out_w}x{out_h}",
                "image_path": img_filename,
                "stats": stats,
                "passed": passed,
                "status": status,
            })

            status_icon = "✓" if passed else "✗"
            print(f"  {scale_name} ({out_w}x{out_h}): {status_icon} {status}")

    # Generate HTML report
    print("\nGenerating HTML report...")
    html = generate_report(scratch_dir, results)
    report_path = scratch_dir / "report.html"
    with open(report_path, 'w') as f:
        f.write(html)
    print(f"  Saved: {report_path}")

    # Save JSON results
    json_path = scratch_dir / "results.json"
    with open(json_path, 'w') as f:
        json.dump(results, f, indent=2)
    print(f"  Saved: {json_path}")

    # Summary
    passed = sum(1 for r in results if r["passed"])
    failed = sum(1 for r in results if not r["passed"])

    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)
    print(f"Total: {len(results)}, Passed: {passed}, Failed: {failed}")

    if failed > 0:
        print("\nFailed tests:")
        for r in results:
            if not r["passed"]:
                print(f"  - {r['profile']} @ {r['scale_name']}: {r['status']}")

    print(f"\nReport: file://{report_path}")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
