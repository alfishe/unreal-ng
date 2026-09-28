# Spec256 Resources — Emulators, Hardware, Games, Tools, Community

All URLs verified reachable 2026-09-27 (or via Wayback Machine where noted).
Local copies: anything marked *local* exists under the emulators collection at
`github/…` on this machine (relative to the collection root).

## Emulators

| Emulator | Spec256 support | Evidence / notes |
|:--|:--|:--|
| **Spec256** (DOS, 1999, Iñigo Ayo Blázquez, x86 asm) | Creator | v1.2 "final"; 48K only; F2/F3 toggle; site on emulatronia.com ([history](http://web.archive.org/web/20000229223744/http://www.emulatronia.com/emusdaqui/spec256/historia-eng.htm), [how it works](http://web.archive.org/web/20000229183516/http://www.emulatronia.com/emusdaqui/spec256/comofunciona-eng.htm), [downloads](http://web.archive.org/web/20000229202622/http://www.emulatronia.com/emusdaqui/spec256/download-eng.htm); emulator zip [Sp256v12.zip](http://web.archive.org/web/20060510135706id_/http://www.emulatronia.com:80/spec256/Sp256v12.zip)) |
| **EmuZWin** ≥2.4 (Windows, Vladimir Kladov, 2004–2006) | Full + authoring tools | Added the GFX Editor (colour painting), backgrounds, probe palettes, `.ezx`; docs `256_color_games.htm`, `EZXFormat_Eng.htm`; [site (archived)](http://web.archive.org/web/2022/http://f0460945.xsph.ru/apps/EmuZ/EmuZWin_Eng.htm) |
| **ZX-Poly** ≥2.0.1 (Java, Igor "raydac" Maznitsa) | Full (restricted), ZIP container | *local*: `github/zxpoly`. Spec256 is a board mode: 1 master module + 8 GFX cores; per-game `spec256appbase.txt` DB; test archives double as fixtures. [repo](https://github.com/raydac/zxpoly) |
| **GZX** (Jiri Svoboda) | Yes | `video/spec256.c` renderer + `sp256.pal` + background loader ([source](https://github.com/jxsvoboda/gzx), [spec256.c](https://github.com/jxsvoboda/gzx/blob/master/video/spec256.c)) |
| **oozx** (fpetrola) | Yes | "nine Z80s in lockstep" (1 main + 8 GFX); 29 public releases tested at 95–100 % visual accuracy ([Spec256 doc](https://github.com/fpetrola/oozx/blob/master/doc/wiki/Spec256-256-Colours.md)) |
| **Xpeccy** | **No** | Its `256color` code is IBM-PC VGA (Pentagon-1024/TS-family) emulation only — verified in *local* `github/Xpeccy/src/libxpeccy/video/vga.c` |
| ZXMAK2, Zero/Ziggy, ZX Spin, zxsp, Spectaculator, Klive, Unreal/UnrealSpeccy | **No evidence** | Code + web search found nothing; treat as unsupported |

⚠ `spec256.narod.ru` could not be verified — zero Wayback captures; the
canonical 1999 site was emulatronia.com.

## FPGA / hardware implementations

No 1999-era hardware exists; Spec256 became real silicon only as FPGA
soft-cores (all in the mvvproject / Andy Karpov circle):

- **ReVerSE-U16 `u16_spec256`** (2016–17) — first core: 8× T80 @3.5/7 MHz
  (one CPU per colour plane), later a single GFX_Z80; HDMI 640×480; games
  from SPIFlash at fixed offsets —
  [readme](https://github.com/mvvproject/ReVerSE-U16/blob/master/u16_spec256/readme.md).
- **DivGMX `divgmx_spec256`** (2017) — T80_GFX @3.5 MHz, VGA 256×192,
  background layer —
  [readme](https://github.com/mvvproject/DivGMX/blob/master/divgmx_spec256/readme.md).
- **karabas-go-core-spec256** (2026, Andy Karpov) — port of the U16 core to
  Karabas Go hardware; loading tool validates SNA=49 179 / GFX=393 216 —
  [core](https://github.com/andykarpov/karabas-go-core-spec256),
  [tool](https://github.com/andykarpov/karabas-go-tools/blob/master/spec256/spec256.py).

Distinguish these from unrelated "256K/512K" **RAM** upgrades (Pentagon 256,
Scorpion 256T+ — the local `github/Scorpion256TPlus` KiCAD project is a RAM
board, not Spec256). A ZX Spectrum Next port was
[asked about](https://gitlab.com/victor.trucco/zx-spectrum-next-cores/-/issues/3)
but never materialized.

## Games (the content library)

- **Original 12 (1999):** Jetpac, Sabre Wulf, Underwurlde, Knight Lore, Game
  Over 1/2, Phantis, Army Moves 1/2, Abu Simbel Profanation, Solomon's Key,
  Cybernoid (colourists: David Goti, Iñigo Ayo, Imanol Zalbidea, Alberto
  Rodríguez).
- **EmuZWin/Yantra era (2004–2006, ~35 titles):** Head Over Heels, Cybernoid
  II, Scooby Doo, Bruce Lee (128K SNA), Atic Atac, Dizzy 1/2, Exolon, Mad Mix
  2, Pac Mania, Gun Runner, Bubbler, Highway Encounter, Chuckie Egg, Booty,
  Renegade, Lode Runner, Silk Worm, … colourists incl. Anthony Lycett,
  Armando Quaranta, João Paulo, MatGubbins, Ville Torren.
- **Primary download:**
  [github.com/mvvproject/Spec256-Games](https://github.com/mvvproject/Spec256-Games)
  (2019 mirror: [helpquick/Spec256-Games](https://github.com/helpquick/Spec256-Games)).
  Some sets ship only `.gfx`/`.ezx` (snapshot under copyright — supply your
  own). Original zips also on archived emulatronia pages.
- ⚠ **"Chase HQ 256" does not exist** — Chase H.Q. appears only as the regular
  128K game; any such claim is conflating Spec256 with something else.

## Tools

| Tool | Purpose |
|:--|:--|
| **EmuZWin GFX Editor** (built-in) | the canonical colourizer: paint the 8 plane bytes while the game runs; GFX memory + register debug panels |
| **Bmp2RawBk256** (+ Delphi source in EmuZWin_Addons) | BMP → `.bnn` background; `/P` emits matching `.pnn` via nearest-colour mapping against the default palette (this source is the cleanest palette definition) |
| **karabas-go-tools `spec256.py`** | SNA+GFX → FPGA `.256` image; useful as a format validator |
| **[spec256-studio](https://github.com/jattree/spec256-studio)** (MIT, browser) | inspect/paint `.gfx` shadow files, JSON palettes, 48K/128K |

## Community / references

- zx-pk.ru thread **"Игры под Spec256"** (2017+, init. Spectramine):
  https://zx-pk.ru/threads/28471-igry-pod-spec256.html
- Wikipedia: [ZX Spectrum graphic modes](https://en.wikipedia.org/wiki/ZX_Spectrum_graphic_modes)
- Yantra Games ZX256 (Arjun Nair, 2005, archived):
  http://web.archive.org/web/2010/http://www.yantragames.com/ZX256.html
- Reddit r/zxspectrum 256-colour how-to (2025):
  https://www.reddit.com/r/zxspectrum/comments/1oaejvm/
- YouTube: DOS emulator demo `EEiKOAsgB9Q`; FPGA demos `0wNCMqNwaIU`, `5JCH4aDUbvE`
- Article: https://www.t2e.pl/try2video/varia/spec256-emulator---zx-spectrum-games-in-256-colours.4162.8

## What exists locally (emulators collection)

- `github/zxpoly` — **the only complete Spec256 implementation in the
  collection** (and our line-level reference; see the survey doc). Includes
  three ZIP fixture archives under
  `zxpoly-emul/src/test/resources/snapshots/` and the per-game profile DB.
- The original DOS emulator source is **not present anywhere** in the
  collection; the 1999 semantics are reconstructed from its readme + EmuZWin
  docs + zxpoly/GZX code (all mutually consistent).
