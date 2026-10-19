# CRT effects in the Qt window

View > CRT Effects and View > CRT Profile draw the picture like a CRT: scanlines, a phosphor mask, curvature, rounded
corners, vignette, bloom and colour grading. The effects work on both screen backends, the GPU one (View > GPU
Acceleration, `DeviceScreenGLWindow`) and the software one (`DeviceScreen`), and **the two pictures are the same bit
for bit**. Background ideas (phosphor persistence, offline filters): [crt-phosphor-effect.md](crt-phosphor-effect.md).

## Profiles

| Profile | Automation word | What it draws |
|---------|-----------------|---------------|
| None | `none` | The plain picture |
| Basic | `basic` | Scanlines, slight curvature, a little bloom |
| Aperture Grille | `aperture` | Vertical RGB stripes (Trinitron) |
| Shadow Mask | `shadow-mask` | Delta dot triads |
| Slot Mask | `slot-mask` | Slotted RGB (arcade monitors) |
| Megatron | `megatron` | Aperture grille with stronger bloom, contrast and saturation |

The parameters are in `unreal-qt/src/widgets/crtprofiles.cpp`.

## One model for both backends

`unreal-qt/src/widgets/crtrenderer.{h,cpp}` (`CRTRenderer`) is the only definition of the effect:

1. **Grading**, once per frame, at the source's resolution, in integers: bloom (a separable 1-4-6-4-1 blur, taps
   `bloomRadius` apart, added by `bloomStrength`), gamma, saturation (luma 77/150/29), contrast and brightness.
2. **Geometry tables**, once per output size and profile: for every output pixel (device pixels) the source pixel it
   shows (curvature, the viewport crop) and a per-channel factor (scanline, phosphor mask, corners, vignette;
   128 = 1.0). Float math here is fine: the tables are integers both backends read.
3. **Composition**, per output pixel and channel: `min(255, graded * factor / 128)`.

The software screen composes on the CPU (rows in parallel) and draws the image 1:1 at device pixels. The GPU screen
uploads the graded frame and the two tables as nearest-sampled textures and runs `CRTRenderer::kCompositionShader`
over a viewport of exactly the picture's device pixels. Every value in the shader's product is an integer below 2^24,
exact in a float, so the GPU cannot round differently (`CRTRenderer_Test.TheShaderArithmeticIsTheIntegerComposition`
checks all 65536 pairs).

The phosphor mask has a pitch of `maskDotPitch` output pixels (0: `width / 640`, at least 1). Below a 2.5x scale its
pattern would alias: it fades to its average and is fully shown from 3.5x.

## Custom shaders (GPU only)

A GLSL 1.20 fragment shader can replace the built-in effect: View > CRT Profile lists the shaders shipped with the app
(`data/shaders/*.glsl`, compiled into the app by `unreal-qt/resources/shaders.qrc`: add a new one there) and the user's
own (`*.glsl`, `*.frag`, `*.fs` in the first existing of `<app>/../data/shaders`, `<app>/data/shaders`,
`<config>/shaders`; a user file replaces a shipped one of the same name). The shader gets `varying vec2 texCoord`
and the uniforms `tex`, `texSize`, `outputSize` (device pixels), `maskType` (0 none, 1 aperture, 2 shadow, 3 slot),
`curvature`, `cornerRadius`, `scanlineWeight`, `maskStrength`, `maskDotPitch`, `bloomStrength`, `saturation`,
`brightness`, `contrast`, `gamma` - the current profile's values, Megatron's when the profile is None. A custom shader
is arbitrary code: it has no software twin, its entries are greyed out without the GPU, and switching the GPU off shows
its base profile.

## Automation

The window's display settings are on every surface (WebAPI `GET/PUT /api/v1/emulator/{id}/video/window`, CLI
`video window`, Python / Lua `window_display()`, MCP `capture_media` `window_display`): `crt`, `crt_profile` (the
words above, or `shader:<name>` for a custom shader with the GPU on), `gpu`. They are the window's, not the machine's:
changing them needs no control session. A screenshot with `source=window` shows the result; on the same window size
the GPU and the software grab are the same image.

## Verification

- `unreal-qt-tests --gtest_filter='CRTRenderer*'`: every profile shows the picture, full white stays bright, a
  profile without effects is a nearest scale, the shader arithmetic, the tables rebuilt only when needed.
- Live: grab `source=window` with `gpu` on and off for every profile and compare the images.
