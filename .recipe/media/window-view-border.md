# Recipe: show the border, the overscan and its crops (window view vs overscan viewport)

Goal: from automation, make the unreal-qt window show the picture with its border or without it, switch the Pentagon's
overscan (the larger frame) on with one of its crops, and not confuse the two crops the emulator has.

Reference: `core/src/emulator/video/windowdisplay.h` (the `view` field), `core/src/emulator/video/pictureview.h` (the
modes), `PUT /api/v1/emulator/{id}/video/window` and `/video/viewport` in
[webapi-interface.md](../../docs/emulator/design/control-interfaces/webapi-interface.md). Verified 2026-11-12 on a
Steam Deck (unreal-qt in Game Mode, a Pentagon 128K): the window started in `content`, `view: full` brought the
border back; overscan on with `symmetric` gave 352x304 of 384x304.

## Two crops

| What | Words | Changes | Surfaces |
|---|---|---|---|
| **Window view**: which part of the frame the window shows | `full` (the frame with its border), `screen` (the 256x192 screen), `content` (the picture area the machine draws) | what the user sees; the machine and its captures stay as they are | WebAPI `PUT /video/window {"view": ...}`, MCP `window_display_set {display: {view}}` |
| **Overscan viewport**: how much of the overscan frame the machine produces | `full`, `symmetric`, `standard`, `screen_only`, `next`, `prev` | the framebuffer itself (captures with `area=full` too) | catalogue `video.viewport {viewport}`, WebAPI `PUT /video/viewport`, MCP `viewport_set` |

"Show the border" / "режим с бордюром" is the **window view** `full`. A handheld start (Steam Deck Game Mode) uses
`content`: the picture fills the screen, no border. A capture with `area=full` shows the border even while the window
hides it: look at the window (`source=window`, or `gamescopectl screenshot` on the Deck) to see what the user sees.

## WebAPI

```bash
H=127.0.0.1:8090
I=$(curl -s $H/api/v1/emulator | python3 -c "import sys,json; print(json.load(sys.stdin)['emulators'][0]['id'])")
curl -s $H/api/v1/emulator/$I/video/window | grep -oE '"view":"[a-z]+"'          # now
curl -s -X PUT $H/api/v1/emulator/$I/video/window -H 'Content-Type: application/json' -d '{"view":"full"}'
```

The window's settings need no control session; headless: 409 `no-window`.

## Overscan and its crops

> **Limitation (for now): overscan exists on the Pentagon only.** Every other model answers
> `overscan_available: false`, and switching overscan on or picking a crop other than `full` answers 409
> `unavailable`; on those machines only the window view (above) changes what of the border is seen.

Overscan is the larger frame of the Pentagon (`overscan_available`): 384x304 instead of 352x288, the paper at
(48, 64), so 48 px border on the left, 80 on the right, 64 lines on top, 48 at the bottom. Its crop (the viewport):

| Viewport | Display | Crop | What |
|---|---|---|---|
| `full` | 384x304 | none | the whole overscan frame, the right and top borders wider than the others |
| `symmetric` | 352x304 | 32 px off the right | **horizontally centred**: 48 px border left and right, all the lines (the default when overscan goes on) |
| `standard` | 352x288 | 32 px right, 16 lines top | the plain Pentagon picture: 48 px border all round |
| `screen_only` | 256x192 | all the border | the paper only |

Overscan off: the normal 352x288 frame, viewport `full`. A crop other than `full` needs overscan on (409 `unavailable`
otherwise); `next` / `prev` step through the four. The crop (`/video/viewport`) changes the picture, not the machine: no
control session. Overscan on / off (`/state/screen/overscan`) changes the machine's video mode (P384): without a
session it opens an implicit one; end it with `DELETE /control/session` and its id in `X-Unreal-Session`.

```bash
# overscan on, horizontally centred (or full / standard / screen_only); off: {"enabled": false}
curl -s -X PUT $H/api/v1/emulator/$I/state/screen/overscan -H 'Content-Type: application/json' \
  -d '{"enabled": true, "viewport": "symmetric"}'
# the crop alone, overscan staying on
curl -s -X PUT $H/api/v1/emulator/$I/video/viewport -H 'Content-Type: application/json' -d '{"viewport": "full"}'
curl -s $H/api/v1/emulator/$I/video/viewport          # {overscan, overscan_available, viewport, framebuffer, display}
```

The same as catalogue actions (any surface: `POST /api/v1/controllers/actions/<id> {instance, params}`, the Stream
Deck, the handheld menu): `video.overscan_toggle {on?}`, `video.viewport {viewport: full|symmetric|standard|
screen_only|next|prev}`. MCP: `viewport` / `viewport_set {viewport}`.

To see the whole overscan frame in the window, the window view must be `full` too (`content` cuts it to the picture).

## MCP

```text
window_display_set {"display": {"view": "full"}}
window_display                                   # the summary line names the view
viewport_set {"viewport": "symmetric"}           # the overscan crop
viewport                                         # "overscan on, viewport symmetric (352x304 of 384x304)"
```
