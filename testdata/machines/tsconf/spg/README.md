# TS-Conf SPG test programs

Three programs in the SPG ("Spectrum Prog") v1.0 snapshot format of the TS-Conf
SDK, used by `core/tests/emulator/machines/tsconf/loaderspg_test.cpp` (SPG-1,
SPG-2) and the TS-Conf boot tests.

| File | Size | Blocks | Compression | What it shows |
|:--|--:|--:|:--|:--|
| `empty.spg` | 6656 | 4 | Hrust | the SDK's empty project: starts, clears the screen, idles at 14 MHz |
| `sprites.spg` | 17920 | 8 | Hrust | the SDK's sprite example: TSU sprites over a tile background |
| `slideshow.spg` | 96256 | 14 | MegaLZ + Hrust | the SDK's slide show: 16-color pictures |

## Origin and licence

Built examples of the TS-Conf SDK `evosdkts` from the tslabs/zx-evo repository
(`pentevo/sdk/evosdkts/empty_project`, `example_sprites`, `example_slideshow`).
The SDK's `license.txt`: the custom code and tools are written by Shiru and
Alone Coder and released into the public domain; the pictures are conversions
of public-domain photos (public-domain-image.com, commons.wikimedia.org). The
bundled MegaLZ depacker is (C) fyrex^mhm, the PT3 player (C) S.V. Bulba.
