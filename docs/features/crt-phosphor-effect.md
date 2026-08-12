# CRT Phosphor & Scanlines Effect

Realtime implementation of authentic CRT display effects for ZX Spectrum emulation.

## Effects Overview

| Effect | Purpose |
|--------|---------|
| Phosphor persistence | Temporal blur simulating phosphor decay |
| Scanlines | Horizontal darkening every other line |
| RGB shadow mask | Optional RGB phosphor triad pattern |

## 1. Phosphor Persistence (Temporal Blur)

Blend current frame with previous frames to simulate phosphor decay. Keep a ring buffer of 2-3 previous frames.

```cpp
// Frame blending weights
float weights[] = {0.5f, 0.3f, 0.2f};  // current, prev1, prev2

for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
        output[y][x] = current[y][x] * weights[0]
                     + prev1[y][x] * weights[1]
                     + prev2[y][x] * weights[2];
    }
}
```

### Chroma-Only Blur (Sharper Edges)

For ZX Spectrum border flicker, blend only color channels while keeping luma sharp:

```cpp
// Convert to YUV
float y_cur = 0.299f * r + 0.587f * g + 0.114f * b;
float u_cur = -0.147f * r - 0.289f * g + 0.436f * b;
float v_cur = 0.615f * r - 0.515f * g - 0.100f * b;

// Blend only U/V (chroma), keep Y (luma) from current frame
float y_out = y_cur;  // no blur
float u_out = u_cur * 0.5f + u_prev1 * 0.3f + u_prev2 * 0.2f;
float v_out = v_cur * 0.5f + v_prev1 * 0.3f + v_prev2 * 0.2f;
```

## 2. Scanlines

Darken every other horizontal line to simulate CRT raster:

```cpp
float scanline_intensity = 0.85f;  // 0.0 = black lines, 1.0 = no effect

for (int y = 0; y < height; y++) {
    float brightness = (y % 2 == 0) ? 1.0f : scanline_intensity;
    for (int x = 0; x < width; x++) {
        output[y][x] = input[y][x] * brightness;
    }
}
```

## 3. RGB Shadow Mask (Optional)

Simulate RGB phosphor triads on CRT surface:

```cpp
for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
        int phase = x % 3;
        float r_mask = (phase == 0) ? 1.0f : 0.7f;
        float g_mask = (phase == 1) ? 1.0f : 0.7f;
        float b_mask = (phase == 2) ? 1.0f : 0.7f;
        
        output[y][x].r = input[y][x].r * r_mask;
        output[y][x].g = input[y][x].g * g_mask;
        output[y][x].b = input[y][x].b * b_mask;
    }
}
```

## GLSL Fragment Shader

Complete shader implementation:

```glsl
#version 330 core

uniform sampler2D currentFrame;
uniform sampler2D prevFrame1;
uniform sampler2D prevFrame2;
uniform vec2 resolution;

in vec2 uv;
out vec4 fragColor;

void main() {
    // Sample frames
    vec3 cur = texture(currentFrame, uv).rgb;
    vec3 p1 = texture(prevFrame1, uv).rgb;
    vec3 p2 = texture(prevFrame2, uv).rgb;
    
    // Phosphor persistence (temporal blend)
    vec3 color = cur * 0.5 + p1 * 0.3 + p2 * 0.2;
    
    // Scanlines
    float y = gl_FragCoord.y;
    float scanline = mod(y, 2.0) < 1.0 ? 1.0 : 0.85;
    color *= scanline;
    
    // Optional: RGB shadow mask
    float x = gl_FragCoord.x;
    int phase = int(mod(x, 3.0));
    vec3 mask = vec3(
        phase == 0 ? 1.0 : 0.7,
        phase == 1 ? 1.0 : 0.7,
        phase == 2 ? 1.0 : 0.7
    );
    color *= mask;
    
    fragColor = vec4(color, 1.0);
}
```

## Metal Shader (macOS/iOS)

```metal
fragment float4 crtFragment(
    VertexOut in [[stage_in]],
    texture2d<float> currentTex [[texture(0)]],
    texture2d<float> prev1Tex [[texture(1)]],
    texture2d<float> prev2Tex [[texture(2)]],
    constant float2& resolution [[buffer(0)]]
) {
    constexpr sampler s(filter::linear);
    
    float3 cur = currentTex.sample(s, in.uv).rgb;
    float3 p1 = prev1Tex.sample(s, in.uv).rgb;
    float3 p2 = prev2Tex.sample(s, in.uv).rgb;
    
    // Phosphor persistence
    float3 color = cur * 0.5 + p1 * 0.3 + p2 * 0.2;
    
    // Scanlines
    float y = in.position.y;
    float scanline = fmod(y, 2.0) < 1.0 ? 1.0 : 0.85;
    color *= scanline;
    
    return float4(color, 1.0);
}
```

## FFmpeg Implementation (Offline)

For video post-processing:

```bash
ffmpeg -i input.mp4 \
  -filter_complex "
    [0:v]format=yuv444p,split=3[y][u][v];
    [y]extractplanes=y[yluma];
    [u]extractplanes=u[uchroma];
    [v]extractplanes=v[vchroma];
    [uchroma]tmix=frames=5:weights='1 1 2 1 1'[ublend];
    [vchroma]tmix=frames=5:weights='1 1 2 1 1'[vblend];
    [yluma][ublend][vblend]mergeplanes=0x001020:yuv444p,
    format=yuv420p,
    geq=lum='lum(X,Y)*if(mod(Y,2),0.85,1)':cb='cb(X,Y)':cr='cr(X,Y)'
  " \
  -c:v libx264 -crf 18 output.mp4
```

## Performance Considerations

- **Frame buffer**: Store 2-3 previous frames (RGB or YUV)
- **Memory**: ~3-4MB for 704x576 RGB at 3 frames depth
- **GPU**: Single-pass shader, texture sampling is the bottleneck
- **CPU fallback**: SIMD (SSE/NEON) for pixel blending loops

## Parameters to Tune

| Parameter | Range | Default | Effect |
|-----------|-------|---------|--------|
| `persistence_weights` | 0.0-1.0 | [0.5, 0.3, 0.2] | Phosphor decay curve |
| `scanline_intensity` | 0.5-1.0 | 0.85 | Scanline darkness |
| `mask_intensity` | 0.5-1.0 | 0.7 | RGB mask strength |
| `blur_chroma_only` | bool | true | Keep edges sharp |
