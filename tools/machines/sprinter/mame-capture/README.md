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

Since 2026-10-02 the session tool also drives MAME's debugger headless (`SPC_DEBUG=1`; `dbg|bpset ...` with a
`printf` action writes to `debug.log`, e.g. the INT handler's beam position), dumps the video RAM (`vram|<name>`:
the mode table that places the INT, compared byte for byte with unreal-ng's), holds input fields for chords the
natural keyboard cannot type (`fields|:IO_LINE4/6   Down       &    YELLOW   MOVE+:IO_LINE4/CS Line4`: the cursor
down of the Spectrum menus) and mounts a floppy (`SPC_FLOP1`). `SPC_WAV=<file.wav>` records the sound (8 channels at 48
kHz; with `-isa0 zxbus_adapter -isa0:zxbus_adapter:card neogs` as MAME arguments the NeoGS is channels 6 / 7: ISA
phase I2, [test-mod/](../test-mod/README.md)). `ZXK_SEP=~` changes the step separator when a step
holds `;`. Used for the zxtime comparison in
[tdd-zx-mode.md](../../../../docs/inprogress/2026-09-28-sprinter/tdd-zx-mode.md) §4.1.
