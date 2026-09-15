# POC 001: CRT Effects GPU/CPU Parity Test

## Purpose
Verify GPU shader and CPU filter produce identical CRT effects across all profiles and resolutions.

## Test Results: ALL PASS

| Profile    | 1x | 2x | 3x | 1080p | 4K |
|------------|:--:|:--:|:--:|:-----:|:--:|
| None       | OK | OK | OK | OK    | OK |
| Basic      | OK | OK | OK | OK    | OK |
| Aperture   | OK | OK | OK | OK    | OK |
| ShadowMask | OK | OK | OK | OK    | OK |
| SlotMask   | OK | OK | OK | OK    | OK |
| Megatron   | OK | OK | OK | OK    | OK |

Max pixel difference: 1 (rounding only)

## Key Implementation Details

### Scanlines
- Formula: `sin(y * PI / scale) * 0.5 + 0.5`
- Scale = outputHeight / sourceHeight

### Phosphor Mask
- Smoothstep transition: 2.5x to 3.5x scale
- Below 2.5x: uniform darkening `1 - maskStrength * 0.55`
- Above 3.5x: full pattern with `dim = 1 - 0.8 * maskStrength`
- Transition: `uniform + (patterned - uniform) * scaleFade`

## Directory Structure

```
001-crt-effects-test/
├── python/
│   ├── test_gpu_vs_cpu.py    # Main parity test (generates HTML report)
│   ├── test_fast.py          # Unified implementation test
│   ├── test_real_128k.py     # CPU-only test with 128K menu
│   ├── crt_test.py           # Legacy test
│   └── generate_report.py    # Legacy report generator
└── cpp/
    ├── crt_test.cpp          # C++ standalone test
    ├── CMakeLists.txt
    └── build/
```

## Running Tests

```bash
# GPU vs CPU parity test (recommended)
cd python && python3 test_gpu_vs_cpu.py

# View report
open /Volumes/TB4-4Tb/Projects/Test/unreal-ng/scratch/gpu_vs_cpu_report.html
```

## Requirements
- Python 3.x with PIL (pillow) and numpy
- Source image: `/Users/dev/Downloads/128k.png` (352x288 ZX Spectrum 128K menu)

## Fixed Issues
- Burning white on Megatron at low resolution
- CPU scanlines using binary instead of sine wave
- CPU mask not blending in transition zone (2.5x-3.5x)
- CPU mask dim value formula mismatch
