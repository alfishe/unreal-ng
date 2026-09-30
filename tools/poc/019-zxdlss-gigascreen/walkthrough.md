# Walkthrough - Producing the Reference Material

How the *Across the Edge* reference material was produced on 2026-09-27/28,
in order, including the dead ends. Follow §1-§6 to reproduce it; §8 lists
the pitfalls that cost time.

---

## 0. Prerequisites

- unreal-ng built with the TTD positioning fix (2026-09-28, see §5): use
  `cmake-build-agent-release`.
  ```bash
  cmake --build cmake-build-agent-release --target unreal-qt -j 10
  ```
- Python 3 with `pip3 install -r requirements.txt`.
- `features.ini` next to the binary with `screenhq = on` (per-T-state
  rendering; without it there is no multicolor and no border stripes at all).

## 1. Start the emulator

```bash
./cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt &
curl -s http://localhost:8090/api/v1/emulator      # WebAPI is up when this answers
```

Only one process can own port 8090. Check with `curl`, not `lsof` - on this
machine `lsof -i :8090` hung for minutes (network volumes).

## 2. Record the demo into a TTD session

`capture/record_ttd.py` does all of this; the manual equivalent:

1. Create a Pentagon 512K instance: `POST /emulator/start {"model":"PENTAGON","ram_size":512}`.
2. Insert the disk with autostart: `POST /emulator/{id}/disk/A/insert {"path": "...trd", "autostart": true}`.
3. **Then** start recording: `POST /emulator/{id}/ttd/start {}`.
   Autostart performs a quick reset, and a reset invalidates the TTD session,
   so a recording started before the insert is lost (it silently goes back to
   `idle`).
4. Let the demo run in real time. While TTD records, turbo and the loader
   shortcuts (fast disk, fast tape, turbo tape) are locked off
   (`306c596e`, `005771c8`), so the parts load at real speed and every frame of
   the recording is real-speed code. The `/features` endpoint still shows the
   stored (desired) values - `FeatureManager::isEnabled()` masks them.
5. Stop at the end (the end logo is static; the intro is static for ~46 s,
   hence `--min-frame`): `POST /pause`, `POST /ttd/stop`,
   `POST /ttd/dump {"path": "/abs/path.ttd"}`.

Result used for the analysis: frames 25–16595 (≈ 5 min 40 s at 48.83 Hz),
16571 checkpoints, 84 MB `.ttd`. The development-mode write journal is capped
at 100 MB (8.4 M records), so journal-based reverse queries do not cover the
whole run; seeking and stepping do.

## 3. Verify frame stepping

```bash
python3 capture/verify_stepping.py --emulator <id> --from 4520 --count 30
```

Every line must say `OK`, the position must stay at the frame boundary
(t = a few T-states, not growing), and frames of the "Hip-Hop King" part
(4524+) must show 8 border colors. One border color means a static memory
decode - the build predates the fix in §5.

## 4. Extract every frame

```bash
python3 capture/extract_clip.py --ttd data/across_the_edge_full.ttd --out data/clip_full
```

The script loads the `.ttd` into a new instance, seeks to the session start
and walks with `ttd/step-forward`. After each step it takes
`GET /capture/screen?format=png&mode=full` (352x288 including the border),
`/state/screen` and `/state/paging`. Frames are stored as palette-index planes
(the whole demo uses 15 colors) in zstd chunks of 500 - format in
`common/clip.py`. About 24–32 frames/s, ~10 minutes, 13 MB.

Why step-forward and not playback: positioning by frame number shows the
frame's **final** picture (the display rule of
`docs/inprogress/2026-09-28-ttd-positioning-and-display/design.md`), which is
exactly one picture per emulated frame, deterministic and complete.

## 5. The TTD fix this depends on (2026-09-28)

The first extraction attempt produced frames without border stripes or
multicolor. Root causes, all fixed in the core:

1. **Checkpoint restore painted a static memory decode** of the frame-start
   memory (`ResyncScreenCaches` → `RenderOnlyMainScreen` + border fill). Any
   frame that changes border, attributes or the screen page mid-frame was
   wrong. `3cbfbd39` added a beam-accurate re-render for frame-aligned seeks.
2. **Frame steps carried the T-state overshoot** of the previous checkpoint
   into the target (`SeekTo({frame±1, current.tInFrame})`), so steps took
   the intra-frame branch, skipped the accurate render, and the offset grew
   with every step (9 → 19 → 23 → 33 T). Qt used the same call.
3. Redesign (`docs/inprogress/2026-09-28-ttd-positioning-and-display/design.md`):
   restores never paint; one `ComposeDisplay` decides the picture (frame target
   → final picture; time target → beam up to that T over the previous frame);
   frame steps land on frame boundaries; one publication point for Qt and
   WebAPI; sandbox saves/restores pixels, keyboard and the renderer's draw
   cursor.
4. Found on the way: **the #C000 RAM page was not restored** for
   48K/128K/Pentagon models - `Memory::UpdateZ80Banks()` re-derived only the
   ROM slot, so replays after a seek could run with the previous position's
   page mapped. Every model's decoder now completes the bank rebuild.

Tests: `core/tests/debugger/ttd/timetravelmanager_display_test.cpp` (TTD
position ≡ live machine, pixel for pixel), full suite 4162/4162.

## 6. Analyze

```bash
python3 analysis/effect_map.py data/clip_full --out out/effect_map
python3 analysis/contact_sheet.py data/clip_full --every 250 --out out/contact.png
```

`effect_map.py` computes, per pixel of each final picture, period-2..5
flicker (A,B,A…) and "other" change, for the picture area and the border,
plus how often the displayed screen page toggles. 50-frame blocks with
similar indicators merge into segments (64 for this recording). The curated
result is the storyboard in `docs/disasm/demo/across-the-edge/storyboard.md`
and the reference document in
`docs/inprogress/2026-09-27-zxdlss-gigascreen/reference-across-the-edge.md`.

## 7. Findings

- Every case of the catalogue occurs: static page-flip pictures, flicker
  inside one screen (attributes / multicolor), irregular page flipping (C3),
  border flicker with and without stripes, moving objects over flickering
  backgrounds, full-screen moving flicker, pure motion (negatives).
- **Periods 3–5 are practically absent** (≤ 2 % of the area) - this is
  period-2 material. Periods 3–5 need synthetic clips and 3-color images.
- Border flicker covers up to half of the border and happens without page
  flipping - the border path is required.
- A page flip every frame does not mean the whole picture mixes (frames
  12680–14129: page flips every frame, 15 % of the picture flickers) - masks
  per region are required.

## 8. Pitfalls and dead ends

| Pitfall | What happened | Do instead |
|---|---|---|
| TTD started before autostart | reset invalidated the session (state back to `idle`) | start TTD after the disk insert |
| Relative screenshot paths | files landed in the app bundle's working directory | absolute paths |
| `lsof -i :8090` | hung for minutes | probe with `curl` |
| Extracting on a pre-fix build | static decode: no stripes, no multicolor | build with the §5 fix; run `verify_stepping.py` |
| Re-running the demo live to record video | MCP `record_start` on a paused machine recorded 1 frame and stopped; `ttd/resume` truncates the recorded future; recording is suppressed during TTD replay; turbo loading decimates rendered frames | step through the TTD with the fixed build |
| `features.ini` `screenhq = off` | batch rendering: no per-T-state picture at all | `screenhq = on` |
| Journal cap | dev-mode write journal stops at 100 MB | fine for extraction; reverse queries beyond the cap replay instead |
