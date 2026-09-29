# ctprobe on ZEsarUX, headless

Runs the program (by default the contention probe, [`../../contention/ctprobe`](../../contention/ctprobe/README.md))
on the stock ZEsarUX machines, with no window and no sound. The runner drives each emulator over ZRCP, the
ZEsarUX remote command protocol (a text protocol over TCP). It dumps the probe's memory for the compare script.
See [`../README.md`](../README.md) for the contract all runners follow.

```
ZESARUX_BIN=<path to the zesarux binary> ./run.sh            # all machines
ZESARUX_BIN=<path to the zesarux binary> ./run.sh 48k plus3  # some
./run.sh                                                     # zesarux on PATH
```

Needs ZEsarUX built with networking and the remote protocol (the defaults of its `./configure`), and
`python3`. Output goes to `../out/zesarux/`: `<machine>.bin` (the memory dump), `<machine>.log` (the emulator's
output and the runner's progress), `<machine>.screen.txt` (the final screen as text, from ZEsarUX's own OCR),
`<machine>.compare.txt`, `<machine>.result`.

## Machines

| Machine | ZEsarUX `--machine` | How the program is loaded |
|:--|:--|:--|
| `48k` | `48k` | ZEsarUX's tape autoload types `LOAD ""` |
| `128k` | `128k` | autoload picks the 128K menu's Tape Loader (128 BASIC: paging open) |
| `plus2` | `P2` | as the 128K |
| `plus2a` | `P2A41` (ROM 4.1) | autoload picks the +2A menu's Loader |
| `plus3` | `P341` (ROM 4.1) | autoload picks the +3 menu's Loader, from tape (no disk inserted) |
| `pentagon` | `Pentagon` | the `.trd`: 128 menu, 128 BASIC, `RANDOMIZE USR 15616`, then `RUN` in TR-DOS, typed over ZRCP |
| `atm3` | `BaseConf` (ZX-Evolution BaseConf, beta) | the `.trd` with the Betadisk interface, through the ERS boot menu: `error`, it does not boot (see Results) |
| `scorpion`, `profscorp` | - | skipped: ZEsarUX has no Scorpion |
| `atm710` | - | skipped: ZEsarUX has no ATM Turbo (its only ZX-Evo machines are `BaseConf` and `TSConf`) |
| `profi` | - | skipped: ZEsarUX has no Profi |

The machine list is `zesarux --help` (`--machine id`) and the `MACHINE_ID_*` table in `cpu.h`. The other
Spectrum-compatible machines there (`16k`, `48kp`, the Spanish, French and Microdigital variants, `TC2048`,
`TS2068`, `Inves`, `Chloe`, `Chrome`, `Prism`, `ZXUNO`, `TSConf`, `TBBlue`) are not harness machines.

The tape is inserted as a "real tape" (`--realtape`): it plays through the ROM's own loader, as audio, bit by
bit. It is not the fast load that copies blocks into memory. `--accelerate-loading` runs the emulator at top
speed only while the tape plays.

## Settings

Everything else is ZEsarUX's stock setting for the machine: `--noconfigfile` (your `~/.zesaruxrc` is not
read), contended memory on (the `./configure` default; a build with `--disable-contend` gives wrong results),
the stock ROMs. The runner adds only:

| Setting | Why |
|:--|:--|
| `--vo null --ao null` | no window, no sound |
| `--nowelcomemessage --quickexit` | no start-up logo; `exit-emulator` quits without asking |
| `--accelerate-loading` | top speed while the tape plays |
| `--enable-remoteprotocol --remoteprotocol-port N` | ZRCP, on a free port from 10100 up (`ZESARUX_PORT` sets the first one; 8090 is never used) |
| `--enable-betadisk --enable-trd --trd-file ... --trd-write-protection --trd-no-persistent-writes` | Pentagon and ZX-Evo only: the disk, never written back |

## How it works

- `run.sh` starts one ZEsarUX per machine, all at the same time, each on its own ZRCP port. `zrcp.py run`
  connects to each, types the TR-DOS commands for the Pentagon, then polls the `DONE` byte once a second.
- On the ZX-Evo, `zrcp.py` first waits up to 30 seconds for the ERS boot menu: words on the screen
  (`get-ocr`). The steps through that menu to TR-DOS are not written, because ZEsarUX 13.0 never shows it. If
  the menu does come up, the result is `error` with the screen text, so the steps can be added then.
- **The machines run in real time**, 50 frames a second. ZEsarUX's "top speed" is only reachable from its menu
  or an F key, not over ZRCP. Its `--emulatorspeed` setting changes the T-states per frame, which would change
  the timing under test. A full probe run takes 3 to 5 minutes. All machines run together, so one pass over all of
  them takes about 5 minutes.
- Frames are counted from ZEsarUX's T-state counter (`get-tstates-partial`, read and reset at each poll), and
  the run gives up after `MAX_FRAMES`.
- At the end it dumps `START`..`PROBEEND - 1` with `read-memory`, saves the screen text (`get-ocr`), and
  quits the emulator (`exit-emulator`).

`zrcp.py cmd <port> <command>...` sends single commands to a running ZEsarUX, for example
`zrcp.py cmd 10100 get-registers "read-memory 40016 1"`.

## Building ZEsarUX for this

From a ZEsarUX source checkout (`src/`), a minimal build with no GUI and no sound libraries:

```
./configure --disable-cocoa --disable-sdl --disable-xwindows --disable-xext --disable-curses \
  --disable-cursesw --disable-aa --disable-caca --disable-fbdev --disable-coreaudio --disable-sndfile \
  --disable-alsa --disable-pulse --disable-dsp --disable-onebitspeaker --disable-linuxrealjoystick
make
```

It must report `Networking support: yes`, `Remote command protocol support: yes` and
`Contended memory emulation: yes`. The binary is `src/zesarux`; it finds its ROMs in its own folder.

## Results (ZEsarUX 13.0, 2026-09-29)

| Machine | Result |
|:--|:--|
| 48K | 10 values wrong in 1 check: P-02 (floating bus) |
| 128K, +2 | 12 values wrong in 1 check: P-02 |
| +2A, +3 | 381 values wrong in 27 checks: the gate array's waits come 4 ticks late (M1-01), and more |
| Pentagon | all values as expected |
| ZX-Evo | `error`: BaseConf does not boot. With the stock `zxevo_baseconf.rom` the screen stays blank, and later it fills with garbage. The CPU runs in the ROM with interrupts off, polling the Kempston mouse ports (`#FADF`, `#FBDF`, `#FFDF`, which ZEsarUX logs as unhandled). ZEsarUX lists BaseConf as beta, and its `TODO_machines` says of it "No llega a arrancar la rom" (the ROM does not get to boot) |
| Scorpion, Scorpion + ProfROM, ATM Turbo 2+, Profi | `skipped`: not in ZEsarUX |

An earlier probe build (2026-09-28) read port `#1FFD` at its start. ZEsarUX emulates a 128K quirk there: it
treats a read of any port with address bits 1 and 15 low as a write to `#7FFD` of the byte on the data bus. It
gets `#FF` there, so the memory paging locked (page 7 at `#C000`, the second screen shown) and the 128K, +2 and
Pentagon reported paging `no`. The current probe reads `#1FFD` only when it finds the CPU fast, so the page
checks run.
