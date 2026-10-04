# Recipe: Profi v5 (`PROFI`) and v3 (`PROFI3`)

The Profi is a clone with its own `#DFFD` paging extension, a SYS
(service/menu) ROM it boots into, mode-dependent FDC port sets, a Covox DAC
and a 512x240 hi-res video mode. It came as two board families, and both are
machines here
([docs/inprogress/2026-10-01-profi-v3-v5/](../../docs/inprogress/2026-10-01-profi-v3-v5/README.md)):

| | `PROFI3` (v3.x, Kramis, 1990) | `PROFI` (v5.0x, Kondor, 1993-94; alias `PROFI5`) |
|:--|:--|:--|
| RAM | **512K**, 1024K | 512K, **1024K** |
| ROM | `rom/profi/kramis-v03.rom` (BIOS V0.3 + TR-DOS 5.04T; V0.2 + TR-DOS 5.03 in `kramis-v02.rom`) | `rom/profi.rom` |
| Hi-res 512x240 | monochrome | 16 colours, palette `OUT #xx7E` |
| Extended ports (CP/M + ROM14), RTC, IDE | none | yes |
| Frame (default `[PROFI] SyncProm=`) | 69888 T, INT 12580 T before paper | 69888 T, INT 14368 T before paper |

**`PROFI-PLUS`** (alias `PROFIPLUS`, a machine variant, Machine menu in unreal-qt): a `PROFI` with Djoni's V0.03
port decoder PROM (`[PROFI] ExtPorts=v003`: the extended ports also from the SYS ROM, and the RTC / long ports in TR-DOS) running Vadim's ROM BIOS Plus
0.41h1 (`rom/profi/bios-plus-041h1.rom`). It boots PQ-DOS from a floppy or an IDE disk (`ide0.master`) and runs DOS
Navigator. Its start-up board test passes the FDC, drives, parallel port (8255), serial port (8253 + 8251, see
[Serial port](#serial-port-com)), RTC and AY. PQ-DOS disks and a 2 GB HDD image: see
`docs/inprogress/2026-10-04-profi-plus/design.md`; create it with `{"model":"PROFI-PLUS"}`.

`[PROFI] SyncProm=` picks another sync PROM: `0a1d`, `samx6`, `fb0579b6`
(71680 T, INT 48 T before paper) or `v503`.

Ground truth:
[portdecoder_profi.h](../../core/src/emulator/ports/models/portdecoder_profi.h)/[.cpp](../../core/src/emulator/ports/models/portdecoder_profi.cpp),
MCP resource `unreal://machine/profi` (port tables, ROM pages, video modes),
design + status docs in
[docs/inprogress/2026-09-21-profi/](../../docs/inprogress/2026-09-21-profi/)
(`technical-design.md`, `2026-09-25-profi-reconciliation.md` for the parity
matrix and open gaps, `TODO.md`).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `emulator_manage` creates the machine, `inspect_state` / `invoke_api` read
> paging, video and ports. Use [WebAPI](#webapi) only inside host-side
> Python/bash pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)). Shared patterns:
> [_common/machines.md](../_common/machines.md).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"PROFI"}    # v5, boots into the SYS/BIOS menu
emulator_manage {"action":"create","model":"PROFI3"}   # v3, boots into the Kramis BIOS menu

emulator_manage {"action":"list_models"}
#   → models[].name (NOT .id, which is a numeric index) == "PROFI",
#     creatable:true, available_ram_sizes_kb:[512,1024]

inspect_state {"aspects":["paging"]}
#   → profi_board (v3 / v5), p7FFD + pDFFD with decoded fields extended_ram_bank, sco, worom, cpm,
#     scr, video_512x240; banks[0].role shows which ROM page is mapped

inspect_state {"aspects":["video"]}
#   → video_mode = PROFI (256x192) or PROFIHR (512x240, #DFFD bit 7)

invoke_api {"method":"GET","path":"/emulator/{id}/ports"}
#   → decoded port map: #7FFD, #DFFD, palette #xx7E, AY, Beta128 (gated by
#     CF_DOSPORTS). RTC/CMOS and Covox are decoded but have no port-map rows yet
```

### What works / what doesn't

| Area | State |
|:--|:--|
| `#7FFD` + `#DFFD` paging, 64 RAM pages, SCO / WOROM / CPM / SCR, lock + DFFD.4 override | implemented |
| ROM order SYS=0, DOS=1, 128=2, 48=3; reset into SYS; DOS latch via `#3Dxx` M1 trap | implemented |
| FDC ports: normal `#1F..#7F/#FF`, CP/M `#BF`, extended `#83/#A3/#C3/#E3` + `#3F` (v5) | implemented; checked against both boards' port decoder PROMs |
| RTC/CMOS: address `#BF/#FF`, data `#9F/#DF`, EXT mode only (CPM ∧ ROM14) | implemented (v5) |
| Two boards: `PROFI3` without palette, extended ports, RTC, IDE; monochrome hi-res | implemented |
| Frame and INT from the board's sync PROM (`[PROFI] SyncProm=`) | implemented |
| AY decodes A13 (`IN #DFFD` does not read the AY) | implemented |
| Covox: `#5F` left, `#3F` right, only while the disk interface is off the bus | implemented |
| 512x240 hi-res (DS80), palette `OUT #xx7E` (9-bit), `#FE` read bit 7 (GX0) | implemented (palette and GX0: v5) |
| NMI (magic button) → DOS latch while DS80 is off | implemented |
| TTD: `#DFFD` + palette as `PeripheralId::ProfiPaging` | implemented |
| IDE (`[HDD] Scheme=PROFI`, answers in EXT mode): slots `ide0.master` / `ide0.slave`; the SYS ROM boots from a hard disk image; geometry from the disk's ProfiHiDD header (16 x 16 without one) | implemented — see [Hard disk](#hard-disk-ide) |
| Kempston joystick at `#1F`, Covox extended-mode aliases (v5) | implemented |
| TURBO front-panel switch (7 MHz; on v3 a loaded floppy head holds 3.5 MHz), recorded by TTD | implemented — see [TURBO switch](#turbo-switch) |
| AY clock 1.5 MHz in hi-res (v3 always; v5 jumper SB7 `[PROFI] AyClock=old`, the default; `new` keeps 1.75 MHz) | implemented for `TurboSound=AY` / `Single`; the shipped `TurboSound=FM` (TSFM) stays at 1.75 MHz — see [AY clock](#ay-clock-in-hi-res) |
| v5 video WAIT at 3.5 MHz (`[PROFI] WaitPhase` / `WaitConfig` / `RomWait`), v3 turbo waits, v5 turbo waits (approximation) | implemented (feature `contention`) |
| v3 floating bus (pixel byte on an unanswered `IN` with A0 = 1) | implemented |
| PROFI-XT keyboard controller (`[PROFI] Keyboard=XT`, v5 default): PC keys, EXT on `#BFFE` bit 5, the Z80 wait, Ctrl + Alt + Del; `XTTable` (no firmware), `Matrix` (v3 default) | implemented — see [Keyboard](#keyboard) |
| Hi-res (`#DFFD` bit 7) timing: the CPU on its hi-res clock (v3 3 MHz, v5 ZQ3 / 4 = 5 MHz; turbo doubles), frame and INT from the sync PROM's upper half (v3: 320 lines, 48.83 Hz), hi-res waits (v5 model), v3 hi-res floating bus | implemented — see [Hi-res](#hi-res-512x240) |
| The native v5 matrix keyboard's EXT / MODE / GRAF keys, the v3 on-board XT pads | not implemented |
| BIOS menu entries TR-DOS, Sinclair 48 / 128 | verified on both boards |
| CP/M | v5: boots from the BIOS menu "Загрузка системы CP/M" (`testdata/machines/profi/cpm/v5/kondor-system-copyk.fdi`, `ProfiBoot_Test.CpmBootsFromTheKondorSystemDisk`); v3: the Kramis "Profi-DOS" entry. Klug CP/M 2.3 needs the default V0.3 ROM (TR-DOS 5.04T); V0.2 (`kramis-v02.rom`, TR-DOS 5.03) cannot load it. SP-DOS (`cpm/sp-dos/unicopy-sp-dos.td0`, MicroDOS with the BIOS by V. Tereschenko) boots on both boards from the same entries to its hi-res shell |

### Hi-res (512x240)

Writing `#DFFD` bit 7 switches the machine at once: the CPU clock, the frame and the INT position (and the AY clock).

| | v3 (`PROFI3`) | v5 (`PROFI`) |
|:--|:--|:--|
| CPU | 3 MHz (6 with TURBO) | ZQ3 / 4: 5 MHz with the 5.06's 20 MHz crystal (10 with TURBO); `[PROFI] ZQ3MHz=16..24` |
| Frame | 320 lines (0a1d PROM), 61440 T at 3 MHz | 312 lines, 99840 T at 5 MHz |
| AY | 1.5 MHz | 1.5 MHz, or 1.75 with `[PROFI] AyClock=new` (jumper SB7) |

Create-time: WebAPI `"profi": {"zq3_mhz": 16, "ay_clock": "new"}`, CLI `create PROFI --profi-zq3 16 --profi-ay-clock
new`, MCP `emulator_manage action=create model=PROFI profi_zq3_mhz=16`. The paging state reports
`profi_hires_cpu_hz`, `profi_zq3_mhz`, `profi_ay_clock`; the status line and `current_z80_frequency` show the clock
in force.

The v5 BIOS draws its menu in hi-res, so a freshly started `PROFI` shows it a few seconds after power-on:

```bash
ID=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' -d '{"model":"PROFI"}' | jq -r .id)
sleep 8
curl -s "$BASE/emulator/$ID/state/paging" | jq '{profi_hires_cpu_hz, profi_zq3_mhz, profi_ay_clock}'
curl -s "$BASE/emulator" | jq '.emulators[] | {model, video_mode, speed_multiplier}'
```

Expected (verified 2026-10-03): `profi_hires_cpu_hz` 5000000, `profi_zq3_mhz` 20, `profi_ay_clock` `"old"`;
`video_mode` `"PROFIHR"` and `speed_multiplier` 1.43 (5 MHz / 3.5 MHz) while the BIOS menu is up. With
`"profi": {"zq3_mhz": 16, "ay_clock": "new"}` at create: 4000000, 16, `"new"`.

### Keyboard

`[PROFI] Keyboard=` (create: WebAPI `"profi": {"keyboard": "xt"|"xttable"|"matrix"}`, CLI `create PROFI
--profi-keyboard xttable`, MCP `emulator_manage action=create model=PROFI profi_keyboard=xttable`):

| Value | Keyboard |
|:--|:--|
| `xt` (v5 default) | the PROFI-XT controller running its firmware on the MCS-48 core. The image `rom/profixt/profi-xt-v1.27.rom` is a reconstruction: 5 bytes patched, the only dump never enables interrupts ([data/rom/profixt/README.md](../../data/rom/profixt/README.md)); `[ROM] PROFIXT=` takes a re-dump |
| `xttable` | the same controller from its key table, without the firmware image |
| `matrix` (v3 default) | the 40-key Spectrum matrix (host Shift = Caps Shift, no EXT) |

With a controller the host keyboard goes to it alone (route `auto` = `ps2`). PC keys: Ctrl = Caps Shift, Shift =
Symbol Shift, Alt = SS + Enter, Esc = CS + 1, Backspace = CS + 0, arrows = CS + 5..8. F1-F10 = A..J + **EXT**,
Home / End = K / L + EXT, PgUp / PgDn = M / N + EXT, Ins / Del = O / P + EXT. EXT is `#BFFE` bit 5 (KD5 of half-row
A14); BIOS 2.0 and CP/M collect KD5 of every half-row into `#99DA` and read bit 6. On v3 EXT lands on `#FE` bit 7,
which no v3 software reads. While a key is held every `#FE` read waits ~92-113 us for the controller; `#00FE` reads
all half-rows ANDed; two half-rows in A8..A11 (`#FCFE`) read "no key". Ctrl + Alt + Del resets the machine. Scroll
Lock (or a read of `#AAFE` / `#55FE`) toggles the second mode; Num Lock turns the keypad into the cursor block.
Automation's ZX keys arrive as Ctrl / Shift + key (TypeText `&` = Shift + 6).

Verified 2026-10-03 (WebAPI, shipped `rom/profi.rom`, BIOS 2.0):

```bash
ID=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "PROFI"}' | jq -r '.id')
sleep 3                                                     # let the BIOS boot (its keyboard scan runs from then on)
curl -s "$BASE/emulator/$ID/keyboard/status" | jq '{host_route_effective, keyboard_controller}'
#   {"host_route_effective": "PS2", "keyboard_controller": "PROFI-XT firmware 1.27"}
curl -s "$BASE/emulator/$ID/state/paging" | jq -r .profi_keyboard          # xt
curl -s "$BASE/emulator/$ID/memory/read/0x99DA?length=1" | jq -r .hexdump  # 0x99DA: 00
curl -s -X POST "$BASE/emulator/$ID/keyboard/press" -H 'Content-Type: application/json' -d '{"key":"f1"}'
sleep 1
curl -s "$BASE/emulator/$ID/memory/read/0x99DA?length=1" | jq -r .hexdump  # 0x99DA: 40 - the BIOS saw EXT
curl -s -X POST "$BASE/emulator/$ID/keyboard/release" -H 'Content-Type: application/json' -d '{"key":"f1"}'
```

The same with `{"model": "PROFI", "profi": {"keyboard": "xttable"}}` (controller `PROFI-XT table`). An unknown
keyboard name is a 400. Lua / Python: `paging_state().profi_keyboard`, `keyboard_controller()`. The BIOS turns the
letter + EXT into its code (F1 = `75h`, routine `127Ah`), tested in `profixtkbc_test.cpp`. TTD records the keys and
the controller (PeripheralId ProfiXtKbc, 44).

### AY clock in hi-res

The board clocks the AY from its video divider: 1.75 MHz in Spectrum mode, **1.5 MHz while `#DFFD` bit 7 (DS80)
is set** - on v3 always, on v5 with jumper SB7 in "CLCAY OLD" (`[PROFI] AyClock=old`, the default).
`AyClock=new` keeps 1.75 MHz in both modes. Turbo does not change it. Music therefore plays 6/7 lower in hi-res.
The switch takes effect at the T-state of the `#DFFD` write and is part of TTD state.

It applies to the plain AY slot (`[SOUND] TurboSound=AY` or `Single` in `data/configs/profi*/unreal.ini`). The
shipped configs fit `TurboSound=FM`; the TSFM keeps 1.75 MHz (open item in
[design-hires.md](../../docs/inprogress/2026-10-01-profi-v3-v5/design-hires.md)).

Read the clock the AY runs at now (`psg_clock_hz`, the same report on every surface: WebAPI below, MCP
`inspect_state` aspect `audio_ay`, Lua / Python `audio_ay_state()`, CLI `state audio ay` line `AY Clock:`):

```bash
curl -s "$BASE/emulator/$ID/state/audio/ay" | jq '{slot_device, psg_clock_hz}'
#   {"slot_device": "TurboSound", "psg_clock_hz": 1500000}   while a program has hi-res on (TurboSound=AY)
#   {"slot_device": "TSFM",       "psg_clock_hz": 1750000}   shipped config: TSFM keeps 1.75 MHz
```

### TURBO switch

```bash
curl -s "$BASE/emulator/$ID/switches" | jq                       # {"switches":[{"name":"turbo","on":false}]}
curl -s -X POST "$BASE/emulator/$ID/switches" -H 'Content-Type: application/json' \
     -d '{"name":"turbo","on":true}' | jq                           # 7 MHz
```

CLI: `switch turbo on`; Lua / Python: `set_switch("turbo", true)`, `get_switch("turbo")`; Qt: Machine > TURBO
Switch; `[PROFI] Turbo=1` turns it on at power-on. The v5 CP/M switch works the same way (`"cpm"`, Machine > CP/M
Switch, `[PROFI] CpmSwitch=1`): while it is on, `#DFFD` stays `#00`. `[PROFI] ExtPorts=sys` decodes the extended ports (VG93 `#83..`, RTC, IDE) from the SYS ROM too, as Karabas Pro: ROM BIOS Plus and PQ-DOS need it, BIOS 1.0 / 2.0 then cannot boot a disk (default `cpm`; `docs/inprogress/2026-10-01-profi-v3-v5/software-zoo.md`). `ExtPorts=v003` is Djoni's V0.03 PROM of `PROFI-PLUS`: `sys` with CP/M off, and with TR-DOS on and ROM14 = 1 the RTC (`#9F #BF #DF`), 8255, IDE and VG93 `#83 #A3 #C3` answer beside the VG93 at `#1F..#7F` (`#E3 #E7 #EB #EF #F3 #F7 #FB #FF` stay the system register). In turbo, code in RAM runs about 1.33x on v3 (the CPU waits for
its DRAM slot), code in ROM 2x.

## WebAPI

```bash
curl -s "$BASE/emulator/models" | jq '.models[] | select(.name=="PROFI")'
#   name is the string id ("PROFI"); .id in this response is an unrelated numeric index

EMU_ID=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
     -d '{"model": "PROFI"}' | jq -r '.id')
# NOTE: the /start response itself only has {id, message, started, state,
# symbolic_id} - model/ram_kb/config_folder are NOT in it. Confirm with a
# follow-up GET:
curl -s "$BASE/emulator/$EMU_ID" | jq '{id, model, ram_kb, config_folder}'

curl -s "$BASE/emulator/$EMU_ID/state/paging" | jq .
curl -s "$BASE/emulator/$EMU_ID/state/screen/mode" | jq .     # PROFI / PROFIHR
curl -s "$BASE/emulator/$EMU_ID/ports" | jq '.entries[] | {port, device}'
```

## Hard disk (IDE)

The v5 Profi ships with its IDE board on (`[HDD] Scheme=PROFI`); the v3 board has none. Put a hard disk
image on the master with the media verbs
([use-media-slots.md](../media/use-media-slots.md), reference
[docs/features/media.md](../../docs/features/media.md)):

```text
media {"action":"insert","slot":"hd","path":"/home/me/zx/profi.hdd"}   # hd = ide0.master
inspect_state {"aspects":["ide"]}                                          # board, units, task file
```

A hard disk is inserted and ejected while the machine is paused. The SYS ROM
boots from the disk; the geometry comes from the disk's ProfiHiDD header
(16 x 16 from the SYS ROM, 16 x 63 from Karabas), a disk without one gets 16 x 16.

TTD on Profi follows the standard recipes —
[recording](../analysis/ttd-recording.md),
[reverse debugging](../analysis/ttd-reverse-debugging.md).

## Serial port (COM)

The v5 board (`PROFI`, `PROFI-PLUS`) has an RS-232 port: a KR580VV51A (8251 USART) whose clock is counter 0 of a
KR580VI53 (8253 timer) running at 1.5 MHz. It answers only in the extended port map (CP/M + ROM14; with
`[PROFI] ExtPorts=sys`, or `v003` as on `PROFI-PLUS`, also in the SYS ROM). The v3 board has none.

| Port (low byte) | Device |
|:--|:--|
| `#8F` / `#AF` / `#CF` | 8253 counters 0 / 1 / 2 (counter 0 = the 8251's TxC / RxC) |
| `#EF` | 8253 control word |
| `#D3` | 8251 data |
| `#F3` | 8251 mode / command words (write), status (read) |
| `#B3` (and `#93`) | COM control: write D0 = interrupt enable; read D0 = RI, D7 = DCD |

Baud rate = 1 500 000 / (counter 0's count x the 8251's baud factor): ROM BIOS Plus programs mode 3, count 156 and
factor x1 for 9600 baud. Only the asynchronous mode moves bytes (sync mode words are accepted); the COM interrupt
(`#B3` D0) is kept as a latch but raises no INT.

The peer on the other end is picked like every machine's own serial port, with `ComPort=` (`[NETWORK] ComPort=` in
the INI): `LOOPBACK`, `PLUG` (a test plug: DTR to DSR / DCD, RTS to CTS / RI, like the Profi's TESTCOM.COM plug),
`TCP:<host>:<port>`, `SERIAL:<device>[,baud]`, `MODEM`, `ESPNET`, `AT`; `NONE` (the default) = nothing connected
(CTS / DSR / DCD inactive: the 8251 does not send).

```json
{"tool": "invoke_api", "arguments": {"method": "POST", "path": "/api/v1/emulator/{id}/network/config",
  "body": {"com_port": "loopback"}}}
```

CLI `network set com_port=plug`, Lua `network_configure{com_port="tcp:127.0.0.1:2323"}`, Python
`emu.network_configure(com_port="modem")`; Qt: Tools > Network. `inspect_state network` shows the port as
`machine.serial_port = "profi-8251"` and `machine_serial` (flavor `usart8251`, the peer, `baud`, `frame_bits`,
`rts`, `dtr`, `bytes_in`, `bytes_out`, `lost` = overruns). The 8253, the 8251 and the peer are recorded in TTD.

## Pitfalls

- **Many ports depend on the mode.** EXT mode = `#DFFD.5` (CPM) and
  `#7FFD.4` (ROM14) both set; RTC and the extended FDC set only answer then.
  `#3F`/`#5F` are Covox in normal mode but FDC registers while the DOS latch
  or CP/M mode puts the disk interface on the bus. Check `pDFFD`/`p7FFD`
  and the DOS latch before reading a port result.
- **The machine resets into the SYS ROM with the DOS latch on**, not into
  48K BASIC — `banks[0].role` right after reset is the SYS page.
- **`#DFFD` is not Scorpion's `#1FFD`** — different register, different bit
  meanings.
- **`GET /emulator/models` uses `.id` for a numeric index, `.name` for the
  string model id** (`"PROFI"`) — filtering on `.id=="PROFI"` silently
  returns nothing; use `.name`.
- **`POST /emulator/start`'s response doesn't carry `model`/`ram_kb`** —
  confirm those with a follow-up `GET /emulator/{id}`.
- **RAM sizes** — 512K or 1024K on either board (v3 defaults to 512K, v5 to
  1024K); other values are rejected by the RAM bitmask check.
- **`PROFI3` has no RTC and no IDE** — `inspect_state aspects:["rtc"]` reports
  the clock as absent, and `[HDD] Scheme=PROFI` does not fit the v3 board.
