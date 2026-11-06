#!/usr/bin/env python3
"""The libretro CRT presets of unreal-qt (data/shaders/crt/<key>/: GLSL 330 per pass + preset.json, made by
tools/crt-shaders/import.py) as SPIR-V for SDL_GPU.

    crt-convert.py <data/shaders/crt> <out dir> [glslangValidator]

Per preset <out>/<key>/: pass<N>.vert.spv, pass<N>.frag.spv, the LUT images and manifest.txt, a line format the client
reads without a JSON parser (crtpreset.cpp). The GLSL changes are mechanical:

- the loose uniforms go into one std140 block per stage, in SDL_GPU's sets (vertex set 1, fragment set 3, binding 0);
  the manifest lists every member with its offset;
- the samplers take SDL_GPU's fragment set 2, bound in the manifest's order;
- the varyings get locations (LIBRA_VARYING_<N> -> location N);
- SDL_GPU has no clamp-to-border: texture() / textureLod() on a sampler of the pass go through a helper that answers
  transparent black outside 0..1 when the bound texture's wrap is ClampToBorder (a bit of LIBRA_INTERNAL_BorderMask).
  Lookups through a sampler2D function argument clamp to the edge instead.
- resolve.vert.spv / resolve.frag.spv next to the presets: the Lanczos pass crtpreset.cpp resamples a canvas that is a
  whole multiple of the source lines to the output with (a preset drawn at a fractional scale beats against its own
  scanlines and masks);
- the last pass writes alpha 1: the window's swapchain has an alpha channel and the compositor blends it with the
  desktop (a preset's black outside the tube, crt-lottes, left the window see-through). unreal-qt draws the last pass
  opaque as well (librashader's rule).
"""

import json
import os
import re
import shutil
import subprocess
import sys

STD140 = {  # size, alignment
    "float": (4, 4), "int": (4, 4), "uint": (4, 4), "bool": (4, 4),
    "vec2": (8, 8), "ivec2": (8, 8), "uvec2": (8, 8),
    "vec3": (12, 16), "vec4": (16, 16), "ivec4": (16, 16), "uvec4": (16, 16),
    "mat4": (64, 16),
}

UNIFORM = re.compile(r"^uniform\s+(\w+)\s+LIBRA_UNIFORM_(\w+)\s*;\s*$", re.M)
SAMPLER = re.compile(r"^uniform\s+sampler2D\s+LIBRA_TEXTURE_(\w+)\s*;\s*$", re.M)
VARYING = re.compile(r"^((?:(?:flat|noperspective|smooth|centroid)\s+)*)(in|out)\s+(\w+)\s+LIBRA_VARYING_(\d+)\s*;\s*$", re.M)

OUTPUT = re.compile(r"^layout\(location\s*=\s*0\)\s*out\s+vec4\s+(\w+)\s*;", re.M)
MAIN = re.compile(r"\bvoid\s+main\s*\(\s*\)")

FORMATS = {  # slang #pragma format -> SDL_GPUTextureFormat name in crtpreset.cpp; unknown ones are RGBA8
    "R8G8B8A8Unorm": "rgba8", "R8G8B8A8Srgb": "rgba8-srgb", "A2B10G10R10UnormPack32": "rgb10a2",
    "R16G16B16A16Sfloat": "rgba16f", "R32G32B32A32Sfloat": "rgba32f", "R16G16Sfloat": "rg16f",
    "R32G32Sfloat": "rg32f", "R16Sfloat": "r16f", "R32Sfloat": "r32f", "R8Unorm": "r8", "R8G8Unorm": "rg8",
}

WRAPS = {"ClampToEdge": "edge", "Repeat": "repeat", "MirroredRepeat": "mirror"}  # anything else: border


# The Lanczos pass crtpreset.cpp resamples the canvas (a whole multiple of the source lines) to the output with,
# one axis per draw. The quad is crtpreset.cpp's: Position 0..1, TexCoord 0..1, row 0 at the top
RESOLVE_VERT = """#version 450
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main()
{
    gl_Position = vec4(Position.x * 2.0 - 1.0, 1.0 - Position.y * 2.0, 0.0, 1.0);
    vTexCoord = TexCoord;
}
"""

RESOLVE_FRAG = """#version 450
layout(set = 2, binding = 0) uniform sampler2D Src;
layout(std140, set = 3, binding = 0) uniform Resolve
{
    vec2 axis;      // (1, 0) horizontal, (0, 1) vertical
    vec2 srcSize;   // source texels
    float ratio;    // source texels per output texel on the axis
};
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

const float kLobes = 3.0;
const float kPi = 3.14159265;

float lanczos(float x)
{
    x = abs(x);
    if (x < 1e-4)
        return 1.0;
    if (x >= kLobes)
        return 0.0;
    float px = kPi * x;
    return kLobes * sin(px) * sin(px / kLobes) / (px * px);
}

void main()
{
    float scale = max(ratio, 1.0);   // an upscale keeps the plain kernel
    vec2 at = vTexCoord * srcSize;   // the output pixel's centre in source texels
    float a = dot(at, axis);
    vec2 across = at * (vec2(1.0) - axis);
    int first = int(floor(a - kLobes * scale + 0.5));
    int last = int(floor(a + kLobes * scale + 0.5));
    vec3 sum = vec3(0.0);
    float weights = 0.0;
    for (int i = first; i <= last; i++)
    {
        float w = lanczos((float(i) + 0.5 - a) / scale);
        sum += texture(Src, (axis * (float(i) + 0.5) + across) / srcSize).rgb * w;
        weights += w;
    }
    FragColor = vec4(clamp(sum / weights, 0.0, 1.0), 1.0);
}
"""


# Presets that assume a resolution or a content shape of their own. The libretro sources stay untouched; the changes
# are made on the way to SPIR-V, for the Deck client only (what unreal-qt draws is not affected). The other 14 presets
# follow the output / source ratio themselves and are left as they are.
#
# "canvas": the preset draws on a whole multiple of the source lines and is resampled to the output (crtpreset.h);
# "passes": pass index -> keys of its preset.json entry; "glsl": file -> [(old, new)], every `old` must be in the file.
#
# crt-yah: rounds the pixels per source line to a whole number (3 at 2.78) for its scanlines, so they drift off the
#   lines of the source. megatron-v2-sdr: a mask of its own (4 px, made for 4K) and rows by the source lines.
# newpixie: its scanline wave is sin(y * 1.5) in output pixels, a period of 4.19 px whatever the source has: one
#   period is now exactly one source line. Its vignette is drawn from the curved coordinate, so the black of the
#   edges followed the barrel of the curve (thin bowed black bands above and below, pinched at the corners): it is
#   now a smooth oval drawn from the screen's own coordinates (the rectangle product made angular corners); the curve of the
#   picture stays.
# crt-yo6-pvm: made for 224-line content on a baked tube picture (TEX_CRT.png, 1684x1329, a 4:3 screen). Pass 0 copies
#   the source rows 1:1 into 233 rows and stretches the width over the whole tube, so another frame is cut and
#   stretched (288 rows: 15 % too tall). Pass 0 keeps every row and puts the frame in the tube with its own shape (bars
#   at the sides, or above and below); pass 2 keeps the 1684x1329 picture, and crtpreset.cpp takes it down to the output
#   with the Lanczos pass instead of the nearest step the preset ends with (1.6x nearest beats against the baked
#   mask). What stays: the bake has its own 233 scanlines, which do not fall on the lines of a 288-line frame.
# crt-pi: curved, its scanline phase is floor(y * source lines) of the distorted coordinate, sampled once per output
#   pixel: the line pitch against the pixel grid changes across the tube and shows as barrel-shaped moire at 2.78x.
#   The scanline and the row it reads are now averaged over the height of one output pixel (PI_TAPS taps), so the
#   curvature stays and the pattern is filtered instead of aliased.
NEWPIXIE_VIGNETTE = 0.5   # zero exactly in the corners (the squared distance from the centre is 2 there)
NEWPIXIE_VIGNETTE_POWER = 2.0   # the depth of the falloff: 1 is almost flat, higher darkens the edges more, no knee
PI_TAPS = 6
PI_FOOTPRINT = 1.0   # of an output pixel: the width the scanline is averaged over
PI_OLD = """    vec2 texcoordInPixels = texcoord * LIBRA_UNIFORM_SourceSize.xy;
    float tempCoord = floor(texcoordInPixels.y) + 0.5;
    float coord = tempCoord * LIBRA_UNIFORM_SourceSize.w;
    float deltas = texcoordInPixels.y - tempCoord;
    float param_1 = deltas;
    float scanLineWeight = CalcScanLine(param_1);
    float signs = sign(deltas);
    deltas = abs(deltas) * 2.0;
    deltas = (deltas * deltas) * deltas;
    deltas *= ((0.5 * LIBRA_UNIFORM_SourceSize.w) * signs);
    vec2 tc = vec2(texcoord.x, coord + deltas);
    vec3 colour = texture(LIBRA_TEXTURE_Source, tc).xyz;
    scanLineWeight *= LIBRA_UNIFORM_BLOOM_FACTOR;
    colour *= scanLineWeight;
"""
PI_NEW = f"""    vec2 texcoordInPixels = texcoord * LIBRA_UNIFORM_SourceSize.xy;
    float footprint = LIBRA_VARYING_1 * {3.0 * PI_FOOTPRINT};   // source rows under one output pixel
    vec3 colour = vec3(0.0);
    for (int i = 0; i < {PI_TAPS}; i++)
    {{
        float y = texcoordInPixels.y + ((float(i) + 0.5) / {PI_TAPS}.0 - 0.5) * footprint;
        float tempCoord = floor(y) + 0.5;
        float coord = tempCoord * LIBRA_UNIFORM_SourceSize.w;
        float deltas = y - tempCoord;
        float scanLineWeight = CalcScanLine(deltas);
        float signs = sign(deltas);
        deltas = abs(deltas) * 2.0;
        deltas = (deltas * deltas) * deltas;
        deltas *= ((0.5 * LIBRA_UNIFORM_SourceSize.w) * signs);
        colour += texture(LIBRA_TEXTURE_Source, vec2(texcoord.x, coord + deltas)).xyz * scanLineWeight;
    }}
    colour *= LIBRA_UNIFORM_BLOOM_FACTOR / {PI_TAPS}.0;
"""
TUBE_ASPECT = 4.0 / 3.0   # measured 1.31 on the 1684x1329 bake; the tube is a 4:3 one
DECK_TUNING = {
    "crt-yah": {"canvas": True},
    "crt-pi": {"glsl": {"pass0.frag": [(PI_OLD, PI_NEW)]}},
    "megatron-v2-sdr": {"canvas": True, "params": {"hcrt_crt_resolution": 2.0}},
    "newpixie": {
        "glsl": {
            "pass3.frag": [
                ("((((16.0 * curved_uv.x) * curved_uv.y) * (1.0 - curved_uv.x)) * (1.0 - curved_uv.y))",
                 f"pow(max(1.0 - {NEWPIXIE_VIGNETTE} * (pow(2.0 * LIBRA_VARYING_0.x - 1.0, 2.0) + pow(2.0 * LIBRA_VARYING_0.y - 1.0, 2.0)), 0.0), {NEWPIXIE_VIGNETTE_POWER})"),
                ("sin((LIBRA_VARYING_0 * LIBRA_UNIFORM_OutputSize.xy).y * 1.5)",
                 "sin((LIBRA_VARYING_0 * LIBRA_UNIFORM_OutputSize.xy).y * (6.2831853 * LIBRA_UNIFORM_SourceSize.y / LIBRA_UNIFORM_OutputSize.y))"),
                ("((curved_uv.y * LIBRA_UNIFORM_OutputSize.y) * 1.5)",
                 "((curved_uv.y * LIBRA_UNIFORM_OutputSize.y) * (6.2831853 * LIBRA_UNIFORM_SourceSize.y / LIBRA_UNIFORM_OutputSize.y))"),
            ],
        },
    },
    "crt-yo6-pvm": {
        "passes": {
            0: {"scale": {"valid": True, "x": {"type": "Input", "factor": 1.0}, "y": {"type": "Input", "factor": 1.0}}},
            2: {"scale": {"valid": True, "x": {"type": "Absolute", "factor": 1684}, "y": {"type": "Absolute", "factor": 1329}},
                "filter": "Nearest"},
        },
        "glsl": {
            "pass0.vert": [
                ("    LIBRA_VARYING_0 = TexCoord.x;\n",
                 f"    float tubeOverFrame = {TUBE_ASPECT} * LIBRA_UNIFORM_SourceSize.y / LIBRA_UNIFORM_SourceSize.x;\n"
                 "    LIBRA_VARYING_0 = (TexCoord.x - 0.5) * max(tubeOverFrame, 1.0) + 0.5;\n"),
                ("    LIBRA_VARYING_1 = (TexCoord.y * LIBRA_UNIFORM_OutputSize.y) - round((LIBRA_UNIFORM_OutputSize.y - LIBRA_UNIFORM_SourceSize.y) / 2.0);\n",
                 "    LIBRA_VARYING_1 = ((TexCoord.y - 0.5) * max(1.0 / tubeOverFrame, 1.0) + 0.5) * LIBRA_UNIFORM_SourceSize.y;\n"),
            ],
            "pass0.frag": [
                ("    FragColor = vec4(col * (clip0 * clipH), 1.0);\n",
                 "    float clipX = step(0.0, LIBRA_VARYING_0) * step(LIBRA_VARYING_0, 1.0);\n"
                 "    FragColor = vec4(col * (clip0 * clipH * clipX), 1.0);\n"),
            ],
        },
    },
}


def tuned_glsl(key, name, source):
    for old, new in DECK_TUNING.get(key, {}).get("glsl", {}).get(name, []):
        if old not in source:
            raise ValueError(f"{key}/{name}: the Deck tuning does not apply any more: {old.strip()}")
        source = source.replace(old, new)
    return source


def layout(members):
    """std140 offsets of (type, name) members; returns [(name, type, offset)], size"""
    out, offset = [], 0
    for kind, name in members:
        size, align = STD140[kind]
        offset = (offset + align - 1) // align * align
        out.append((name, kind, offset))
        offset += size
    return out, (offset + 15) // 16 * 16


def convert(source, stage, opaque=False):
    """GLSL 330 of one stage -> (Vulkan GLSL 450, uniform layout, block size, sampler names)"""
    members = [(m.group(1), m.group(2)) for m in UNIFORM.finditer(source)]
    samplers = [m.group(1) for m in SAMPLER.finditer(source)]
    body = UNIFORM.sub("", SAMPLER.sub("", source))
    body = VARYING.sub(lambda m: f"layout(location = {m.group(4)}) {m.group(1)}{m.group(2)} {m.group(3)} LIBRA_VARYING_{m.group(4)};", body)
    body = body.replace("#version 330", "", 1)

    if stage == "frag":
        members.append(("uint", "INTERNAL_BorderMask"))
    if not members:
        members.append(("float", "INTERNAL_Pad"))
    fields, size = layout(members)

    s = 1 if stage == "vert" else 3
    block = [f"layout(std140, set = {s}, binding = 0) uniform LibraUniforms\n{{"]
    block += [f"    {kind} LIBRA_UNIFORM_{name};" for name, kind, _ in fields]
    block.append("};")
    head = ["#version 450", ""] + block + [""]
    helpers = []
    if stage == "frag":
        for i, name in enumerate(samplers):
            head.append(f"layout(set = 2, binding = {i}) uniform sampler2D LIBRA_TEXTURE_{name};")
        helpers.append("vec4 LibraBorder(vec4 c, vec2 uv, uint bit)\n{\n"
                       "    if ((LIBRA_UNIFORM_INTERNAL_BorderMask & bit) != 0u &&\n"
                       "        (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))))\n"
                       "        return vec4(0.0);\n    return c;\n}")
        for i, name in enumerate(samplers):
            t = f"LIBRA_TEXTURE_{name}"
            body = re.sub(rf"\btexture\s*\(\s*{t}\s*,", f"LibraTexture_{name}(", body)
            body = re.sub(rf"\btextureLod\s*\(\s*{t}\s*,", f"LibraTextureLod_{name}(", body)
            bit = f"{1 << i}u"
            helpers.append(f"vec4 LibraTexture_{name}(vec2 uv) {{ return LibraBorder(texture({t}, uv), uv, {bit}); }}\n"
                           f"vec4 LibraTexture_{name}(vec2 uv, float bias) "
                           f"{{ return LibraBorder(texture({t}, uv, bias), uv, {bit}); }}\n"
                           f"vec4 LibraTextureLod_{name}(vec2 uv, float lod) "
                           f"{{ return LibraBorder(textureLod({t}, uv, lod), uv, {bit}); }}")
        if opaque:
            out = OUTPUT.search(body)
            if not out or len(MAIN.findall(body)) != 1:
                raise ValueError("the last pass has no single main() and vec4 output at location 0")
            body = MAIN.sub("void LibraMain()", body) + f"\nvoid main()\n{{\n    LibraMain();\n    {out.group(1)}.a = 1.0;\n}}\n"
    elif samplers:
        raise ValueError("a vertex shader samples a texture: not supported")
    return "\n".join(head + [""] + helpers + [body]), fields, size, samplers


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src_root, out_root = sys.argv[1], sys.argv[2]
    glslang = sys.argv[3] if len(sys.argv) > 3 else "glslangValidator"
    keys = sorted(k for k in os.listdir(src_root) if os.path.isfile(os.path.join(src_root, k, "preset.json")))
    for key in keys:
        src, out = os.path.join(src_root, key), os.path.join(out_root, key)
        os.makedirs(out, exist_ok=True)
        preset = json.load(open(os.path.join(src, "preset.json")))
        for name, value in DECK_TUNING.get(key, {}).get("params", {}).items():
            preset["parameters"][name]["initial"] = value
        for index, changes in DECK_TUNING.get(key, {}).get("passes", {}).items():
            preset["passes"][index].update(changes)
        lines = [f"preset {key}"]
        if DECK_TUNING.get(key, {}).get("canvas"):
            lines.append("canvas 1")
        for name, p in preset["parameters"].items():
            lines.append(f"param {name} {p['initial']} {p['minimum']} {p['maximum']}")
        for t in preset["textures"]:
            shutil.copyfile(os.path.join(src, t["file"]), os.path.join(out, t["file"]))
            lines.append(f"lut {t['name']} {t['file']} {'linear' if t['filter'] == 'Linear' else 'nearest'} "
                         f"{WRAPS.get(t['wrap'], 'border')} {1 if t.get('mipmap') else 0}")
        passes = preset["passes"]
        for i, p in enumerate(passes):
            # The preset's flags win over the shader's #pragma format, sRGB last; the last pass draws into the output
            fmt = FORMATS.get(p["format"], "rgba8")
            if p["float_framebuffer"]:
                fmt = "rgba16f"
            if p["srgb_framebuffer"]:
                fmt = "rgba8-srgb"
            if i == len(passes) - 1:
                fmt = "rgba8"
            sc = p["scale"]
            scale = (f"{sc['x']['type']} {sc['x']['factor']} {sc['y']['type']} {sc['y']['factor']}"
                     if sc.get("valid") else "Input 1.0 Input 1.0")
            lines.append(f"pass {i} {p['alias'] or '-'} {'linear' if p['filter'] == 'Linear' else 'nearest'} "
                         f"{WRAPS.get(p['wrap'], 'border')} {1 if p['mipmap_input'] else 0} {p['frame_count_mod']} "
                         f"{fmt} {scale}")
            for stage in ("vert", "frag"):
                glsl, fields, size, samplers = convert(
                    tuned_glsl(key, f"pass{i}.{stage}", open(os.path.join(src, f"pass{i}.{stage}")).read()), stage, i == len(passes) - 1)
                tmp = os.path.join(out, f"pass{i}.{stage}")
                with open(tmp, "w") as f:
                    f.write(glsl)
                spv = tmp + ".spv"
                r = subprocess.run([glslang, "-V", "--quiet", "-S", stage, "-o", spv, tmp], capture_output=True, text=True)
                if r.returncode != 0:
                    sys.exit(f"{key} pass {i} {stage}:\n{r.stdout}{r.stderr}")
                os.remove(tmp)
                lines.append(f"{stage} {size} {len(samplers)}")
                lines += [f"u {name} {kind} {offset}" for name, kind, offset in fields]
                lines += [f"s {name}" for name in samplers]
        with open(os.path.join(out, "manifest.txt"), "w") as f:
            f.write("\n".join(lines) + "\n")
    for stage, code in (("vert", RESOLVE_VERT), ("frag", RESOLVE_FRAG)):
        tmp = os.path.join(out_root, f"resolve.{stage}")
        with open(tmp, "w") as f:
            f.write(code)
        r = subprocess.run([glslang, "-V", "--quiet", "-S", stage, "-o", tmp + ".spv", tmp], capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"resolve {stage}:\n{r.stdout}{r.stderr}")
        os.remove(tmp)
    print(f"{len(keys)} presets -> {out_root}")


if __name__ == "__main__":
    main()
