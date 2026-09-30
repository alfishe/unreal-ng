#pragma once

#include <string>

#include "emulator/state/statenode.h"

class EmulatorContext;

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
