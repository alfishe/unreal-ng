# Vulkan renderer

Status: in progress (2026-11-19). Owner decision: a Vulkan renderer of its own next to GL (not Qt RHI, not "GL draws,
Vulkan shows"); GL stays the fallback. Every shader effect runs on both and the two are compared pixel by pixel.

Background: [frame presenter](../2026-10-26-frame-presenter/README.md) section 6.2 (the deferred Vulkan presenter) and
[POC 053](../../../tools/poc/053-wayland-vulkan-presenter/README.md) (Wayland screen times, timed presents, Mesa's commit
timer).

## 1. What the screen renders today (GL)

| Part | Code | GPU features |
|---|---|---|
| CRT composition (the software screen's picture, bit for bit) | `widgets/crtrenderer.*`, `devicescreenglwindow.cpp` `drawComposedCrt` | RGBA8 tables, `texelFetch` |
| Beam models (crt-lottes, ...) | `widgets/crtbeammodelgpu.*` | 14 integer textures (R16U/I, RGBA16U/I) |
| Custom fragment shader (legacy uniforms) | `drawCustomCrt` | RGBA8, the quad's `texRect` |
| libretro slang presets (19, 115 passes) | `widgets/slangpresetrunner.*`, `data/shaders/crt/` | pass chain, RGBA8 / sRGB / RGBA16F / RGBA32F targets, history, feedback, aliases, LUTs, mipmaps, wrap modes, FrameCount |
| Smooth motion's GPU device | `widgets/smoothmotiongpu.*` | ~20 fragment passes over integer textures (R8UI ... RGBA32I), stripes (scissor + flush), integer readback |
| HUD, the on-screen keyboard | `QOpenGLPaintDevice` + `QPainter` | GL only |
| Present | GL swap, or `NativePresenter` (Metal, DXGI, GL timed) | |

## 2. Design

### 2.1 One GPU layer, two backends

`unreal-qt/src/gpu/`: `gpu::Device`, `gpu::Texture`, `gpu::Program`. What the passes need and no more:

- textures of the formats above, any of them a render target; upload of a sub-rectangle with a row length; tight
  readback of a rectangle; mipmap generation; a scaled / flipped copy (blit);
- programs from GLSL 330 sources (a vertex shader: the pass's own, or the built-in one with `gl_VertexID`), uniforms
  set by name (int / ivec / float / vec / mat4, arrays), samplers bound by name with a sampler state (nearest /
  linear, clamp / border / repeat / mirror, mipmapped);
- a draw: the 4-vertex strip (attributes 0 = vec4 position, 1 = vec2 texture coordinate, the slang layout) into a
  target at a viewport, optional scissor, optional premultiplied alpha blend, optional sRGB write.

The passes (composition, beam models, custom shader, slang runner, smooth motion device, HUD overlay) are written once
against the layer. `gpu::GlDevice` (`unreal-qt/src/gpu/gldevice.*`) is the GL 3.3 core backend (the window's
context); `gpu::VulkanDevice` (`vulkandevice.*`) the Vulkan 1.3 one: dynamic rendering and synchronization2, a ring of
8 frames (command buffer, fence, uniform and staging arenas, descriptor pools), deletes deferred to the frame's fence,
each image's layout tracked. Built by default on Linux only (owner: Vulkan is for Linux); macOS (MoltenVK) and
Windows with `-DUNREAL_VULKAN=ON`.

### 2.2 Shaders: the same sources

The GL sources compile for Vulkan with glslang's relaxed Vulkan rules (`GL_EXT_vulkan_glsl_relaxed`): loose uniforms
go into one uniform block per program (offsets by reflection), bindings and locations mapped automatically, stage
interfaces matched by name, `gl_VertexID` is `gl_VertexIndex`. Checked: all 115 passes of the 19 presets compile
unchanged (2026-11-19, glslang 16).

### 2.3 Rows: GL's memory order everywhere

The Vulkan backend keeps GL's memory layout: row 0 of every image is GL's row 0 (the bottom of a GL framebuffer).
With a positive Vulkan viewport, NDC y = -1 lands in row 0 in both APIs and `gl_FragCoord.y` is 0.5 in row 0 in both,
so the shaders, the viewports (GL's y) and the readbacks are the same numbers on both backends, without a flip. Only
the last step differs: the window's picture is blitted turned over into the swapchain image (a GL default framebuffer
shows row 0 at the bottom, a swapchain image at the top).

### 2.4 The window

`DeviceScreenGLWindow` keeps its frame timing (frame source, smooth motion's plan, native pacing, policies, stats) and
draws every pass through `_gpu`: on GL the bound framebuffer is wrapped as the target (`GlDevice::WrapFramebuffer`).
With `UNREAL_RENDERER=vulkan` the window takes the native way and `initializeGL` makes the Vulkan device on the
window's surface (`gpu::CreateWindowSurface`, `vulkansurface*`: macOS the view's CAMetalLayer, `VK_EXT_metal_surface`;
Windows the HWND; Linux the X11 window or the Wayland `wl_surface` through `QPlatformNativeInterface`).
`gpu::VulkanPresenter` (`vulkanpresenter.*`, a `NativePresenter`) gives the window a texture to draw into
(`BeginTexture`); `PresentFrame` acquires a swapchain image, blits the texture into it turned over and submits on the
GUI thread, and a present thread hands the image to `vkQueuePresentKHR` at the frame's time: FIFO, IMMEDIATE /
FIFO_RELAXED for policy `vrr`; at most `images - minImageCount` images held for their time. Screen times from
`VK_KHR_present_wait` (Mesa, NVIDIA: the moment the wait returns) or `VK_GOOGLE_display_timing` (MoltenVK); they feed
the lead, the late counts and the beat as the other presenters' do. The HUD and the overlay are painted with QPainter
into an image and blended (premultiplied) over the picture. Smooth motion's device (`SmoothMotionGPU`) runs on
`_gpu` as well, and the GPU pass times (`GpuPassTimes`) come from the layer's timestamps (`Device::CreateTimestamps`:
GL timer queries; a Vulkan query pool, a query read once the fence of the submission that wrote it signalled). The
GL context stays (offscreen) only as the place GL falls back to: a device or presenter that fails, or a lost device,
and GL draws from then on (`dropVulkan`).

### 2.5 Verification

- `gpu_compare_test` (unreal-qt-tests): every effect rendered on both backends from the same frames and compared:
  the composition and beam models (exact), each preset at its parameters and a few frames (FrameCount, history,
  feedback; tolerance per channel), the custom shader. Smooth motion's device steps run on both backends against
  the CPU reference (`smoothmotiongpu_test`, exact: they are integer), the pass times on both (`gpupasstimes_test`).
- The window on .238 (Wayland, X11) and the Mac (MoltenVK): presenter stats through the WebAPI.
- One GPU per comparison: under Xvfb GL is Mesa's llvmpipe, so the test takes the Vulkan device of that name
  (`Setup::preferredDevice`; the app: `UNREAL_VULKAN_DEVICE`). Two GPUs differ by their rasterisers' ties and their
  compilers' maths (crt-yah mean 1.7, a mask step on a tie), which is not a backend's fault.

Results 2026-11-20:

| Run | Result |
|-----|--------|
| Mac (Apple GL / MoltenVK), unreal-qt-tests | 38 passed; custom shader 0 bytes differ; presets max 1 except crt-royale (mean 0.09) |
| .238 Xvfb, GL llvmpipe / Vulkan llvmpipe | 39 passed; custom shader 2 pixels by 6 (mask tie); newpixie mean 0.6 (its `fract(sin(x) * 43758)` grain) |
| .238 Xvfb, smooth motion + pass times on the NVIDIA T1200 | 19 passed (bit-exact with the CPU) |
| .238 Xvfb, the app's window (`capture/screen?source=window`), every CRT profile and preset, GL vs Vulkan llvmpipe / NVIDIA | profiles exact on all; presets mean <= 0.1 except newpixie 0.9-1.2, crt-yah 1.7 (NVIDIA), crt-royale 0.87 (NVIDIA); NVIDIA swapchain 301 presented, 0 dropped, error p99 0.17 ms |

The owner's desktop session on .238 (GNOME 60 Hz, `SESSION=1` runs of `window_compare.py`; Intel UHD, NVIDIA T1200):
the pictures of Vulkan Intel and GL Intel within 4 levels on Wayland and X11 (profiles exact). Present timing over
25 s of a running snapshot (error: screen time minus the time asked):

| Run | Late of ~1465 | Error p50 / p99 | Lead |
|-----|---------------|-----------------|------|
| X11, Vulkan NVIDIA | 3 | 1.3 / 1.5 ms | 10 ms |
| X11, Vulkan Intel | 44 | 8.5 / 18 ms | 20 ms |
| X11, GL timed Intel | 26 | 8.0 / 17.9 ms | 20 ms |
| Wayland, Vulkan NVIDIA | 404 | 13.6 / 23 ms | 8 ms |
| Wayland, Vulkan Intel | 969 | 16.6 / 33.7 ms | 8 ms |

Wayland: the rows above present at the frame's time, and mutter takes a commit for a refresh only some ms before it,
so nearly every frame came a refresh late (the draw lead does not help: 8, 20 ms ~16 ms after, 40 ms ~33 ms). The
present now goes ahead of the time on Wayland (`aheadNs`, 2-20 ms, from 10 ms: a late present widens it by 1 ms, one
shown more than 1 ms early narrows it by 0.25 ms) and the window draws 2 ms before that. 40 s runs, 2026-11-20:

| Wayland run | Late | Error p50 / p99 | Present ahead at the end |
|-------------|------|-----------------|--------------------------|
| Intel, at the time (before) | 845 of 1459 | 16.1 / 34.8 ms | 0 |
| NVIDIA, at the time (before) | 1072 of 1083 | 17.1 / 50.5 ms | 0 |
| Intel, fixed 4 / 6 / 8 / 10 ms | 1223 / 1311 / 168 / 21 of ~1460 | 17.1 / 17.5 / 8.3 / 5.9 ms p50 | fixed |
| Intel, widening only (to 20 ms) | 16-18 of 2199 | -5 to -6 / 5-14 ms (shown early) | 20 ms |
| Intel, adaptive both ways | 79-91 of 2198 | 5.0-6.2 / 17-26 ms | 7.25-9.5 ms |
| NVIDIA, adaptive both ways | 50-67 of 2198 | 6.2-7.3 / 17-18 ms | 6.5-9.25 ms |

A 50 Hz frame on a 60 Hz screen waits up to a refresh for the first one after its time (p50 ~8 ms with no lateness),
so the extra refresh is gone; the rest (2-4 % late) is mutter's deadline wandering by a few ms. A commit held to its
time by the compositor (`VK_EXT_present_timing`, Mesa 26.1; .238 has 26.0.8) would remove that too. GL on Wayland has
no native presenter (no screen times) to compare with. The refresh estimate: the screen's rate (QScreen)
refined by the steps between screen times near 1-3 refreshes (the shortest step alone read 71-329 Hz at 60 Hz:
present_wait wakes jitter).

Intel under Xvfb: no DRI3, Mesa's Vulkan cannot present to the window; the window takes the GPU that can (NVIDIA).
Found on the way: gcc evaluated `Made(create(..., &surface), surface, ...)` right to left (the surface copied before
it was made: no Vulkan window on Linux, no reason given); the GPU probe refused the Vulkan renderer when the
display's GL was a software one.

## 3. Plan

1. The layer: `gpu::Device` API, `VkDevice` (glslang, reflection, pipelines per state, a frame's command buffer,
   staging uploads and readbacks), `GlDevice`.
2. The passes on the layer: composition, beam model, custom shader, slang runner, HUD; then smooth motion's device.
3. The comparison test for every effect.
4. The Vulkan window path and presenter; WebAPI stats; .238 and Mac runs.
5. Settings / automation: `renderer` gl | vulkan on every plane (the window display settings: `renderer`, `renderers`
   offered, `renderer_active`; `video.present` carries `renderer`), View > Renderer (a build with Vulkan), kept in
   `View/Renderer`; `UNREAL_RENDERER` wins at start. Done 2026-11-20.
