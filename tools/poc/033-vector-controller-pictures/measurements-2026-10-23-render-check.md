# Measurements 2026-10-23: render check, sizes, render cost

## Setup

- macOS 15 (Darwin 24.6), Apple Silicon, the machine shared with other sessions (loaded).
- Qt: PySide6 6.8.3 `QSvgRenderer`, offscreen platform (the app links Qt 6.9.3, same SVG module).
- rsvg: `rsvg-convert` (librsvg from Homebrew).
- Pictures: `python3 generate.py` at the commit of this file, 23 image sets (the second round added dualshock-4
  and the nine 8BitDo sets and redrew generic-playstation-like).

## Method

- `render.py` renders every `front.svg` at 2048 x 1536 in both renderers, flattens both on the same background and
  takes the mean absolute difference per channel (of 255) and the share of pixels off by more than 32 in any channel.
- The same script repaints the dialog the way `ControllerPicture::paintEvent` does (same constants: 170 px callout
  columns, lit QColor(255, 170, 0, 150), selected (0, 140, 255, 120), rounded rect radius 6) on the dark and the light
  palette, with nothing held, every control held, and one control held at a time.
- Render cost: `QSvgRenderer::render` into an 800 x 600 ARGB32 image, median of 20 runs, one picture at a time.

## Qt vs rsvg

| Set | Controls | SVG bytes | Mean diff /255 | Max channel mean | Pixels off > 32 |
|:--|--:|--:|--:|--:|--:|
| xbox-series | 20 | 11 626 | 0.071 | 0.072 | 0.0 % |
| xbox-one | 19 | 10 867 | 0.071 | 0.072 | 0.0001 % |
| xbox-elite-2 | 24 | 21 341 | 0.085 | 0.086 | 0.0 % |
| dualsense | 21 | 14 785 | 0.063 | 0.064 | 0.0 % |
| switch-pro | 20 | 13 128 | 0.066 | 0.066 | 0.0 % |
| generic-xbox-like | 23 | 15 702 | 0.071 | 0.073 | 0.0 % |
| generic-playstation-like | 19 | 11 462 | 0.065 | 0.066 | 0.0 % |
| generic-nintendo-like | 19 | 10 887 | 0.067 | 0.069 | 0.0 % |
| generic-unknown | 19 | 11 438 | 0.068 | 0.069 | 0.0 % |
| generic-db9-stick | 5 | 5 932 | 0.016 | 0.018 | 0.0 % |
| kempston-joystick | 6 | 5 898 | 0.016 | 0.018 | 0.0 % |
| sinclair-joystick | 5 | 5 902 | 0.016 | 0.018 | 0.0 % |
| cursor-joystick | 5 | 5 902 | 0.016 | 0.018 | 0.0 % |
| dualshock-4 | 20 | 13 774 | 0.064 | 0.065 | 0.0 % |
| 8bitdo-ultimate | 22 | 14 966 | 0.072 | 0.073 | 0.0001 % |
| 8bitdo-pro-2 | 23 | 15 431 | 0.049 | 0.05 | 0.0 % |
| 8bitdo-sn30-pro-plus | 21 | 13 199 | 0.048 | 0.049 | 0.0 % |
| 8bitdo-sn30-pro | 20 | 12 354 | 0.031 | 0.032 | 0.0001 % |
| 8bitdo-sf30-pro | 20 | 12 667 | 0.031 | 0.032 | 0.0001 % |
| 8bitdo-sn30 | 13 | 8 042 | 0.022 | 0.023 | 0.0 % |
| 8bitdo-sf30 | 13 | 8 352 | 0.022 | 0.023 | 0.0 % |
| 8bitdo-m30 | 16 | 8 927 | 0.066 | 0.067 | 0.0 % |
| 8bitdo-zero-2 | 12 | 6 700 | 0.029 | 0.034 | 0.0 % |

The previous line drawings were 0.9-2.7 KB per set.

## Render cost (800 x 600, median of 20, loaded machine)

| Set | ms | Set | ms |
|:--|--:|:--|--:|
| xbox-series | 5.7 | generic-xbox-like | 7.3 |
| xbox-one | 10.8 | generic-playstation-like | 4.2 |
| xbox-elite-2 | 8.5 | generic-nintendo-like | 3.2 |
| dualsense | 9.0 | generic-unknown | 4.1 |
| switch-pro | 6.8 | DB9 sticks | 2.5-6.9 |
| md6-pad (old line drawing) | 0.4 | surface-dial (old) | 0.4 |

Render cost was measured in the first round (13 sets); the new sets are of the same kind and size.

The numbers scatter by about 2x between runs under the load; the order of magnitude is the result.

## Outputs (scratch, not in the repository)

`scratch/2026-10-23-poc-033/overview.png`, `check.json`, and per set `<set>/sheet.png`, `controls.png`, `qt.png`,
`rsvg.png`, `qt-vs-rsvg.png`.
