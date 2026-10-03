# Lua Bindings - Lightweight Scripting

## Overview

The Lua bindings provide a lightweight, fast scripting interface to the emulator. Perfect for macros, event handlers, quick automation, and embedded logic.

**Status**: ✅ Implemented
**Implementation**: `core/automation/lua/`
**Binding Library**: sol2 (C++/Lua bridge)
**Lua Version**: 5.4

**Note**: See [command-interface.md](./command-interface.md) for the complete command reference that these Lua bindings implement.  

## Architecture

```
┌──────────────────────────────┐
│   Lua Scripts                │
│   (macro.lua, event.lua)     │
└───────────┬──────────────────┘
            │ Lua C API
            ▼
┌──────────────────────────────┐
│   sol2 Bindings              │
│   • Emulator userdata        │
│   • Memory userdata          │
│   • Z80 userdata             │
└───────────┬──────────────────┘
            │ Direct calls
            ▼
┌──────────────────────────────┐
│   C++ Emulator Core          │
└──────────────────────────────┘
```

## Key Advantages

1. **Lightweight**: Minimal memory footprint (~200KB)
2. **Fast Execution**: JIT compilation possible with LuaJIT
3. **Easy to Learn**: Simple, clean syntax
4. **Sandboxed**: Safe execution environment
5. **Hot Reload**: Reload scripts without restarting emulator
6. **Embeddable**: Scripts can be embedded in save states

## Basic Usage

### Hello World
```lua
-- hello.lua
print("Hello from Lua!")

-- Access emulator
local emu = get_emulator()
print("Emulator ID: " .. emu:get_id())

-- Read registers
local cpu = emu:get_cpu()
print(string.format("PC = 0x%04X", cpu:get_pc()))
```

### Machine Identity and Lifecycle
The Lua bindings operate on the existing emulator instance (`get_emulator()`); they do not expose model-selecting instance creation or model switching. For lifecycle operations with strict model validation (`creatable` flags, reason-carrying failures) use the WebAPI (`POST /api/v1/emulator/create`, `GET /api/v1/emulator/models`) or the CLI (`create`/`start <model>`). Machine identity of the current instance is observable through state endpoints (e.g. TTD status reports `model_id`/`model_ram_pages`).

**ZX-Poly** (four synchronized instances of one model, through the same
`EmulatorManager::CreateZXPolyMachine` every surface uses):
```lua
local id, err = zxpoly_start("ZXPOLY-PENTAGON", "/path/to/Alien8.zxp")  -- model and file optional; id = the master
local id2 = zxpoly_start("ZXPOLY-48K", nil, "zero")  -- third argument: power-on RAM of all four modules, "random" | "zero"
local status = zxpoly_status(id)   -- nil if not a ZX-Poly machine
print(status.locked, status.video_mode, status.diverged, status.modules[2].registers[1])
print(status.parallel_slaves, status.pipelined_slaves)   -- how the slaves are scheduled
```
The model is a configuration name (`ZXPOLY-48K`, `ZXPOLY-128K`,
`ZXPOLY-PENTAGON`) or a base model such as `PENTAGON`.

### Running Scripts

#### From Command Line
```bash
unreal-emulator --lua-script macro.lua
```

#### From CLI Interface
```
> script run macro.lua
```

#### Programmatically (C++)
```cpp
AutomationLua* lua = new AutomationLua();
lua->start();
lua->executeScript("macro.lua");
```

## API Reference

**Which emulator a function acts on.** Every emulator function (registers, memory, pages, assembler, breakpoints, stepping, features, speed, tape, disk, mouse, TTD, ...) acts on the emulator the interpreter is bound to, or, when the interpreter is not bound to one, on the selected emulator (for example, a script run through the WebAPI interpreter, `POST /api/v1/lua/exec` or `/api/v1/lua/file`). A bound emulator always wins.

### Global Functions

```lua
-- Get main emulator instance
emu = get_emulator()

-- Print to console
print(message)

-- Sleep (milliseconds)
sleep(100)

-- Get current timestamp
time = os.time()

-- Toggle Videowall Single Sync Mode
videowall_singlesync(true, "emulator-id")
```

### Tape Operations

Global functions mirroring the CLI `tape` commands and the WebAPI `/tape/*` endpoints one-to-one (same names, states and catalog indices).

```lua
-- Load / eject
local ok, why = tape_load("/path/to/game.tap")  -- A tape (.tap/.tzx/.spc/.sta/.ltp/.zxt) or a folder
local ok = tape_eject()                     -- The tape leaves the tape slot (false while TTD records)

-- Transport (same semantics as `tape play|pause|stop|rewind|seek`)
tape_play()      -- start at consumption cursor; resumes in place when paused
tape_pause()     -- freeze mid-block (idempotent); false when not playing
tape_stop()      -- terminal stop: invalidates the loaded image
tape_rewind()    -- rewind to block 0, image kept
tape_seek(4)     -- position head at catalog block 4

-- Inspection
local inserted = tape_is_inserted()
local path     = tape_get_path()
local pos      = tape_pos()      -- nil without a tape, else
                                 -- {state="playing", block=4, pulse=1234,
                                 --  seconds_into_block=1.2,
                                 --  block_total_seconds=4.5,
                                 --  cursor=5, block_count=12}
local blocks   = tape_blocks()   -- nil without a tape, else array of tables
                                 -- {index, kind, name, type, declared_length,
                                 --  param1, param2, paired_header_index,
                                 --  paired_data_index,
                                 --  speed={profile, baud}, checksum_valid,
                                 --  checksum_applicable, seconds, raw_size,
                                 --  playable, fast_load="yes"|"<reason>"}
local info     = tape_info()     -- nil without a tape subsystem, else
                                 -- {status, file, format, state, cursor,
                                 --  block_count, total_seconds, fast_tape,
                                 --  turbo_tape, fast_load={verdict,
                                 --  eligible_blocks, accelerated_seconds,
                                 --  total_seconds, summary}}

-- Audio bridge (pure file conversions, no emulator state touched)
local result = tape_render("game.tzx", "out.wav")  -- whole tape, 44100 Hz
local result = tape_render("game.tzx", "out.flac", {
    first_block = 2, last_block = 5, sample_rate = 48000,
    amplitude = 0.8, invert_level = false })
-- result = {ok, error, duration_sec, samples, blocks, encoder, warnings}

local result = tape_import("recording.wav", "imported.tzx")   -- hysteresis optional
local result = tape_import("recording.wav", "imported.tap", 0.25)
-- result = {ok, error, decoder, sample_rate, samples_decoded,
--          signal_edges, blocks_recognized, blocks_written,
--          output_path, warnings}
```

Playback `state` is one of `"idle"`, `"playing"`, `"paused"`, `"ended"` — identical strings across CLI, WebAPI, Lua and Python.

### Media (every slot)

Floppy drives, the SD card and every other slot through one set of functions — the same verbs,
slot names, options and errors as the WebAPI, CLI, MCP and Python. Full reference:
[docs/features/media.md](../../../features/media.md).

```lua
media_list()                                            -- slots + detached media
media_insert("A", "/games/elite-1.trd")                 -- slot: fdd.a, A, a:, floppy:0, tag:...; "auto"
media_insert("sd", "/home/me/zx/sdcard", {fs = "fat32"})
media_insert("cd", "/home/me/music/album", {format = "audio-cd"})  -- MP3 / FLAC / WAV files as an audio CD
media_swap("A", "/games/elite-2.trd", {save = true})    -- a dirty disk needs save / export / discard
media_eject("B", {export = "/tmp/b.trd"})
media_info("sd"); media_formats("floppy"); media_save("A"); media_export("sd", "/tmp/card.img")
media_targets("/discs/dna_nemo.iso")                    -- where a file can go: file, targets, default, refusal
media_discard("A"); media_rescan("sd"); media_create("B"); media_protect("A", true)
media(verb, slot, path, opts)                           -- any verb
```

Each returns the result table: `ok`, `error`, `message`, `slot`, `pending`, `revision`, `report`
and the verb's fields (`slots`, `info`, `formats`, `targets`, ...).

### Disk Operations

Global functions for the four floppy drives (0-3 / A-D), mirroring the CLI `disk` commands
and the WebAPI `/disk/{drive}/*` endpoints.

```lua
-- Load / eject (path: .trd/.scl/.fdi/.udi/.dsk/.td0/.mgt/.img, auto-detected)
local r = disk_load("/path/to/game.trd")          -- insert into drive 0 (A)
-- r = {success, started=false, message}
local r = disk_load("/path/to/game.trd", 0, true) -- + autostart: quick-reset into TR-DOS
                                                    -- and run the disk (same as the Qt UI's
                                                    -- drag-and-drop autostart / WebAPI
                                                    -- "autostart":true / CLI "disk insert
                                                    -- <drive> <file> autostart) - drive 0 only
-- r = {success, started, message}; message explains what autostart did
--     (e.g. "Autostart: boot", "No BASIC programs on disk - mounted only")
local ok = disk_eject(0)

-- A non-zero `drive` is accepted but currently still mounts into drive 0 - a pre-existing
-- limitation of the underlying LoadDisk/AutostartDisk (not specific to Lua); disk_load
-- adds a `warning` field to the result in that case rather than silently mis-inserting.

-- Blank disk
local ok = disk_create(1, 80, 2)   -- drive 1 (B), 80 cylinders, 2 sides

-- Inspection
local inserted = disk_is_inserted(0)
local path     = disk_get_path(0)
local drives   = disk_list()   -- array of {id, letter, inserted, path}
local info     = disk_info(0)  -- nil without a disk, else geometry/catalog details

-- Raw sector access (read-only)
local sector = disk_read_sector(0, 0, 0, 1)      -- drive 0, cyl 0, side 0, sector 1
local hex    = disk_read_sector_hex(0, 0, 1)      -- drive 0, track 0, sector 1
```

### Mouse Input

> **Status**: ✅ Implemented (2026-09). Source: `core/automation/lua/src/emulator/lua_emulator.h`
> (`mouse_*` functions; helpers `mouseIntArg`, `mouseStateTable`, `mouseResult`).

Global functions that drive the emulated Kempston Mouse, mirroring the CLI `mouse` commands.
Units, limits and reasoning: [command-interface.md §11](./command-interface.md#11-mouse-input-injection).
They act on the bound emulator, or on the selected one when the script is not bound to an
instance (for example, a script started through the WebAPI interpreter).

The mouse is **relative**: `mouse_move(10, -5)` means "travelled 10 pixels right and 5 down".
`dy` positive = **up**.

```lua
mouse_move(dx, dy)                 --> state | nil, err    (-127..127 each, not both 0)
mouse_press(button)                --> state | nil, err    ("left"/"right"/"middle" or "l"/"r"/"m")
mouse_release(button)              --> state | nil, err
mouse_click(button [, frames=2])   --> state | nil, err    (hold 1..65535 frames)
mouse_buttons({"left","middle"})   --> state | nil, err    ({} = none)
mouse_wheel(steps)                 --> state | nil, err    (-7..7, not 0)
mouse_release_all()                --> state | nil, err
mouse_set_counters(x, y)           --> state | nil, err    (debug: raw 0..255)
mouse_status()                     --> state | nil, err
mouse_click_pending()              --> true while a click is still holding its button
mouse_button_names()               --> {"left","right","middle"}
```

`state` is a table with the same key names as the WebAPI state object: `x`, `y`,
`buttons = {left, right, middle}`, `button_mask` (active-low: 254 = left down), `wheel`,
`wheel_enabled`, `present`, `ports = {FADF, FBDF, FFDF}`, `pending_click`
(`{button, frames_left}`, or **absent** when no click is pending), `ttd_journal`, plus
`warning` when the change cannot reach the program (mouse not fitted, or a wheel step with no
wheel fitted).

`mouse_status()` additionally carries `routing = {ports_decoded, note}` — the same live
answer as the WebAPI `GET /mouse/status` routing object: whether a mouse port read is decoded
right now, and the reason when it is shadowed (mouse not fitted, TR-DOS ports accessible, a
registered peripheral claims the port family, or model-specific gating — Scorpion DOS
trigger / Shadow Monitor). The changing functions (`mouse_move` etc.) do not carry it, also
matching the WebAPI.

> [!NOTE]
> `ports` values are integers, the same as in Python and the WebAPI.

Errors do not raise: the function returns `nil, "message"`, so a call can be wrapped in
`assert(...)`. A non-integer number (`mouse_move(1.5, 0)`) returns
`nil, "dx must be an integer"` instead of being truncated. During TTD replay every changing
function returns `nil, "TTD replay in progress; live mouse input refused"`.

```lua
-- run_frames pauses a running emulator first, so the sequence below is reproducible
run_frames(1)
local st = assert(mouse_move(10, -5))
print(st.x, st.y)                  -- 41   80   (from reset X=31 Y=85)
assert(mouse_click("left", 2))
run_frames(3)                      -- 2 frames held + 1 for the program to react
print(mouse_status().buttons.left) -- false

local ok, err = mouse_wheel(12)
print(ok, err)                     -- nil   steps=12 out of range -7..7
```

### Joystick Input

> **Status**: ✅ Implemented (2026-10). Source: `core/automation/lua/src/emulator/lua_emulator.h`
> (`joystick_*` functions; helpers `joystickStateTable`, `joystickResult`, `joystickNamesArg`).

Global functions that drive the emulated Kempston joystick, mirroring the CLI `joystick` commands.
Semantics, units and limits: [command-interface.md §13](./command-interface.md#13-joystick-input-injection).
They act on the bound emulator, or on the selected one, like the mouse functions. (The design note
spells them `emu:joystickPress`; the Lua API is global snake-case functions, so they are named like `mouse_*`.)

```lua
joystick_press(buttons)            --> state | nil, err    ("up+fire", "up,fire" or {"up","fire"})
joystick_release(buttons)          --> state | nil, err
joystick_set(state)                --> state | nil, err    (a byte 0..255, a name string, or a table; {} = none)
joystick_tap(buttons [, frames=2]) --> state | nil, err    (hold 1..65535 frames)
joystick_state()                   --> state | nil, err
joystick_tap_pending()             --> true while a tap is still holding its buttons
joystick_button_names()            --> {"up","down","left","right","fire","b5","b6","b7"}
```

`state` has the same key names as the WebAPI state object: `available`, `present`, `wired`,
`state` (the byte), `port_value` (what `IN #1F` returns), `buttons = {up=..., ...}`, `pressed`
(array of names), `button_names`, `keys`, `pending_tap` (`{mask, frames_left}`, or **absent**),
plus `warning` when the guest cannot see the buttons.

Errors do not raise: the function returns `nil, "message"` with the shared wording
(`state=300 out of range 0..255`, `unknown joystick button 'jump' (...)`, `TTD replay in progress ...`).

```lua
run_frames(1)
local st = assert(joystick_press("up+fire"))
print(st.state, st.port_value)        -- 24   24
assert(joystick_tap("left", 3))
run_frames(4)
print(joystick_state().buttons.left)  -- false
print(joystick_set(300))              -- nil   state=300 out of range 0..255
```

### Feature Management

`feature_list()` enumerates every registered runtime feature dynamically (the same list the CLI `feature` table and the WebAPI `/features` endpoint return), keyed by feature id:

```lua
local features = feature_list()
-- { sound = true, fasttape = true, turbotape = true, calltrace = false, ... }

if features.turbotape == nil then
    print("turbo tape not available in this build")
end

feature_set("fasttape", false)     -- same switch as `setting fast_tape off`
print(feature_get("turbotape"))    -- true
feature_set("turbomode", true)     -- turbo mode (same switch as `setting speed unlimited`)
```

`feature_list()` and `feature_get()` report the state in effect. Time-travel debugging holds some features off: `turbomode` while a recording runs, and `fasttape`, `turbotape`, `fastdisk` while a recording runs, history is replayed, or the machine sits in history. A held feature reads as `false`, and `feature_set(name, true)` on it returns `false`. The full list of features, aliases and defaults: [command-interface.md §5](./command-interface.md#5-feature-management--configuration).

### Speed and Turbo

Global functions, the same switches as CLI `setting speed` and the WebAPI `speed` setting.

```lua
set_speed(4)          --> bool  -- host speed multiplier: 1, 2, 4, 8 or 16 (applied at the next frame)
                                -- false for any other value, and false for 2..16 while TTD records
get_speed()
-- --> { multiplier   = 4,      -- the host multiplier set above
--       effective    = 4,      -- what runs, including the machine's own hardware turbo (ATM, Scorpion)
--       turbo_mode   = false,  -- the turbomode feature (switch it with feature_set("turbomode", true))
--       turbo_active = false,  -- the engine runs unthrottled now: turbo mode, or turbo tape warping a load
--       turbo_audio  = false } -- audio kept on in turbo mode
```

Changing the speed on a stopped or loaded TTD session drops that session's history (frame timing is part of the recording); re-selecting the current speed changes nothing.

### Device State Reports

The same reports the WebAPI, Python, CLI and MCP return
([command-interface.md §3.3](./command-interface.md#33-device-state-reports-ay--ssg-turbosound-fm-beta-disk-fdc)),
as Lua tables (arrays are 1-based sequences):

```lua
ay  = audio_ay_state()      -- overview: available_chips, slot_device, chips[]
ay0 = audio_ay_state(0)     -- one chip: registers, channels[3], envelope, noise, mixer, io_ports
fm  = audio_fm_state()      -- TurboSound FM: board latches + chips[2] summaries
fm1 = audio_fm_state(1)     -- one YM2203 FM half: mode, timers, channels[3].operators[4] ...
gs  = gs_state()            -- General Sound / NeoGS: the WebAPI /state/audio/gs report ("neogs" block on NeoGS)
cv  = audio_covox_state()   -- Covox / SoundDrive: fitment, ports, shared_with_beta128, channels[4]
ms  = audio_moonsound_state()       -- MoonSound OPL4: NEW/NEW2, latches, mix, wave_memory, keyed channels/slots
msf = audio_moonsound_state("fm")   -- its 18 FM channels, timers, register banks
msp = audio_moonsound_state("pcm")  -- its 24 wavetable slots, envelopes, register file
fdc = fdc_state()           -- Beta Disk WD1793: registers, status_bits, fsm_state, signals, drives[4]
ide = ide_state()           -- IDE board: scheme, adapter latches, units[2] (task_file, command, atapi)
cd = cdaudio_state()        -- CD drives' audio: drives[] (slot, disc + tracks, audio status / head / track / index, drive_volume, mixer)
r = cdaudio("play", "", {track=2})   -- verbs: status, play (track/to, lba/frames, msf/end), pause, resume, stop, volume, mixer
r = cdaudio("volume", "ide0.slave", {left=128, route="mono"})   -- reply: ok, error, message, drive
ts = tsconf_state()         -- TS-Conf: memory map, video (mode, geometry, TSU, the engine's line), interrupts, DMA, clock, SD
tsu = tsconf_tsu()          -- TS-Conf TSU objects for debug views: tile_layers, sprites (85 decoded), cram (256 cells)
sp = sprinter_state()       -- Sprinter Sp2000: pld, decoder, windows, registers, cells, clock, frame, video, z84c15, fdc, cmos, ide, bios
tbl, err = sprinter_ports{map=0, dos=1, rw="w"}  -- the decoded port table (page #40); omitted keys = the current state
lk, err = sprinter_port(0x21BC, {rw="w"})        -- one port: index, code, name (or the Z84C15); also sprinter_port("21BC")
txt = sprinter_text()       -- the screen text of the mode table's text squares (80 x 32: BIOS SETUP, DSS)
vid, err = sprinter_video{page=1, all=false, squares=false}  -- the mode table: map (G 320, g 640, T text 40, t text 80, Z Spectrum cell, B border, . blank, * INT), hold, frame, rgmod, port_y, palettes_used, squares
pal, err = sprinter_palette(4)    -- palettes (0-7, "all", default "used"): pens n / rgb "#RRGGBB" (R,G,B as stored) / vram
ring = sprinter_sound_ring()      -- the Covox-Blaster ring: rows (16 words, [ ] playing, < > next write), words[256]
zx = sprinter_zx_mode()           -- the ZX mode: active, config.best_match.file ("SP.ZX"), options, clock, frame.int, rom, ports; sprinter_zx_mode(false) skips the RAM search
j, err = sprinter_pld_journal{kinds="cnf,port_1ffd", source="live"}  -- who changed the PLD setup: events {frame, t, pc, kind, port, value, text}; source="ttd": the recording
sprinter_pld_journal_control{enabled=true, clear=true}            -- switch / clear the PLD journal
bios = sprinter_bios()            -- BIOS images (file, alias, crc32, present, loaded, selected), loaded, reload_pending, options
r, err = sprinter_bios_select{bios="3.06", fast_start=false, reset=true}  -- the image loads at the reset (now unless reset=false)
regs = memory_regions()           -- device memory regions: {name="vram", size=262144, pages=16, ...} on the Sprinter
bytes, err = region_read("vram", 0x17F0, 3)     -- table of bytes; region_write("vram", 0x17F0, "0000A8") / {0,0,0xA8}
ok, err = region_save("vram", "vram.bin")       -- region_load("vram", "vram.bin" [, offset])
ch = video_changes(2)             -- video change log: frames[] {start, writes[] (t, line, t_in_line, pc, changes), tables}
fb, err = framebuffer("index")    -- {width, height, format, encoding, data = string}: "rgba" (R,G,B,A) or "index" (Sprinter pens)
shot, err = screenshot{area="screen", format="png", path="scratch/shot.png"}
          -- screenshot of the presented frame; every field optional: area "full" (default, the whole frame with
          -- border) or "screen" (the working picture), format "png" (default) or "gif". Returns {format, area, width,
          -- height, size, crop = {x,y,width,height}, screen_window = {...}, frame = {width, height, mode, source,
          -- frame_number}, data = the encoded image as a string of bytes} (`file` instead of `data` with a path);
          -- nil, err for a bad word or no frame. screenshot() = the whole frame as PNG
mx = audio_mixer()                -- per-device mixer: master, devices[] (source, muted, solo, volume, gain_db, peak, active)
mx, err = audio_mixer_set("covox", {muted=true})  -- solo=, volume=0..1, gain_db=; "master" takes muted
r = audio_capture_start(1.0, "covox")             -- capture one device's own buffer (default: the master mix)
rtc = rtc_state()           -- CMOS clock: chip, ports, time_mode, time, register_a..d, alarm, dump
net = network_state()       -- network adapters: card (ZXNETUSB, W5300 sockets), com_port (UART, peer), virtual network (leases, sockets, activity); available=false without one
ok, err = network_configure{card="zxnetusb", host_access=true, hosts="name=10.0.2.50"}  -- change [NETWORK] settings (the card is fitted again)
route, err = key_route("ps2")          -- where keys go: "auto" | "matrix" | "ps2" | "both"; key_route() queries
ok, err = network_configure{com_port="tcp:127.0.0.1:2323"}          -- the machine's own serial port (ZX-Evo AVR, ATM Turbo 2+ keyboard controller): none | loopback | tcp:host:port | serial:device[,baud] | espnet[,baud] | at[,baud] (an ESP module's baud defaults to the port's: 38400 on ATM2, else 115200); com_modem_lines=true|false; esp_chip="esp32"|"esp8266"
ok, err = network_configure{card="zxwifi", zx_wifi="espnet"}          -- cards: none | zxnetusb | zxwifi | atm2ioesp (ATM Turbo 2+ INTERNAL I/O; atm2ioesp="espnet", atm2ioesp_address="0xF0") or a list "zxnetusb,zxwifi"; zx_wifi: what the ZX-WiFi card's 16550 is wired to (default "at"); avr_firmware="ts2013" etc. (ZX-Evo, [EVO] Avr= names); kbc_firmware="v41" etc. (ATM Turbo 2+ keyboard controller, [ATM] Kbc= names; com_port is its RS-232 from v31); zifi="at" (TS-Conf / ZX-Evo TS firmware: the ZiFi board's ESP, default "none"; network_state().zifi has the API registers and rings)
cells, err = rtc_read(0x0E, 4)      -- CMOS cells {b1, b2, ...} as the guest reads them (nil, err without a clock)
ok, err = rtc_write(0x40, {0x12, 0x34})  -- write cells like the guest (time registers set the clock)
isa = isa_state()           -- ISA slots (Sprinter): latch, window, slots[] (configured, card, not_fitted, counters); available=false elsewhere
v, err = isa_io_read(2, "#30A")      -- one ISA I/O cycle in slot 2 at ISA #30A (the RTL8019AS ID byte #50); isa_io_write(slot, addr, v), isa_io_peek(slot, addr)
ok, err = isa_reset()                -- one RESET DRV pulse to both slots; isa_mem_read / isa_mem_write, isa_latch(v) as well
j = isa_journal(16)                  -- the last 16 ISA accesses: entries[] (frame, t, pc, access, cpu_address, what = register name, value)
f = network_frames("isa2.eth", 8)    -- the Ethernet gateway's capture: frames[] (index, frame, direction, port, summary, hex); "" = every card
ok, err = network_inject_frame("isa2.eth", "FFFFFFFFFFFF...")   -- a frame towards the card, offered at the next frame boundary
con = contention_state()    -- rule, switch, effective, memory_interface, io_rule, slots[4], even_m1, scorpion_turbo_logic (Scorpion), atm710_turbo_waits (ATM Turbo 2+ v7.10: active / off / contention_off), statistics (debug mode)
scr = screen_state()        -- video_mode, resolution, active_screen, active_ram_page(s), contention, flash_inverted
scv = screen_state(true)    -- + screen_0/screen_1 (z80_access, ula_display) and port_0x7FFD
mode = screen_mode()        -- picture format, memory_layout, active_ram_pages, eff7/dffd/ff77
fl  = screen_flash()        -- flash_phase, frames_until_toggle, flash_cycle_position
-- screen fields: [command-interface.md §6.6](./command-interface.md#66-screen-configuration); screen_video_state() is the former name of screen_mode()

if not fm1.available then print(fm1.description) end
for i, ch in ipairs(fm1.channels) do
  if ch.key_on then
    print(string.format("ch%d %.1f Hz alg %d", ch.index, ch.frequency_hz, ch.algorithm))
    for _, op in ipairs(ch.operators) do
      print("  " .. op.slot .. " " .. op.envelope_state .. " " .. op.attenuation_db .. " dB")
    end
  end
end
```

### Emulator Object

```lua
-- Create new emulator
emu = Emulator.new()
emu = Emulator.new("symbolic_id")

-- Initialize
success = emu:init()

-- Control
emu:reset()
emu:pause()
emu:resume()
pc = emu:step()          -- Returns PC after step
pc = emu:steps(count)    -- Returns PC after N steps
emu:run_frame()          -- Run exactly one video frame
emu:run_frames(count)    -- Run exactly N video frames

-- Properties
id = emu:get_id()
sym_id = emu:get_symbolic_id()
emu:set_symbolic_id("name")
state = emu:get_state()  -- "running", "paused", "stopped"
ram = emu:ram_power_on()  -- RAM contents at creation: "random" | "zero" ([MISC] RAMPowerOn / ram_power_on)

-- Subsystems
cpu = emu:get_cpu()
mem = emu:get_memory()
debug_mgr = emu:get_debug_manager()
bp_mgr = emu:get_breakpoint_manager()

-- Cleanup
emu:release()
```

### CPU Object (Z80)

```lua
cpu = emu:get_cpu()

-- Register access
af = cpu:get_af()
cpu:set_af(0x44C4)

bc = cpu:get_bc()
cpu:set_bc(0x3F00)

-- Individual registers (16-bit pairs)
cpu:get_de()
cpu:get_hl()
cpu:get_ix()
cpu:get_iy()
cpu:get_sp()
cpu:get_pc()

-- 8-bit register access
a = cpu:get_a()
f = cpu:get_f()
b = cpu:get_b()
c = cpu:get_c()
-- etc.

-- Flags
flags = cpu:get_flags()  -- Returns table
-- flags.S, flags.Z, flags.H, flags.P, flags.N, flags.C

-- Execution
cycles = cpu:execute(instruction_count)
```

### Memory Object

```lua
mem = emu:get_memory()

-- Byte access
value = mem:read(0x8000)
mem:write(0x8000, 0x42)

-- Word access (16-bit, little-endian)
word = mem:read_word(0x8000)
mem:write_word(0x8000, 0x1234)

-- Block operations
bytes = mem:read_bytes(0x8000, 256)  -- Returns string
mem:write_bytes(0x8000, data_string)

mem:fill(0x8000, 256, 0xFF)  -- Fill with value

-- Helper to get RAM/ROM size
ram_size = mem:get_ram_size()
rom_size = mem:get_rom_size()
```

### Physical Page Access

> **Status**: ✅ Implemented (2026-01)

Access physical memory pages directly, bypassing Z80 paging. Useful for ROM patching, shadow memory inspection, and cache/misc memory access.

```lua
-- Read/write single byte from physical page
-- type: "ram", "rom", "cache", "misc"
-- page: page number (0-255 for RAM, 0-63 for ROM, etc.)
-- offset: offset within 16KB page (0-16383)
value = page_read("ram", 5, 0x100)
page_write("ram", 5, 0x100, 0xFF)

-- Block operations
data = page_read_block("rom", 2, 0, 256)    -- Read 256 bytes from ROM page 2
page_write_block("ram", 7, 0x1000, data)    -- Write block to RAM page 7

-- Get memory configuration
info = memory_info()
-- Returns: {
--   pages = {ram_count=256, rom_count=64, cache_count=4, misc_count=4},
--   z80_banks = {{bank=0, start=0, end=16383, mapping="ROM0"}, ...}
-- }
```

**Writes during a time-travel recording.** While a TTD recording runs, every memory write (`mem_write`, `mem_write_word`, `mem_write_block`), physical page write (`page_write`, `page_write_block`) and assembler write (`assemble` with write on) records a `debugger_edit` marker, a replay barrier, and briefly pauses a running emulator for the edit, so the recording sees the change. A write by Z80 address into a ROM bank leaves the ROM unchanged, as a CPU write would. No marker is written when no recording runs.

### Breakpoint Manager

```lua
bp_mgr = emu:get_breakpoint_manager()

-- Add breakpoints
bp_id = bp_mgr:add_execution_bp(0x8000)
bp_id = bp_mgr:add_memory_read_bp(0x4000)
bp_id = bp_mgr:add_memory_write_bp(0x5C00)
bp_id = bp_mgr:add_port_in_bp(0xFE)
bp_id = bp_mgr:add_port_out_bp(0xFE)

-- Remove breakpoints
bp_mgr:remove_bp(bp_id)
bp_mgr:clear_all_bps()

-- Activate/deactivate
bp_mgr:activate_bp(bp_id)
bp_mgr:deactivate_bp(bp_id)
bp_mgr:activate_all()
bp_mgr:deactivate_all()

-- Query
count = bp_mgr:get_bp_count()
bp_list = bp_mgr:get_all_bps()  -- Returns table of breakpoints
```

### Analyzer Management

```lua
-- List registered analyzers
local names = analyzer_list()  -- → {"trdos"}

-- Enable/disable analyzer
analyzer_enable("trdos")   -- → true
analyzer_disable("trdos")  -- → true

-- Get analyzer status
local status = analyzer_status("trdos")
-- status.enabled = true
-- status.state = "IN_TRDOS"
-- status.event_count = 42

-- Get captured events (optional limit parameter)
local events = analyzer_events("trdos", 50)
-- → {"[0001234] TR-DOS Entry (PC=$3D00)", ...}

-- Clear analyzer events
analyzer_clear("trdos")
```

**TRDOSAnalyzer States:**
- `IDLE` - Not in TR-DOS ROM
- `IN_TRDOS` - In TR-DOS ROM but not executing command
- `IN_COMMAND` - Executing TR-DOS command
- `IN_SECTOR_OP` - Sector read/write in progress
- `IN_CUSTOM` - Custom sector operation

### Debug Commands (Direct Methods)

> **Status**: ✅ Implemented (2026-01)

The `Emulator` object exposes these debug methods directly:

```lua
-- Breakpoint Status (Last Triggered)
local status = emu:bp_status()  -- Returns table with type-specific fields:
-- Memory: {valid=bool, id=int, type='memory', address=int,
--          execute=bool, read=bool, write=bool, active=bool, note=str, group=str}
-- Port:   {valid=bool, id=int, type='port', address=int,
--          in_=bool, out=bool, active=bool, note=str, group=str}
emu:bp_clear_last()  -- Clear last triggered breakpoint ID

-- Disassembly
local lines = disasm()                   -- Disassemble from PC (default 10 lines)
local lines = disasm(0x8000, 20)         -- Disassemble from address, count
local lines = disasm_page("rom", 2, 0, 20)   -- Disassemble physical ROM page (e.g., TR-DOS)
local lines = disasm_page("ram", 5, 0x100, 10)  -- Disassemble physical RAM page
-- Returns: table with entries: {offset, bytes, mnemonic, size, label,
--          target/targetLabel (jumps and calls), displacement/effectiveAddress/effectiveAddressLabel (IX/IY+d)}
-- Mnemonics print both label and address when a label exists at the target: 'call TEST_ROUTINE (#8010)'

-- Debug Mode (via feature manager)
emu:feature_set("debugmode", true)   -- Enable debug mode
emu:feature_set("debugmode", false)  -- Disable debug mode
local enabled = emu:feature_get("debugmode")
```

> [!NOTE]
> Memory counters (`memcounters`) and call trace (`calltrace`) are available via CLI and WebAPI. Full Lua bindings for these analysis features are planned.

### Opcode Profiler

> **Status**: ✅ Implemented (2026-01)

Track Z80 opcode execution statistics and capture execution traces for crash forensics.

```lua
-- Session control
profiler_start()    -- Enable feature, clear data, start capture
profiler_stop()     -- Stop capture (data preserved)
profiler_pause()    -- Pause capture (retain data)
profiler_resume()   -- Resume paused capture
profiler_clear()    -- Clear all profiler data

-- Status query
local status = profiler_status()
-- status.feature_enabled = true
-- status.capturing = true
-- status.session_state = "capturing"  -- "stopped", "capturing", "paused"
-- status.total_executions = 15234567
-- status.trace_size = 10000
-- status.trace_capacity = 10000

-- Get opcode execution counters (optional limit parameter, default 100)
local counters = profiler_counters(100)
-- Each entry: {prefix=0, opcode=126, count=2156789, mnemonic="LD A,(HL)"}

-- Get recent execution trace (for crash forensics)
local trace = profiler_trace(500)
-- Each entry: {pc=0x1234, prefix=0, opcode=0x7E, flags=0x44, a=0x42, frame=1200, tstate=45000}
```

### Memory Profiler

> **Status**: ✅ Implemented (2026-01)

Track memory access patterns across all physical memory pages (RAM/ROM/Cache).

```lua
-- Session control
memory_profiler_start()    -- Enable feature, start capture
memory_profiler_stop()     -- Stop capture
memory_profiler_pause()    -- Pause capture (retain data)
memory_profiler_resume()   -- Resume paused capture
memory_profiler_clear()    -- Clear all data

-- Status query
local status = memory_profiler_status()
-- status.feature_enabled, status.capturing, status.session_state, status.tracking_mode

-- Data retrieval
local pages = memory_profiler_pages(20)      -- Get per-page access summaries
local counters = memory_profiler_counters(5, "physical")  -- Address-level counters for page
local regions = memory_profiler_regions()    -- Monitored region statistics
memory_profiler_save("/path/output", "yaml") -- Save data to file
```

### Call Trace Profiler

> **Status**: ✅ Implemented (2026-01)

Track CPU control flow events (CALL, RET, JP, JR, RST, etc.).

```lua
-- Session control
calltrace_profiler_start()    -- Enable feature, start capture
calltrace_profiler_stop()     -- Stop capture
calltrace_profiler_pause()    -- Pause capture (retain data)
calltrace_profiler_resume()   -- Resume paused capture
calltrace_profiler_clear()    -- Clear all data

-- Status query
local status = calltrace_profiler_status()
-- status.feature_enabled, status.capturing, status.session_state
-- status.entry_count, status.buffer_capacity

-- Data retrieval
local entries = calltrace_profiler_entries(100)  -- Get trace entries
local stats = calltrace_profiler_stats()    -- Get call/return statistics
```

### Unified Profiler Control

> **Status**: ✅ Implemented (2026-01)

Control all profilers (opcode, memory, calltrace) simultaneously.

```lua
-- Start all profilers (enables features automatically)
profilers_start_all()

-- Pause all profilers (retain data)
profilers_pause_all()

-- Resume all paused profilers
profilers_resume_all()

-- Stop all profilers
profilers_stop_all()

-- Clear all profiler data
profilers_clear_all()

-- Get status of all profilers
local status = profilers_status_all()
-- status.opcode.session_state = "capturing"
-- status.opcode.total_executions = 15234567
-- status.memory.session_state = "capturing"
-- status.calltrace.session_state = "capturing"
-- status.calltrace.entry_count = 450
```

### RZX Playback

Play RZX input recordings: the start snapshot loads, then every `IN` returns the recorded value and the interrupts follow the recording until its end, a desync (strict mode) or `rzx_stop()`; the machine then runs live. Terms and conventions: [command-interface.md §12](./command-interface.md#12-rzx-playback).

| Function | Returns | Description |
| :--- | :--- | :--- |
| `rzx_play(path [, options])` | table `{ok, error, message, emulator_id, model_switched, model, required_model, summary}` | Play a recording. `options`: `desync_mode` (`"strict"` / `"tolerant"`), `ei_short_frame_blocks_int`, `ld_air_parity_quirk`, `ignore_later_snapshots`, `switch_model` (default `true`). The model switches only when the interpreter follows the selected machine; an interpreter bound to one machine gets `error = "model_mismatch"` and `required_model` instead (its machine is never replaced under it). |
| `rzx_seek(frame)` | `ok, reason` | Move to the boundary after `frame` frames (back through keyframes, forward by playing on). |
| `rzx_stop()` | boolean | Stop playing; `false` when nothing played. |
| `rzx_status()` | table | `loaded`, `active`, `summary`, `path`, `creator`, `state` (`playing` / `finished` / `desynced` / `stopped`), `frame`, `total_frames`, `block`, `blocks`, `interrupts`, `desyncs`, `drift`, `max_drift`, `snapshots_applied`, `keyframes`, `keyframe_bytes`, `reason`, `first_desync` `{kind, frame, expected, actual, pc}`. |

`snapshot_load("game.rzx")` plays a recording on the machine as it is. `snapshot_load` returns `ok, reason, emulator_id`: a TS-Conf program (`.spg`) on another model switches the machine to TS-Conf first when the script is not bound to one machine (`emulator_id` is then the new instance), and is refused with the reason when it is.

```lua
local r = rzx_play("games/ericfloaters.rzx", {desync_mode = "tolerant"})
if not r.ok then print(r.message) end
run_frames(500)
print(rzx_status().summary)   -- playing frame 500 / 32315 (1.5%), block 1 / 1, 0 desyncs
```

### Time-Travel Debugging

The TTD functions are **global functions** (like the mouse functions), not methods on the emulator object. They act on the bound emulator, or on the selected one when the script is not bound to an instance. Bindings: `core/automation/lua/src/emulator/lua_emulator.h`. Command semantics and background: [command-interface.md §8](./command-interface.md#8-time-travel-debugging-ttd).

**Session rules** — read [command-interface.md → TTD Session Rules](./command-interface.md#ttd-session-rules). In short: states are `idle`, `recording`, `detached`; seek/step/find-last/reverse functions do nothing useful while recording (the core refuses them — `ttd_seek` returns `reached = false`, the boolean functions return `false`), so call `ttd_stop()` first; `ttd_start()` switches the `timetravel` feature on by itself; while recording, `snapshot_load`, `tape_load`, `disk_create`, `feature_set` (switching `timetravel`/`debugmode` off), `ttd_invalidate`, `ttd_set_journal_enabled` and `gs_switch_personality` are refused and return `false, reason` (`disk_load`: `success = false` with the reason in `message`); on a stopped session loads, disk create, ROM reload, a host speed change and `ttd_invalidate()` drop the history, while a reset keeps it; while recording, the host speed is locked to 1x and turbo / fast tape / turbo tape / fast disk are off; `tinframe` counts T-states at the machine's top CPU clock (plain T-states without a hardware turbo, ×2 on Scorpion/ATM Turbo 2+, ×4 on ZX-Evo - see Time in the session rules).

```lua
local ok, reason = snapshot_load("game.sna")
if not ok then print(reason) end   -- "Cannot load a snapshot while TTD is recording: ... Stop the recording first."
```

Unlike the WebAPI, the Lua functions do not pause the emulator for you: pause it before browsing history. Errors never raise; they come back as `false` (plus a reason for a TTD refusal), an empty table, or a table with an `error` string. When the build has no TTD engine every function returns `false` / an empty or `error` table.

**Session lifecycle:**

```lua
ttd_start()                  --> bool   -- keeps the ttd_set_journal_enabled choice (journal on by default)
ttd_start("development")     --> bool   -- write journal on
ttd_start("gaming")          --> bool   -- no write journal (smaller)
ttd_set_journal_enabled(b)             -- choose journal mode for the next start
ttd_set_history_limit(frames, bytes)   --> frames, bytes  -- keep only the newest history while recording
                                       --   (0 = no limit, nil keeps a value; status: history_* fields)
ttd_get_journal_enabled()    --> bool
ttd_stop()                             -- stop recording, keep history
ttd_invalidate([reason])               -- drop all history (default reason "lua invalidate")
```

**Status:**

```lua
local status = ttd_status()
-- status.ttd_available         = true
-- status.state                 = "idle"   -- idle | recording | detached
--
-- Provenance: recorded here, or opened from a file?
-- status.loaded_from_file      = true
-- status.source_path           = "/sessions/bug-1274.ttd"
-- status.captured_at_unix_ms   = 1755712345678  -- 0 for a live recording
--
-- Machine
-- status.model_id              = 0
-- status.model_ram_pages       = 8    -- BOUND, not a count (48K reports 6)
-- status.machine               = { model = "PENTAGON", model_id, ram_page_bound, rom_signature = "0x...",
--                                  peripheral_mask, peripherals = { "betadisk", ... },
--                                  general_sound = "none"|"z80"|"lw"|"ngs", turbo_sound = "none"|"turbosound"|"tsfm" }
--                                  -- the recorded machine; nil while there is no session
-- status.recorded_by           = "emu-..."  -- the instance that recorded a loaded file; nil for a live one
--
-- Timeline
-- status.session_start_frame   = 98
-- status.current_end_frame     = 397
-- status.checkpoint_count      = 301
--
-- Sections
-- status.write_journal_enabled = true
-- status.write_journal_complete = true  -- false: write/io find-last replays history
-- status.write_journal_wrapped = false  -- true: a "no match" from the journal replays
-- status.write_journal_gap     = nil    -- when incomplete: {reason, frame, tinframe}
-- status.bookmark_count        = 2
-- status.write_journal_records = 729025
-- status.write_journal_bytes   = 8748300
-- status.coverage_index_frames = 300  -- 0 => reverse queries replay instead
-- status.coverage_index_bytes  = 13926
-- status.input_event_count     = 10
-- status.external_event_count  = 0
-- status.input_history_complete = true
-- status.port_journal_active   = true   -- replay needs no media or host device
-- status.port_journal_off_reason = nil  -- set when off (e.g. NeoGS in the GS slot)
-- status.port_read_count       = 8225
-- status.port_write_count      = 15520
-- status.port_journal_bytes    = 9234
-- status.port_replay_value_mismatches = 0  -- > 0: a device answered otherwise on replay
-- status.port_replay_divergences = 0     -- > 0: execution left the recording
--
-- Memory
-- status.page_store_bytes         = 40960
-- status.page_store_used_bytes    = 665600
-- status.baseline_frames_captured = 2159
-- status.session_heap_bytes       = 1043968
--
-- status.last_drop_reason         = "snapshot-load"  -- "" until a history is dropped
-- status.unavailable_reason       = nil  -- set when this machine has no time travel (a ZX-Poly member)
```

`source_path` is filled by `ttd_load` (the path it was given).

**Navigation:**

```lua
ttd_seek(4823)                   -- seek to frame 4823, tinframe 0
ttd_seek(4823, 14982)            -- seek to (frame, tinframe)
-- --> { reached = true, arrived_at = {frame = 4823, tinframe = 14982},
--       halt_reason = "target",          -- "target" | "external_event" | "out_of_range"
--       blocking_marker = {frame, tinframe, kind, reason} }  -- only for external_event

ttd_step_back()                  --> bool  -- one frame back (same position inside the frame)
ttd_step_forward()               --> bool  -- one frame forward, inside recorded history
ttd_step_instruction_back()      --> bool
ttd_step_instruction_forward()   --> bool

ttd_resume()                     --> bool  -- record again from the exact current position (frame and tinframe)
ttd_resume(4823, 0)              --> bool  -- ...or from (frame, tinframe; tinframe defaults to 0); future is discarded
                                            -- fails from idle: seek first

ttd_position()
-- --> { current = {frame, tinframe}, session_end = {frame, tinframe} }
```

**Reverse execution:**

```lua
ttd_reverse_step()               --> bool  -- one instruction back
ttd_reverse_step(10)             --> bool  -- ten instructions back
ttd_reverse_step_tstates(5000)   --> bool  -- back 5000 t-states (nearest instruction start)

local r = ttd_reverse_continue({0x8000, 0x8010})
-- --> { matched = true, pc = 0x8000, frame = 4700, tinframe = 812 }
--     frame/tinframe are present only when matched.
--     A replay barrier met on the way adds
--     blocked_by_marker = {kind, reason, frame, tinframe}  (as the WebAPI does)
--     covered_from, covered_from_tinframe, covered_to, covered_to_tinframe:
--     the searched span (command-interface.md -> Search window)
```

**Reverse search:**

```lua
-- Positional form: addr, access, value, pc_from, pc_to, before_frame, before_tin,
--                  phys_page, addr_from, addr_to
local r = ttd_find_last(0x5800, "write")

-- Table form (snake_case or camelCase keys: addr_from/addrFrom, pc_from/pcFrom, phys_page/physPage):
local r2 = ttd_find_last{
    addr_from = 0x4000,        -- or addr = 0x5800 for one address
    addr_to   = 0x8000,
    access    = "write",       -- "write" (default) | "read" | "execute" | "io"
    value     = 0x07,
    pc_from   = 0x4000,        -- pc_to defaults to 0xFFFF
    pc_to     = 0x8000,
    before_frame = 4823,       -- search at or before this point (default: current position)
    before_tin = 0,
    phys_page = 5              -- 0..255: one physical RAM page
}
-- --> { found = false }  (no match)  or
--     { found = true, frame, tinframe, pc, value, phys_page, access }  or
--     { found = false, blocked = true, marker_frame, marker_tinframe,
--       marker_kind, marker_reason }  (a replay barrier stopped the search first)
--     phys_page is absent (nil) for ROM / no RAM page.
--     Every answer also carries covered_from / covered_from_tinframe /
--     covered_to / covered_to_tinframe: the part of history searched.
--     For writes the write journal answers when it holds every write of the session;
--     otherwise the search replays history (see command-interface.md "When the write journal answers").
```

**When did the program ... (port events):** `ttd_port_events(event, [arg], [options])` searches the port journals - every IN and OUT with its time and PC - without replay. Events, arguments and options: [command-interface.md → Port events](./command-interface.md).

Output below is from the recorded fixture `testdata/ttd/port-journals/dizzyx.ttd` (Dizzy X):

```lua
local r = ttd_port_events("key", "space")          -- when the game saw SPACE pressed
-- r.ok = true, r.count = 1, r.truncated = false, r.scanned = 7905, r.direction = "in"
-- r.hits[1] = { index = 6082, frame = 430, tinframe = 7824, port = 0x7FFE, value = 0xFE, pc = 0x72B2 }

local w = ttd_port_events("ay-write", 7, { limit = 3 })
-- w.count = 3, w.truncated = true; w.hits[1]: frame 50, tinframe 9202, pc 0xD86E, value 0x18, ay_register 7

local f = ttd_port_events("key", "q", { file = "testdata/ttd/port-journals/dizzyx.ttd" })
-- a saved session, searched without loading it: f.hits[1].frame = 365

local j = ttd_port_events("in", nil, { port = 0x1F, port_mask = 0xFF, match = "any-set", value_mask = 0x1F })
-- Kempston joystick reads with a direction or fire down
if not j.ok then print(j.error) end                 -- no journals, recording running, bad option
```

**Markers and bookmarks:**

```lua
for _, m in ipairs(ttd_markers()) do        -- replay barriers
    print(m.frame, m.tinframe, m.kind, m.reason)
end   -- kind: tape_control | disk_write | debugger_edit | other
      -- (hardware_reset is a reserved kind, never written: a reset stops the recording instead)

ttd_bookmark_add("before crash")            -- at the current position
ttd_bookmark_add("umt entry", 4823, 14982)  -- at (frame, tinframe)
-- --> { added = true, label, frame, tinframe }  or  { added = false, error = "..." }
for _, bm in ipairs(ttd_bookmarks()) do     -- time-sorted
    print(bm.frame, bm.tinframe, bm.label)
end
ttd_bookmark_delete("before crash")        --> bool
ttd_seek_bookmark("umt entry")
-- --> same table as ttd_seek (blocking_marker included when a marker stops it)
--     plus bookmark = "umt entry" (error = "..." for an unknown label)
```

Bookmarks are advisory and never stop a seek; markers do.

**Sessions on disk:**

```lua
ttd_dump("/tmp/session.ttd")               --> bool
ttd_load("/tmp/session.ttd")
-- --> { ok = true, checkpoint_count, session_start_frame, current_end_frame }
--     or { ok = false, error = "..." }  (e.g. recorded on a different model: both model ids named)
-- After a load the session is idle: use ttd_seek to position the emulator.

ttd_file_info("/tmp/session.ttd")
-- A .ttd file read without loading it (headers only, no emulator needed):
-- --> { ok = true, path, file_bytes, schema_version, flags, captured_at_unix_ms, recorded_by,
--       session_state, session_start_frame, session_end_frame, checkpoint_count, page_slot_count,
--       sections = { write_journal, coverage_index, input_journal, port_journals, ... },
--       machine = { model, general_sound, turbo_sound, peripherals, rom_signature, ... },
--       peripherals_from_header }
--     or { ok = false, path, error = "..." }
-- Provision the machine it needs first: its model, its General Sound card (machine.general_sound).
```

**Coverage index queries:**

```lua
local probe = ttd_coverage_probe{frame = 100, kind = "executed", addr_from = 0x0038, addr_to = 0x0040}
-- { frame = 100, kind = "executed", touched = true, index_available = true }
-- Frames outside the covered window: index_available = false, touched = false

local scan = ttd_coverage_scan{kind = "executed", addr_from = 0x0038, addr_to = 0x0040,
                               from_frame = 1, to_frame = 200, limit = 200}
-- { kind = "executed", frames = {18, 19, 20}, first_match = 18, last_match = 20,
--   matching_frames = 3, scanned_frames = 183, truncated = false,
--   covered_from = 18, covered_to = 197, index_available = true }

local summary = ttd_coverage_summary{from_frame = 1, to_frame = 500, bucket_size = 50, limit = 100}
-- { from_frame = 1, to_frame = 500, covered_from = 18, covered_to = 497,
--   bucket_size = 50, bucket_count = 10, index_available = true,
--   buckets = { {frame_start = 1, frame_end = 50, executed_distinct = 412,
--                written_distinct = ..., read_distinct = ..., has_keyframe = true}, ... } }
```

`kind` is `executed`, `written` or `read` (an unknown kind falls back to `executed`; `summary` without `kind` counts all three). `to_frame` defaults to the session end; `phys_page` above 255 returns a table with `error`.

### Analysis, Capture & Assembly

> **Status**: ✅ Implemented (2026-09)

Analysis, capture and assembly functions mirroring the WebAPI endpoints of the
same names (see [command-interface.md](./command-interface.md)). All functions
return a result table; on failure the table carries an `error` string.

```lua
-- Stepping helpers
emu.step_out()                       -- run until the current subroutine returns
emu.skip_until(0x8000)               -- fast-forward until PC == target (breakpoints skipped)
emu.skip_until("0x8000", 70000000)   -- optional explicit t-state budget

-- Memory search
emu.mem_find("AF 3C")                -- hex pattern as string (spaces optional)
emu.mem_find(0xAF3C)                 -- or as a number
emu.mem_find("AF 3C", 0x8000, 0xFFFF, 2, 32)  -- start, end, alignment, max matches

-- Screen state
emu.screen_digest()                  -- digest screen area (0x4000-0x5AFF), border folded in
emu.screen_digest(0x4000, 0x5AFF, false)       -- explicit range, border folding off
emu.screen_digest(nil, nil, nil, "active")      -- hash the surface the video mode displays now
                                               -- (ATM modes follow the 7FFD bit-plane pair);
                                               -- result carries active_surface = {video_mode, pages}
emu.ports_map()                      -- static port map + live routing flags (P1-5 + P1-2 tags):
                                     -- { model, entries = {{port, mask, match, device, gate?,
                                     --                      tags = {"memory","rom",...}, latch?}},
                                     --   live = {trdos_active, mouse_ports_decoded,
                                     --           mouse_routing_note, shadow_monitor_paged?} }
                                     -- tags: semantic categories (keyboard/memory/rom/screen/storage/
                                     --   mouse/joystick/system + sound members sound_ay/sound_covox/...);
                                     --   a row can carry several (Pentagon #FB = covox AND sounddrive).
                                     -- latch: live-value binding name (p7FFD, p1FFD, pDFFD, ...) when the
                                     --   row is a paging latch; key absent otherwise.
emu.paging_state()                   -- tagged paging latches + bank table (P1-2):
                                     -- { model, paging_locked, trdos_active,
                                     --   latches = {{port, latch, tags, device?, gate?, value,
                                     --               decoded = {ram_bank=.., shadow_screen=.., ...}}},
                                     --   banks = {{bank, address_range, type, page,
                                     --             name?, role?, signature?, contended?}} }
                                     -- ROM bank rows carry the §5.2 identification: name = recognized
                                     -- content (SHA-256 catalog), role = the model's layout slot; a
                                     -- role/name mismatch is the one-glance wrong-ROM signal.
emu.beam_position()                  -- { frame, line, tstate, zone, ..., layers = {{id, x, x_end, y}} }
emu.video_layout()                   -- layers (surface, beam window, dots_per_t), framebuffer placement, family
emu.video_pixel(x, y [, layer])      -- sources (space, page, offset, bit_mask, role, z80), colour_index, rgb, rendered_rgb
emu.video_pixel_at(t)                -- the same for the point under the beam at frame T (layer pixel or border)
emu.video_address(page, offset)      -- areas a RAM byte feeds; emu.video_address_z80(addr) through current paging
emu.video_address_in(space, offset [, page]) -- "ram", "sprite_ram" (attribute word, byte offset) or "palette" (cell, byte offset)
emu.video_text([layer])              -- exact text grid of ATM / ZX-Evo text modes (lines: text, codes, attrs)
emu.video_temporal()                 -- ZX DLSS de-flicker status: { algorithm ("" = off), active, inactive_reason, applicable,
                                     --   video_delay_frames, video_delay_ms, audio_extra_delay_frames, processed,
                                     --   correcting, showing_processed, corrected_frames, written, shown_raw, late, restarts,
                                     --   last_ms, average_ms, shown_frame / last_frame = {pattern, period2..period5,
                                     --   field, field_stage, whole_paper, scene_average},
                                     --   algorithms = {...},
                                     --   default_algorithm = "mod-tpgwafsd" }
emu.video_temporal_set("mod-tpgwafsd") -- switch it on; "off" or "" switches it off. Returns the new status,
                                     --   or { ok = false, error = "..." } for an unknown name. While on, the
                                     --   picture is shown later by the algorithm's look-ahead (7 frames for
                                     --   mod-tpgwafsd with the default A/V delay of 2) and the sound is delayed
                                     --   by the difference (audio_extra_delay_frames, 5 here) to stay in sync.
emu.frame_cost()                     -- per-frame halt/run cost accounting

-- Coverage analyzer
emu.coverage_start()                 -- start clean; emu.coverage_start(true) keeps old data
emu.coverage_stop()
emu.coverage_status()                -- executed count + first ranges
emu.coverage_gaps()                  -- executed ranges and gaps over the full 64K
emu.coverage_gaps(0x8000, 0xFFFF, 10)

-- AY register log
emu.ay_log_start()                   -- default capacity; emu.ay_log_start(8192) to override
emu.ay_log_stop()
emu.ay_log_status()
emu.ay_log_dump()                    -- last 16 entries; emu.ay_log_dump(32, 100) = count, offset

-- Audio capture
emu.audio_capture_start(2.5)         -- capture 2.5 s of stereo audio
emu.audio_capture_status()
emu.audio_capture_result()           -- sample stats + per-channel peak/RMS
emu.audio_capture_result("out.wav")  -- additionally export a 16-bit WAV file

-- Core audio rate (same switch as CLI 'setting audio_rate' and WebAPI
-- PUT settings/audio_rate): pin 44100..192000 for this run - never persisted.
-- 0 = auto: follow the resolution chain device rate > [SOUND] CoreRate >
-- 44100. Applied at the next frame boundary; deferred while a recording is
-- in progress.
emu.set_audio_rate(96000)            -- true when the pin took effect
emu.set_audio_rate(0)                -- release the pin (auto)
emu.get_audio_rate()                 -- { pin = 96000, core_rate = 96000, target_rate = 96000 }

-- Video recording (requires a build with ENABLE_RECORDING)
emu.video_record("start", {format = "gif", fps = 50, scale = 2})  -- opts table optional
emu.video_record("start", {audio_rate = 48000})  -- pin the core rate first (number or "auto");
                                     -- waits up to 1 s for the rate before recording starts,
                                     -- errors if the emulator is paused
emu.video_record("start", {format = "h264", filename = "run.mp4", audio = "aac"})  -- with the sound
                                     -- track (audio = true means aac; video_bitrate / audio_bitrate
                                     -- in kbps). Omit audio for video only. gif + audio is refused
emu.video_record("stop")             -- also "pause" / "resume"
emu.video_record_status()             -- recording state + live stats (frames, duration, fps,
                                     -- audio, audio_codec, audio_sample_rate, audio_duration)

-- Assembler
emu.assemble("ld a,2\nout (254),a", 0x8000)          -- assemble, listing only
emu.assemble("ld a,2\nout (254),a", "0x8000", true)  -- + write bytes to RAM

-- Label resolution
emu.label_resolve("main_loop")       -- by name
emu.label_resolve(0x8100)            -- by address: exact, aliases, nearest below/above

-- Source listings
emu.listing_load("game.lst")
emu.listing_source_at()              -- source line at PC; emu.listing_source_at(0x8100)
emu.listing_step_line()              -- run until the source line changes (~2 s budget)
emu.listing_run_to_line(120)         -- run to first code byte of line 120 (~10 s budget)
```

## Usage Examples

### Register Dump Macro

```lua
-- dump_regs.lua
function dump_registers()
    local emu = get_emulator()
    local cpu = emu:get_cpu()
    
    print(string.format("AF = 0x%04X", cpu:get_af()))
    print(string.format("BC = 0x%04X", cpu:get_bc()))
    print(string.format("DE = 0x%04X", cpu:get_de()))
    print(string.format("HL = 0x%04X", cpu:get_hl()))
    print(string.format("PC = 0x%04X", cpu:get_pc()))
    print(string.format("SP = 0x%04X", cpu:get_sp()))
    
    local flags = cpu:get_flags()
    print(string.format("Flags: S=%d Z=%d H=%d P=%d N=%d C=%d",
        flags.S and 1 or 0,
        flags.Z and 1 or 0,
        flags.H and 1 or 0,
        flags.P and 1 or 0,
        flags.N and 1 or 0,
        flags.C and 1 or 0))
end

dump_registers()
```

### Memory Scanner

```lua
-- find_pattern.lua
function find_pattern(pattern, start_addr, end_addr)
    local emu = get_emulator()
    local mem = emu:get_memory()
    
    local pattern_len = #pattern
    local found = {}
    
    for addr = start_addr, end_addr - pattern_len do
        local match = true
        for i = 1, pattern_len do
            if mem:read(addr + i - 1) ~= pattern[i] then
                match = false
                break
            end
        end
        
        if match then
            table.insert(found, addr)
        end
    end
    
    return found
end

-- Find all occurrences of: 0xCD 0x00 0x00 (CALL 0x0000)
local pattern = {0xCD, 0x00, 0x00}
local results = find_pattern(pattern, 0x4000, 0xFFFF)

print(string.format("Found %d occurrences:", #results))
for _, addr in ipairs(results) do
    print(string.format("  0x%04X", addr))
end
```

### Breakpoint Automation

```lua
-- auto_bp.lua
function on_breakpoint_hit(bp_id, address)
    local emu = get_emulator()
    local cpu = emu:get_cpu()
    local mem = emu:get_memory()
    
    print(string.format("Breakpoint %d hit at 0x%04X", bp_id, address))
    
    -- Dump registers
    print(string.format("  AF=0x%04X BC=0x%04X DE=0x%04X HL=0x%04X",
        cpu:get_af(), cpu:get_bc(), cpu:get_de(), cpu:get_hl()))
    
    -- Read instruction bytes
    local op1 = mem:read(address)
    local op2 = mem:read(address + 1)
    print(string.format("  Opcode: %02X %02X", op1, op2))
    
    -- Continue execution
    emu:resume()
end

-- Set up breakpoints
local emu = get_emulator()
local bp_mgr = emu:get_breakpoint_manager()

bp_mgr:add_execution_bp(0x0562)  -- Tape load routine
bp_mgr:add_execution_bp(0x0E9B)  -- Print routine

print("Breakpoints set. Resuming...")
emu:resume()
```

### Frame Counter

```lua
-- frame_counter.lua
local frame_count = 0
local emu = get_emulator()

function on_frame()
    frame_count = frame_count + 1
    
    if frame_count % 50 == 0 then  -- Every second (50 fps)
        print(string.format("Frame: %d", frame_count))
        
        local cpu = emu:get_cpu()
        print(string.format("  PC: 0x%04X", cpu:get_pc()))
    end
end

-- Register frame callback
emu:set_frame_callback(on_frame)

print("Frame counter started. Press Ctrl+C to stop.")
while true do
    sleep(100)
end
```

### Auto-Save Macro

```lua
-- autosave.lua
local emu = get_emulator()
local save_interval = 60  -- seconds
local save_count = 0

while true do
    sleep(save_interval * 1000)
    
    save_count = save_count + 1
    local filename = string.format("autosave_%03d.z80", save_count)
    
    emu:pause()
    emu:save_snapshot(filename)
    print(string.format("Auto-saved to %s", filename))
    emu:resume()
end
```

### Cheat Code Injector

```lua
-- cheat.lua
-- Infinite lives cheat for a hypothetical game

local emu = get_emulator()
local mem = emu:get_memory()

local LIVES_ADDR = 0x8000  -- Address storing lives count

function apply_cheat()
    local lives = mem:read(LIVES_ADDR)
    if lives < 9 then
        mem:write(LIVES_ADDR, 9)
        print("Lives set to 9")
    end
end

-- Apply cheat every frame
emu:set_frame_callback(apply_cheat)

print("Cheat activated: Infinite lives")
```

### Disassembler Output

```lua
-- disasm.lua
function disassemble(start_addr, count)
    local emu = get_emulator()
    local mem = emu:get_memory()
    
    -- Simple Z80 disassembler (subset)
    local opcodes = {
        [0x00] = "NOP",
        [0x3E] = "LD A,#",
        [0x06] = "LD B,#",
        [0xC3] = "JP nn",
        [0xCD] = "CALL nn",
        [0xC9] = "RET",
        -- ... more opcodes
    }
    
    local addr = start_addr
    for i = 1, count do
        local opcode = mem:read(addr)
        local mnemonic = opcodes[opcode] or string.format("DB 0x%02X", opcode)
        
        print(string.format("0x%04X: %02X  %s", addr, opcode, mnemonic))
        
        addr = addr + 1
    end
end

-- Disassemble 16 instructions from current PC
local emu = get_emulator()
local cpu = emu:get_cpu()
disassemble(cpu:get_pc(), 16)
```

## Event Callbacks

### Supported Events

```lua
-- Frame rendered
emu:set_frame_callback(function()
    -- Called every frame
end)

-- Breakpoint hit
emu:set_breakpoint_callback(function(bp_id, address)
    -- Called when breakpoint triggers
end)

-- State changed
emu:set_state_callback(function(old_state, new_state)
    print("State: " .. old_state .. " -> " .. new_state)
end)

-- Memory written
emu:set_memory_write_callback(function(address, value)
    print(string.format("Write: [0x%04X] = 0x%02X", address, value))
end)
```

### Callback Management

```lua
-- Register callback
callback_id = emu:register_callback("frame", my_function)

-- Unregister callback
emu:unregister_callback(callback_id)

-- Clear all callbacks
emu:clear_callbacks()
```

## Lua Standard Library

Available Lua modules:
- `base` - Basic functions (print, tonumber, etc.)
- `string` - String manipulation
- `table` - Table operations
- `math` - Mathematical functions
- `io` - File I/O (sandboxed)
- `os` - OS functions (time, date)

**Note**: Unsafe functions are disabled in sandboxed mode:
- `os.execute()` - Execute shell commands (disabled)
- `io.popen()` - Pipe to process (disabled)
- `loadfile()` - Load arbitrary files (restricted)
- `dofile()` - Execute arbitrary files (restricted)

## Performance

- **Function Call Overhead**: ~100-200ns
- **Memory Access**: Comparable to C (no GC overhead for reads)
- **Script Load Time**: <1ms for typical scripts
- **Memory Usage**: ~200KB base + script size

### Optimization Tips

1. **Cache emulator objects**:
   ```lua
   local emu = get_emulator()  -- Once
   local mem = emu:get_memory()  -- Once
   
   for i = 1, 1000 do
       mem:read(0x8000)  -- Fast
   end
   ```

2. **Use local variables**:
   ```lua
   local mem = emu:get_memory()  -- Faster than global
   ```

3. **Batch operations**:
   ```lua
   -- Slow: individual reads
   for addr = 0x8000, 0x8100 do
       local val = mem:read(addr)
   end
   
   -- Fast: block read
   local data = mem:read_bytes(0x8000, 256)
   ```

4. **Avoid frequent table creation**:
   ```lua
   -- Reuse tables instead of creating new ones
   local result = {}
   for i = 1, 1000 do
       table.insert(result, mem:read(0x8000 + i))
   end
   ```

## Error Handling

```lua
-- Protected call
local success, result = pcall(function()
    local emu = get_emulator()
    emu:init()
    -- ... operations that might fail
end)

if not success then
    print("Error: " .. result)
end

-- Error with context
local function safe_read(addr)
    if addr < 0 or addr > 0xFFFF then
        error("Invalid address: " .. addr)
    end
    return emu:get_memory():read(addr)
end
```

## Debugging Lua Scripts

### Print Debugging
```lua
print("Debug: PC = " .. cpu:get_pc())
print(string.format("Debug: 0x%04X", value))
```

### Interactive Debugging
```lua
-- Simple REPL
while true do
    io.write("> ")
    local line = io.read()
    if line == "quit" then break end
    
    local func, err = load("return " .. line)
    if func then
        print(func())
    else
        print("Error: " .. err)
    end
end
```

### Using MobDebug (with ZeroBrane Studio)
```lua
-- Install MobDebug
require("mobdebug").start()

-- Script will pause here for debugger
local emu = get_emulator()
-- Set breakpoints in ZeroBrane
```

## Integration with Emulator

### Loading Scripts at Startup
```bash
unreal-emulator --lua-init init.lua --lua-script main.lua
```

### Hot Reload
```lua
-- reload.lua
function reload_script(script_name)
    package.loaded[script_name] = nil
    require(script_name)
    print("Reloaded: " .. script_name)
end

-- Usage
reload_script("my_module")
```

### Persistent State
```lua
-- Save Lua state with snapshot
emu:save_snapshot("save.z80", {
    lua_state = true,
    include_scripts = true
})

-- Load restores Lua state
emu:load_snapshot("save.z80")
-- Scripts automatically resumed
```

## See Also

### Interface Documentation
- **[Command Interface Overview](./command-interface.md)** - Core command reference and architecture
- **[CLI Interface](./cli-interface.md)** - TCP-based text protocol for interactive debugging
- **[WebAPI Interface](./webapi-interface.md)** - HTTP/REST API for web integration
- **[Python Bindings](./python-interface.md)** - Direct C++ bindings for automation and AI/ML

### Advanced Interfaces (Future)
- **[GDB Protocol](./gdb-protocol.md)** - Professional debugging with standard GDB/LLDB clients
- **[Universal Debug Bridge](./udb-protocol.md)** - High-performance analysis and profiling

### Navigation
- **[Interface Documentation Index](./README.md)** - Overview of all control interfaces

### External Resources
- **[Lua 5.4 Reference Manual](https://www.lua.org/manual/5.4/)** - Lua programming language specification
- **[sol2 Documentation](https://sol2.readthedocs.io/)** - C++/Lua binding library
