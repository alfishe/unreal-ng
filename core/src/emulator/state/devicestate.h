#pragma once

#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;
struct ScreenDigestQuery;
namespace SprinterBios
{
struct Options;
}

/// @file devicestate.h
/// @brief Device state reports for analysis, built once in the core and
/// rendered by every automation interface (WebAPI, Python, Lua, CLI, MCP).
///
/// Each builder returns a StateNode object. When the device is not present
/// the object carries `available: false` and a `description` instead of
/// failing, so interfaces can always return something structured.
///
/// Reports:
/// - AY / SSG: `Ay()` overview of every AY chip (the TSFM's SSG halves
///   included), `AyChip(i)` full register decode of one chip. Shape matches
///   the historical WebAPI `state/audio/ay` responses.
/// - FM (TSFM, 2 x YM2203): `Fm()` board latches + per-chip summary,
///   `FmChip(i)` the full FM half: mode register, timers, busy, and every
///   channel/operator with its registers decoded, the live envelope state
///   and attenuation from ymfm, key-on mask, pitch in Hz, and the last DAC
///   word.
/// - FDC: `Fdc()` reports the machine's disk controller. Beta Disk WD1793:
///   registers, decoded status bits, last command, FSM state, Beta128 system
///   register, DRQ/INTRQ, and all four drives (inserted image, track, side,
///   motor, write protect, geometry). +3 uPD765A (`controller` says which):
///   phase, main status register, the command in hand with its C H R N,
///   result bytes, ST0-ST2 decoded, SPECIFY times, the four units' cylinder
///   and seek state, and drives A and B.
namespace DeviceState
{
StateNode Ay(EmulatorContext* context);
StateNode AyChip(EmulatorContext* context, int chip);
StateNode Fm(EmulatorContext* context);
StateNode FmChip(EmulatorContext* context, int chip);
StateNode Fdc(EmulatorContext* context);

/// General Sound (classic GS, lightweight, NeoGS): `Gs()` device and firmware,
/// mailbox (status, pending flags, queue counts, the three latches), MPAG page,
/// every DAC channel's sample and volume, the card CPU (PC/SP/AF/halted, or
/// `coprocessor: false` on the lightweight card) and, on NeoGS, a `neogs`
/// block: stereo mode, flash, GSCFG0 (raw and decoded), clock, windows,
/// interrupts, SD card, MP3 decoder and the DMA engines incl. ZX-DMA.
/// `ramWindow` adds a hex dump of the card CPU's #4000-#7FFF window (peeked,
/// no side effects) where GS-compatible firmwares keep their variables.
StateNode Gs(EmulatorContext* context, bool ramWindow = false);

/// Covox / SoundDrive: `Covox()` fitment (mono #FB or the quad SoundDrive),
/// the ports this model's decoder routes to it (from its port map), the ports
/// it shares with the Beta-128 interface and who wins them, the four DAC
/// latches with their mute state, the last output amplitude per side and
/// whether the DAC was written last frame.
StateNode Covox(EmulatorContext* context);

/// IDE board: `Ide()` the scheme and its gate, the adapter latches, the
/// selected unit and INTRQ, and per unit: kind (hard disk / CD-ROM), slot,
/// medium (source, sectors, geometry, write protect), the task file with the
/// status / error / device control bits decoded, the command in progress
/// with its transfer position, the CHS translation, and on a CD drive the
/// disc, the byte count limit, unit attention and the sense (key / ASC / ASCQ)
StateNode Ide(EmulatorContext* context);

/// CMOS clock (MC146818 / DS12887; the ZX-Evo AVR's emulation of one):
/// `Rtc()` the part, the ports the machine wires it to, the NVRAM file, the
/// address latch, the time base (host / emulated / fixed), the time as the
/// guest reads it, registers A-D and the alarms decoded, and every cell as a
/// hex dump. Peeked: reading never clears register C. Unavailable, with the
/// reason, when the machine has no clock the guest can reach
StateNode Rtc(EmulatorContext* context);

/// The ZX Profi's board peripherals (v3 and v5; the PROFI-PLUS variant too): `ProfiPeripherals()` the board, which
/// port map answers now (the `[PROFI] ExtPorts` rule, the DOS latch, CP/M, ROM14, hi-res), the 8255 (mode word, the
/// direction and value of A, B and the two halves of C), the 8253 baud timer (per counter: mode, count, OUT, GATE;
/// v5 only) and the 8251 USART (mode, command, status flags, baud, byte counters) with the `#B3` interrupt-enable
/// latch. Peeked: reading changes nothing. Unavailable on a machine that is not a Profi
StateNode ProfiPeripherals(EmulatorContext* context);

/// Expansion slots of the ISA kind (the Sprinter's two ISA-8 slots, Sprinter ISA tdd §10): the #9FBD latch
/// (A19-A14, AEN, RESET), what window 3 shows now (mapped, slot, space, page), and per slot the configured
/// and fitted card, why a configured card is not fitted, the card's own report and cycle counters.
/// Unavailable ("no ISA slots on this machine") elsewhere. Built in emulator/io/sprinter/isa/isaaccess.cpp
StateNode Isa(EmulatorContext* context);
/// The ISA access journal (`IsaJournal()`, /state/isa/journal): the last `last` card accesses and bus events, oldest
/// first - frame, base T, PC, slot, io / memory, read / write, ISA address, the CPU address, value, the card
/// register ("ISR", "data port") or the event (RESET DRV, a stall). Recorded live and while a TTD recording replays
StateNode IsaJournal(EmulatorContext* context, unsigned last);

/// ZX-bus slots (docs/inprogress/2026-10-03-zx-bus-slots, R-REP-1): `Slots()` the machine's declaration (board,
/// buses with their kind, arbitration, physical slots and whether the bus is retrofitted), where the cards came from
/// ([SLOTS] or the translated legacy keys), per configured slot the card, its options, adapter, fit (real / adapter /
/// unrealistic), state (active / disabled with the reason), functions and port claims (IORQGE flag, gate) and what the
/// plan said, and per built-in device its state (active / switched off / shadowed by a slot / replaced in its
/// socket). The core report every automation surface renders (surfaces arrive with SL-7). Built in
/// emulator/slots/slotreport.cpp
StateNode Slots(EmulatorContext* context);

/// Network adapters (network adapters TDD §9): `Network()` the fitted card
/// (ZXNETUSB: its ports, the W5300 held in reset or running, the chip's
/// address registers and per socket mode / state / ports / buffers), the
/// virtual network (addresses, DNS mode, hosts, forwarding, DHCP leases,
/// sockets with their remote end and byte counts, guest servers), counters
/// and the recent socket activity. A snapshot taken at the last frame
/// boundary. Unavailable, with the reason, when no adapter is fitted
StateNode Network(EmulatorContext* context);

/// TS-Conf machine state (TSConf technical-design §3.14): the memory map
/// (MEM_CONFIG decoded, the four windows, LCK128 / lock48, DOS / vdos, cache),
/// video (V_CONFIG decoded: mode, geometry, NOGFX / NOTSU / GFXOVR; V_PAGE,
/// PAL_SEL, BORDER, offsets, T_CONFIG and the TSU pages; the line the engine is
/// on with its latched set), interrupts (mask, pending sources, frame position),
/// DMA (busy, task, live addresses, counters), the CPU clock, the FM window and
/// the SD card. Unavailable on every other machine. Built beside the TS-Conf
/// platform code (tsconfdevicestate.cpp), so shared code names no TS-Conf type
StateNode TsConf(EmulatorContext* context);
/// The TS-Conf TSU and palette for debug views (TsConfTsu()): T_CONFIG, the tile
/// layers (enabled, graphics page, offsets, palette bits, tile-0 drawing), the
/// tilemap and sprite pages, all 85 sprite descriptors decoded (active, LEAP,
/// layer s0 / s1 / s2, position, size, flips, tile, bitmap position, palette,
/// raw words) and the 256 CRAM cells (value, rgb). Unavailable on other machines
StateNode TsConfTsu(EmulatorContext* context);

/// The screen digest (screendigest.h; /state/screen/digest on every interface): FNV-1a 64 over
/// the screen memory - RAM pages 5 / 7 by default, the pages the video mode displays (active),
/// given pages or a Z80 range, or, on a machine whose picture lives outside the RAM pages (the
/// Sprinter's video RAM), that surface in the default and active modes - with the border folded
/// in and the change against the previous poll (which this call records)
StateNode ScreenDigestReport(EmulatorContext* context, const ScreenDigestQuery& query);

/// The per-device mixer (audiomixer.h; /audio/mixer): master (muted, rate) and every device of
/// SoundManager's registry by source key (beeper, ay1, covox, gs, ...): name, muted, solo, audible,
/// volume and gain_db, the last frame's peak and activity, capturable (/audio/capture {source}).
/// `AudioChannels()` is /state/audio/channels: beeper (peak, active from the mixer), the AY tone
/// generators, GS and Covox subsets, master, and the mixer devices
StateNode AudioMixer(EmulatorContext* context);
StateNode AudioChannels(EmulatorContext* context);

/// The video change log (videowritelog.h; /video/changes): per frame the latches at its start,
/// every change of a video latch with its frame T, beam line / T in line, PC and the latches that
/// changed by name ("rgmod": "0x00 -> 0x01"), and the palette / mode table writes counted with
/// their first and last write. Every machine (the Sprinter adds RGMOD, HOLD, PORT_Y, ALL_MODE and
/// the frame height). `frames` 1 = the last completed frame, 2 = also the current one (paused)
StateNode VideoChanges(EmulatorContext* context, unsigned frames);

/// Device memory regions (emulator/memory/devicememory.h): memory a device owns outside the
/// CPU's pages, by name. `MemoryRegions()` lists them (name, description, size, page size,
/// writable, how a write reaches the device); `MemoryRegionRead()` reads [offset, offset +
/// length) as `format` "hex" (default: one string), "data" (an array) or "sparse" (fill runs
/// folded, as the page reads do). Unavailable (with the reason) for an unknown name or a range
/// outside the region; writes: DeviceMemory::Write
StateNode MemoryRegions(EmulatorContext* context);
StateNode MemoryRegionRead(EmulatorContext* context, const std::string& name, uint32_t offset, uint32_t length,
                           const std::string& format);

/// Peters Plus Sprinter Sp2000 (Sprinter tdd-integration §3; built beside the Sprinter code,
/// ports/models/sprinter/sprinterdevicestate.cpp):
/// - `Sprinter()`: the PLD configuration (state, active module, bitstream hashes, port decoder
///   opened), the decoder (CNF map, DOS, PN5, #7FFD / #1FFD after the clean rules), the four
///   windows (physical page and kind: ROM / fast RAM / vROM / RAM / graphics / ISA / port table),
///   registers (ROM_RG, ALL_MODE, PORT_Y, RGMOD, HOLD, SCALE) and the cells #C0-#FF, the clock
///   (turbo x6, the 21 MHz wait rule and the windows it applies to), the frame (320 / 312 lines),
///   a video summary of the mode table (square kinds, INT positions), the block accelerator
///   (mode, length, function, blocked, counters, buffer CRC), the sound devices, the Z84C15
///   (system registers, wait generator, daisy chain, watchdog, CTC, SIO with the keyboard FIFO,
///   PIO), the WD1793 density latch, links to the CMOS (`Rtc()`) and IDE, and the BIOS images.
/// - `SprinterPaging()`: the windows and the paging latches alone (the /state/paging view).
/// - `SprinterText()`: the screen text of the mode table's text squares (80 x 32).
/// - `SprinterPortTable(query)`: the decoded port table of RAM page #40 for one map / DOS / PN5
///   (unset = the machine's current ones) and direction: rows of code, name and address pattern.
/// - `SprinterPortLookup(port, query)`: one port: the index into page #40, the code and its name
///   (or the Z84C15 when the chip answers the port itself).
/// Unavailable on every other machine.
struct SprinterPortQuery
{
    int map = -1;        ///< 0-3, -1 = the current CNF map
    int dos = -1;        ///< 1 = TR-DOS on, 0 = off, -1 = current
    int pn5 = -1;        ///< #7FFD bit 5: 0 / 1, -1 = current
    int direction = -1;  ///< 1 = read (IN), 0 = write (OUT), -1 = both
};
StateNode Sprinter(EmulatorContext* context);
StateNode SprinterPaging(EmulatorContext* context);
/// The text of the picture's text squares (80 x 32: BIOS SETUP, DSS) from the mode table - the
/// Sprinter has no ZX screen to OCR; graphics squares read as spaces
StateNode SprinterText(EmulatorContext* context);
/// The mode table per square (`SprinterVideo()`, /state/sprinter/video): HOLD, frame length, RGMOD,
/// PORT_Y, ALL_MODE, counts per kind, a one-letter map per row (G 320, g 640, T text 40, t text 80, Z Spectrum cell,
/// B border, . blank, * blank + INT), the palettes the picture uses and (squares) every square
/// decoded: kind, mode bytes, palette / source column / row / low-res quarter, or the characters
struct SprinterVideoQuery
{
    int page = -1;         ///< mode table page 0 / 1, -1 = RGMOD's (the one displayed)
    bool all = false;      ///< the whole table (56 x 40) instead of the picture (40 x 32)
    bool squares = true;   ///< the per-square objects (false: the map and the counts only)
};
StateNode SprinterVideo(EmulatorContext* context, const SprinterVideoQuery& query);
/// page "0" / "1" (empty = RGMOD's), all / squares "0" / "1"; false with `error` on a bad value
bool SprinterVideoQueryFromStrings(const std::string& page, const std::string& all, const std::string& squares,
                                   SprinterVideoQuery& query, std::string& error);
/// The 8 palettes of 256 pens (`SprinterPalette()`, /state/sprinter/palette): R, G, B as video RAM
/// holds them, the pen's VRAM address, which palettes the picture uses. `palette` 0-7, or:
constexpr int kSprinterPalettesUsed = -1;  ///< the palettes the picture's squares use (the default)
constexpr int kSprinterPalettesAll = -2;   ///< all eight
StateNode SprinterPalette(EmulatorContext* context, int palette);
/// "0"-"7", "all", "used" / empty
bool SprinterPaletteFromString(const std::string& text, int& palette, std::string& error);
/// The BIOS images and start options (`SprinterBios()`, /state/sprinter/bios): the shipped images
/// (file, alias, version, CRC-32, present, loaded = the flash's CRC matches, selected = the config
/// names it), known_issues (of the loaded image; SprinterBios::KnownIssues), reload_pending, options fast_start / accel_int_suspend. `SprinterBiosSelect()` writes
/// the selection into this instance's configuration, the new image loads at the next reset, which
/// `options.reset` makes now (sprinterbios.h); unavailable with the reason on a bad name
StateNode SprinterBios(EmulatorContext* context);
/// The known issues of the loaded flash image (SprinterBios::KnownIssues; the same list as `known_issues` of
/// SprinterBios()); empty on other machines. Hashes the 256 KB flash: for a UI, call it now and then, not per frame
std::vector<std::string> SprinterBiosKnownIssues(EmulatorContext* context);
StateNode SprinterBiosSelect(EmulatorContext* context, const SprinterBios::Options& options);
/// The Covox-Blaster ring (`SprinterSoundRing()`, /state/sprinter/sound/ring): 256 words with the
/// play and write index marked
StateNode SprinterSoundRing(EmulatorContext* context);
StateNode SprinterPortTable(EmulatorContext* context, const SprinterPortQuery& query);
StateNode SprinterPortLookup(EmulatorContext* context, uint16_t port, const SprinterPortQuery& query);
/// The query from text parameters, the same on every interface: map "0"-"3", dos / pn5 "0" / "1"
/// (also on / off), rw "r" / "w" / "rw"; empty = current. False with `error` on a bad value
/// A port number as text: hex, with or without "#" / "0x" ("21BC", "#21BC", "0x21BC")
bool SprinterPortFromString(const std::string& text, uint16_t& port);
bool SprinterPortQueryFromStrings(const std::string& map, const std::string& dos, const std::string& pn5,
                                  const std::string& rw, SprinterPortQuery& query, std::string& error);

/// The ZX (Spectrum) mode report (`SprinterZxMode()`, /state/sprinter/zx-mode, also the `zx_mode` section of
/// `Sprinter()`; tdd-zx-mode.md §12). Everything is read from the hardware state, and the launcher's RAM
/// where it is found:
/// - `active`: window 0 shows a vROM page and ALL_MODE bit 0 = 0 (ZX screen shadow + ZX keyboard);
/// - `config`: each `.ZX` option as the PLD implements it (/turbo, /sprinter, /7FFD, /1FFD, /mem512,
///   /lines312, /origin, the INT position, /ret-fn | /ret-zx) with its evidence, the option line they make,
///   the best-matching known mode file (SP.ZX, P128.ZX, P512.ZX, SC256.ZX, ORIGIN.ZX, the Peters Plus
///   SPRINTER.ZX ...) with a confidence and the differences of the others;
/// - `launcher`: what SPECTRUM.EXE left in RAM (the `.ZX` text, its option table, the reset intercept, the
///   BIOS system page's CNF copy), the authoritative source when present, cross-checked with the hardware;
/// - `clock` (CNF request, the F12 switch, the MHz and why), `frame` (lines, T, the INT position and its
///   kind), `rom` (the vROM cells, each page's CRC-32 and the ROM it is), `paging`, and `ports`: the table
///   decode of #7FFD, #1FFD, #01FD, the #xxFD variants, #FE, #1F - code, device and what it does now, with
///   the TTD port-events port / mask that finds every spelling of the port in a recording.
/// `deep` = false skips the whole-RAM search for the launcher's option table (the GUI's status line)
StateNode SprinterZxMode(EmulatorContext* context, bool deep = true);
/// One line for a status bar ("Sprinter ZX (turbo req, 21 MHz, /1FFD)", "Pentagon 128 (3.5 MHz)") and the
/// report as text for its tooltip; `sprinter` false on other machines, `active` false outside the ZX mode
struct SprinterZxBrief
{
    bool sprinter = false;
    bool active = false;
    std::string text;
    std::string details;
};
/// `details` false: the line only (a GUI tick that keeps the last tooltip)
SprinterZxBrief SprinterZxModeBrief(EmulatorContext* context, bool details = true);

/// The PLD journal (`SprinterJournal()`, /state/sprinter/pld-journal; the Sprinter decoder's PldJournal):
/// events with seq, frame, T (base, and as line / T in line), PC, kind, port, value, text, details.
/// source "live" (default): the journal; "ttd": the OUTs of the TTD recording that reach the PLD's
/// configuration codes (CNF #C6/#CE, #1FFD #C0/#C8, #7FFD #C1/#C9, ALL_MODE #C3, RGMOD #C5/#CD, HOLD #CB,
/// frame #2C/#2D, reload #2E), found through the port table as it decodes now (the current map, DOS and
/// PN5), from the TTD write journal. Both list the TTD port-events queries for those codes
struct SprinterJournalQuery
{
    std::string kinds;     ///< "cnf,port_1ffd" (empty = all)
    uint64_t since = 0;    ///< events after this seq
    int64_t frameFrom = -1;
    int64_t frameTo = -1;
    size_t limit = 200;
    bool ttd = false;      ///< source=ttd
};
bool SprinterJournalQueryFromStrings(const std::string& kinds, const std::string& since, const std::string& from,
                                     const std::string& to, const std::string& limit, const std::string& source,
                                     SprinterJournalQuery& query, std::string& error);
StateNode SprinterJournal(EmulatorContext* context, const SprinterJournalQuery& query);
/// Switch the journal on / off (`enable` 1 / 0, -1 = keep) and / or clear it; replies with the journal's state
StateNode SprinterJournalControl(EmulatorContext* context, int enable, bool clear);

/// MoonSound (ZXM-MoonSound, YMF278B OPL4). A snapshot as of the chip's last
/// guest access or frame run - reading it never advances the chip.
/// - `MoonSound()`: NEW / NEW2, status, the guest address latches, the block
///   mix latches (FM #F8, PCM #F9) decoded, wave memory (ROM size and loaded
///   bytes, SRAM size, dirty pages), keyed FM channels and PCM slots.
/// - `MoonSoundFm()`: status, both timers, the 4-op connection register, and
///   all 18 channels (bank, F-number, block, frequency, key-on, feedback,
///   connection, output route, render peak), plus both register banks as hex.
/// - `MoonSoundPcm()`: the wave memory address register and all 24 slots
///   (wave number, octave, F-number, playback rate, key-on, total level,
///   pan, damp, sample width, start / loop / end, position, envelope phase,
///   attenuation and rates, LFO / vibrato / AM, render peak), plus the
///   register file as hex.
StateNode MoonSound(EmulatorContext* context);
StateNode MoonSoundFm(EmulatorContext* context);
StateNode MoonSoundPcm(EmulatorContext* context);

/// Screen reports (Screen::DescribeScreenState):
/// - `Screen(verbose)`: model, video mode, resolution, border, shadow screen,
///   active screen and RAM pages, contention, flash phase; verbose adds each
///   screen's RAM page and Z80 mapping and the decoded #7FFD latch.
/// - `ScreenMode()`: the video mode's picture format (colour depth, bpp,
///   attribute cell, text grid, memory layout), displayed RAM pages and the
///   machine's video latches (#EFF7, #DFFD, #FF77).
/// - `ScreenFlash()`: FLASH phase and timing.
/// - `ScreenAttributes(screen)`: per-cell ink/paper/bright/flash decoded from
///   the classic ZX attribute memory layout (offset 0x1800 within a RAM
///   page, 32x24 cells), read directly off the RAM page (not the Z80 bank
///   mapping). `screen` selects which page: -1 (default) both screens when
///   the model is shadow-capable, else just the one; 0 forces page 5; 1
///   forces page 7 (only valid when shadow-capable). Shape: `available`,
///   `cols` (32), `rows` (24), and `screens`: an array of
///   `{screen, ram_page, cells}` where `cells` is a row-major array of 768
///   `{ink, paper, bright, flash}` objects (bits 0-2 ink, 3-5 paper, 6
///   bright, 7 flash).
StateNode Screen(EmulatorContext* context, bool verbose);
StateNode ScreenMode(EmulatorContext* context);
StateNode ScreenFlash(EmulatorContext* context);
StateNode ScreenAttributes(EmulatorContext* context, int screen = -1);

/// Video debug translation (PLAN #42, video-debug-translation design §6), built
/// on VideoMapService so every interface shows the same fields (devicestatevideo.cpp):
/// - `VideoBeam()`: the beam now (`tstate`, `line`, `dot_in_line`, `zone`, `paper{}`, ...,
///   `frame_timing{}`, `raster{}`) plus `layers[]` - the layer pixel under the beam
///   (`id`, `x`, `x_end`, `y`).
/// - `VideoLayout()`: `mapped`, `family`, frame geometry, `layers[]` with `surface{}` and
///   the beam `window{}`, and the `framebuffer{}` placement of the surface.
/// - `VideoPixel(layer, x, y)` / `VideoPixelAtBeam(t)`: `sources[]` (`space`, `page`,
///   `offset`, `bit_mask`, `role`, `z80[]`), `colour_index`, `rgb`, `rendered_rgb`,
///   `state_at` / `values_at` (design §4.6); at the beam a border point reports `border`.
/// - `VideoAddress(page, offset)` / `VideoAddressZ80(address)`: `areas[]` of every layer
///   the byte feeds. `VideoAddressIn(space, page, offset)` asks the same of another space:
///   "ram" (page + offset), "sprite_ram" (a sprite attribute word, byte offset - TS-Conf
///   SFILE: word n at 2n) or "palette" (a palette cell, byte offset - 16-bit cells at 2n).
/// - `VideoText(layer)`: `columns`, `rows` and `lines[]` (`text`, `codes`, `attrs`) of a
///   text layer (ATM / ZX-Evo text modes); unavailable for bitmap layers.
StateNode VideoBeam(EmulatorContext* context);
StateNode VideoLayout(EmulatorContext* context);
StateNode VideoPixel(EmulatorContext* context, unsigned layer, unsigned x, unsigned y);
StateNode VideoPixelAtBeam(EmulatorContext* context, unsigned tInFrame);
StateNode VideoAddress(EmulatorContext* context, unsigned page, unsigned offset);
StateNode VideoAddressZ80(EmulatorContext* context, unsigned address);
StateNode VideoAddressIn(EmulatorContext* context, const std::string& space, unsigned page, unsigned offset);
StateNode VideoText(EmulatorContext* context, unsigned layer = 0);

/// Video memory contention (`Contention()`): the machine's rule (none / ula48 / ula128 / gatearray), whether
/// it applies, the 'contention' switch and whether contention is in effect, the selected memory interface,
/// the I/O rule, per slot its mapping and whether the CPU waits there, the +2A/+3 floating-bus latch, and -
/// while the debugger is on - contended accesses and wait T-states per kind (fetch / read / write / io) for
/// the current frame, the last frame and in total.
StateNode Contention(EmulatorContext* context);

/// Human-readable rendering (CLI): "key: value" lines, nested by indentation,
/// arrays as "[index]" blocks
std::string ToText(const StateNode& node, int indent = 0);
}  // namespace DeviceState
