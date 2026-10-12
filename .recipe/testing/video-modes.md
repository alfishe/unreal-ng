# Recipe: Which video mode are we in, and do all the surfaces agree

Goal: know the video mode a machine is in, put it into another one, and find every place where the picture's
size or shape is not what the machine says. Every platform: one reading procedure, one table of the modes and how to
reach them, one script that compares the surfaces.

> **How to use the sections:** the reads are WebAPI calls (the script and its checks run host-side over HTTP, which the
> policy in [_common/transports.md](../_common/transports.md) allows for scripted pipelines). MCP: `inspect_state`
> aspects `screen`, `video`, `video_layout` answer the same fields; `capture_media {"action":"screenshot"}` the frame.

## The one place the shape is defined

`core/src/emulator/video/displaygeometry.h` (`DisplayGeometry`). A framebuffer stores a machine's picture in its own
pixels, and those are not square: TS-Conf and Sprinter store 14 MHz dots (720 / 736 wide) one line per TV line, the ATM
and Profi hi-res modes 640 / 608 wide on the same 288 lines. Every picture is shown on a CRT in the **352:288** frame of
the ZX family. Whoever shows or records a frame asks `DisplayGeometry`; nobody derives a shape from the raw size.

| Function | Answers | Used by |
|----------|---------|---------|
| `DisplayFrame(w, h)` | the smallest 352:288 frame holding a `w` x `h` picture, no pixel squeezed (720x288 -> 720x590) | the unreal-qt window and its minimum size |
| `WindowAspect(frame, window)` | the shape of a rectangle of the frame (the working picture) on the CRT: a 256x192 paper 4:3, the ATM's 640x200 of 640x288 1.76 | video wall tiles (fit inside the 4:3 plate, bars left over) |
| `Compute(w, h)` | the square-pixel picture: each stored line shown once or twice (`lineRepeat`), whichever is nearer 352:288 | `Screen::GetDisplayGeometry()`; recording (to be moved onto it, see below) |

## What each surface says

| Surface | Read it with | Fields to compare |
|---------|--------------|-------------------|
| The machine's report | `GET $BASE/emulator/$ID/state/screen/mode` | `video_mode` (text; the name differs per family), `resolution` (the working picture), `framebuffer` (when the family reports it) |
| The presented frame | `GET $BASE/emulator/$ID/capture/screen?area=full&source=live` | `frame.mode` (the mode's name), `frame.width/height`, `frame.source` (`native` / `external` / `composed`), `screen_window` |
| The raster table | `core/src/emulator/video/screen.h` `rasterDescriptors` | `fullFrameWidth/Height` per mode |
| The window (unreal-qt) | macOS: the window list of the process (`--window-port` of the script) | window - chrome = picture; must be `DisplayFrame` x zoom |
| A video wall tile | the tile's source rectangle is `PictureGeometry.screenWindow` of the latched frame | the tile keeps `WindowAspect` inside its 4:3 plate |
| A recording | `GET /recording/status`, the file's size | see [media/video-recording.md](../media/video-recording.md) |

Platform extras, when a family has more than the shared report: TS-Conf `inspect_state {"aspects":["tsconf"]}` (`V_CONFIG`
geometry and mode), Sprinter `state/sprinter/video` (the per-8x8 mode map) and `sprinter_zx_mode`, Next `next_video`,
ZX-Poly `GET /emulator/$ID/zxpoly` (`video_mode`), ATM `afe` / `#FF77` in `state/screen/mode`, Profi `dffd` there.

## Frame sizes and how to reach each mode

A video mode is a machine state, not an API setting: it is reached the way the guest reaches it, by writing the
machine's port. Frame sizes come from the raster table. "Checked" = driven and read back on 2026-10-11 with
`check_video_modes.py --switch`.

| Platform (models) | Mode (`frame.mode`) | Frame | Working picture | How to reach it | |
|-------------------|---------------------|-------|-----------------|-----------------|--|
| 48K, 128K, +2, +2A, +3 | `ZX` / `ZX128` | 352x288 | 256x192 | the only mode | checked |
| Pentagon 128/512/1024 | `Pentagon128K` | 352x288 | 256x192 | default | checked |
| Pentagon | `P384` (overscan) | 384x304 | cropped by the viewport | `PUT /state/screen/overscan {"enabled":true,"viewport":"symmetric\|standard\|screen_only\|full"}` (409 on other machines); the unreal-qt View menu does the same | checked |
| Pentagon 1024 | `P16`, `PMC`, `PHR` | 352x288 | see [pentagon.md](../machines/pentagon.md) | port `#EFF7` features | not driven |
| Scorpion, ProfScorp | `Scorpion` | 352x288 | 256x192 | default | checked |
| Profi (`PROFI`, `PROFI-PLUS`) | `PROFI` / `PROFIHR` | 352x288 / 608x288 | 256x192 / 512x240 | `#DFFD` bit 7 = hi-res (`LD BC,#DFFD / LD A,#80 / OUT (C),A`) | checked |
| Profi 3 (`PROFI3`) | `PROFI` | 352x288 | 256x192 | no hi-res (monochrome board only) | checked |
| ATM-Turbo 450 | `ATM16` / `ATMHR` / `ZX` | 320x288 / 640x288 / 352x288 | 320x200 / 640x200 / 256x192 | address bits A6-A5 of an `OUT`: `#9E` EGA, `#BE` 640x200, `#FE` ZX | checked |
| ATM-Turbo 710, ATM3 | `ATM16` / `ATMHR` / `ATMTX` / `ATMTL` / `ZX` | 320x288 / 640x288 / 640x288 / 640x288 / 352x288 | 320x200 / 640x200 / 640x200 | `#FF77` bits 0-2: 0 EGA, 2 HWMC, 3 ZX, 6 text, 7 linear text (ATM3 only); see [atm/README.md](../machines/atm/README.md) | boot mode checked, `#FF77` not driven |
| TS-Conf, TSL-VDAC2 | `TS16` / `TS256` / `TSTX` / `TSZX` | 720x288 | the graphics window of `V_CONFIG` | the TS registers (`tsconf` aspect); one frame size for all four | boot mode checked |
| Sprinter | `Sprinter` | 736x288 | 640x256 | per-8x8-square mode table, see [sprinter.md](../machines/sprinter.md) | boot mode checked |
| ZX Spectrum Next | `Next` | 640x512 | 512x384 | NR registers / Timex port, `next_video` aspect | boot mode checked |
| ZX-Poly (all three) | the module's (`ZX`, `ZX128`, `Pentagon128K`) | 704x576 (the group's composed frame, `source: composed`) | 512x384 | the master's mode | checked |

Reaching a mode with code (what the script does): pause, write the code at `#8000` in RAM page 2
(`PUT /memory/ram/2/0 {"offset":0,"data":[...]}`), `PUT /registers/pc {"value":32768}`, resume, wait about a second.
The code is `DI`, the `OUT`, `JR $`.

## Detecting a disagreement

```bash
# a running unreal-qt on its own ports (WebAPI 8195 here); every creatable model, plus the modes the script can drive
UNREAL_WEBAPI_PORT=8195 UNREAL_CLI_PORT=8295 UNREAL_MCP_PORT=8395 UNREAL_GDB_PORT=8495 UNREAL_DEZOG_PORT=8595 \
  UNREAL_ZRCP_PORT=8695 cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
python3 tools/verification/video/check_video_modes.py --url http://127.0.0.1:8195/api/v1 --switch --window-port 8195
```

Exit code 0 = every surface agrees; each disagreement is printed as `<-- what`. `--model NAME` (repeatable) limits the
machines. The checks, one line each:

1. `frame.mode` is a mode the script's raster table knows (a mode the table lacks is a **new mode**: add it here and to
   `displaygeometry_test.cpp`).
2. The frame size equals the raster table's size for that mode.
3. The screenshot's size equals its frame's; the working picture (`screen_window`) lies inside the frame.
4. `state/screen/mode`'s `framebuffer`, when present, equals the frame; its `resolution` equals the working picture
   (native frames only: a ZX-Poly master's composed frame has the group's own sizes).
5. With `--window-port` (macOS): the window's picture is the `DisplayFrame` shape at the zoom of the first fit.

Known limits, so they are not taken for bugs:

- Most families' `state/screen/mode` has no `framebuffer` (only Sprinter and Profi report it); the script says so and
  checks the screenshot against the raster table instead.
- The Next reports `resolution: 512x384` and `video_mode: Next`: its working picture, not its 640x512 frame.
- An active automation session shows the remote-control bar in the unreal-qt window; its toolbar needs about 790 px,
  so the window cannot go below that width and the window check prints a note instead of a mismatch.
- Overscan: `state/screen/mode` reports `framebuffer` 384x304 and the viewport's crop is in the screenshot's `crop`; the script drives it with `--switch` (symmetric, full, off).

Without the script, the manual comparison for one machine is three reads: `state/screen/mode`, `capture/screen`
(`frame.width/height`, `screen_window`), and the window size against `DisplayFrame` of the frame.

## What the GUI does with the mode

- **unreal-qt window.** At start and at every video mode change the window is resized to `DisplayFrame` of the new
  picture at the remembered zoom (aspect kept, shrunk to fit the display), until the user sizes the window by hand
  (`_userResizedWindow`); after that the size is theirs. The screen's minimum size is the picture at 1x of the current
  mode, so no pixel is squeezed.
- **Video wall.** A tile shows the machine's working picture, the rectangle the latched frame names (no longer a fixed
  256x192 at (48,48)), at its own CRT shape inside the 4:3 plate. Screenshots: Tools > *Copy Tile Screenshot*,
  *Save Tile Screenshot As...* (the focused tile), *Save Screenshots of All Emulators...* (one PNG each),
  *Save Wall Screenshot As...* (the wall as it is on the screen). The first three are the core `Screenshotter`, the same
  frame the WebAPI / MCP / CLI return.
- **Recording.** Still sizes its own picture (`recordingmanager.cpp`: half-height rule for TS-Conf / Sprinter, a Profi
  window special case). Moving it onto `DisplayGeometry` is the next step, with a recording of every model and mode.
