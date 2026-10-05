# Python Bindings - Direct C++ Integration

## Overview

The Python bindings provide direct in-process access to the emulator core through pybind11. This enables powerful automation, testing, AI/ML integration, and complex scripting scenarios.

**Status**: ✅ Implemented
**Implementation**: `core/automation/python/`
**Binding Library**: pybind11
**Python Version**: 3.10+

**Note**: See [command-interface.md](./command-interface.md) for the complete command reference that these Python bindings implement.  

## Architecture

```
┌──────────────────────────────┐
│   Python Script/REPL         │
│   (test.py, notebook, etc.)  │
└───────────┬──────────────────┘
            │ Python C API
            ▼
┌──────────────────────────────┐
│   pybind11 Bindings          │
│   • Emulator class           │
│   • Memory class             │
│   • Z80 class                │
│   • BreakpointManager        │
└───────────┬──────────────────┘
            │ Direct calls (zero-copy)
            ▼
┌──────────────────────────────┐
│   C++ Emulator Core          │
│   (native performance)       │
└──────────────────────────────┘
```

## Key Advantages

1. **In-Process Execution**: No IPC overhead, direct memory access
2. **Zero-Copy Data Access**: Read memory buffers without serialization
3. **Python Ecosystem**: NumPy, Pandas, Matplotlib, PyTorch, TensorFlow
4. **Interactive Development**: IPython, Jupyter notebooks
5. **Full Language Features**: Classes, exceptions, decorators, context managers
6. **Type Hints**: Modern Python with type checking support

## Installation

### Module Import
```python
import unreal_emulator as ue

# Create emulator instance
emu = ue.Emulator()
emu.init()

# Access subsystems
cpu = emu.get_cpu()
memory = emu.get_memory()
debug = emu.get_debug_manager()
```

### Embedded Python
The emulator can embed Python interpreter internally:
```cpp
// C++ side
AutomationPython* python = new AutomationPython();
python->start();
python->executePython("print('Hello from embedded Python!')");
```

### Machine Identity and Lifecycle
The Python bindings do not expose model-selecting instance creation: `ue.Emulator()` always builds the default machine. For multi-instance lifecycle, model switching and strict model validation (`creatable` flags, 400-with-reason failures) use the WebAPI (`POST /api/v1/emulator/create`, `GET /api/v1/emulator/models`) or the CLI (`create`/`start <model>`). Machine identity of an existing instance is observable through state endpoints (e.g. TTD status reports `model_id`/`model_ram_pages`).

**ZX-Poly** (four synchronized instances of one model, through the same
`EmulatorManager::CreateZXPolyMachine` every surface uses):
```python
id = zxpoly_start("ZXPOLY-PENTAGON", "/path/to/Alien8.zxp")   # raises RuntimeError with the reason on failure
id2 = zxpoly_start("ZXPOLY-48K", ram_power_on="zero")        # every RAM page of all four modules reads 0
emu_get(id2).ram_power_on()                                  # "random" | "zero": RAM contents at creation
status = zxpoly_status(id)                              # dict; None if not a ZX-Poly machine
status["locked"], status["video_mode"], status["diverged"], status["modules"][1]["registers"]
status["parallel_slaves"], status["pipelined_slaves"]      # how the slaves are scheduled
```
The model is a configuration name (`ZXPOLY-48K`, `ZXPOLY-128K`,
`ZXPOLY-PENTAGON`) or a base model such as `PENTAGON`.

## API Reference

### Module Functions

```python
def videowall_singlesync(enable: bool, emulator_id: str = "") -> bool:
    """Toggle Videowall Single Sync Mode"""
```

### Emulator Class

```python
class Emulator:
    """Main emulator instance"""
    
    def __init__(self, symbolic_id: str = "", log_level: LogLevel = LogLevel.Trace):
        """Create emulator instance"""
        
    def init(self) -> bool:
        """Initialize emulator hardware"""
        
    def release(self):
        """Cleanup and release resources"""
        
    def reset(self):
        """Hardware reset (equivalent to pressing reset button)"""

    # Device state reports - the same trees the WebAPI, Lua, CLI and MCP
    # return (command-interface.md section 3.3); dicts/lists/scalars
    def audio_ay_state(self, chip: int = -1) -> dict:
        """AY/SSG report: overview (chip=-1; active_chip = the one the ports talk to) or one chip fully
        decoded (latched_register = the register #FFFD last selected, selected = the ports talk to it)"""

    def audio_fm_state(self, chip: int = -1) -> dict:
        """TurboSound FM report: board + chip summaries (chip=-1) or one YM2203 FM half
        (mode, timers, channels[3] with operators[4]: registers, pitch, envelope_state,
        attenuation_db, key_on). available=False with a description without TSFM"""

    def gs_state(self) -> dict:
        """General Sound / NeoGS report (the WebAPI /state/audio/gs tree): device, mailbox,
        MPAG page, DAC channels, card CPU, and on NeoGS a 'neogs' dict (windows, SD, MP3, DMA,
        ZX-DMA). available=False with a description when no card is fitted"""

    def audio_covox_state(self) -> dict:
        """Covox / SoundDrive report: fitment, the ports this model decodes, shared_with_beta128,
        the four DAC latches. available=False when no Covox is fitted"""

    def ide_state(self) -> dict:
        """IDE board report: scheme, gate, adapter latches, selected unit, intrq,
        units[2] (kind, slot, medium, translation, task_file with decoded bits,
        command, atapi sense on a CD drive). available=False without a board"""

    def cdaudio_state(self) -> dict:
        """The ATAPI CD drives' audio: drives[] (unit, slot, disc with tracks, audio
        status / status_code / lba / msf / track / index / relative_msf / play range,
        drive_volume (page 0Eh), mixer row). available=False without a CD drive"""

    def cdaudio(self, verb: str = "status", drive: str = "", **options) -> dict:
        """A CD audio verb (CdAudioControl): play (track=2, to=3 | lba=, frames= |
        msf="00:04:16", end=...), pause, resume, stop, volume (left=, right=, route=,
        sotc=), mixer (volume=, mute=, solo=). Reply: ok, error, message, drive"""

    def tsconf_state(self) -> dict:
        """TS-Conf machine report: memory (mem_config decoded, pages, lck128,
        lock48, dos, vdos, cache, fm_window, fm_maps raw), video (mode, geometry,
        nogfx / notsu / gfxovr, v_page, pal_sel, border, offsets, tsu, the engine's
        line with t0_gpage / t1_gpage - the tile pages it is drawn with), interrupts,
        dma (live and programmed_source / programmed_destination, ctrl decoded),
        cpu_clock, sys_config, cache_en, sd, regs (the register file #00-#47 as last
        written). available=False on other machines"""

    def sprinter_state(self) -> dict:
        """Sprinter Sp2000 report: pld (state, module Standard / Game, selected_by +
        why, cell_EE, game grid offset, bitstream hashes), decoder (CNF
        map, DOS, PN5, #7FFD / #1FFD), windows (kind + physical page), registers,
        cells #C0-#FF, clock, frame, video (mode table summary), z84c15, fdc, cmos,
        ide, bios (images, how to select). available=False on other machines"""

    def sprinter_ports(self, map=None, dos=None, pn5=None, rw="") -> dict:
        """The decoded Sprinter port table: rows of code, name, direction, address
        pattern, example; None = the machine's current map / DOS / PN5; rw 'r' / 'w' /
        'rw'. ValueError on a bad value"""

    def sprinter_port(self, port, rw="", map=None, dos=None, pn5=None) -> dict:
        """One Sprinter port (int or hex string): index into page #40, code, name,
        or the Z84C15 when the chip answers it"""

    def sprinter_text(self) -> dict:
        """The Sprinter screen text: lines[] (row, text, codes) of the mode table's
        text squares, 80 x 32"""

    def sprinter_video(self, page=None, all=False, squares=True) -> dict:
        """The mode table per square: map (one letter a square), hold, frame, rgmod,
        port_y, counts, palettes_used, squares[b][a] decoded"""

    def sprinter_palette(self, k=None) -> dict:
        """Palettes (k 0-7, 'all', default the used ones): pens n / rgb (R,G,B as stored) / vram"""

    def sprinter_sound_ring(self) -> dict:
        """The Covox-Blaster ring: 256 words, play / write index"""

    def sprinter_zx_mode(self, deep=True) -> dict:
        """The ZX (Spectrum) mode: active, config (options with evidence, best_match file and confidence), launcher
        (its .ZX text in RAM), clock, frame / INT, rom, ports (#7FFD / #1FFD / #01FD ... decodes, ttd_query)"""

    def sprinter_pld_journal(self, kinds=None, since=None, from_frame=None, to_frame=None, limit=None, source="live") -> dict:
        """Who changed the PLD setup: events (frame, t, pc, kind, port, value, text); source='ttd' reads the recording"""

    def sprinter_pld_journal_control(self, enabled=None, clear=False) -> dict:
        """Switch or clear the PLD journal"""

    def sprinter_bios(self) -> dict:
        """BIOS images, the one loaded (CRC-32), its known_issues, the configured one, start options"""

    def sprinter_bios_select(self, bios=None, fast_start=None, accel_int_suspend=None, reset=True) -> dict:
        """Select the BIOS (3.04 / 3.06 / 3.07 / a file) and start options; loads at the reset"""

    def memory_regions(self) -> dict:
        """Device memory regions (the Sprinter's 'vram'; TS-Conf 'cram' and 'sfile'; 'cmos' with a CMOS clock; ZX-Evo 'eeprom')"""

    def region_read(self, name, offset=0, length=256) -> bytes: ...
    def region_write(self, name, offset, data) -> int:
        """data: bytes, a list of ints or a hex string; through the device's write path"""
    def region_save(self, name, path, offset=0, length=0) -> None: ...
    def region_load(self, name, path, offset=0) -> int: ...

    def video_changes(self, frames=2) -> dict:
        """Video change log: latch changes with t, line, pc; palette / mode table writes per frame"""

    def framebuffer(self, format="rgba") -> dict:
        """Raw pixels: width, height, format, encoding, data (bytes), array (numpy when installed)"""

    def capture_screen(self, format="png", full=None, area="", path="", source="") -> dict:
        """Screenshot of the presented frame (source="live": the frame as drawn now; a paused machine adds the beam position and partial to frame). area="full" (default, the whole frame with border) or "screen" (the
        working picture); format "png" (default) or "gif"; path writes the file on the machine running the
        emulator. Returns {success, format, area, width, height, size, crop, screen_window, frame, data (base64)
        | file} or {success: False, error, kind}. full= is a deprecated alias (True = "full", False = "screen")"""

    def audio_mixer(self) -> dict: ...
    def audio_mixer_set(self, source, muted=None, solo=None, volume=None, gain_db=None) -> dict: ...
    # audio_capture_start(seconds=1.0, source="") records one mixer device's own buffer

    def tsconf_tsu(self) -> dict:
        """TS-Conf TSU objects and palette for debug views: t_config, tilemap_page,
        sprite_page, tile_layers (t0 / t1: enabled, draw_tile_zero, graphics_page,
        x_offset, y_offset, palette), sprites (all 85 SFILE descriptors decoded:
        active, leap, layer s0 / s1 / s2, x, y, width, height, x_flip, y_flip,
        tile, bitmap_x, bitmap_y, palette, words), active_sprites, cram (256
        cells: value, rgb). available=False on other machines"""

    def network_state(self) -> dict:
        """Network adapters: card (ZXNETUSB ports, W5300 registers and sockets), virtual network (DHCP leases,
        sockets, guest servers, counters, recent activity); available=False without an adapter"""

    def key_route(self, route: str = "") -> str:
        """Where host and injected keys go: 'auto' | 'matrix' | 'ps2' | 'both' (the ZX matrix, the PS/2 keyboard
        controller of a ZX-Evo / ATM Turbo 2+, both); empty = query. Returns the route in force."""

    def network_configure(self, **settings) -> None:
        """Change [NETWORK] settings: card='none'|'zxnetusb'|'zxwifi'|'atm2ioesp' (a list with ','), host_access=True|False, dns_mode='host'|'pass',
        hosts='name=ip,...', forwards='tcp:host:guest,...', remote_access=True|False (the host listeners of guest servers: True, default,
        = 0.0.0.0, other computers on the LAN can connect; False = 127.0.0.1 only; alone it keeps every connection; network_state()
        ['virtual_network']['listen_address'] shows it), connect_timeout_ms=n,
        com_port='none'|'loopback'|'tcp:host:port'|'serial:device[,baud]'|'espnet[,baud]'|'at[,firmware][,baud]' (firmware 'esp32'|'esp8266'|'esp8266-at221'|'esp8266-at222' for this module alone) (the machine's own serial port: the ZX-Evo AVR's, the ATM Turbo 2+ keyboard controller's or the ZX Profi v5's 8251; an ESP module's baud defaults to the port's, 38400 on ATM2, else 115200), zx_wifi=<same values> (the ZX-WiFi card's ESP, default 'at'), com_modem_lines=True|False, esp_chip='esp32'|'esp8266'|'esp8266-at221'|'esp8266-at222' (the Sprinter's SprinterESP takes an ESP8266 build, else esp8266-at222), isa1_peer / isa2_peer='at'|'modem[,guest port]'|'loopback'|'tcp:host:port'|'serial:device[,baud]' (Sprinter: a UART card's line - SprinterESP default 'at', ISA modem 'modem', SprinterSerial COM1 'none'), isa1_peer_b / isa2_peer_b (SprinterSerial COM2), modem_phonebook='5551234=host:port,...' (the numbers a Hayes modem peer dials; com_port='modem' on any machine), avr_firmware='baseconf'|'base2010'..'base2023'|'ts'|'ts2013'|'ts2016-02'|'ts2016-04' (ZX-Evo), kbc_firmware='none'|'v22-7'..'v41'
        (ATM Turbo 2+ keyboard controller; com_port is its RS-232 from v31, shown as machine_serial in network_state(); ZX Profi v5: the board's 8251, also machine_serial),
        atm2ioesp=<com_port values> and atm2ioesp_address=0xF0|0xF8 (the ATM2IOESP card on the ATM Turbo 2+ INTERNAL I/O connector, shown as
        atm2ioesp in network_state()), zifi=<com_port values> | 'zifi-native[,s3|esp01s]' (TS-Conf, ZX-Evo with a TS-Labs AVR firmware: the ZiFi board's ESP; 'at' = the original ESP-01 with NonOS AT 1.7.4 unless an ESP8266 build is named, 'at,esp8266-at222' = ESP-AT 2.2.2, 'zifi-native' = the 2026 firmware;
        default 'none'; network_state()['zifi'] shows the API registers and both rings). Applied at the next frame
        boundary; the card is fitted again, so every connection closes. ValueError with the reason"""

    def rtc_state(self) -> dict:
        """CMOS clock report: chip, ports, cells, nvram_file, address_latch, time_mode
        (host / emulated / fixed), time (as the guest reads it now), register_a..d decoded,
        alarm, dump (hex lines). Peeked: never clears register C. available=False without a clock"""

    def profi_state(self) -> dict:
        """ZX Profi board chips: board (v3 / v5), port_map (ext_ports cpm / sys / v003, dos_latch, cpm,
        rom14, hires, extended_map, long_ports_beside_vg93), ppi8255 (control, port directions and
        latches), pit8253 (counters[]: mode, count, out, gate; v5), usart8251 (mode, command, baud,
        status flags, byte counters, com_interrupt_enable = #B3 D0; v5). available=False elsewhere"""

    def rtc_read(self, start: int, count: int = 1) -> bytes:
        """CMOS cells as the guest reads them, without side effects. ValueError without a
        clock or for a range outside the chip"""

    def rtc_write(self, start: int, values: list[int]) -> None:
        """Write CMOS cells like a guest write: the time registers set the clock (set B bit 7
        SET around a multi-register set, as a guest does), C and D are read-only, RAM cells
        are stored. The address latch is not touched. ValueError without a clock / bad range"""

    def isa_state(self) -> dict:
        """ISA slots (Sprinter Sp2000): latch (the #9FBD byte: value, a19_a14, aen, reset), window
        (whether window 3 shows a slot: page, slot, space), slots[] (slot 1 = J6 page #D4 / #D0,
        2 = J7 #D6 / #D2; configured, card, not_fitted, the card's own fields, counters with the IRQ
        counters, irq_line: the slot's IRQ line - level, driver, route to PIO port B bit 0 / 1, the
        PIO's bit-mode setup, pending / under service, reaches_cpu; summary_line; the ZX-bus
        adapter's zx_bus: the General Sound / NeoGS behind it - cards[0] personality, ports,
        cpu_addresses, status, machine_reset - its reset_held / reset_pulses), pio_port_b,
        irq_summary. A 3C509B
        (card 'el3c509b') adds resources['id_port'] (its ID port range, how the Z80 reaches it, the
        isolation state); network_state()['slots'][n] then shows its ID port, window, FIFOs, EEPROM,
        statistics, link state, events and a one-line summary.
        available=False on other machines"""

    def isa_io_read(self, slot: int, address) -> int:
        """One ISA I/O read cycle at a 20-bit ISA address (int or '#30A' text); an empty slot
        reads 0xFF. Also isa_io_write(slot, address, value), isa_io_peek(slot, address) (no side
        effect), isa_mem_read / isa_mem_write, isa_reset() (one RESET DRV pulse to both slots) and
        isa_latch(value). A cycle with an effect is a tool edit while TTD records. ValueError for
        a bad slot / address / value or on a machine without ISA slots"""

    def isa_journal(self, last: int = 64) -> dict:
        """The ISA access journal: entries[] (frame, t, pc, slot, access, space, isa_address,
        cpu_address, what = the card's register name, value); interrupt events have event='irq'
        (IRQ line edges with the card's cause, PIO port B requests, INT acknowledged, RETI), also
        in irq_events (their own 128-entry ring, which a polled card does not flush)"""

    def network_frames(self, link: str = "", last: int = 32) -> dict:
        """The Ethernet gateway's frame capture (frame-level cards such as the Sprinter's NE2000):
        frames[] (index, frame, direction to_card / from_card, port, length, summary, hex).
        Also network_frames_pcap(link='') -> bytes (a pcap file) and
        network_inject_frame(link, hex) (a frame towards the card at the next frame boundary;
        ValueError without a gateway / bad hex / unknown link)"""

    def audio_moonsound_state(self, part: str = "") -> dict:
        """MoonSound (OPL4) report: overview (part=''), the FM half (part='fm': 18 channels,
        timers, register banks) or the wavetable half (part='pcm': 24 slots with envelopes).
        available=False without the card; ValueError for another part"""

    def fdc_state(self) -> dict:
        """Beta Disk WD1793 report: registers, status_bits, last_command, fsm_state,
        signals (intrq/drq), beta128_register, density, selected_drive, drives[4]"""

    def contention_state(self) -> dict:
        """Memory contention report: rule (none/ula48/ula128/gatearray), applicable, switch,
        effective, memory_interface, io_rule, slots[4] (mapping, contended), the +2A/+3
        floating_bus_latch, even_m1, scorpion_turbo_logic (Scorpion: SC15.1 / SC15.3), atm710_turbo_waits (ATM Turbo 2+ v7.10: active / off / contention_off), statistics
        per kind while debug mode is on"""

    # Screen reports - same fields as every other module (command-interface.md section 6.6)
    def screen_state(self, verbose: bool = False) -> dict:
        """model, video_mode, resolution, border_color, shadow_screen_capable, active_screen,
        active_ram_page, active_ram_pages, contention, flash_inverted; verbose adds
        screen_0/screen_1 (or screen) with z80_access and port_0x7FFD"""

    def screen_mode(self) -> dict:
        """Video mode: resolution, color_depth, colors, bpp, attribute_size, text grid,
        memory_layout, displayed RAM pages, eff7 / dffd / ff77 latches"""

    def screen_flash(self) -> dict:
        """flash_phase, frames_until_toggle, flash_cycle_position, toggle interval"""

    # Single-value getters: screen_get_mode(), screen_get_border(), screen_get_flash(),
    # screen_get_active(); screen_video_state() is the former name of screen_mode()
        
    def pause(self):
        """Pause emulation"""
        
    def resume(self):
        """Resume emulation"""
        
    def step(self, skip_breakpoints: bool = True) -> dict:
        """Execute one instruction; returns {executed, stopped, breakpoint_id, address, access}
        (with skip_breakpoints=False an execution breakpoint stops before its instruction)"""
        
    def steps(self, count: int, skip_breakpoints: bool = False) -> dict:
        """Execute up to N instructions; a breakpoint ends the run early. Returns the same dict"""

    def get_registers(self) -> dict:
        """pc sp af bc de hl ix iy af_ bc_ de_ hl_ i r memptr q im iff1 iff2 halted boundary t
        (r with bit 7 as last written; memptr = the internal WZ latch; q = the flag capture
        register; boundary = none / prefix_dd / prefix_fd / int_shadow / ld_a_ir / nmi_ack;
        t = CPU T-states since the frame's start)"""

    def get_register(self, name: str) -> int | None:
        """Any name of the register table (a, hl, af', ir, memptr / wz, im, iff1, iff2, ...)"""

    def set_register(self, name: str, value: int) -> bool:
        """False for an unknown name or a value im (0-2) / iff1, iff2 (0-1) cannot hold;
        8-bit registers take the low byte"""
        
    def run_frame(self):
        """Run exactly one video frame (config.frame t-states)"""
        
    def run_frames(self, count: int):
        """Run exactly N video frames"""
        
    def get_id(self) -> str:
        """Get emulator UUID"""
        
    def get_symbolic_id(self) -> str:
        """Get symbolic ID (if set)"""
        
    def set_symbolic_id(self, id: str):
        """Set symbolic ID"""
        
    def get_state(self) -> EmulatorState:
        """Get current state (Running/Paused/Stopped)"""
        
    def get_cpu(self) -> Z80:
        """Get Z80 CPU instance"""
        
    def get_memory(self) -> Memory:
        """Get memory subsystem"""
        
    def get_debug_manager(self) -> DebugManager:
        """Get debug manager"""
        
    def get_breakpoint_manager(self) -> BreakpointManager:
        """Get breakpoint manager"""
```

### Tape Operations

`Emulator` methods mirroring the CLI `tape` commands and the WebAPI `/tape/*` endpoints one-to-one (same names, states and catalog indices).

```python
emu = Emulator()
emu.init()

# Load / eject
emu.tape_load("/path/to/game.tap")   # .tap/.tzx/.spc/.sta/.ltp/.zxt, or a folder built into a tape
emu.tape_eject()                      # the tape leaves the tape slot (False while TTD records)

# Transport (same semantics as `tape play|pause|stop|rewind|seek`)
emu.tape_play()    # start at consumption cursor; resumes in place when paused
emu.tape_pause()   # freeze mid-block (idempotent); False when not playing
emu.tape_stop()    # terminal stop: invalidates the loaded image
emu.tape_rewind()  # rewind to block 0, image kept
emu.tape_seek(4)   # position head at catalog block 4

# Inspection
emu.tape_is_inserted()
emu.tape_get_path()
emu.tape_pos()     # None without a tape, else dict
                  # {"state": "playing", "block": 4, "pulse": 1234,
                  #  "seconds_into_block": 1.2, "block_total_seconds": 4.5,
                  #  "cursor": 5, "block_count": 12}
emu.tape_blocks()  # None without a tape, else list of dicts
                  # {"index", "kind", "name", "type", "declared_length",
                  #  "param1", "param2", "speed": {"profile", "baud"},
                  #  "checksum_valid", "checksum_applicable", "seconds",
                  #  "raw_size", "playable", "fast_load"}
emu.tape_info()    # None without a tape subsystem, else dict
                  # {"status", "file", "format", "state", "cursor",
                  #  "block_count", "total_seconds", "fast_tape",
                  #  "turbo_tape", "fast_load": {...}}

# Audio bridge (pure file conversions, no emulator state touched)
emu.tape_render("game.tzx", "out.wav")            # whole tape, 44100 Hz
emu.tape_render("game.tzx", "out.flac",
                options={"first_block": 2, "last_block": 5,
                         "sample_rate": 48000, "amplitude": 0.8,
                         "invert_level": False})
# -> {"ok", "error", "duration_sec", "samples", "blocks",
#     "encoder", "warnings"}

emu.tape_import("recording.wav", "imported.tzx")  # hysteresis defaults to 0.2
emu.tape_import("recording.wav", "imported.tap", 0.25)
# -> {"ok", "error", "decoder", "sample_rate", "samples_decoded",
#     "signal_edges", "blocks_recognized", "blocks_written",
#     "output_path", "warnings"}
```

Playback `state` is one of `"idle"`, `"playing"`, `"paused"`, `"ended"` — identical strings across CLI, WebAPI, Lua and Python.

### Media (every slot)

Floppy drives, the SD card and every other slot through one set of methods — the same verbs,
slot names, options and errors as the WebAPI, CLI, MCP and Lua. Full reference:
[docs/features/media.md](../../../features/media.md).

```python
emu.media_list()                                         # slots + detached media
emu.media_insert("A", "/games/elite-1.trd")              # slot: fdd.a, A, a:, floppy:0, tag:...; "auto"
emu.media_insert("sd", "/home/me/zx/sdcard", fs="fat32")
emu.media_insert("cd", "/home/me/music/album", format="audio-cd")  # MP3 / FLAC / WAV files as an audio CD
emu.media_swap("A", "/games/elite-2.trd", save=True)     # a dirty disk needs save / export / discard
emu.media_eject("B", export="/tmp/b.trd")
emu.media_eject("B", discard=True, async_=True)          # "async" is a Python keyword
emu.media_info("sd"); emu.media_formats(kind="floppy"); emu.media_save("A"); emu.media_export("sd", "/tmp/card.img")
emu.media_targets("/discs/dna_nemo.iso")                 # where a file can go: file, targets, default, refusal
emu.media_discard("A"); emu.media_rescan("sd"); emu.media_create("B"); emu.media_protect("A", True)
emu.media(verb, slot, path, **options)                   # any verb
```

Each returns the result dict: `ok`, `error`, `message`, `slot`, `pending`, `revision`, `report`
and the verb's fields. Errors are results (`ok: False`), not exceptions.

### Disk Operations

`Emulator` methods for the four floppy drives (0-3 / A-D), mirroring the CLI `disk` commands and the WebAPI `/disk/{drive}/*` endpoints.

```python
# Load / eject (path: .trd/.scl/.fdi/.udi/.dsk/.td0/.mgt/.img, auto-detected)
emu.disk_load("/path/to/game.trd")             # insert into drive 0 (A)
# -> {"success", "started": False, "message"}
emu.disk_load("/path/to/game.trd", drive=0, autostart=True)  # + autostart: quick-reset
    # into TR-DOS and run the disk (same as the Qt UI's drag-and-drop autostart /
    # WebAPI "autostart": true / CLI "disk insert <drive> <file> autostart") - drive 0 only
# -> {"success", "started", "message"}; message explains what autostart did
#    (e.g. "Autostart: boot", "No BASIC programs on disk - mounted only")
emu.disk_eject(0)

# A non-zero `drive` is accepted but currently still mounts into drive 0 - a pre-existing
# limitation of the underlying LoadDisk/AutostartDisk (not specific to Python); disk_load
# adds a "warning" key to the result in that case rather than silently mis-inserting.

# Blank disk
emu.disk_create(drive=1, cylinders=80, sides=2)   # drive 1 (B)

# Inspection
emu.disk_is_inserted(0)
emu.disk_get_path(0)
emu.disk_list()          # list of {"id", "letter", "inserted", "path"}
emu.disk_info(0)         # None without a disk, else geometry/catalog details

# Raw sector access (read-only)
emu.disk_read_sector(0, 0, 0, 1)      # drive 0, cyl 0, side 0, sector 1
emu.disk_read_sector_hex(0, 0, 1)     # drive 0, track 0, sector 1
# Write into a sector's data field (SectorWrite: CRC recalculated, image modified, TTD tool edit); the sector
# number is 0-based like disk_read_sector's (ID - 1). ValueError when refused, RuntimeError on a 503-like busy
emu.disk_write_sector(0, 0, 0, 8, b"MYDISK", offset=245)  # TR-DOS sector ID 9: the disk title
```

### Mouse Input

`Emulator` methods that drive the machine's own mouse (Kempston interface, Sprinter serial mouse,
ZX-Evo / TS-Conf PS/2 mouse), mirroring the CLI `mouse` commands
and the WebAPI `/mouse/*` routes (source: `core/automation/python/src/emulator/python_emulator.h`).
Units, limits and the reasoning behind them: [command-interface.md §11](./command-interface.md#11-mouse-input-injection).

The mouse is **relative**: `mouse_move(10, -5)` means "travelled 10 pixels right and 5 down";
the program on the machine moves its own cursor by that much. `dy` positive = **up**.

```python
emu.mouse_move(dx, dy)                  # -127..127 each, not both 0
emu.mouse_glide(dx, dy)                 # -4096..4096; one step per frame, later input queues behind it
emu.mouse_busy()                        # True while a glide (and its queue) is in progress
emu.mouse_devices()                     # the machine's mouse devices (list of device dicts)
emu.mouse_press(button)                 # "left" | "right" | "middle" (or "l" | "r" | "m")
emu.mouse_release(button)
emu.mouse_click(button, frames=2)       # hold 1..65535 frames, then release on its own
emu.mouse_buttons(["left", "middle"])   # exact pressed set; [] = none
emu.mouse_wheel(steps)                  # -7..7, not 0; + = away from you
emu.mouse_release_all()                 # also cancels a pending click
emu.mouse_set_counters(x, y)            # debug: raw counters 0..255
emu.mouse_status(device="")             # state dict (below); device: "kempston" | "sprinter" | "evo-ps2"
emu.mouse_click_pending()               # True while a click is still holding its button
emu.mouse_button_names()                # ["left", "right", "middle"]
```

Every changing method returns the resulting **state dict**:

```python
{'x': 41, 'y': 80,
 'buttons': {'left': False, 'right': False, 'middle': False},
 'button_mask': 255,          # active-low: a pressed button is bit 0 (254 = left down)
 'wheel': 0, 'wheel_enabled': False, 'present': True,
 'ports': {'FADF': 255, 'FBDF': 41, 'FFDF': 80},   # what IN returns now (integers, as in the WebAPI)
 'pending_click': None,       # or {'button': 'left', 'frames_left': 1}
 'ttd_journal': 'supported'}
# plus the machine's mouse: 'mouse_fitted', 'device' (None when none is fitted; the WebAPI
# device object: id, kind, fitted, in_use, ports, and 'serial' or 'ps2'), 'devices', 'queue'
# plus 'warning': '...' when a wheel step was sent with no wheel fitted
```

`mouse_status()` additionally carries `'routing': {'ports_decoded': bool, 'note': str}` —
the same live answer as the WebAPI `GET /mouse/status` routing object: whether a mouse port
read is decoded right now, and the reason when it is shadowed (mouse not fitted, TR-DOS ports
accessible, a registered peripheral claims the port family, or model-specific gating —
Scorpion DOS trigger / Shadow Monitor). The changing methods (`mouse_move` etc.) do not carry
it, also matching the WebAPI.

> [!NOTE]
> `ports` values are integers, the same as in the WebAPI. The Python dict has no `available` key: `mouse_status()` raises
> instead when there is no mouse device.

**Errors raise** (the `key_*` methods return `False` instead; the difference is deliberate,
so a mistake is not silently ignored):

| Cause | Exception |
|-------|-----------|
| Out-of-range value, zero move/wheel, unknown button name | `ValueError` (message says which value and the allowed range) |
| TTD replay in progress | `RuntimeError("TTD replay in progress; live mouse input refused")` |
| No mouse fitted on the machine | `RuntimeError("no mouse fitted on this machine: ...")` |
| `mouse_status(device=...)` names a device the machine does not have | `ValueError` (lists the ids it has) |
| No mouse manager / device | `RuntimeError` |

**Worked example** (paused, reproducible):

```python
emu = unreal.emu_get_selected()
emu.pause()
st = emu.mouse_move(10, -5)          # from reset X=31 Y=85 -> st['x'] == 41, st['y'] == 80
emu.mouse_click("left", frames=2)    # left held for the next 2 frames
emu.run_frames(3)                    # 2 frames held + 1 for the program to react
assert emu.mouse_status()["buttons"]["left"] is False

try:
    emu.mouse_move(200, 0)
except ValueError as e:
    print(e)   # dx=200 out of range -127..127; split into several moves with run_frames between them
```

While TTD records, these calls are written to the TTD input journal, so a replay reproduces them.

### Joystick Input

`Emulator` methods that drive the emulated Kempston joystick, mirroring the CLI `joystick` commands
and the WebAPI `/joystick/*` routes (source: `core/automation/python/src/emulator/python_emulator.h`).
Semantics, units and limits: [command-interface.md §13](./command-interface.md#13-joystick-input-injection).

```python
emu.joystick_press(buttons)             # "up+fire", "up,fire" or ["up", "fire"]
emu.joystick_release(buttons)
emu.joystick_set(state)                 # a byte 0..255, a name string or a list; [] = none
emu.joystick_tap(buttons, frames=2)     # hold 1..65535 frames, then release on its own
emu.joystick_state()                    # state dict (below)
emu.joystick_tap_pending()              # True while a tap is still holding its buttons
emu.joystick_button_names()             # ["up", "down", "left", "right", "fire", "b5", "b6", "b7"]
```

Every changing method returns the resulting **state dict**, with the keys of the WebAPI state object:
`available`, `present`, `wired`, `state` (the byte), `port_value` (what `IN #1F` returns),
`buttons` (a bool per name), `pressed`, `button_names`, `keys`, `pending_tap` (`None` or
`{'mask', 'frames_left'}`), plus `warning` when the guest cannot see the buttons.

**Errors raise**: `ValueError` for a bad name, type or range (`state=300 out of range 0..255`),
`RuntimeError` for a TTD replay in progress or a missing device; the messages are the shared ones.

```python
emu = unreal.emu_get_selected()
emu.pause()
st = emu.joystick_press("up+fire")      # st['state'] == 0x18, st['port_value'] == 0x18
emu.joystick_tap("left", frames=3)
emu.run_frames(4)
assert emu.joystick_state()["buttons"]["left"] is False
```

### Feature Management

`feature_list()` enumerates every registered runtime feature dynamically (the same list the CLI `feature` table and the WebAPI `/features` endpoint return), keyed by feature id:

```python
features = emu.feature_list()
# {"sound": True, "fasttape": True, "turbotape": True, "calltrace": False, ...}

emu.feature_set("fasttape", False)     # same switch as `setting fast_tape off`
print(emu.feature_get("turbotape"))    # True
emu.feature_set("turbomode", True)     # turbo mode (same switch as `setting speed unlimited`)
```

`feature_list()` and `feature_get()` report the state in effect. Time-travel debugging holds some features off: `turbomode` while a recording runs, and `fasttape`, `turbotape`, `fastdisk` while a recording runs, history is replayed, or the machine sits in history. A held feature reads as `False`, and `feature_set(name, True)` on it returns `False`. The full list of features, aliases and defaults: [command-interface.md §5](./command-interface.md#5-feature-management--configuration).

### Speed and Turbo

The same switches as CLI `setting speed` and the WebAPI `speed` setting.

```python
emu.set_speed(4)      # -> bool; host speed multiplier 1, 2, 4, 8 or 16 (applied at the next frame)
                      #    False for 2..16 while TTD records; ValueError for any other value
emu.get_speed()
# -> {'multiplier': 4,        # the host multiplier set above
#     'effective': 4,         # what runs, including the machine's own hardware turbo (ATM, Scorpion)
#     'turbo_mode': False,    # the turbomode feature (switch it with feature_set('turbomode', True))
#     'turbo_active': False,  # the engine runs unthrottled now: turbo mode, or turbo tape warping a load
#     'turbo_audio': False}   # audio kept on in turbo mode
```

Changing the speed on a stopped or loaded TTD session drops that session's history (frame timing is part of the recording); re-selecting the current speed changes nothing.

### Z80 CPU Class

```python
class Z80:
    """Z80 CPU emulation"""
    
    def get_af(self) -> int:
        """Get AF register pair"""
        
    def set_af(self, value: int):
        """Set AF register pair"""
        
    def get_bc(self) -> int:
        """Get BC register pair"""
        
    # Similar for DE, HL, IX, IY, SP, PC
    
    def get_pc(self) -> int:
        """Get program counter"""
        
    def set_pc(self, value: int):
        """Set program counter"""
        
    def get_flags(self) -> dict:
        """Get flags as dictionary"""
        # Returns: {'S': bool, 'Z': bool, 'H': bool, 'P': bool, 'N': bool, 'C': bool}
        
    def execute(self, instructions: int = 1) -> int:
        """Execute N instructions, return cycles"""
```

### Memory Class

```python
class Memory:
    """Memory subsystem with banking"""
    
    def read(self, address: int) -> int:
        """Read byte from Z80 address space (0x0000-0xFFFF)"""
        
    def write(self, address: int, value: int):
        """Write byte to Z80 address space"""
        
    def read_word(self, address: int) -> int:
        """Read 16-bit word (little-endian)"""
        
    def write_word(self, address: int, value: int):
        """Write 16-bit word (little-endian)"""
        
    def read_bytes(self, address: int, length: int) -> bytes:
        """Read byte array (zero-copy view)"""
        
    def write_bytes(self, address: int, data: bytes):
        """Write byte array"""
        
    def fill(self, address: int, length: int, value: int):
        """Fill memory region with byte value"""
        
    def get_ram(self) -> memoryview:
        """Get direct view of RAM (zero-copy)"""
        
    def get_rom(self) -> memoryview:
        """Get direct view of ROM (read-only)"""
```

### Physical Page Access

> **Status**: ✅ Implemented (2026-01)

Access physical memory pages directly, bypassing Z80 paging. Useful for ROM patching, shadow memory inspection, and cache/misc memory access.

```python
# Page types: "ram", "rom", "cache", "misc"

# Read/write single byte from physical page
value = emu.page_read("ram", 5, 0x100)        # page 5, offset 0x100
emu.page_write("ram", 5, 0x100, 0xFF)

# Block operations
data = emu.page_read_block("rom", 2, 0, 256)  # Read 256 bytes from ROM page 2
data = emu.mem_read_bytes(0, 65536)           # the whole CPU view as bytes (wraps at 0xFFFF)
data = emu.mem_read_bytes(0x1800, 768, space="ram5")  # a page window; "ram" = all RAM pages back to back
# ValueError with the reason on a bad space, address or length
emu.page_write_block("ram", 7, 0x1000, data)  # Write block to RAM page 7

# A port write through the machine's decoder, like a CPU OUT (paging, TS-Conf registers, AY, border), without
# breakpoints or device waits, recorded by TTD as a tool edit; paused, stopped or running
emu.port_out(0x13AF, 0x20)                    # TS-Conf: RAM page 0x20 into window 3

# PC history: the newest instructions with their window's page; the first call starts recording
h = emu.pc_history(depth=16)                  # dict {armed, started_now, total, capacity, entries [{address, kind, page}]}
emu.pc_history_arm(False)                     # stop (True: restart empty); emu.debug_snapshot(pchist=16) carries it too

# Long-poll: block (GIL released) until the debugger snapshot's seq moves past `since` (default: now) or the timeout
w = emu.debug_wait(since=snap["seq"], timeout_ms=5000)   # dict {seq, changed, state, pause}
# ValueError for a bad port (0..0xFFFF) or value (0..0xFF); RuntimeError when no coherent moment came (503 case)

# Get memory configuration
info = emu.memory_info()
# Returns: {
#   'pages': {'ram_count': 256, 'rom_count': 64, 'cache_count': 4, 'misc_count': 4},
#   'z80_banks': [{'bank': 0, 'start': 0, 'end': 16383, 'mapping': 'ROM0'}, ...]
# }
```

**Writes during a time-travel recording.** While a TTD recording runs, every memory write (`mem_write`, `mem_write_word`, `mem_write_block`), physical page write (`page_write`, `page_write_block`) and assembler write (`assemble` with write on) records a `debugger_edit` marker, a replay barrier, and briefly pauses a running emulator for the edit, so the recording sees the change. A write by Z80 address into a ROM bank leaves the ROM unchanged, as a CPU write would. No marker is written when no recording runs.

### Breakpoints

Methods of `Emulator` (breakpoints fire while debug mode is on; what stops where:
[.recipe/analysis/breakpoints-and-events.md](../../../../.recipe/analysis/breakpoints-and-events.md)).

```python
id = emu.bp(0x8000)                 # execution breakpoint, returns its id (-1 when refused)
id = emu.bp(0x8000, to=0x80FF)      # a range #8000-#80FF (one check per access, however many ranges)
id = emu.bp(0x0038, hits="50")      # stop on the 50th hit only; ">=50" from the 50th on, "%50" every 50th
id = emu.bp(0xC000, page="ram32")   # physical: RAM page 32, offset #0000, in whatever slot shows it ("rom3", "cache0")
id = emu.bp(0xC000, page="ram32", slot_only=True)  # only through slot 3 (#C000)
id = emu.bp_read(0x4000)            # memory read; the same keyword arguments
id = emu.bp_write(0x4000, to=0x57FF)  # memory write: the screen bitmap
id = emu.bp_port_in(0xFE, mask=0x00FF)  # port IN on #FE with any high byte; mask= and hits=
id = emu.bp_port_out(0x7FFD)
emu.bp_reset_hits(id)               # hit counter back to 0 (no id: all)
emu.bp_remove(id); emu.bp_clear()
emu.bp_enable(id); emu.bp_disable(id)
emu.bp_note(id, "main loop")        # annotation (empty clears); False for an unknown id
emu.bp_group(id, "game")            # group, created on use; switched on / off together (CLI bpgroup)
emu.bp_count()
print(emu.bp_list())                # the text table: "to 0x80FF", "in ram32", "mask 0x00FF", "hits >=50", "(hit 12x)"
emu.bp_status()                     # the last hit, see below; 'hit_count', and 'page' = {'kind', 'page'} for a physical one
```

### DebugManager Class

```python
class DebugManager:
    """High-level debugging coordination"""
    
    def get_breakpoint_manager(self) -> BreakpointManager:
        """Get breakpoint manager"""
        
    def get_label_manager(self) -> LabelManager:
        """Get label manager (symbols)"""
        
    def get_disassembler(self) -> Z80Disassembler:
        """Get disassembler"""
```

### Analyzer Management

```python
# List registered analyzers
names = emu.analyzer_list()  # → ["trdos"]

# Enable/disable analyzer
emu.analyzer_enable("trdos")  # → True
emu.analyzer_disable("trdos")  # → True

# Get analyzer status
status = emu.analyzer_status("trdos")
# → {"enabled": True, "state": "IN_TRDOS", "event_count": 42}

# Get captured events (optional limit parameter)
events = emu.analyzer_events("trdos", limit=50)
# → ["[0001234] TR-DOS Entry (PC=$3D00)", ...]

# Clear analyzer events
emu.analyzer_clear("trdos")
```

**TRDOSAnalyzer States:**
- `IDLE` - Not in TR-DOS ROM
- `IN_TRDOS` - In TR-DOS ROM but not executing command
- `IN_COMMAND` - Executing TR-DOS command
- `IN_SECTOR_OP` - Sector read/write in progress
- `IN_CUSTOM` - Custom sector operation

### Debug Commands (Direct Methods)

> **Status**: ✅ Implemented (2026-01)

The `Emulator` class exposes these debug methods directly:

```python
# Debug Mode Control
emu.debugmode_on()         # Enable debug mode
emu.debugmode_off()        # Disable debug mode
emu.debugmode()            # Returns True if debug mode enabled

# Memory Access Counters
emu.memcounters()          # Returns dict with:
# {
#     'total_reads': int, 'total_writes': int, 'total_executes': int,
#     'total_accesses': int,
#     'banks': [{'bank': 0, 'reads': int, 'writes': int, 'executes': int}, ...]
# }
emu.memcounters_reset()    # Reset all counters

# Call Trace
emu.calltrace(limit=50)    # Returns list of recent control flow events

# Breakpoint Status (Last Triggered)
emu.bp_status()            # Returns dict with type-specific fields:
# Memory: {'valid': bool, 'id': int, 'type': 'memory', 'address': int,
#          'execute': bool, 'read': bool, 'write': bool, 'active': bool, 'note': str, 'group': str}
# Port:   {'valid': bool, 'id': int, 'type': 'port', 'address': int,
#          'in': bool, 'out': bool, 'active': bool, 'note': str, 'group': str}
emu.bp_clear_last()        # Clear last triggered breakpoint ID

# Disassembly
emu.disasm()                                    # Disassemble from PC (default 10 lines)
emu.disasm(address=0x8000, count=20)           # Disassemble from address
emu.disasm_page("rom", 2, offset=0, count=20)  # Disassemble physical ROM page (e.g., TR-DOS)
emu.disasm_page("ram", 5, offset=0x100, count=10)  # Disassemble physical RAM page
# Returns: list of dicts with keys: address/offset, bytes, mnemonic, size, label,
#          target/targetLabel (jumps and calls), displacement/effectiveAddress/effectiveAddressLabel (IX/IY+d)
# Mnemonics print both label and address when a label exists at the target: 'call TEST_ROUTINE (#8010)'
```

### Opcode Profiler

> **Status**: ✅ Implemented (2026-01)

Track Z80 opcode execution statistics and capture execution traces for crash forensics.

```python
# Session control
emu.profiler_start()    # Enable feature, clear data, start capture
emu.profiler_stop()     # Stop capture (data preserved)
emu.profiler_pause()    # Pause capture (retain data)
emu.profiler_resume()   # Resume paused capture
emu.profiler_clear()    # Clear all profiler data

# Status query
status = emu.profiler_status()
# Returns: {
#     'feature_enabled': True,
#     'capturing': True,
#     'session_state': 'capturing',  # 'stopped', 'capturing', 'paused'
#     'total_executions': 15234567,
#     'trace_size': 10000,
#     'trace_capacity': 10000
# }

# Get opcode execution counters (top N by count)
counters = emu.profiler_counters(100)

# Get recent execution trace (for crash forensics)
trace = emu.profiler_trace(500)
```

### Memory Profiler

> **Status**: ✅ Implemented (2026-01)

Track memory access patterns (reads/writes/executes) across all physical memory pages.

```python
# Session control
emu.memory_profiler_start()    # Enable feature, start capture
emu.memory_profiler_stop()     # Stop capture
emu.memory_profiler_pause()    # Pause capture (retain data)
emu.memory_profiler_resume()   # Resume paused capture
emu.memory_profiler_clear()    # Clear all data

# Status query
status = emu.memory_profiler_status()
# Returns: {'feature_enabled': True, 'capturing': True, 'session_state': 'capturing', 'tracking_mode': 'physical'}

# Data retrieval
pages = emu.memory_profiler_pages(limit=20)   # Get per-page access summaries
counters = emu.memory_profiler_counters(page=5, mode='physical')  # Address-level counters
regions = emu.memory_profiler_regions()       # Monitored region statistics
emu.memory_profiler_save('/path/output', format='yaml')  # Save data to file
```

### Call Trace Profiler

> **Status**: ✅ Implemented (2026-01)

Track CPU control flow events (CALL, RET, JP, JR, RST, etc.).

```python
# Session control
emu.calltrace_profiler_start()    # Enable feature, start capture
emu.calltrace_profiler_stop()     # Stop capture
emu.calltrace_profiler_pause()    # Pause capture (retain data)
emu.calltrace_profiler_resume()   # Resume paused capture
emu.calltrace_profiler_clear()    # Clear all data

# Status query
status = emu.calltrace_profiler_status()
# Returns: {'feature_enabled': True, 'capturing': True, 'session_state': 'capturing', 
#           'entry_count': 450, 'buffer_capacity': 10000}

# Data retrieval
entries = emu.calltrace_profiler_entries(count=100)  # Get trace entries
stats = emu.calltrace_profiler_stats()     # Get call/return statistics
```

### Unified Profiler Control

> **Status**: ✅ Implemented (2026-01)

Control all profilers (opcode, memory, calltrace) simultaneously.

```python
# Start all profilers (enables features automatically)
emu.profilers_start_all()

# Pause all profilers (retain data)
emu.profilers_pause_all()

# Resume all paused profilers
emu.profilers_resume_all()

# Stop all profilers
emu.profilers_stop_all()

# Clear all profiler data
emu.profilers_clear_all()

# Get status of all profilers
status = emu.profilers_status_all()
# Returns: {
#     'opcode': {'session_state': 'capturing', 'total_executions': 15234567},
#     'memory': {'session_state': 'capturing', 'feature_enabled': True},
#     'calltrace': {'session_state': 'capturing', 'entry_count': 450}
# }
```

### RZX Playback

Play RZX input recordings: the start snapshot loads, then every `IN` returns the recorded value and the interrupts follow the recording until its end, a desync (strict mode) or `rzx_stop()`; the machine then runs live. Terms and conventions: [command-interface.md §12](./command-interface.md#12-rzx-playback).

Module functions, by emulator id (default: the selected machine). A model switch replaces the machine, so these take an id rather than an `Emulator` object:

| Function | Returns | Description |
| :--- | :--- | :--- |
| `unreal.rzx_play(path, emulator_id="", desync_mode="strict", ei_short_frame_blocks_int=False, ld_air_parity_quirk=False, ignore_later_snapshots=False, switch_model=True)` | dict `{ok, error, message, emulator_id, model_switched, model, previous_emulator_id, required_model, status}` | Play a recording; a recording made on another model switches to that model first (`emulator_id` is the new machine). `ValueError` on a bad `desync_mode`. |
| `unreal.rzx_seek(frame, emulator_id="")` | bool | Move to the boundary after `frame` frames (back through keyframes, forward by playing on); `RuntimeError` with the reason when refused. |
| `unreal.rzx_stop(emulator_id="")` | bool | Stop playing; `False` when nothing played. |
| `unreal.rzx_status(emulator_id="")` | dict | `loaded`, `active`, `summary`, `path`, `creator`, `version`, `snapshot`, `state` (`playing` / `finished` / `desynced` / `stopped`), `frame`, `total_frames`, `block`, `blocks`, `interrupts`, `desyncs`, `drift`, `max_drift`, `snapshots_applied`, `keyframes`, `keyframe_bytes`, `keyframe_interval`, `reason`, `first_desync` `{kind, frame, expected, actual, pc}`. |

`Emulator.rzx_stop()` and `Emulator.rzx_status()` act on that machine; `Emulator.snapshot_load("game.rzx")` plays a recording on it as it is.

`unreal.snapshot_load(path, emulator_id="", switch_model=True)` returns `{ok, message, emulator_id, model_switched, previous_emulator_id, required_model}`: a TS-Conf program (`.spg`) on another model switches the machine to TS-Conf first (`emulator_id` is the new one). `Emulator.snapshot_load` is bound to its machine and raises `RuntimeError` for such a file instead.

```python
r = unreal.rzx_play("games/greenberet.rzx")          # on a 48K: switches to a 128K
emu = unreal.emu_get(r["emulator_id"])
emu.run_frames(500)
print(unreal.rzx_status(r["emulator_id"])["summary"])
```

### Time-Travel Debugging

TTD methods live on the `Emulator` object (`emu.ttd_*`). Bindings: `core/automation/python/src/emulator/python_emulator.h`. Command semantics and background: [command-interface.md §8](./command-interface.md#8-time-travel-debugging-ttd).

**Session rules** — read [command-interface.md → TTD Session Rules](./command-interface.md#ttd-session-rules). In short: states are `idle`, `recording`, `detached`; seek/step/find-last/reverse methods are refused by the core while recording (`ttd_seek` returns `reached: False` with `halt_reason: 'out_of_range'`, the boolean methods return `False`, `ttd_find_last` / `ttd_reverse_continue` return `None`), so call `ttd_stop()` first; `ttd_start()` switches the `timetravel` feature on by itself; while recording, `snapshot_load`, `tape_load`, `disk_create`, `feature_set` (switching `timetravel`/`debugmode` off), `ttd_invalidate`, `ttd_set_journal_enabled` and `gs_switch_personality` raise `RuntimeError` with the reason (`disk_load` returns `success: False` with the reason in `message`); on a stopped session loads, disk create, ROM reload, a host speed change and `ttd_invalidate()` drop the history, while a reset keeps it; while recording, the host speed is locked to 1x and turbo / fast tape / turbo tape / fast disk are off; `tinframe` counts T-states at the machine's top CPU clock (plain T-states without a hardware turbo, ×2 on Scorpion/ATM Turbo 2+, ×4 on ZX-Evo - see Time in the session rules).

```python
try:
    emu.snapshot_load('game.sna')
except RuntimeError as refusal:
    print(refusal)   # Cannot load a snapshot while TTD is recording: ... Stop the recording first.
```

Unlike the WebAPI, these methods do not pause the emulator for you: call `emu.pause()` before browsing history. Failures are reported in the return value (`False`, `None`, or a dict with `error`), not as exceptions — except an out-of-range `phys_page`, which raises `ValueError`, and a refusal to protect a running recording, which raises `RuntimeError`.

**Session lifecycle:**

```python
emu.ttd_start()                  # -> bool; keeps the ttd_set_journal_enabled choice (off by default)
emu.ttd_start(journal=True)              # also record the write journal
emu.ttd_set_journal_enabled(True)        # switch it at any moment, also while recording (a segment starts)
emu.ttd_build_journal(from_frame=1200, to_frame=1500)  # build it by replay for those frames (default: all)
# -> {'ok': True, 'error': None, 'cancelled': False, 'frames_built': 301, 'frames_covered': 0,
#     'frames_refused': 0, 'records': ...}
emu.ttd_set_history_limit(frames=3000)   # -> (frames, bytes) in force; keep the newest 3000 frames
emu.ttd_set_history_limit(bytes=4 << 30) # ... or 4 GB of checkpoint data; None keeps a value, 0 = no limit
emu.ttd_get_journal_enabled()            # -> bool
emu.ttd_stop()                   # stop recording, keep history
emu.ttd_invalidate()             # drop all history (reason defaults to 'python invalidate')
emu.ttd_invalidate(reason='manual')
```

The write journal answers "who wrote this address last" at once; without it the search replays one frame (same answer, slower). It is off by default, can be switched at any moment and built later for any span by replay (about 2-4 ms per frame) - see [command-interface.md → The write journal](./command-interface.md#ttd-session-rules). This matches the CLI (`ttd start --journal`, `ttd journal on|off|build`), the WebAPI (`{"journal": true}`, `/ttd/journal`, `/ttd/journal/build`) and Lua (`ttd_start(true)`, `ttd_build_journal`).

**Status:**

```python
status = emu.ttd_status()
# {
#   'ttd_available': True,
#   'state': 'idle',                  # idle | recording | detached
#
#   # Provenance — recorded here, or opened from a file?
#   'loaded_from_file': True,
#   'source_path': '/sessions/bug-1274.ttd',
#   'captured_at_unix_ms': 1755712345678,   # 0 for a live recording
#
#   # Machine the session belongs to
#   'model_id': 0,
#   'model_ram_pages': 8,             # BOUND, not a count (48K reports 6)
#   'machine': {'model': 'PENTAGON', 'model_id': 1, 'ram_page_bound': 8, 'rom_signature': '0x...',
#               'peripheral_mask': ..., 'peripherals': ['betadisk', ...], 'not_recorded': [] or ['gs-lw'],
#               'general_sound': 'none'|'z80'|'lw'|'ngs', 'turbo_sound': 'none'|'turbosound'|'tsfm'},
#                                     # the recorded machine; None while there is no session
#   'recorded_by': None,              # the instance that recorded a loaded file
#
#   # Timeline
#   'session_start_frame': 98,
#   'current_end_frame': 397,
#   'checkpoint_count': 301,
#
#   # Sections
#   'write_journal_enabled': True,
#   'write_journal_complete': True,   # one span over the whole session
#   'write_journal_segments': [{'from_frame': 98, 'from_tinframe': 4, 'to_frame': 397, 'to_tinframe': 11}],
#                                     # the spans it covers; outside them a write search replays one frame
#   'bookmark_count': 2,
#   'write_journal_records': 729025,
#   'write_journal_bytes': 8748300,   # in memory; on disk it is compressed
#   'coverage_index_frames': 300,     # 0 => reverse queries fall back to replay
#   'coverage_index_bytes': 13926,
#   'input_event_count': 10,
#   'external_event_count': 0,
#   'input_history_complete': True,
#   'port_journal_active': True,      # replay needs no media or host device
#   'port_journal_off_reason': None,  # e.g. 'NeoGS: ...' when off
#   'port_read_count': 8225,
#   'port_write_count': 15520,
#   'port_journal_bytes': 9234,
#   'port_replay_value_mismatches': 0,  # > 0: a device answered otherwise on replay
#   'port_replay_divergences': 0,     # > 0: execution left the recording
#
#   # Memory
#   'page_store_bytes': 40960,
#   'page_store_used_bytes': 665600,
#   'baseline_frames_captured': 2159,
#   'session_heap_bytes': 1043968,
#
#   'last_drop_reason': 'snapshot-load',   # None until a history is dropped
#   'unavailable_reason': None,            # set when this machine has no time travel (a ZX-Poly member)
# }
```

`source_path` is filled by `ttd_load` (the path it was given).

`loaded_from_file` is the field to check first when a session is handed to you:
a loaded recording and a live one are otherwise indistinguishable from the
counters. `coverage_index_frames == 0` means reverse search and reverse
breakpoints will replay frames instead of consulting the index — correct, but
orders of magnitude slower.

**Navigation:**

```python
emu.ttd_seek(4823)                          # frame 4823, tinframe 0
emu.ttd_seek(frame=4823, tinframe=14982)    # (frame, tinframe)
# -> {'reached': True,
#     'arrived_at': {'frame': 4823, 'tinframe': 14982},
#     'halt_reason': 'target',              # 'target' | 'external_event' | 'out_of_range'
#     'blocking_marker': {...}}             # only for external_event: frame, tinframe, kind, reason

emu.ttd_step_back()                         # -> bool; one frame back (same position inside the frame)
emu.ttd_step_forward()                      # -> bool; one frame forward, inside recorded history
emu.ttd_step_instruction_back()             # -> bool
emu.ttd_step_instruction_forward()          # -> bool

emu.ttd_resume()                            # -> bool; record again from the exact current position (frame and tinframe)
emu.ttd_resume(frame=4823, tinframe=0)      # ...or from a given point; the future is discarded
                                            # fails from idle: seek first

emu.ttd_position()
# -> {'current': {'frame': ..., 'tinframe': ...}, 'session_end': {'frame': ..., 'tinframe': ...}}
```

**Reverse execution:**

```python
emu.ttd_reverse_step()                      # -> bool; one instruction back
emu.ttd_reverse_step(count=10)              # ten instructions back
emu.ttd_reverse_step_tstates(tstates=5000)  # back 5000 t-states (nearest instruction start)

hit = emu.ttd_reverse_continue([0x8000, 0x8010])
# -> {'matched': True, 'pc': 0x8000, 'frame': 4700, 'tinframe': 812}
#    A replay barrier met on the way adds
#    'blocked_by_marker': {'kind': ..., 'reason': ..., 'frame': ..., 'tinframe': ...}
#    (with 'matched': False when the barrier stopped the search before a match).
#    None only when nothing matched and no marker stopped the search.
#    Dicts carry 'covered_from', 'covered_from_tinframe', 'covered_to',
#    'covered_to_tinframe': the searched span (command-interface.md -> Search window).
```

**Reverse search:**

```python
result = emu.ttd_find_last(addr=0x5800, access='write')
# None if no match. On a hit:
# {
#   'found': True,
#   'frame': 4823,
#   'tinframe': 14982,
#   'pc': 0x4A21,
#   'value': 0x07,
#   'phys_page': 5,          # None for ROM / no RAM page
#   'access': 'write',
#   'covered_from': 4823, 'covered_from_tinframe': 14982,  # the searched span:
#   'covered_to': 4900, 'covered_to_tinframe': 0            # it ended at the hit
# }

# Full filter set (single address or address/PC range search):
result = emu.ttd_find_last(
    addr=None,                 # single address, or use addr_from/addr_to
    addr_from=0x4000,
    addr_to=0x8000,
    access='write',            # 'write' (default) | 'read' | 'execute' | 'io'
    value=0x07,                # exact byte value
    pc_from=0x4000,            # PC range filter
    pc_to=0x8000,
    before_frame=4823,         # search at or before this point (default: current position)
    before_tin=0,
    phys_page=5                # 0..255: one physical RAM page (ValueError otherwise)
)

# A replay barrier stopped the search before any match:
# {'found': False, 'blocked': True, 'marker_frame': 4700, 'marker_tinframe': 0,
#  'marker_kind': 'debugger_edit', 'marker_reason': 'Python memory write',
#  'covered_from': 4700, 'covered_from_tinframe': 0, 'covered_to': 4900, 'covered_to_tinframe': 0}
#  The search covered frame 4700..4900 only: nothing before the marker was examined.
```

**When did the program ... (port events):** `ttd_port_events(event, arg=None, **options)` searches the port journals - every IN and OUT with its time and PC - without replay. Events, arguments and options: [command-interface.md → Port events](./command-interface.md).

Output below is from the recorded fixture `testdata/ttd/port-journals/dizzyx.ttd` (Dizzy X):

```python
r = emu.ttd_port_events('key', 'space')        # when the game saw SPACE pressed
# {'ok': True, 'direction': 'in', 'count': 1, 'truncated': False, 'scanned': 7905,
#  'hits': [{'index': 6082, 'frame': 430, 'tinframe': 7824, 'port': 0x7FFE, 'value': 0xFE, 'pc': 0x72B2}]}

w = emu.ttd_port_events('ay-write', 7, **{'from': 200, 'to': '260:0', 'limit': 100})
# 60 hits, one a frame: the mixer written from PC 0xC176, 'ay_register': 7

f = emu.ttd_port_events('key', '5', file='testdata/ttd/port-journals/dizzyx.ttd')
# a saved session, searched without loading it

edges = emu.ttd_port_events('ear', limit=100000)['hits']        # every tape edge the loader saw
if not r['ok']:
    print(r['error'])     # no journals, recording running, bad option
```

`from` is a Python keyword: pass it through a dict (`**{'from': 200}`).

For writes the write journal answers when it holds every write of the session; otherwise the search replays history (see [command-interface.md → TTD Session Rules](./command-interface.md#ttd-session-rules), "When the write journal answers").

**Markers and bookmarks:**

```python
for m in emu.ttd_markers():                 # replay barriers
    print(m['frame'], m['tinframe'], m['kind'], m['reason'])
# kind: tape_control | disk_write | debugger_edit | other
# (hardware_reset is a reserved kind, never written: a reset stops the recording instead)

emu.ttd_bookmark_add('before crash')                        # at the current position
emu.ttd_bookmark_add('umt entry', frame=4823, tinframe=14982)
# -> {'added': True, 'label': ..., 'frame': ..., 'tinframe': ...}
#    or {'added': False, 'error': '...'}
for bm in emu.ttd_bookmarks():              # time-sorted
    print(bm['frame'], bm['tinframe'], bm['label'])
emu.ttd_bookmark_delete('before crash')     # -> bool
emu.ttd_seek_bookmark('umt entry')          # same dict as ttd_seek (blocking_marker included) plus 'bookmark'
                                            # ('error' for an unknown label)
```

Bookmarks are advisory and never stop a seek; markers do.

**Sessions on disk:**

```python
emu.ttd_dump('/tmp/session.ttd')            # -> bool
emu.ttd_load('/tmp/session.ttd')
# -> {'ok': True, 'checkpoint_count': ..., 'session_start_frame': ..., 'current_end_frame': ...}
#    or {'ok': False, 'error': '...'}  (e.g. recorded on a different model: both model ids named)
# After a load the session is idle: use ttd_seek to position the emulator.

emu.ttd_file_info('/tmp/session.ttd')
# A .ttd file read without loading it (headers only):
# -> {'ok': True, 'path', 'file_bytes', 'schema_version', 'flags', 'captured_at_unix_ms', 'recorded_by',
#     'session_state', 'session_start_frame', 'session_end_frame', 'checkpoint_count', 'page_slot_count',
#     'sections': {'write_journal': ..., 'port_journals': ..., ...},
#     'machine': {'model': ..., 'general_sound': ..., 'turbo_sound': ..., 'peripherals': [...], ...},
#     'peripherals_from_header': ...}
#    or {'ok': False, 'path': ..., 'error': '...'}
# Provision the machine it needs first: its model, its General Sound card (machine['general_sound']).
```

**Coverage index queries:**

```python
probe = emu.ttd_coverage_probe(frame=100, kind='executed', addr_from=0x0038, addr_to=0x0040)
# {'frame': 100, 'kind': 'executed', 'touched': True, 'index_available': True}
# Frames outside the covered window: index_available=False, touched=False

scan = emu.ttd_coverage_scan(kind='executed', addr_from=0x0038, addr_to=0x0040,
                             from_frame=1, to_frame=200, limit=200)
# {'kind': 'executed', 'frames': [18, 19, 20], 'first_match': 18, 'last_match': 20,
#  'matching_frames': 3, 'scanned_frames': 183, 'truncated': False,
#  'covered_from': 18, 'covered_to': 197, 'index_available': True}

summary = emu.ttd_coverage_summary(from_frame=1, to_frame=500, kind=None, bucket_size=50, limit=100)
# {'from_frame': 1, 'to_frame': 500, 'covered_from': 18, 'covered_to': 497,
#  'bucket_size': 50, 'bucket_count': 10, 'index_available': True,
#  'buckets': [{'frame_start': 1, 'frame_end': 50, 'executed_distinct': 412,
#               'written_distinct': ..., 'read_distinct': ..., 'has_keyframe': True}, ...]}
```

`kind` is `'executed'`, `'written'` or `'read'` (an unknown kind falls back to `'executed'`; `summary` with `kind=None` counts all three). `to_frame=None` means the session end.

### Analysis, Capture & Assembly

> **Status**: ✅ Implemented (2026-09)

Analysis, capture and assembly methods mirroring the WebAPI endpoints of the
same names (see [command-interface.md](./command-interface.md)). All methods
return a dict; on failure the dict carries an `error` string.

```python
# Stepping helpers
emu.step_out()                       # run until the current subroutine returns
emu.skip_until(0x8000)               # fast-forward until PC == target (breakpoints skipped)
emu.skip_until("0x8000", max_tstates=70000000)  # optional explicit t-state budget

# Memory search
emu.mem_find("AF 3C")                # hex pattern as string (spaces optional)
emu.mem_find(0xAF3C)                 # or as a number
emu.mem_find("AF 3C", start=0x8000, end=0xFFFF, alignment=2, max=32)
emu.mem_find("CD ?? 00")             # ?? = any byte, "A?" = any low nibble

# One coherent debugger snapshot (the same as GET /debug/snapshot): read at one moment, paused or between frames
snap = emu.debug_snapshot(disasm=21, stack=8, memory=["cpu:0x8000:256", "ram5:0:6912"])
# snap["seq"], snap["consistency"], snap["regs"]["special"]["pc"], snap["prev_regs"], snap["disasm"][0]["mnemonic"],
# snap["memory"][0]["bytes"] (bytes); ValueError with the reason when refused
emu.mem_find("C3", space="ram")      # every RAM page: matches as page {kind, page} + offset
emu.mem_find("C3 00 80", space="ram5", end=0x3FFF)   # one page (offsets), also "rom2", "cache0"
emu.mem_find("21 00 40", mask="FF FF F0")             # 1 bits must match
# Result: {space, count, truncated, matches: [{address | page, offset, context_start, context}]};
# context = 4 bytes before, the match, 4 after; {"error": "..."} when refused

# Screen state
emu.screen_digest()                  # digest screen area (0x4000-0x5AFF), border folded in
emu.screen_digest(0x4000, 0x5AFF, include_border=False)  # explicit range
emu.screen_digest(mode="active")     # hash the surface the video mode displays now (ATM modes
                                     # follow the 7FFD bit-plane pair); result carries
                                     # 'active_surface': {'video_mode': str, 'pages': [..]}
emu.ports_map()                      # static port map + live routing flags (P1-5 + P1-2 tags):
                                     # {'model': str, 'entries': [{'port','mask','match','device','gate',
                                     #                             'tags': ['memory','rom',...], 'latch',
                                     #                             'latch_value', 'latch_fields'}],  # e.g. pFE: border, mic, ear
                                     #  'live': {'trdos_active','mouse_ports_decoded',
                                     #           'mouse_routing_note','shadow_monitor_paged'}}
                                     # tags: semantic categories (keyboard/memory/rom/screen/storage/
                                     #   mouse/joystick/system + sound members sound_ay/sound_covox/...);
                                     #   a row can carry several (Pentagon #FB = covox AND sounddrive).
                                     # latch: live-value binding name ('p7FFD', 'p1FFD', 'pDFFD', ...) when
                                     #   the row is a paging latch; None otherwise.
emu.paging_state()                   # tagged paging latches + bank table (P1-2):
                                     # {'model','paging_locked','trdos_active',
                                     #  'latches': [{'port','latch','tags','device','gate','value',
                                     #               'decoded': {'ram_bank':..,'shadow_screen':..,...}}],
                                     #  'banks': [{'bank','address_range','type','page',
                                     #             'name','role','signature','contended',
                                     #             'writable'}]}  # writable: a CPU write reaches the page
                                     # ROM bank rows carry the §5.2 identification: name = recognized
                                     # content (SHA-256 catalog), role = the model's layout slot; a
                                     # role/name mismatch is the one-glance wrong-ROM signal.
emu.beam_position()                  # { "frame": N, "line": N, "tstate": N, "zone": "...", "layers": [{id, x, x_end, y}] }
emu.video_layout()                   # layers (surface, beam window, dots_per_t), framebuffer placement, family
emu.video_pixel(x, y, layer=0)       # sources (space, page, offset, bit_mask, role, z80), colour_index, rgb, rendered_rgb
emu.video_pixel_at(t)                # the same for the point under the beam at frame T (layer pixel or border)
emu.video_address(page, offset)      # areas a RAM byte feeds; emu.video_address_z80(addr) through current paging
emu.video_address_in(space, offset, page=0)  # "ram", "sprite_ram" (attribute word, byte offset) or "palette" (cell, byte offset)
emu.video_text(layer=0)              # exact text grid of ATM / ZX-Evo text modes (lines: text, codes, attrs)
emu.video_temporal()                 # ZX DLSS de-flicker status: {'algorithm' ('' = off), 'active', 'inactive_reason', 'applicable',
                                     #   'video_delay_frames', 'video_delay_ms', 'audio_extra_delay_frames', 'processed',
                                     #   'correcting', 'showing_processed', 'corrected_frames', 'written', 'shown_raw', 'late',
                                     #   'restarts', 'last_ms', 'average_ms', 'shown_frame' / 'last_frame': {'pattern',
                                     #   'period2'..'period5', 'field', 'field_stage', 'whole_paper', 'scene_average'},
                                     #   'algorithms': [...],
                                     #   'default_algorithm': 'mod-tpgwafsd'}
emu.video_temporal_set('mod-tpgwafsd') # switch it on; 'off' or '' switches it off. Returns the new status,
                                     #   or {'ok': False, 'error': '...'} for an unknown name. While on, the
                                     #   picture is shown later by the algorithm's look-ahead (7 frames for
                                     #   mod-tpgwafsd with the default A/V delay of 2) and the sound is delayed
                                     #   by the difference ('audio_extra_delay_frames', 5 here) to stay in sync.
emu.frame_cost()                     # per-frame halt/run cost accounting

# Coverage analyzer
emu.coverage_start()                 # start clean; keep=True retains old data
emu.coverage_stop()
emu.coverage_status(max_ranges=100)  # executed count + first ranges
emu.coverage_gaps()                  # executed ranges and gaps (start=0x4000, end=0xFFFF)

# AY register log
emu.ay_log_start()                   # default capacity; capacity=8192 to override
emu.ay_log_stop()
emu.ay_log_status()
emu.ay_log_dump(count=32)            # last N entries (offset=... for pagination)

# Audio capture
emu.audio_capture_start(2.5)         # capture 2.5 s of stereo audio (default 1.0)
emu.audio_capture_status()
emu.audio_capture_result()           # sample stats + per-channel peak/RMS
emu.audio_capture_result("out.wav")  # additionally export a 16-bit WAV file

# Core audio rate (same switch as CLI 'setting audio_rate' and WebAPI
# PUT settings/audio_rate): pin 44100..192000 for this run - never persisted.
# 0 = auto: follow the resolution chain device rate > [SOUND] CoreRate >
# 44100. Applied at the next frame boundary; deferred while a recording is
# in progress.
emu.set_audio_rate(96000)            # True when the pin took effect (False = unsupported rate)
emu.set_audio_rate(0)                # release the pin (auto)
emu.get_audio_rate()                 # {'pin': 96000, 'core_rate': 96000, 'target_rate': 96000}

# Video recording (requires a build with ENABLE_RECORDING)
emu.video_record("start", {"format": "gif", "fps": 50, "scale": 2})  # opts dict optional
emu.video_record("start", {"format": "h264", "filename": "run.mp4", "audio": "aac"})  # with sound
                                     # (True = aac; video_bitrate / audio_bitrate in kbps). No
                                     # "audio" = video only; gif + audio is refused
emu.video_record("stop")             # also "pause" / "resume"
emu.video_record_status()             # recording state + live stats (frames, duration, fps,
                                     # audio, audio_codec, audio_sample_rate, audio_duration)
# video_record has no audio-rate option - for a fixed-rate recording pin the
# rate first: emu.set_audio_rate(48000) before video_record("start", ...)

# Assembler
emu.assemble("ld a,2\nout (254),a", 0x8000)              # assemble, listing only
emu.assemble("ld a,2\nout (254),a", "0x8000", write=True)  # + write bytes to RAM

# Label resolution
emu.label_resolve("main_loop")       # by name
emu.label_resolve(0x8100)            # by address: exact, aliases, nearest below/above

# Source listings
emu.listing_load("game.lst")
emu.listing_source_at()              # source line at PC; emu.listing_source_at(0x8100)
emu.listing_step_line()              # run until the source line changes (~2 s budget)
emu.listing_run_to_line(120)         # run to first code byte of line 120 (~10 s budget)
```

### Enumerations

```python
class EmulatorState(Enum):
    Unknown = 0
    Initialized = 1
    Run = 2
    Paused = 3
    Resumed = 4
    Stopped = 5

class LogLevel(Enum):
    Trace = 0
    Debug = 1
    Info = 2
    Warning = 3
    Error = 4
    Fatal = 5
```

## Usage Examples

### Basic Emulator Control

```python
import unreal_emulator as ue

# Create and initialize
emu = ue.Emulator("test_instance")
emu.init()

# Reset and run
emu.reset()
emu.resume()

# Pause after 1 second
import time
time.sleep(1)
emu.pause()

# Inspect state
cpu = emu.get_cpu()
print(f"PC: 0x{cpu.get_pc():04X}")
print(f"AF: 0x{cpu.get_af():04X}")
print(f"Flags: {cpu.get_flags()}")

# Cleanup
emu.release()
```

### Memory Analysis

```python
import unreal_emulator as ue
import numpy as np

emu = ue.Emulator()
emu.init()

mem = emu.get_memory()

# Read screen memory as NumPy array (zero-copy!)
screen_data = np.frombuffer(
    mem.read_bytes(0x4000, 6912),
    dtype=np.uint8
)

# Analyze pixel distribution
print(f"Mean: {screen_data.mean():.2f}")
print(f"Std: {screen_data.std():.2f}")
print(f"Unique values: {len(np.unique(screen_data))}")

# Find all non-zero pixels
pixels = screen_data[:6144].reshape(192, 256//8)
print(f"Active pixels: {np.count_nonzero(pixels)}")
```

### Automated Testing

```python
import unreal_emulator as ue

def test_basic_boot():
    """Test emulator boots correctly"""
    emu = ue.Emulator()
    assert emu.init(), "Failed to initialize"
    
    cpu = emu.get_cpu()
    assert cpu.get_pc() == 0x0000, "PC not at reset vector"
    
    # Execute boot code
    emu.steps(1000)
    
    # Check we're past ROM initialization
    assert cpu.get_pc() > 0x1000, "Boot sequence didn't progress"
    
    emu.release()

def test_memory_write():
    """Test memory write operations"""
    emu = ue.Emulator()
    emu.init()
    
    mem = emu.get_memory()
    
    # Write test pattern
    mem.write(0x8000, 0x42)
    assert mem.read(0x8000) == 0x42
    
    # Write word
    mem.write_word(0x8001, 0x1234)
    assert mem.read_word(0x8001) == 0x1234
    
    emu.release()

if __name__ == "__main__":
    test_basic_boot()
    test_memory_write()
    print("All tests passed!")
```

### Breakpoint-Based Automation

```python
import unreal_emulator as ue

emu = ue.Emulator()
emu.init()

bp_mgr = emu.get_breakpoint_manager()

# Set breakpoint at ROM print routine
bp_id = bp_mgr.add_execution_breakpoint(0x0010)  # RST 10h

# Run until breakpoint
emu.resume()

# Wait for breakpoint hit (simplified)
while emu.get_state() != ue.EmulatorState.Paused:
    time.sleep(0.01)

cpu = emu.get_cpu()
print(f"Breakpoint hit at PC=0x{cpu.get_pc():04X}")

# Inspect character being printed (in A register)
char_code = cpu.get_af() >> 8
print(f"Character: {chr(char_code)}")

# Continue
bp_mgr.remove_breakpoint(bp_id)
emu.resume()
```

### Machine Learning Integration

```python
import unreal_emulator as ue
import numpy as np
import torch

class EmulatorEnvironment:
    """OpenAI Gym-style environment for RL"""
    
    def __init__(self):
        self.emu = ue.Emulator()
        self.emu.init()
        self.mem = self.emu.get_memory()
        
    def reset(self):
        self.emu.reset()
        return self.get_state()
        
    def step(self, action):
        # Execute action (e.g., key press)
        self.execute_action(action)
        
        # Run for one frame
        self.emu.steps(69888)  # One frame @ 3.5 MHz
        
        # Get new state and reward
        state = self.get_state()
        reward = self.compute_reward()
        done = self.is_done()
        
        return state, reward, done, {}
        
    def get_state(self):
        """Extract screen as state"""
        screen = np.frombuffer(
            self.mem.read_bytes(0x4000, 6144),
            dtype=np.uint8
        )
        return screen.reshape(192, 32)
        
    def compute_reward(self):
        # Game-specific reward logic
        score_addr = 0x5C00  # Example
        return self.mem.read_word(score_addr)
        
    def execute_action(self, action):
        # Simulate key press
        pass

# Train agent
env = EmulatorEnvironment()
state = env.reset()

for episode in range(1000):
    action = select_action(state)
    state, reward, done, _ = env.step(action)
    # Train neural network...
```

### Jupyter Notebook Integration

```python
# Cell 1: Setup
import unreal_emulator as ue
import matplotlib.pyplot as plt
import numpy as np

emu = ue.Emulator()
emu.init()
mem = emu.get_memory()

# Cell 2: Run emulation
emu.reset()
emu.steps(100000)

# Cell 3: Visualize screen
screen_data = np.frombuffer(mem.read_bytes(0x4000, 6144), dtype=np.uint8)
screen = decode_zx_screen(screen_data)  # Custom function
plt.imshow(screen, cmap='gray')
plt.title('ZX Spectrum Screen')
plt.show()

# Cell 4: Register dump
cpu = emu.get_cpu()
regs = {
    'AF': f"0x{cpu.get_af():04X}",
    'BC': f"0x{cpu.get_bc():04X}",
    'DE': f"0x{cpu.get_de():04X}",
    'HL': f"0x{cpu.get_hl():04X}",
    'PC': f"0x{cpu.get_pc():04X}",
}
print(regs)
```

## Performance Characteristics

- **Function Call Overhead**: ~50-100ns per call (negligible)
- **Memory Access**: Zero-copy via `memoryview` (native speed)
- **Array Operations**: NumPy integration (native C speed)
- **Python Interpreter**: GIL released during long-running C++ operations

## Threading and Concurrency

### Global Interpreter Lock (GIL)
- Python's GIL limits true parallelism
- Long C++ operations release GIL automatically
- Use multiprocessing for true parallelism:

```python
from multiprocessing import Process

def run_emulator(instance_id):
    emu = ue.Emulator(instance_id)
    emu.init()
    # Run independently
    
# Create multiple processes
processes = [Process(target=run_emulator, args=(f"emu_{i}",)) 
             for i in range(4)]
for p in processes:
    p.start()
```

### Thread Safety
- Emulator instances are NOT thread-safe
- Use one emulator per thread
- Synchronize access with locks if sharing

```python
import threading

lock = threading.Lock()

def safe_read_memory(emu, addr):
    with lock:
        return emu.get_memory().read(addr)
```

## Exception Handling

Python exceptions are propagated from C++ exceptions:

```python
try:
    emu = ue.Emulator()
    emu.init()
    
    mem = emu.get_memory()
    mem.write(0xFFFFFF, 0x42)  # Invalid address
    
except ue.EmulatorError as e:
    print(f"Emulator error: {e}")
    
except Exception as e:
    print(f"Unexpected error: {e}")
    
finally:
    emu.release()
```

## Type Hints and IDE Support

```python
from typing import Optional
import unreal_emulator as ue

def analyze_code(emu: ue.Emulator, start: int, end: int) -> list[int]:
    """Analyze code region"""
    mem: ue.Memory = emu.get_memory()
    cpu: ue.Z80 = emu.get_cpu()
    
    # IDE provides autocompletion and type checking
    data: bytes = mem.read_bytes(start, end - start)
    return list(data)
```

## Debugging Python Scripts

### Using pdb
```python
import pdb
import unreal_emulator as ue

emu = ue.Emulator()
emu.init()

pdb.set_trace()  # Breakpoint here

cpu = emu.get_cpu()
print(cpu.get_pc())
```

### Using VS Code
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Python: Emulator Script",
            "type": "python",
            "request": "launch",
            "program": "${file}",
            "console": "integratedTerminal"
        }
    ]
}
```

## Installation & Distribution

### Building Python Module
```bash
cd core/automation/python
mkdir build && cd build
cmake .. -DENABLE_PYTHON_AUTOMATION=ON
make
```

### Installing Module
```bash
pip install ./dist/unreal_emulator-1.0-cp310-cp310-linux_x86_64.whl
```

### Requirements
- Python 3.10+
- NumPy (optional, for array operations)
- Matplotlib (optional, for visualization)

## See Also

### Interface Documentation
- **[Command Interface Overview](./command-interface.md)** - Core command reference and architecture
- **[CLI Interface](./cli-interface.md)** - TCP-based text protocol for interactive debugging
- **[WebAPI Interface](./webapi-interface.md)** - HTTP/REST API for web integration
- **[Lua Bindings](./lua-interface.md)** - Lightweight scripting and embedded logic

### Advanced Interfaces (Future)
- **[GDB Protocol](./gdb-protocol.md)** - Professional debugging with standard GDB/LLDB clients
- **[Universal Debug Bridge](./udb-protocol.md)** - High-performance analysis and profiling

### Navigation
- **[Interface Documentation Index](./README.md)** - Overview of all control interfaces

### External Resources
- **[pybind11 Documentation](https://pybind11.readthedocs.io/)** - C++/Python binding library
