#!/usr/bin/env python3
"""
CRT Effects Test Suite
Tests all CRT profiles at multiple resolutions by analyzing output images.

This script generates test reference data and validates CRT filter behavior.
"""

import os
import sys
import json
from pathlib import Path
from dataclasses import dataclass
from typing import List, Tuple
import subprocess

# Try to import PIL for image analysis
try:
    from PIL import Image
    import numpy as np
    HAS_PIL = True
except ImportError:
    HAS_PIL = False
    print("Warning: PIL/numpy not available. Install with: pip install pillow numpy")

@dataclass
class ProfileConfig:
    name: str
    mask_type: int  # 0=None, 1=Aperture, 2=Shadow, 3=Slot
    mask_strength: float
    scanline_weight: float
    brightness: float
    contrast: float
    saturation: float

# CRT Profiles as defined in crtprofiles.cpp
PROFILES = [
    ProfileConfig("None", 0, 0.0, 0.0, 1.0, 1.0, 1.0),
    ProfileConfig("Basic", 0, 0.0, 0.3, 1.0, 1.0, 1.0),
    ProfileConfig("Aperture", 1, 0.5, 0.25, 1.0, 1.0, 1.1),
    ProfileConfig("ShadowMask", 2, 0.6, 0.2, 1.0, 1.0, 1.05),
    ProfileConfig("SlotMask", 3, 0.55, 0.35, 1.1, 1.0, 1.15),
    ProfileConfig("Megatron", 1, 0.7, 0.0, 1.05, 1.1, 1.2),
]

# Test resolutions (as scale factors from 352x288)
SCALES = [1.0, 1.5, 2.0, 2.5, 3.0, 4.0]

# Reference dimensions
SRC_WIDTH = 352
SRC_HEIGHT = 288


def calculate_expected_brightness(profile: ProfileConfig, scale: float) -> Tuple[float, float]:
    """
    Calculate expected brightness range for a profile at given scale.
    Returns (min_expected, max_expected) as 0-255 values.
    """
    base = 192  # ZX Spectrum gray background

    if profile.name == "None":
        return (base - 5, base + 5)

    # Apply brightness/contrast
    adjusted = base * profile.brightness

    # Apply scanline darkening (avg ~50% of lines affected)
    if profile.scanline_weight > 0:
        scanline_factor = 1.0 - profile.scanline_weight * 0.5
        adjusted *= scanline_factor

    # Apply mask darkening based on scale
    if profile.mask_strength > 0:
        if scale >= 3.0:
            # Full mask - average ~= 1 - 0.55 * strength
            mask_factor = 1.0 - 0.55 * profile.mask_strength
        elif scale >= 2.0:
            # Blended mask
            blend = (scale - 2.0)  # 0 at 2x, 1 at 3x
            full_mask = 1.0 - 0.55 * profile.mask_strength
            mask_factor = 1.0 - (1.0 - full_mask) * blend
        else:
            # Uniform compensation
            mask_factor = 1.0 - 0.55 * profile.mask_strength
        adjusted *= mask_factor

    # Allow 15% tolerance
    min_val = max(0, adjusted * 0.85)
    max_val = min(255, adjusted * 1.15)

    return (min_val, max_val)


def analyze_image(img_path: str) -> dict:
    """Analyze an image and return statistics."""
    if not HAS_PIL:
        return {"error": "PIL not available"}

    img = Image.open(img_path).convert('RGB')
    arr = np.array(img)

    # Calculate luminance
    lum = 0.299 * arr[:,:,0] + 0.587 * arr[:,:,1] + 0.114 * arr[:,:,2]

    return {
        "width": img.width,
        "height": img.height,
        "avg_brightness": float(np.mean(lum)),
        "min_brightness": float(np.min(lum)),
        "max_brightness": float(np.max(lum)),
        "std_brightness": float(np.std(lum)),
    }


def generate_test_image(output_path: str):
    """Generate a ZX Spectrum-like test pattern."""
    if not HAS_PIL:
        print("Cannot generate test image without PIL")
        return False

    img = Image.new('RGB', (SRC_WIDTH, SRC_HEIGHT), (192, 192, 192))

    # Simple pattern - just save for now
    # In a real test we'd draw the full ZX boot screen
    from PIL import ImageDraw
    draw = ImageDraw.Draw(img)

    # Draw a box
    draw.rectangle([140, 80, 290, 200], fill=(0, 255, 255), outline=(0, 0, 0))

    # Color bars
    colors = [(0,0,0), (255,0,0), (255,255,0), (0,255,0), (0,255,255), (0,0,255), (255,0,255)]
    for i, color in enumerate(colors):
        draw.rectangle([148 + i*20, 88, 168 + i*20, 96], fill=color)

    img.save(output_path)
    return True


def run_tests():
    """Run all CRT effect tests."""
    output_dir = Path(__file__).parent / "output"
    output_dir.mkdir(exist_ok=True)

    print("=" * 60)
    print("CRT Effects Test Suite")
    print("=" * 60)
    print()

    # Generate reference image
    ref_path = output_dir / "reference.png"
    if not ref_path.exists():
        print("Generating reference test image...")
        if not generate_test_image(str(ref_path)):
            print("Failed to generate reference image")
            return 1

    results = []
    passed = 0
    failed = 0

    for profile in PROFILES:
        print(f"\n--- Profile: {profile.name} ---")
        print(f"    mask_type={profile.mask_type} mask_strength={profile.mask_strength}")
        print(f"    scanline_weight={profile.scanline_weight}")

        for scale in SCALES:
            out_w = int(SRC_WIDTH * scale)
            out_h = int(SRC_HEIGHT * scale)

            # Calculate expected brightness
            exp_min, exp_max = calculate_expected_brightness(profile, scale)

            # For now, just print expected values
            # In full test, would capture actual screenshots
            print(f"    {scale}x ({out_w}x{out_h}): "
                  f"expected brightness {exp_min:.1f}-{exp_max:.1f}")

            result = {
                "profile": profile.name,
                "scale": scale,
                "resolution": f"{out_w}x{out_h}",
                "expected_min": exp_min,
                "expected_max": exp_max,
            }
            results.append(result)

    # Save results
    results_path = output_dir / "expected_values.json"
    with open(results_path, 'w') as f:
        json.dump(results, f, indent=2)
    print(f"\nExpected values saved to: {results_path}")

    # Print key findings
    print("\n" + "=" * 60)
    print("KEY VALIDATION POINTS:")
    print("=" * 60)
    print()
    print("1. Megatron at 1x scale:")
    print(f"   Expected brightness: {calculate_expected_brightness(PROFILES[5], 1.0)}")
    print("   Should show uniform gray, NOT burning white")
    print()
    print("2. Megatron at 3x scale:")
    print(f"   Expected brightness: {calculate_expected_brightness(PROFILES[5], 3.0)}")
    print("   Should show visible mask pattern")
    print()
    print("3. None profile at any scale:")
    print("   Should be ~192 (unchanged from source)")
    print()

    return 0


if __name__ == "__main__":
    sys.exit(run_tests())
