// eve-emu - TO VERIFY constants.
//
// Every open item of the behavior spec (ft812-behavior-spec.md §12) that can be a
// number or a switch is one constant here, with its spec reference and the current
// choice. When an item is settled, the constant gets the measured value and a comment
// with the evidence. No guess is hidden inside an expression elsewhere in the library.
#pragma once

#include <cstdint>

namespace EveLib
{

// --- Address space and host interface (spec §1, §2, V11, V12) -------------------------

// spec §1, V11: reads of reserved addresses return this value; writes are ignored.
constexpr uint8_t kReservedReadValue = 0x00;                        // TO VERIFY
// spec §3, V11: a writable register keeps all 32 bits written and reads them back; the
// chip uses only its own bits (BT8XX, golden case register-masks).
constexpr bool kRegistersKeepAllBits = true;                        // TO VERIFY
// spec §2.1, V11: MISO while the host clocks address and dummy bytes.
constexpr uint8_t kMisoUndefinedValue = 0xFF;                       // TO VERIFY
// spec §2.1, V11: transactions with prefix 11 are ignored.
// spec §2.1, V11: a partial word left in REG_CMDB_WRITE at CS_N high is dropped.
constexpr bool kCmdbDropPartialWord = true;                         // TO VERIFY
// spec §2.1: REG_SPI_WIDTH bit 2 (extra dummy byte on reads) is honored.
constexpr bool kSpiWidthExtraDummy = true;                          // TO VERIFY
// spec §2.2: memory transactions are served only in the ACTIVE power state.
constexpr bool kMemoryAccessOnlyWhenActive = true;                  // TO VERIFY
// spec §2.2, V12: system clocks from a reset (ACTIVE after power-on, RST_PULSE,
// clock switch) until REG_ID reads 0x7C. The PG allows up to 300 ms; 0 works for
// software that polls, which all VDAC2 software does.
constexpr uint64_t kIdReadyDelayClocks = 0;                          // TO VERIFY
// spec §2.2: RST_PULSE keeps RAM_G (and re-writes the chip ID); PWRDOWN loses it.
constexpr bool kRstPulseClearsRamG = false;                         // TO VERIFY
// spec §2.2: system clock multiplier when CLKSEL selected 0 (DS: 5 x input).
constexpr uint32_t kDefaultClockMultiplier = 5;
// spec §2.2: internal oscillator frequency.
constexpr uint32_t kInternalOscillatorHz = 12000000;
// spec §2.2: CLKSEL multipliers outside this range are ignored. VDAC2 runs 6...10,
// the DS allows 2...5.
constexpr uint32_t kMinClockMultiplier = 2;                          // TO VERIFY
constexpr uint32_t kMaxClockMultiplier = 63;                         // TO VERIFY

// --- Registers (spec §3, V10, V11) ------------------------------------------------------

// spec §3.1, V10: REG_CPURESET reset value: 0 [PG] / 2 [DS]. The SDK boot waits for 0.
constexpr uint32_t kCpuResetResetValue = 0;                          // TO VERIFY
// spec §3.4, V10: REG_GPIOX reset value: 0x8000 [PG] / 0x0080 [DS]; BT8XX: 0
// (golden case reset-registers, as REG_GPIO_DIR and REG_GPIOX_DIR: 0).
constexpr uint32_t kGpioxResetValue = 0;
// spec §3.1, V10: REG_FREQUENCY width: 28 bits [DS] / 32 bits [PG].
constexpr uint32_t kFrequencyMask = 0xFFFFFFFF;                      // TO VERIFY
// spec §3.5, V10: REG_ADAPTIVE_FRAMERATE reset value; BT8XX: 0 (golden reset-registers).
constexpr uint32_t kAdaptiveFramerateResetValue = 0;
// spec §3.5, V10: REG_DATESTAMP contents (16 bytes); to be read from a BT8XX dump.
constexpr uint8_t kDatestampByte = 0x00;                             // TO VERIFY
// spec §3, V11: a register's side effect runs when its last byte (offset 3) is written
// or when the transaction ends, whichever comes first.
// spec §5.3, V14: a write of 3 to REG_DLSWAP stays in the register and swaps nothing
// (BT8XX, golden case dlswap-3); a write of 0 leaves a pending swap as it is (TO VERIFY).
constexpr bool kDlswapIgnoreZeroAndThree = true;                    // TO VERIFY
// spec §3.1: REG_CLOCK keeps running while REG_PCLK = 0 (the system clock runs).
constexpr bool kClockRunsWithoutPclk = true;                        // TO VERIFY

// --- Scan and swap (spec §3.2, §5, V8, V9, V14) -----------------------------------------

// spec §5.1, V8: how many lines ahead of the scan a line is drawn.
constexpr uint32_t kLineLookahead = 1;                               // TO VERIFY
// spec §5.3, V9: the frame event (REG_FRAMES, DLSWAP_FRAME, INT_SWAP, completed frame)
// happens at the end of the last visible line (true) or the last line of the frame.
constexpr bool kFrameEventAtEndOfVisible = true;                    // TO VERIFY
// spec §6.7, V14: a swap copies the pending list into the active one (true) or
// exchanges the two (false). BT8XX: exchange - after a swap RAM_DL reads the list that
// was active before (golden case swap-copy).
constexpr bool kSwapCopiesList = false;

// --- Display list (spec §5.2, §6, V1, V7, V15, V17) -------------------------------------

// spec §6.2, V1: when the graphics context is reset. BT8XX: per line (golden case
// context-reset: a COLOR_RGB after the rectangle does not color it on later lines).
enum class ContextReset { PerLine, PerFrame, Never };
constexpr ContextReset kContextReset = ContextReset::PerLine;
// spec §5.2, V7: clocks of the line period not available to drawing.
constexpr uint32_t kLineBudgetOverhead = 0;                          // TO VERIFY (R-Type: ~44 at HCYCLE 1344)
// Line budget metrics: the soft budget's default margin below the hard budget, percent
// (practice on VDAC2: keep about 10 % below the theoretical 1344 clocks per line).
constexpr uint32_t kLineBudgetMarginPercent = 10;
// spec §5.2, V7: the line budget floor, applied when HCYCLE x PCLK >= this value.
constexpr uint32_t kLineBudgetFloor = 2048;                          // TO VERIFY
// spec §5.2, V7: fill cost of points, lines, rectangles and edge strips, pixels per clock.
constexpr uint32_t kPrimitivePixelsPerClock = 16;                    // TO VERIFY
// spec §6.5, V3: BILINEAR samples at x' itself (no half-texel offset); with the weights
// and rounding of eve-bitmap.cpp it matches BT8XX exactly (golden cases bilinear-*,
// bitmap-format-*).
constexpr int32_t kBilinearOffset = 0;
// spec §5.2: bilinear fill rates from the PG table (the PG text says 1/4).
constexpr uint32_t kBilinearPixelsPerClock = 4;                      // TO VERIFY
constexpr uint32_t kBilinearPalettedPixelsPerClock = 2;              // TO VERIFY
// spec §6.2, V15: running off the end of RAM_DL without DISPLAY ends the list.
// spec §6.1, V15: unknown opcodes are executed as NOP.
// spec §6.2, V17: graphics engine bitmap parameters of handles 16...31 are set to the
// ROM fonts at reset (true) or only when the coprocessor draws text.
constexpr bool kRomFontHandlesAtReset = true;                       // TO VERIFY
// spec §6.1: SCISSOR_SIZE initial value 2048 x 2048 [PG §4.40] (context table: HSIZE).
constexpr uint32_t kScissorInitialSize = 2048;                       // TO VERIFY
// spec §6.2: a list that loops (JUMP back) is cut after this many commands on one line;
// the hardware runs out of line time long before.
constexpr uint32_t kMaxCommandsPerLine = 8 * 2048;

// --- Pixels (spec §6.4-6.6, V2, V3, V4) ----------------------------------------------------

// spec §6.4, V2: where a pixel is sampled, offset in 1/16 pixel from its corner. BT8XX: at
// the integer position (profiles of lines, rectangles and points are symmetric about
// integer coordinates, golden cases probe-*).
constexpr int32_t kPixelCenter = 0;
// spec §6.4, V2: antialiased points, lines and rectangles: the BT8XX table of
// eve-aa-table.cpp (tools/oracle/make-aa-table.py), by radius and distance.
// spec §6.4, V2: edge strips are not antialiased on BT8XX; the edge position is truncated
// to whole pixels and a pixel is filled when its far side (x + 1, y + 1) reaches it
// (golden case probe-edge: an edge at 100.0...100.94 fills from pixel 99). EDGE_STRIP_A /
// B: see DrawEdge (golden case edge-strips; one EDGE_STRIP_L vertex pixel still differs).
constexpr bool kEdgeStripHard = true;                                // TO VERIFY (L vertex)
// spec §6.5, V3: expansion of n-bit channels to 8 bits by bit replication
// (L4 x -> x * 17, 5-bit x -> (x << 3) | (x >> 2)); false = shift left only.
constexpr bool kExpandByReplication = true;                         // TO VERIFY
// spec §6.5, V3 / §6.6, V4: product of two 8-bit values (color modulation, blend factors):
// true = (a x b + 127) / 255, false = (a x (b + 1)) >> 8.
constexpr bool kMultiplyRoundDiv255 = true;                         // TO VERIFY
// spec §6.6, V4: stencil ops 6 and 7 (INCR_WRAP / DECR_WRAP in the TS-Labs SDK) keep the
// value on BT8XX (golden case stencil-ops-6-7).
constexpr bool kStencilWrapOps = false;
// spec §6.5, V3: BITMAP_SIZE / BITMAP_LAYOUT leave the _H high bits as they are.
constexpr bool kSizeKeepsHighBits = true;                           // TO VERIFY
// spec §6.5: TEXTVGA draws a set glyph pixel in the VGA color of the attribute's low
// nibble, the rest transparent (BT8XX, golden case format-textvga-attributes; the chip
// itself is TO VERIFY).
// spec §6.3, V13: REG_ROTATE matches BT8XX (golden cases rotate-*); a portrait orientation
// draws the whole logical picture when the frame's first line is due (not line by line).
// spec §3.2, V13: REG_CSPREAD = 1 at line edges: a pixel without a neighbor keeps its own
// red / blue.
constexpr bool kCspreadEdgeKeepsOwn = true;                          // TO VERIFY

// --- Coprocessor (spec §7, V5, V6, V16, V19) -----------------------------------------------

// spec §7.2, V6: coprocessor costs in system clocks. All 0 = instant execution.
constexpr uint32_t kCostCommand = 0;                                 // TO VERIFY
constexpr uint32_t kCostDisplayListWord = 0;                         // TO VERIFY
constexpr uint32_t kCostMemoryPerByte = 0;                           // TO VERIFY (MEMCPY, MEMSET, MEMZERO, MEMWRITE, APPEND)
constexpr uint32_t kCostMemcrcPerByte = 0;                           // TO VERIFY
constexpr uint32_t kCostInflatePerOutputByte = 0;                    // TO VERIFY
constexpr uint32_t kCostLoadImagePerPixel = 0;                       // TO VERIFY
// spec §7.5, V5: the coprocessor matrix is kept as the bitmap transform (screen -> bitmap);
// CMD_SETMATRIX converts 16.16 to 8.8 / 15.8 by rounding down (BT8XX: rotate 45 degrees
// gives D = -182, golden case cmd-matrix).
constexpr bool kMatrixRoundToNearest = false;
// spec §7.5: CMD_LOADIMAGE limits seen on the card (forum): larger images fault.
constexpr uint32_t kJpegMaxPixels = 524288;                          // TO VERIFY
constexpr uint32_t kPngMaxPixels = 483328;                           // TO VERIFY
constexpr uint32_t kImageMaxWidth = 1024;                            // TO VERIFY
constexpr uint32_t kImageMaxHeight = 768;                            // TO VERIFY
// spec §7.5, V3: 8-bit channels to RGB565 / ARGB4 by truncation (false: rounding).
constexpr bool kLoadImageTruncates = true;                           // TO VERIFY
// spec §7.5, V5: OPT_FULLSCREEN scales the image by an integer factor to fill the screen
// (BITMAP_TRANSFORM_A / E and BITMAP_SIZE); the real words are unknown.
constexpr bool kLoadImageFullscreenScales = true;                    // TO VERIFY
// spec §7.5, V18: CMD_PLAYVIDEO decodes each frame (RGB565) at this RAM_G address and
// shows it with its own display list (CLEAR, the bitmap, DISPLAY) and a frame swap.
constexpr uint32_t kVideoFrameAddress = 0;                           // TO VERIFY
// spec §7.5, V18: frame n is shown no earlier than n x dwMicroSecPerFrame after the start,
// measured in REG_FREQUENCY clocks; OPT_NOTEAR waits for the previous swap before the
// next frame's pixels are written.
constexpr bool kVideoPacedByFrequency = true;                       // TO VERIFY
// spec §7.4: REG_CMD_DL after fault recovery is left unchanged.
constexpr bool kRecoveryKeepsCmdDl = true;                          // TO VERIFY
// spec §7.5, V16: CMD_INFLATE expects a zlib stream with its 2-byte header.
constexpr bool kInflateZlibHeader = true;                           // TO VERIFY
// spec §7.5, V16: CMD_MEMCPY copies forward, byte by byte.
// spec §7.5, V16: CMD_MEDIAFIFO sets READ = WRITE = ptr.

// --- Audio (spec §3.3) -----------------------------------------------------------------

// spec §3.3: a non-silent sound effect ends after this time and raises INT_SOUND.
constexpr uint32_t kSoundEffectDurationMs = 100;                     // TO VERIFY

// --- State (arch §7.4) -----------------------------------------------------------------

// arch §7.4: capacity of the INFLIGHT region: the largest image file the chip accepts
// plus the inflate decoder state.
constexpr uint32_t kInflightCapacity = 1024 * 1024 + 64 * 1024;      // TO VERIFY against real files

} // namespace EveLib
