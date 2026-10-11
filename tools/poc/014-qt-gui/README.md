# 014 — qt-gui

Charter reconstructed on 2026-10-10 from the code and the git history (pre-guideline POC).

History: the Qt GUI proof of concept was developed on branch `new-gui` as `tools/poc/qt-gui/`
(`8960753a6`, 2026-08-06, "feat(poc): add Qt GUI proof-of-concept": main window, screen widget, status
indicators, SVG toolbar icons; later framebuffer widget, video modes, app icons, emulator core
integration, zoom, per-platform fullscreen). That branch is not merged into master. Only
`resources/icons/generate_icons.py` reached master, as `tools/poc/qt-gui/`, through the `time-travel`
branch (merge `a921dc1b4`, 2026-08-19); it was copied here in `06afdb117` (2026-09-11); the duplicate was removed in
`d5b7abb99` (2026-10-10).

## Goals

On master: generate the application icon of the new GUI — dark tile, ZX rainbow stripes, white-bordered
badge ("variant 3 (thick white border)") — at every size, plus the macOS `.icns`. The goals of the GUI
exploration itself are on branch `new-gui` (its `tools/poc/qt-gui/README.md`), not on master.

## Requirements

- Python 3 with Pillow; `iconutil` (macOS) for the `.icns`.
- Sizes 1024 / 512 / 256 drawn natively, 128 / 64 / 48 / 32 / 16 scaled from 256.
- Anything else: not recorded.

## Constraints

Not recorded.

## Acceptance criteria / expected results

Not recorded.

## Status

**concluded (pre-guideline)** — the icon design was adopted for unreal-qt in `0a02af4ec` (2026-09-07,
"Adopt new-gui app icon for unreal-qt"), re-implemented as `tools/icons/generate_unreal_icon.py` +
`tools/icons/iconlib.py`, which render every size natively and emit `.icns`, `.ico`, hicolor and Qt
resource PNGs (`unreal-qt/resources/icons/app/`). This script is superseded by them. No `docs/inprogress/`
folder references this POC.

## Files

- `resources/icons/generate_icons.py` — the icon generator. Run: `python3 generate_icons.py`; it writes
  into `<repo>/scratch/poc-014-icons/`.
