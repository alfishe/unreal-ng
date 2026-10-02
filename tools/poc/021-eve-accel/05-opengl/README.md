# 05 - OpenGL 4.1 fragment shader

Feeds: nothing to build on (a viability check asked for in the brief).

## Goal

macOS caps OpenGL at 4.1 (no compute shaders). Try the same evaluation as a fragment
shader, or document why it is not viable.

## Method

`gl-render.cpp`: a headless CGL 4.1 core context; one full-screen triangle per frame; the
fragment shader is a GLSL port of the op-list evaluation (integer arithmetic, no
fixed-function blending - its 8-bit unorm arithmetic cannot give eve-emu's rounding). The
memory image, op list and band lists are texture buffers (`usamplerBuffer`); the target is
an `RGBA8UI` texture read back with `glReadPixels`. GLSL 4.1 has no 64-bit integers, so the
antialiased primitives (whose line distance needs 64-bit products) are not ported: frames
with points, lines or rectangles are reported as unsupported.

```
tools/poc/021-eve-accel/05-opengl/run.sh   # not run in full: the session was stopped
```

## Results

Two R-Type play snapshot frames (run during development, load ~180):

| Frame | Ops | Result | GPU (timer query) | draw-to-finish | readback |
|---|---|---|---|---|---|
| level frame | 216 | **bit-exact** | 1.0 ms | 1.4 ms | 1.1-9.4 ms |
| title frame | 7 | **bit-exact** | 0.44 ms | 1.1 ms | 1.1 ms |

The renderer string is "OpenGL 4.1 Metal": on Apple Silicon OpenGL is a layer over Metal.

## Analysis and conclusions

- A fragment shader can do the job exactly - compute shaders are not required, because
  the whole blend chain runs inside the shader with integer math.
- It is not a path to recommend: OpenGL is deprecated on macOS and runs through a
  translation layer there; the user's decision is native APIs only (Metal on macOS) with
  the SIMD CPU renderer as the fallback (09). On Windows / Linux the native compute APIs
  (Vulkan, D3D12, CUDA) are the targets, not OpenGL.
