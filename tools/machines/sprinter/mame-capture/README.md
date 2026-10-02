# mame-capture

`mame-capture.sh` starts MAME's `sprinter` driver headless with `mame-capture.lua` as the autoboot
script and writes reference captures (modes `boot`, `loader`, `sync`, `palette`). The captures, the
exact command lines and the variables (`SPC_END`, `SPC_FLOP1`, `SPC_FLOP2`, ...) are documented in
[testdata/machines/sprinter/reference/README.md](../../../../testdata/machines/sprinter/reference/README.md).
The MAME build itself: [tools/verification/coemu/mame/README.md](../../../verification/coemu/mame/README.md).
Run files go to `build/` here (git-ignored).

`mame-zxsteps.sh` (with `mame-zxsteps.lua`) plays a scripted user session instead: keys typed at fixed frames
(DSS through the PC keyboard, the Spectrum mode through MAME's matrix), screenshots, cassette play, a snapshot
load, a soft reset. Used for the ZX-mode captures in
[testdata/machines/sprinter/reference/zx-mode/](../../../../testdata/machines/sprinter/reference/zx-mode/README.md).
