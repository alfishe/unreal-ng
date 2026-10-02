# VDAC2 test programs from the TS-Labs FT812 SDK

Prebuilt `.spg` programs (TS-Conf executables) from the TS-Labs repository
<https://github.com/tslabs/zx-evo>, `pentevo/sdk/ft812sdk/<name>/<name>.spg`, last
change of that folder `550d5348` (2026-02-19). Copied unchanged, except that
`test_sd.spg` is stored as `test-sd.spg` (project file-name rule). Sources:
`pentevo/sdk/ft812sdk/<name>/src/main.c`, blocks listed in `res/spg.ini`.

They need a TS-Conf machine with the VDAC2 build (`[MISC] TS_VDAC2=1`) and check
`STATUS & 7 == 7` before using the FT812. All start at 14 MHz. What each exercises,
and what the tests built on them prove: `docs/inprogress/2026-10-01-tsconf-vdac2/vdac2-test-corpus.md` §2.1.

| File | Bytes | Content | Mode |
|---|---|---|---|
| `test1.spg` | 4 608 | code: points, lines, blending, ROM-font text | 1024×768 59 Hz |
| `test2.spg` | 3 584 | code: points of many sizes and colors, alpha | 800×600 60 Hz |
| `test3.spg` | 4 608 | code: color mask, clear alpha, two-pass blending | 800×600 60 Hz |
| `test4.spg` | 964 608 | code + four "DXT" images (240 000 bytes each) uploaded by DMA | 800×600 60 Hz |
| `test5.spg` | 40 448 | code + seven ZX screens shown through the FT812 palette | 800×600 60 Hz |
| `test6.spg` | 750 592 | code + one 1940×768 "DXT" image, scrolled every frame | 1024×768 59 Hz |
| `test9.spg` | 3 584 | code: ROM fonts, `CMD_TEXT`, `CMD_NUMBER`, `CMD_GRADIENT` | 1024×768 59 Hz |
| `test-sd.spg` | 11 264 | code: SD card access with the FT812 on the same SPI bus | - |

Each program also carries the SDK's ZX warning screen, shown when no VDAC2 is found.
`test9` draws ROM-font glyphs: tests that compare its picture need the FT81x ROM image
(`tools/machines/tsconf/vdac2/extract-ft81x-rom.py`, never committed).

SHA-1 (first 12 digits): test1 c80760e52c30, test2 0a4a7a808b2c, test3 85195e06e034,
test4 5d27c78cfd1c, test5 c4e1daa2d012, test6 301fb6563646, test9 d810cab90038,
test-sd cfeac6ecd2fb.
