# Recording Color Issues

## Problem

Recorded video appears washed out / desaturated compared to the live emulator display. This affects all YUV-based encoders (H.264, H.265, VP9) on all backends (FFmpeg, VideoToolbox). GIF recordings are unaffected because GIF uses indexed RGB, not YUV.

**Visual symptom**: Yellow text that appears vibrant in the emulator looks pale/greenish in recordings when played back in QuickTime Player, VLC, Bluray Player Pro, web browsers, etc.

**Affected players**: All tested players exhibit the same issue - this is not player-specific.

## Technical Analysis

### Source Colors (ZX Spectrum Palette)

The emulator uses these palette colors (from `screenzx.cpp:337-343`):

```cpp
static uint32_t palette[2][8] = {
    // LSB ABGR encoded colors
    //     Black,       Blue,        Red,    Magenta,      Green,       Cyan,     Yellow,      White
    {0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4, 0xFF25C500, 0xFFC9C700, 0xFF2AC8CC,
     0xFFCACACA},  // Brightness = 0
    {0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF, 0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF}
    // Brightness = 1
};
```

Bright yellow example: `0xFF36FCFF` = ABGR → RGB(255, 252, 54) - a pure, saturated yellow.

### Qt Display Path

The emulator display uses QImage with the same framebuffer (`devicescreen.cpp:40`):

```cpp
devicePixels = new QImage(static_cast<const unsigned char*>(buffer), width, height, QImage::Format_RGBA8888);
```

This displays correctly because it's direct RGB rendering with no color space conversion.

### Recording Path

Both FFmpeg and VideoToolbox convert RGB to YCbCr (YUV) for H.264/HEVC/VP9 encoding. The round-trip RGB→YCbCr→RGB loses color fidelity.

## Experiments and Results

### Experiment 1: Full-Range vs Limited-Range (FFmpeg)

**Hypothesis**: Players assume limited range (16-235) but we're encoding full range (0-255), causing expansion/compression mismatch.

**Code change** in `ffmpeg_pipe_encoder.cpp`:

```cpp
// Original (limited range):
vf += "scale=in_range=full:out_range=limited:out_color_matrix=bt709,format=yuv420p";
// ...
args.push_back("-color_range");
args.push_back("tv");  // tv = limited (16-235)

// Attempted fix (full range):
vf += "scale=in_range=full:out_range=full:out_color_matrix=bt709,format=yuv420p";
// ...
args.push_back("-color_range");
args.push_back("pc");  // pc = full range (0-255)
```

**Result**: No visible improvement. Colors still washed out.

**ffprobe verification**:
```
color_range=tv (or pc depending on setting)
color_space=bt709
color_transfer=bt709
color_primaries=bt709
```

Metadata was correctly written, but playback still wrong.

### Experiment 2: Color Space Metadata Tagging (VideoToolbox)

**Hypothesis**: VideoToolbox needs explicit color space hints on pixel buffers.

**Code change** in `videotoolbox_encoder.mm`:

```objc
// Attempt 1: sRGB color space
CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
if (colorSpace)
{
    CVBufferSetAttachment(pixelBuffer, kCVImageBufferCGColorSpaceKey,
                          colorSpace, kCVAttachmentMode_ShouldPropagate);
    CGColorSpaceRelease(colorSpace);
}

// Attempt 2: BT.709 color properties
CVBufferSetAttachment(pixelBuffer, kCVImageBufferColorPrimariesKey,
                      kCVImageBufferColorPrimaries_ITU_R_709_2,
                      kCVAttachmentMode_ShouldPropagate);
CVBufferSetAttachment(pixelBuffer, kCVImageBufferTransferFunctionKey,
                      kCVImageBufferTransferFunction_ITU_R_709_2,
                      kCVAttachmentMode_ShouldPropagate);
CVBufferSetAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey,
                      kCVImageBufferYCbCrMatrix_ITU_R_709_2,
                      kCVAttachmentMode_ShouldPropagate);
```

**Result**: No visible improvement.

### Experiment 3: Pre-Compensation via Saturation Boost

**Hypothesis**: If players desaturate, pre-boost saturation to compensate.

**Code change (FFmpeg)** in `ffmpeg_pipe_encoder.cpp`:

```cpp
// Added eq filter before scale
vf += "eq=saturation=1.15,scale=in_range=full:out_range=full:out_color_matrix=bt709,format=yuv420p";
```

**Code change (VideoToolbox)** in `videotoolbox_encoder.mm`:

```cpp
constexpr float SAT_BOOST = 1.15f;

// In pixel copy loop:
float r = s[0], g = s[1], b = s[2];
float gray = 0.299f * r + 0.587f * g + 0.114f * b;
r = gray + SAT_BOOST * (r - gray);
g = gray + SAT_BOOST * (g - gray);
b = gray + SAT_BOOST * (b - gray);
// Clamp to [0, 255]
uint8_t rb = static_cast<uint8_t>(r < 0 ? 0 : (r > 255 ? 255 : r));
uint8_t gb = static_cast<uint8_t>(g < 0 ? 0 : (g > 255 ? 255 : g));
uint8_t bb = static_cast<uint8_t>(b < 0 ? 0 : (b > 255 ? 255 : b));
```

**Result**: No visible improvement. The saturation boost didn't compensate correctly - colors still looked wrong.

### Experiment 4: Pixel Value Analysis

**Method**: Extract frame from recording to PNG and compare pixel values.

```bash
ffmpeg -y -i /Users/dev/Movies/recording.mp4 -vf "select=eq(n\,0)" -vframes 1 -update 1 /tmp/frame.png
```

```python
from PIL import Image
img = Image.open('/tmp/frame.png')
# Sample pixels at various locations
for y in [100, 200, 300]:
    for x in [100, 200, 300, 400]:
        r, g, b = img.getpixel((x, y))[:3]
        print(f'Pixel at ({x},{y}): RGB({r},{g},{b})')
```

**Result**:
```
Image size: (704, 576)
Pixel at (100,100): RGB(0,0,0)
Pixel at (200,100): RGB(255,255,255)
Pixel at (300,100): RGB(254,254,254)
Pixel at (400,100): RGB(0,0,0)
...
```

Black and white pixels are close to correct (0/255), but the extracted PNG doesn't show the same washed-out appearance as video playback. This suggests the issue is in video player color pipeline, not the encoded data itself.

## SDK/API Research

### AVFoundation (VideoToolbox)

Searched for full-range control in Apple SDKs:

```bash
grep -r "FullRange" /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/CoreMedia.framework/Headers/*.h
```

Found:
```c
CM_EXPORT const CFStringRef kCMFormatDescriptionExtension_FullRangeVideo
// CFBoolean; by default, false for YCbCr-based compressed formats
```

However, AVAssetWriter doesn't expose a direct way to set this. The pixel buffer format `kCVPixelFormatType_32BGRA` is RGB and should be treated as full-range, but the encoder internally converts to YCbCr.

From `AVAssetWriterInput.h`:
> The H.264 and HEVC encoders natively support kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange and kCVPixelFormatType_420YpCbCr8BiPlanarFullRange... If you need to work in the RGB domain then kCVPixelFormatType_32BGRA is recommended on iOS and macOS.

### AVVideoColorPropertiesKey

```objc
AVVideoColorPropertiesKey // NSDictionary, all 3 below keys required
AVVideoColorPrimariesKey  // e.g., AVVideoColorPrimaries_ITU_R_709_2
AVVideoTransferFunctionKey // e.g., AVVideoTransferFunction_ITU_R_709_2  
AVVideoYCbCrMatrixKey     // e.g., AVVideoYCbCrMatrix_ITU_R_709_2
```

We already set these in `videoSettings`:

```objc
NSDictionary* colorProps = @{
    AVVideoColorPrimariesKey: AVVideoColorPrimaries_ITU_R_709_2,
    AVVideoTransferFunctionKey: AVVideoTransferFunction_ITU_R_709_2,
    AVVideoYCbCrMatrixKey: AVVideoYCbCrMatrix_ITU_R_709_2
};

NSDictionary* videoSettings = @{
    AVVideoCodecKey: _useHEVC ? AVVideoCodecTypeHEVC : AVVideoCodecTypeH264,
    AVVideoWidthKey: @(_width),
    AVVideoHeightKey: @(_height),
    AVVideoCompressionPropertiesKey: compressionProps,
    AVVideoColorPropertiesKey: colorProps
};
```

**No AVVideoColorRangeKey exists** - there's no way to explicitly request full-range output in AVFoundation video settings.

### FFmpeg Color Options

Full set of color-related options we have access to:

```
-pix_fmt yuv420p          # Output pixel format
-colorspace bt709         # Color space (BT.709 = HD)
-color_primaries bt709    # Color primaries
-color_trc bt709          # Transfer characteristics (gamma)
-color_range tv|pc        # tv=limited(16-235), pc=full(0-255)
```

Filter options:
```
scale=in_range=full:out_range=full|limited:out_color_matrix=bt709
eq=saturation=N           # Saturation multiplier
colorspace=...            # Full color space conversion
```

## Current State

All experiments failed to produce correct colors. The issue appears to be in how video players interpret the encoded YCbCr data, not in the encoding itself.

**Current encoding settings** (reverted to baseline):

FFmpeg (`ffmpeg_pipe_encoder.cpp`):
```cpp
vf += "scale=in_range=full:out_range=limited:out_color_matrix=bt709,format=yuv420p";
// ...
args.push_back("-colorspace"); args.push_back("bt709");
args.push_back("-color_primaries"); args.push_back("bt709");
args.push_back("-color_trc"); args.push_back("bt709");
args.push_back("-color_range"); args.push_back("tv");
```

VideoToolbox (`videotoolbox_encoder.mm`):
```objc
NSDictionary* colorProps = @{
    AVVideoColorPrimariesKey: AVVideoColorPrimaries_ITU_R_709_2,
    AVVideoTransferFunctionKey: AVVideoTransferFunction_ITU_R_709_2,
    AVVideoYCbCrMatrixKey: AVVideoYCbCrMatrix_ITU_R_709_2
};
```

## Workarounds

1. **GIF format** - Uses indexed RGB, pixel-perfect colors, but limited to 256 colors and large files.

2. **APNG format** - Lossless RGB via FFmpeg, correct colors, but very large files.

3. **Accept the shift** - For most content the difference is subtle.

## Future Investigation Ideas

### 1. Direct RGB Encoding

Some codecs support RGB pixel format without YUV conversion:

```bash
# libx264rgb - H.264 in RGB mode
ffmpeg -f rawvideo -pix_fmt rgb24 -s 640x480 -i pipe:0 -c:v libx264rgb output.mp4

# ProRes 4444 - Professional RGB codec
ffmpeg -f rawvideo -pix_fmt rgba -s 640x480 -i pipe:0 -c:v prores_ks -profile:v 4444 output.mov
```

Need to verify player support and quality.

### 2. 10-bit Encoding

Higher bit depth may preserve more color fidelity:

```bash
ffmpeg ... -pix_fmt yuv420p10le -c:v libx265 -x265-params profile=main10 output.mp4
```

### 3. Player-Specific Testing

Test with players that have explicit color management options:

```bash
# mpv with explicit color settings
mpv --target-prim=bt.709 --target-trc=bt.709 --video-output-levels=full recording.mp4

# ffplay (ffmpeg's player)
ffplay -color_range pc recording.mp4
```

### 4. Custom Color Matrix Calculation

Capture the exact transformation:
1. Encode a test pattern with known RGB values
2. Extract decoded RGB values from playback
3. Calculate the transformation matrix
4. Apply inverse matrix before encoding

### 5. ICC Profile Embedding

Some containers support ICC profiles:
```bash
ffmpeg ... -color_primaries bt709 -color_trc iec61966-2-1 output.mp4
```

The `iec61966-2-1` is sRGB transfer function.

### 6. VP9 with YUV444

VP9 supports 4:4:4 chroma (no subsampling), which may preserve colors better:

```bash
ffmpeg ... -pix_fmt yuv444p -c:v libvpx-vp9 output.webm
```

## Related Files

- `core/src/emulator/recording/encoders/ffmpeg_pipe_encoder.cpp` - FFmpeg command line construction, filter chain
- `core/src/emulator/recording/encoders/videotoolbox_encoder.mm` - AVAssetWriter setup, pixel buffer handling
- `core/src/emulator/recording/encoders/gif_encoder.cpp` - GIF encoder (no color issues - reference for correct colors)
- `core/src/emulator/video/zx/screenzx.cpp` - ZX Spectrum palette definition
- `unreal-qt/src/widgets/devicescreen.cpp` - Qt display (correct colors - reference)

## Test Recording

To create a test recording with known colors for analysis:

1. Load a demo with solid color areas (e.g., title screen with yellow text on black)
2. Record ~5 seconds
3. Extract first frame: `ffmpeg -i recording.mp4 -vframes 1 frame.png`
4. Compare pixel values between:
   - Expected (palette values from `screenzx.cpp`)
   - PNG extraction (should match encoding)
   - Screenshot of video playback (shows the actual issue)

## Date

Investigation performed: 2026-07-16
