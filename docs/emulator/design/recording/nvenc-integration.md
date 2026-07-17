# NVENC Integration Design

## Overview

Native NVIDIA NVENC hardware encoder for Windows, providing H.264 and HEVC encoding without FFmpeg dependency.

## Architecture

```
NvencEncoder (encoder_base.h interface)
    └── Impl (PIMPL pattern)
        ├── D3D11 Device (on NVIDIA adapter via DXGI enumeration)
        ├── NVENC Session (nvEncOpenEncodeSessionEx)
        ├── Buffer Queue (8 input/output pairs for B-frame pipeline)
        ├── Mp4Muxer (native MP4 container with ctts for B-frames)
        └── NV12 conversion (RGBA → NV12 BT.601)
```

## Key Files

- `core/src/emulator/recording/platform/windows/nvenc_encoder.cpp` - Main encoder
- `core/src/emulator/recording/platform/windows/nvenc_encoder.h` - Public interface
- `core/src/emulator/recording/platform/windows/nvenc_probe.cpp` - Capability detection
- `core/src/emulator/recording/platform/windows/mp4_muxer.cpp` - Native MP4 muxer
- `core/src/emulator/recording/platform/windows/mp4_muxer.h` - Muxer interface
- `core/src/emulator/recording/platform/windows/nvEncodeAPI.h` - Official FFmpeg nv-codec-headers v12.2

## Issues Encountered & Solutions

### 1. nvEncOpenEncodeSessionEx failed: 15 (INVALID_VERSION)

**Cause:** Wrong version format calculation.

**Details:** NVENC has two different version formats:
- `NVENCAPI_VERSION = MAJOR | (MINOR << 24)` - for API calls
- `NvEncodeAPIGetMaxSupportedVersion` returns `(MAJOR << 4) | MINOR` - different format!

**Solution:** Use official FFmpeg nv-codec-headers which handle this correctly with proper macros.

### 2. nvEncInitializeEncoder failed: 12 (UNSUPPORTED_PARAM)

**Cause:** Must call `nvEncGetEncodePresetConfigEx` before `nvEncInitializeEncoder`.

**Solution:** Get preset config first, then pass `encodeConfig` in init params:
```cpp
nvenc.nvEncGetEncodePresetConfigEx(encoder, codecGuid, presetGuid, tuning, &presetConfig);
initParams.encodeConfig = &presetConfig.presetCfg;
```

### 3. D3D9 not working

**Cause:** Modern NVENC requires D3D11, not D3D9.

**Solution:** Use D3D11 with DXGI adapter enumeration to find NVIDIA GPU:
```cpp
IDXGIFactory1* factory = nullptr;
CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
// Enumerate adapters, find VendorId == 0x10DE (NVIDIA)
D3D11CreateDevice(nvidiaAdapter, D3D_DRIVER_TYPE_UNKNOWN, ...);
```

### 4. MP4 file not playable (raw bitstream written to .mp4)

**Cause:** Writing raw H.264/HEVC Annex-B bitstream to .mp4 file without proper container structure.

**Solution:** Implemented native Mp4Muxer that:
- Writes ftyp, mdat, moov atoms in correct order
- Converts Annex-B (start codes) to AVCC/HVCC format (length-prefixed NALUs)
- Extracts VPS/SPS/PPS parameter sets for codec configuration
- Writes proper sample tables: stts, stsz, stss, stco, stsc

### 5. HEVC hvcC extradata invalid ("Invalid NAL unit size in extradata")

**Cause:** Malformed HEVCDecoderConfigurationRecord in hvcC box.

**Solution:** Follow ISO/IEC 14496-15 structure exactly:
```
configurationVersion = 1
profile_space + tier_flag + profile_idc (use safe defaults)
profile_compatibility_flags (4 bytes)
constraint_indicator_flags (6 bytes) 
level_idc
min_spatial_segmentation_idc with reserved bits
parallelismType, chromaFormat, bitDepth fields
numOfArrays = 3 (VPS, SPS, PPS)
Each array: [completeness+type (1)][numNalus (2)][nalUnitLength (2)][NAL data]...
```

Key detail: `array_completeness` bit must be set (0xA0, 0xA1, 0xA2 for VPS/SPS/PPS NAL types 32, 33, 34).

### 6. B-frames cause broken playback ("Could not find ref with POC")

**Cause:** B-frames require proper handling of:
1. Decode order vs display order (PTS/DTS)
2. Buffer pipeline (encoder needs multiple frames before outputting)
3. ctts box in MP4 for composition time offsets

**Understanding the problem:**
- NVENC with HIGH_QUALITY tuning enables B-frames automatically
- B-frames are bidirectionally predicted (reference both past and future frames)
- Encoder must buffer frames to reorder them for encoding
- Output order (decode order) differs from input order (display order)
- MP4 stores frames in decode order with ctts offsets indicating display order

**Solution approach:**

1. **Buffer Queue:** Allocate multiple input/output buffer pairs (8 pairs for safety):
```cpp
static constexpr int NUM_BUFFERS = 8;  // >= 1 + numBFrames
NV_ENC_INPUT_PTR inputBuffers[NUM_BUFFERS];
NV_ENC_OUTPUT_PTR outputBuffers[NUM_BUFFERS];
int encodeIdx = 0;   // Next buffer to encode into
int outputIdx = 0;   // Next buffer to read output from  
int pendingFrames = 0;
```

2. **Handle NV_ENC_ERR_NEED_MORE_INPUT:** When returned, encoder is buffering for B-frame reordering:
```cpp
NVENCSTATUS status = nvenc.nvEncEncodePicture(encoder, &picParams);
if (status == NV_ENC_SUCCESS) {
    // Output ready - read it
    pendingFrames++;
    writeOutput();
} else if (status == NV_ENC_ERR_NEED_MORE_INPUT) {
    // Frame buffered, will be output later
    pendingFrames++;
}
encodeIdx = (encodeIdx + 1) % NUM_BUFFERS;
```

3. **Flush at end:** Send EOS and drain remaining frames:
```cpp
auto eos = nvencStruct<NV_ENC_PIC_PARAMS>(...);
eos.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
nvenc.nvEncEncodePicture(encoder, &eos);
while (pendingFrames > 0)
    writeOutput();
```

4. **MP4 ctts box:** Track PTS (from outputTimeStamp) and compute ctts offset:
```cpp
// In muxer:
int64_t dts = _nextDts;  // Sequential
_nextDts += 1000;        // Fixed frame duration
int32_t cttsOffset = pts - dts;  // Composition offset
if (cttsOffset != 0) _hasCtts = true;
```

5. **Write ctts box** if any offsets are non-zero:
```cpp
if (_hasCtts) {
    // version 1 for signed offsets (B-frames can have negative)
    writeU8(ctts, 1);
    for (const auto& f : _frames) {
        writeU32(ctts, 1);  // sample_count
        writeU32(ctts, f.cttsOffset);  // sample_offset
    }
}
```

**Current Status:** RESOLVED — B-frame playback works correctly with proper buffer pipeline and ctts handling

**Key NVIDIA Documentation Points:**
- "The client can call NvEncLockBitstream() API on the output buffers in the same order in which it has called NvEncEncodePicture()"
- "The client should allocate at least (1 + N_B) input and output buffers, where N_B is the number of B frames"
- "When enablePTD = 1, NVIDIA Encoder takes care of the reordering in case of B frames transparently"

## Buffer Queue Model

```
Input frames:    F0 → F1 → F2 → F3 → F4 → F5 → ...
                  ↓    ↓    ↓    ↓    ↓    ↓
Encoder queue:  [B0] [B1] [B2] [B3] [B4] [B5] [B6] [B7]
                  ↓
                NVENC (internal reordering for B-frames)
                  ↓
Output order:   decode order (may differ from input order)
                  ↓
MP4 muxer:      writes in decode order, ctts provides display offset
```

## MP4 Container Structure

```
ftyp (file type)
  major_brand: isom
  compatible_brands: isom, iso2, avc1/hev1, mp41

mdat (media data)
  [frame 0 in AVCC format: 4-byte length + NAL data]
  [frame 1 ...]
  ...

moov (movie metadata)
  mvhd (movie header)
    timescale, duration
  trak (track)
    tkhd (track header)
    mdia (media)
      mdhd (media header)
      hdlr (handler: vide)
      minf (media info)
        vmhd (video media header)
        dinf/dref (data reference)
        stbl (sample table)
          stsd (sample description: avc1/hev1 + avcC/hvcC)
          stts (decoding time to sample)
          ctts (composition time to sample) ← B-frame reordering
          stss (sync samples / keyframes)
          stsz (sample sizes)
          stco/co64 (chunk offsets)
          stsc (sample to chunk)
```

## Quality Settings

### NVENC Profile GUIDs

Available profile GUIDs from `nvEncodeAPI.h` (v12.2):

**HEVC (H.265):**

| GUID Constant | Profile | Bit Depth | Chroma | Used by Unreal-NG |
|---------------|---------|-----------|--------|-------------------|
| `NV_ENC_HEVC_PROFILE_MAIN_GUID` | Main | 8-bit | 4:2:0 | **Yes** ✓ |
| `NV_ENC_HEVC_PROFILE_MAIN10_GUID` | Main 10 | 10-bit | 4:2:0 | No |
| `NV_ENC_HEVC_PROFILE_FREXT_GUID` | Main 4:4:4 | 8/10-bit | 4:4:4 | No |

**H.264 (AVC):**

| GUID Constant | Profile | B-Frames | Used by Unreal-NG |
|---------------|---------|----------|-------------------|
| `NV_ENC_H264_PROFILE_BASELINE_GUID` | Baseline | No | No |
| `NV_ENC_H264_PROFILE_MAIN_GUID` | Main | Yes | No |
| `NV_ENC_H264_PROFILE_HIGH_GUID` | High | Yes | **Yes** ✓ |
| `NV_ENC_H264_PROFILE_HIGH_444_GUID` | High 4:4:4 | Yes | No |
| `NV_ENC_H264_PROFILE_PROGRESSIVE_HIGH_GUID` | Progressive High | Yes | No |
| `NV_ENC_H264_PROFILE_CONSTRAINED_HIGH_GUID` | Constrained High | No | No |
| `NV_ENC_H264_PROFILE_STEREO_GUID` | Stereo High | Yes | No |
| `NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID` | Auto | Depends | No (explicit selection preferred) |

### NVENC Tuning Info

The encoder uses `NV_ENC_TUNING_INFO_HIGH_QUALITY` for all presets. Available tuning modes:

| Tuning | Value | Description | Temporal Filter | B-Frames | Used |
|--------|-------|-------------|-----------------|----------|------|
| `UNDEFINED` | 0 | Invalid | — | — | No |
| `HIGH_QUALITY` | 1 | Latency-tolerant, best quality | **On by default** ⚠️ | Yes | **Yes** ✓ |
| `LOW_LATENCY` | 2 | Low-latency streaming | Off | No | No |
| `ULTRA_LOW_LATENCY` | 3 | Ultra-low-latency streaming | Off | No | No |
| `LOSSLESS` | 4 | Mathematically lossless | Off | Yes | No |
| `ULTRA_HIGH_QUALITY` | 5 | Highest quality (HEVC only, Turing+) | **On by default** ⚠️ | Yes | No |

> **Note:** `HIGH_QUALITY` and `ULTRA_HIGH_QUALITY` both enable temporal filtering (`tfLevel=4`) by default, which blurs pixel-art content. The encoder explicitly overrides this to `tfLevel=0` (see [Pixel-Art Quality Overrides](#pixel-art-quality-overrides) below).

### Quality Preset Mapping

The UI exposes 5 quality levels mapped to NVENC presets and QP values:

| UI Level | qualityPreset | NVENC Preset | QP Value | Rate Control | Description |
|----------|---------------|--------------|----------|-------------|-------------|
| Fastest  | 1 | P1 | 28 | ConstQP | Fastest, lowest quality |
| Fast     | 3 | P3 | 24 | ConstQP | Good speed/quality |
| Medium   | 5 | P5 | 20 | ConstQP | Balanced (default) |
| High     | 7 | P6 | 16 | ConstQP | High quality |
| Best     | 9 | P7 | 12 | ConstQP | Best quality, slowest |

Preset mapping: `q <= 2 → P1`, `q <= 4 → P3`, `q <= 6 → P5`, `q <= 8 → P6`, `else P7`

QP formula (ConstQP mode): `qp = clamp(30 - qualityPreset * 2, 8, 28)`

### Rate Control

- **When `videoBitrate > 0`**: VBR mode with `targetQuality` hint, VBV buffer at 2x bitrate, and two-pass quarter-resolution analysis at quality ≥ 6
- **When `videoBitrate == 0`**: ConstQP mode (shown above)

Both modes enable spatial AQ (`enableAQ=1`, `aqStrength=8`) to preserve sharp pixel-art edges.

### Pixel-Art Quality Overrides

The `NV_ENC_TUNING_INFO_HIGH_QUALITY` preset enables several defaults designed for natural video that are harmful to pixel-art content. The following overrides are applied after loading the preset config:

| Setting | Preset Default | Override | Reason |
|---------|---------------|----------|--------|
| `hevcConfig.tfLevel` | `LEVEL_4` (temporal filter on) | `LEVEL_0` (disabled) | Temporal filter blurs sharp pixel-art edges across frames; NVENC docs say "recommended for natural contents" |
| `hevcConfig.minCUSize` | Auto | `8x8` | Smallest coding unit preserves fine pixel detail |
| `profileGUID` | Auto-select | HEVC Main / H.264 High | Explicit profile for consistency |
| `enableAQ` | Off | On (strength 8) | Spatial AQ protects text/edge regions in flat color areas |
| `enableLookahead` | Off | On (q ≥ 5, depth min(fps,8)) | Better scene-cut detection and B-frame placement |
| `gopLength` | Auto | `fps * 2` | Fixed 2-second GOP for seekability |
| `frameIntervalP` | Auto | 3 (IBBP) at q ≥ 4, else 1 (IP-only) | B-frames improve compression for recording |

### Scale Factor (Nearest-Neighbor Upscaling)

The `scaleFactor` config field (default 1, UI default 2) applies integer upscaling before encoding. This is **critical** for ZX pixel art with YUV codecs:

- At 1x (native 352×288), 4:2:0 chroma subsampling halves color resolution and smears per-pixel multicolor effects
- At 2x+, each 2×2 chroma block maps to exactly one source pixel, so colors are preserved exactly
- Upscaling is nearest-neighbor (no interpolation) to keep edges crisp

The NVENC encoder applies this during RGBA→NV12 conversion, matching the behavior of the FFmpeg pipe encoder path.

## Current Status

### Working
- NVENC detection and capability probing with caching
- H.264 and HEVC codec selection based on config
- Quality presets (P1-P7 mapping from 0-10 scale)
- ConstQP and VBR rate control modes
- MP4 container with proper atom structure
- Parameter set extraction (VPS/SPS/PPS) from Annex-B
- Annex-B to AVCC/HVCC conversion (start codes → length prefix)
- Buffer queue for B-frame pipeline
- PTS tracking via inputTimeStamp/outputTimeStamp
- Nearest-neighbor scaleFactor upscaling (RGBA→NV12 conversion)
- Pixel-art quality overrides (temporal filter disabled, spatial AQ, lookahead)
- Explicit profile setting (HEVC Main / H.264 High)

### Known Issues Resolved
- **Heavy blur on H.265 output**: Fixed by disabling temporal filtering (`tfLevel=0`) which the HIGH_QUALITY preset enables by default — it is designed for natural video content, not pixel art
- **Suspiciously small output files**: Fixed by proper rate control (VBR when bitrate configured, better QP range) and by no longer ignoring the `videoBitrate` config field
- **Color smearing**: Fixed by respecting `scaleFactor` for nearest-neighbor upscaling before encoding
- **B-frame playback (slideshow)**: Fixed via proper buffer pipeline and ctts handling

### Not Implemented
- AV1 encoding (requires Ada Lovelace+ GPU, separate codec config)
- Audio muxing (NVENC is video-only; would need separate AAC encoder)

## Quality Preset Test Results

Automated testing validates that all 5 UI quality levels produce correct output characteristics. Test harness: `tools/nvenc_poc/test_quality_presets.py`

### Test Methodology

1. Generate animated pixel-art source content (mandelbrot zoom with per-frame motion, sharp edges)
2. Encode at all 5 quality levels using ffmpeg NVENC with parameters matching the C++ code
3. Validate: monotonic file size increase, detail preservation (luma σ), bitrate progression

### H.265 (HEVC) Results

| UI Level | Preset | QP | File Size | Bitrate | Profile |
|----------|--------|----|-----------|---------|---------|
| Fastest  | P1     | 28 | 921 KB    | 1,886 kbps | Main   |
| Fast     | P3     | 24 | 1,361 KB  | 2,788 kbps | Main   |
| Medium   | P5     | 20 | 1,428 KB  | 2,924 kbps | Main   |
| High     | P6     | 16 | 2,104 KB  | 4,308 kbps | Main   |
| Best     | P7     | 12 | 3,012 KB  | 6,169 kbps | Main   |

**Size ratio**: 3.3× (Fastest → Best)

### H.264 Results

| UI Level | Preset | QP | File Size | Bitrate | Profile |
|----------|--------|----|-----------|---------|---------|
| Fastest  | P1     | 28 | 671 KB    | 1,375 kbps | High   |
| Fast     | P3     | 24 | 1,073 KB  | 2,197 kbps | High   |
| Medium   | P5     | 20 | 1,486 KB  | 3,043 kbps | High   |
| High     | P6     | 16 | 2,234 KB  | 4,575 kbps | High   |
| Best     | P7     | 12 | 3,169 KB  | 6,490 kbps | High   |

**Size ratio**: 4.7× (Fastest → Best)

### Validation Results

- **Monotonic file size increase**: PASS (both codecs)
- **Detail preservation (luma σ)**: PASS — temporal filtering disabled, so sharpness is maintained at all quality levels
- **Profile correctness**: PASS — HEVC Main, H.264 High at all levels
- **Bitrate progression**: 3.3–4.7× from Fastest to Best, confirming clear quality differentiation

### Running the Tests

```bash
# HEVC test (default)
python tools/nvenc_poc/test_quality_presets.py

# H.264 test
python tools/nvenc_poc/test_quality_presets.py --h264
```

Output files (comparison frames and encoded MP4s) are written to `%TEMP%\nvenc_quality_test\`.

## References

- [NVIDIA Video Codec SDK](https://developer.nvidia.com/video-codec-sdk)
- [NVENC Programming Guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-video-encoder-api-prog-guide/index.html)
- [FFmpeg nv-codec-headers](https://github.com/FFmpeg/nv-codec-headers)
- [FFmpeg NVENC implementation](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/nvenc.c)
- [minimp4](https://github.com/lieff/minimp4) - Reference for MP4 muxing
- [ISO/IEC 14496-15](https://www.iso.org/standard/74429.html) - MP4 file format (avcC/hvcC boxes)
- [FFmpeg nvenc DTS fix patch](https://patchwork.ffmpeg.org/comment/62737/)
