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

**Current Status:** IN PROGRESS - frames output but playback shows only keyframes (slideshow effect)

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

| qualityPreset | NVENC Preset | QP Value | Description |
|---------------|--------------|----------|-------------|
| 0-2 | P1 | 28-24 | Fastest, lowest quality |
| 3-4 | P3 | 22-20 | Fast |
| 5-6 | P5 | 18-16 | Balanced |
| 7-8 | P6 | 14-12 | High quality |
| 9-10 | P7 | 10-8 | Best quality, slowest |

QP formula: `qp = 28 - qualityPreset * 2`

All use `NV_ENC_TUNING_INFO_HIGH_QUALITY` tuning and `NV_ENC_PARAMS_RC_CONSTQP` rate control.

## Current Status

### Working
- NVENC detection and capability probing with caching
- H.264 and HEVC codec selection based on config
- Quality presets (P1-P7 mapping from 0-10 scale)
- Constant QP rate control for consistent quality
- MP4 container with proper atom structure
- Parameter set extraction (VPS/SPS/PPS) from Annex-B
- Annex-B to AVCC/HVCC conversion (start codes → length prefix)
- Buffer queue for B-frame pipeline
- PTS tracking via inputTimeStamp/outputTimeStamp

### In Progress
- B-frame playback (slideshow instead of smooth video)
- Proper ctts handling and frame ordering

### Not Implemented
- AV1 encoding (requires Ada Lovelace+ GPU, separate codec config)
- Audio muxing (NVENC is video-only; would need separate AAC encoder)

## References

- [NVIDIA Video Codec SDK](https://developer.nvidia.com/video-codec-sdk)
- [NVENC Programming Guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-video-encoder-api-prog-guide/index.html)
- [FFmpeg nv-codec-headers](https://github.com/FFmpeg/nv-codec-headers)
- [FFmpeg NVENC implementation](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/nvenc.c)
- [minimp4](https://github.com/lieff/minimp4) - Reference for MP4 muxing
- [ISO/IEC 14496-15](https://www.iso.org/standard/74429.html) - MP4 file format (avcC/hvcC boxes)
- [FFmpeg nvenc DTS fix patch](https://patchwork.ffmpeg.org/comment/62737/)
