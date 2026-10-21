# Recipe: CRT effects in the GUI window (profiles, shader presets, GPU vs software)

Goal: from automation, switch the unreal-qt window's CRT effects on and off, choose a profile (Basic, one of the
seven shader models, a libretro preset, a custom shader), look at the result, and check that the GPU and the software
screen draw the same picture.

Reference: [docs/features/crt-effects.md](../../docs/features/crt-effects.md) (what each profile is, why the two
backends agree bit for bit), `core/automation/windowdisplay.h` (the rules every surface shares), `GET` / `PUT
/api/v1/emulator/{id}/video/window`. Verified 2026-10-20 on macOS (a GUI unreal-qt, a Pentagon): every MCP and WebAPI
call below, the errors, and the GPU vs software comparison.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi) inside host-side pipelines
> (the comparison script) or when MCP is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## What there is

- **The window's settings, not the machine's.** They exist only in the GUI (unreal-qt) and belong to the instance the
  window shows; headless or another instance: 409 `no-window`. No control session needed. They are the user's window:
  put them back after a test.
- **Fields:** `gpu` (GPU acceleration on / off: the screen backend), `crt` (effects on / off), `crt_profile`,
  `blending` / `blending_frames` / `blending_weights` (temporal blending, not CRT). The answer adds `backend`
  (`gpu` / `software`), `gpu_available`, `crt_profiles` (the words the window offers) and `emulator`.
- **Profiles** (`crt_profiles`, take the words from there, never from memory):

  | Word | What | Backends |
  |:--|:--|:--|
  | `none` | the plain picture | GPU + software |
  | `basic` | scanlines, slight curvature, a little bloom | GPU + software |
  | `crt-lottes`, `crt-geom`, `crt-easymode`, `crt-gdv-mini`, `crt-consumer`, `crt-pi`, `zfast-crt` | integer models of the single-pass libretro shaders | GPU + software, **the same image bit for bit** |
  | `shader:<key>` (19 libretro presets: `shader:crt-royale`, `shader:guest-advanced`, ...) | the original slang presets | GPU only |
  | `shader:<name>` (a user's `*.glsl` / `*.frag` / `*.fs`) | a custom shader | GPU only |

- **Rules:**
  - A `crt_profile` other than `none` switches `crt` on; `crt=false` keeps the profile (CRT on again shows it).
  - `shader:...` needs the GPU: with `gpu` off (now or in the same change) the change is refused, 409 `unavailable`
    ("A shader preset needs GPU acceleration: add gpu=on"). Send `gpu: true` together with the preset.
  - `gpu` off while a preset runs shows the built-in profile chosen before the preset (its base profile), not the
    preset; `crt_profile` then reads that profile.
  - `gpu: true` on a host without OpenGL (an offscreen GUI, llvmpipe): 409 `unavailable`; `shader:` words are not
    offered there.
  - An unknown word: 400 `bad-parameter`, the error lists every offered word.
- **Seeing it:** a screenshot with `source=window` is the picture as the window draws it (CRT, HUD, its shape, the
  display's pixels); `area=full` only. The other sources (`presented`, `live`) are the emulated frame without effects.

## MCP (preferred)

```text
capture_media {"action":"window_display"}
```

The text names the state and the offered words (an agent sees enough without `structuredContent`):

```text
window gpu on (available), crt off (profile none), blending off (2 frames, equal)
crt_profiles: none basic crt-lottes crt-geom crt-easymode crt-gdv-mini crt-consumer crt-pi zfast-crt shader:crt-beans-rgb ...
```

Change any of the fields with `window_display_set` and `display`; the answer is the state after the change:

```text
capture_media {"action":"window_display_set","display":{"crt_profile":"crt-geom"}}
  -> window gpu on (available), crt crt-geom, blending off (2 frames, equal)
capture_media {"action":"window_display_set","display":{"gpu":true,"crt_profile":"shader:crt-royale"}}
capture_media {"action":"window_display_set","display":{"gpu":false}}          # back to the software screen
capture_media {"action":"window_display_set","display":{"crt":false}}          # effects off, the profile kept
```

Look at it: save the window's picture server-side and open the file (never route base64 through the transcript):

```text
capture_media {"action":"screenshot","source":"window","path":"/abs/path/scratch/crt-geom.png"}
  -> Screenshot saved to ... (703x576 png, ...; full, as the window draws it (gpu, picture 703x576 in a 704x576
     viewport, pixel ratio 1) of the 352x288 Pentagon128K frame, ...)
```

A changing picture makes two grabs differ: pause first when comparing (`control_execution {"action":"pause"}`,
`{"action":"resume"}` after).

Errors (`isError: true`, the HTTP status in the text):

```text
{"display":{"crt_profile":"crt-nonsense"}}               -> HTTP 400: Unknown CRT profile 'crt-nonsense': use none, basic, ...
{"display":{"gpu":false,"crt_profile":"shader:crt-royale"}} -> HTTP 409: A shader preset needs GPU acceleration: add gpu=on
```

## WebAPI

```bash
B=http://localhost:8090/api/v1
ID=$(curl -s $B/emulator | jq -r '.emulators[0].id')          # the instance the window shows
curl -s $B/emulator/$ID/video/window | jq '{backend, gpu, crt, crt_profile, crt_profiles}'
curl -s -X PUT $B/emulator/$ID/video/window -H 'Content-Type: application/json' \
     -d '{"crt": true, "crt_profile": "crt-pi"}' | jq '{backend, crt, crt_profile}'
curl -s "$B/emulator/$ID/capture/screen?source=window&path=$PWD/scratch/crt-pi.png" | jq '{width, height, window}'
```

Without `path` the screenshot answer is JSON with the PNG in `data` (base64): decode it in the script.
Errors are `{"error": ..., "kind": ...}` with 400 `bad-parameter`, 409 `unavailable` or 409 `no-window`.

Other surfaces, the same fields and rules: CLI `video window crt_profile=crt-pi` (`video window` lists the offered
profiles), Lua / Python `window_display()` / `window_display_set(...)`
([command-interface.md](../../docs/emulator/design/control-interfaces/command-interface.md)).

## GPU = software: check that both backends draw the same picture

Every built-in profile (`basic` and the seven models) must give the same image on both backends. Pause, grab with
`gpu` on and off, compare the pixels; put the user's settings back:

```python
# python3 crt_gpu_vs_software.py [port] - every built-in CRT profile, GPU against software, in the live window
import base64, io, json, sys, time, urllib.request
from PIL import Image, ImageChops

B = f"http://127.0.0.1:{sys.argv[1] if len(sys.argv) > 1 else 8090}/api/v1"

def call(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(B + path, method=method, data=data, headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req, timeout=20).read())

ID = call("GET", "/emulator")["emulators"][0]["id"]
window = f"/emulator/{ID}/video/window"
before = call("GET", window)
call("POST", f"/emulator/{ID}/pause")
time.sleep(0.5)
differ = 0
for word in [w for w in before["crt_profiles"] if w != "none" and not w.startswith("shader:")]:
    images = []
    for gpu in (True, False):
        call("PUT", window, {"crt": True, "crt_profile": word, "gpu": gpu})
        time.sleep(1.5)  # the screen re-created, the tables built, a frame drawn
        shot = call("GET", f"/emulator/{ID}/capture/screen?source=window")
        images.append(Image.open(io.BytesIO(base64.b64decode(shot["data"]))).convert("RGB"))
    box = ImageChops.difference(*images).getbbox() if images[0].size == images[1].size else "size"
    print(f"{word:14s} {images[0].size[0]}x{images[0].size[1]} {'same' if box is None else f'DIFFER {box}'}")
    differ += box is not None
call("PUT", window, {k: before[k] for k in ("gpu", "crt", "crt_profile")})
call("POST", f"/emulator/{ID}/resume")
sys.exit(1 if differ else 0)
```

Expected (2026-10-20, a 703x576 window):

```text
basic          703x576 same
crt-lottes     703x576 same
...
zfast-crt      703x576 same
```

`gpu_available: false` (no OpenGL): only the software screen exists, nothing to compare. A difference is a bug in
`CRTRenderer` / `CRTBeamModel` (the unit tests `*_Test.TheGpuPictureIsTheSoftwarePicture` and
`*TheSameBitsOnEveryCompiler` cover the same ground offline).

## Pitfalls

- The words come from `crt_profiles`: the list grows with the presets shipped and the user's own shaders.
- `shader:crt-lottes` (the original preset, GPU) and `crt-lottes` (the integer model, both backends) are different
  profiles; only the models have the bit-for-bit guarantee.
- `source=presented` / `live` screenshots never show CRT effects: use `source=window`.
- The window's size decides the picture's size; a test that compares files across runs keeps the window as it is.
