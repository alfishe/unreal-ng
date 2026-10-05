# Running the original TS-Labs Unreal Speccy on macOS (CrossOver / wine)

The TS-Labs fork of Unreal Speccy (`tslabs/zx-evo-unreal`, the emulator TS-Conf grew up with) is the
reference we compare unreal-ng's TS-Conf against when a program looks different from the real machine.
It is a Windows program (DirectX), so on the macOS development host it runs under wine. These are the
steps that worked on 2026-10-04 with CrossOver's wine; nothing has to be built.

Use it for side-by-side checks: run the same program (the same SD card image) in both emulators and
compare the screens. Example from 2026-10-04: `zifi.spg` 0.733 in both, the picture geometry (360 x 288
raster, 20-dot side borders, 24-line top and bottom borders), the grey strip under the status bar and the
mouse pointer vanishing into it at the bottom are the same in both (the program's own limit, see
[the ZiFi TODO](../2026-10-02-tsconf-zifi/TODO.md), Z5 follow-up 7).

## 1. Get the emulator

The binaries live in the TS-Labs repository, packed with 7-Zip:

| Archive | Content |
|:--|:--|
| [`unreal_x64.7z`](https://github.com/tslabs/zx-evo/raw/master/pentevo/unreal/Unreal/bin/unreal_x64.7z) | 64-bit build (used here) |
| [`unreal.7z`](https://github.com/tslabs/zx-evo/raw/master/pentevo/unreal/Unreal/bin/unreal.7z) | 32-bit build |

The 64-bit archive at `tslabs/zx-evo` commit `ee7de015` (2026-09-06) holds `Unreal.exe` built 2024-07-08,
SHA-256 `5c903e4a28994f4d6601039c9c94dd55bb51daf1bd6d0a57fafe3d498f0d97ee`; the archive itself is
`b7a7b5e6f683b3f8dfadc6bba0e701a58d2e9703d94346d3362ca75d10a400dd`.

Unpack it into a folder of its own, under `scratch/` when it is only for a check:

```bash
mkdir -p scratch/tslabs-unreal
curl -L -o scratch/unreal_x64.7z https://github.com/tslabs/zx-evo/raw/master/pentevo/unreal/Unreal/bin/unreal_x64.7z
7z x -oscratch/tslabs-unreal scratch/unreal_x64.7z     # 7z from Homebrew: brew install p7zip
```

The folder then holds `Unreal.exe`, `Unreal.ini`, the ROMs (`rom/`, TS-Conf uses `rom\zxevo.rom`), the
DLLs and a sample SD card image `wc.img` (Wild Commander).

## 2. Set it up for TS-Conf

`Unreal.ini` already starts the TS-Conf machine (`HIMEM=TSL`, 4 MB RAM, Z-Controller on). The settings
worth knowing (the file is in the Windows-1251 code page; plain ASCII edits are safe):

| Setting | Section | Meaning |
|:--|:--|:--|
| `HIMEM=TSL` | `[MISC]` | the machine: TS-Conf |
| `TS_VDAC=5BIT` | `[MISC]` | video DAC: `NONE` (2-bit DAC + PWM, a board without VDAC), `3BIT`, `4BIT`, `5BIT` |
| `SDCARD=wc.img` | `[ZC]` | the SD card image (a path relative to the folder works) |
| `Border=3` | `[VIDEO]` | visible area: 3 = 360 x 288, the whole TS-Conf raster |
| `ScrShot=PNG`, `ScrShotDir=.` | `[VIDEO]` | screenshot format and folder |

To run a program from an unreal-ng SD image, copy the image next to `Unreal.exe` and point `SDCARD` at it:

```bash
cd scratch/tslabs-unreal
cp ~/path/to/tsconf-zifi-sd.img zifi.img
sed -i '' 's/^SDCARD=wc.img/SDCARD=zifi.img/' Unreal.ini
```

## 3. Run it with CrossOver

CrossOver's wine is at `/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine`. Make a
bottle (a separate Windows environment) once:

```bash
CX=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
"$CX/cxbottle" --bottle unreal-tslabs --create --template win10_64 --description "TS-Labs Unreal"
```

Let the Option key work as Alt (Unreal's hot keys use Alt; the wine Mac driver does not map Option to Alt
by default):

```bash
"$CX/wine" --bottle unreal-tslabs reg add 'HKCU\Software\Wine\Mac Driver' /v LeftOptionIsAlt /t REG_SZ /d y /f
"$CX/wine" --bottle unreal-tslabs reg add 'HKCU\Software\Wine\Mac Driver' /v RightOptionIsAlt /t REG_SZ /d y /f
```

Start the emulator with the absolute path of the program and its folder as the working directory
(`wine --bottle unreal-tslabs Unreal.exe` alone fails with `winewrapper.exe:error: cannot execute
L"Unreal.exe"`):

```bash
cd scratch/tslabs-unreal
"$CX/wine" --bottle unreal-tslabs --workdir "$PWD" -- "$PWD/Unreal.exe"
```

The window opens with the TS-BIOS boot and then Wild Commander from the SD image. Plain wine (Homebrew
`wine-stable`, Linux) should work the same way with `WINEPREFIX=<folder> wine Unreal.exe`; that was not
tried.

## 4. Use it

| Key | Action |
|:--|:--|
| Shift+Esc | capture the mouse for the Kempston mouse (press again to release) |
| Option+F8 (Alt+F8) | screenshot: `sshot000000.png`, `sshot000001.png`, ... in the emulator folder |
| Shift+Option+F8 | screenshot to the clipboard |
| Option+Enter | full screen on / off |

The full key list is in `[SYSTEM.KEYS]` of `Unreal.ini`; `help_eng.html` in the folder describes the rest.

The emulator's own screenshot is the way to get its picture into a file for comparison: it is the exact
emulated frame, while a macOS screen capture needs the Screen Recording permission for the terminal
(without it `screencapture` returns only the desktop background).

## 5. Things to know

- **Drive it by hand.** Scripting the window from a terminal (System Events / CGEvent key presses) is
  unreliable: the wine window does not keep the keyboard focus when another application takes it back.
  For a side-by-side check a person operates the original and unreal-ng is driven through the WebAPI.
- **Newer programs may refuse it.** `zifi.spg` 0.733 (sources:
  [andrewinsidelazarev/ZiFi-ESP32-S3-Zero](https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero), `ZiFi SPG/`) stops in it with "Error: Please update TS Conf." (the
  program checks the TS-Conf version the emulator reports); the screen layout, sprites and the mouse
  cursor are still shown, which is enough for video comparisons.
- **It is not the hardware.** The fork differs from the TS-Conf Verilog in a number of places
  (hardware-spec.md §12). When the original, unreal-ng and a photo of the real machine disagree, the RTL
  (`tslabs/zx-evo`, `pentevo/fpga/current`) decides.
- **Clean up.** Delete the bottle when it is no longer needed:
  `"$CX/cxbottle" --bottle unreal-tslabs --delete --force`, and the `scratch/` folder with it.
