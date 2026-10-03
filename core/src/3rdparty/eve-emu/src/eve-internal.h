// eve-emu - chip context, control state and constants shared by the units.
//
// Layout (arch §8.1): the control state is trivially copyable and saved whole; the
// memory regions are captured by dirty 4 KB pages; everything else is derived and is
// rebuilt after a restore.
#pragma once

#include "eve/eve.h"
#include "eve-tunables.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>

namespace EveLib
{

// --- Memory map (spec §1) --------------------------------------------------------------

constexpr uint32_t kAddressMask = 0x3FFFFF;  // 22-bit host address space
constexpr uint32_t kRamGBase = 0x000000;
constexpr uint32_t kRamGSize = 1024 * 1024;
constexpr uint32_t kChipIdAddress = 0x0C0000;
constexpr uint32_t kRomFontBase = 0x1E0000;
constexpr uint32_t kRomEnd = 0x300000;       // ROM_FONT .. ROM_FONTROOT end
constexpr uint32_t kRomFontRootAddress = 0x2FFFFC;
constexpr uint32_t kRomFontRootValue = 0x00201EE0;
constexpr uint32_t kRamDlBase = 0x300000;
constexpr uint32_t kRamDlSize = 8 * 1024;
constexpr uint32_t kDlWordBytes = 4;
constexpr uint32_t kDlWords = kRamDlSize / kDlWordBytes; // 2048 commands
constexpr uint32_t kRamRegBase = 0x302000;
constexpr uint32_t kRamRegSize = 4 * 1024;
constexpr uint32_t kRamCmdBase = 0x308000;
constexpr uint32_t kRamCmdSize = 4 * 1024;
constexpr uint32_t kRamCmdMask = kRamCmdSize - 1;
constexpr uint32_t kSpecialBase = 0x309000;
constexpr uint32_t kSpecialSize = 4 * 1024;

constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kPageShift = 12;
constexpr uint32_t kPagesPerWord = 64;   // dirty bits per uint64_t

// Font metric block [PG §5.5.1]: 128 width bytes, then five 32-bit words.
constexpr uint32_t kFontMetricSize = 148;
constexpr uint32_t kFontMetricFormat = 128;
constexpr uint32_t kFontMetricStride = 132;
constexpr uint32_t kFontMetricWidth = 136;
constexpr uint32_t kFontMetricHeight = 140;
constexpr uint32_t kFontMetricData = 144;
constexpr uint32_t kFirstRomFont = 16;      // ROM fonts 16...34
constexpr uint32_t kLastRomFont = 34;
constexpr uint32_t kHandleCount = 32;       // bitmap handles 0...31
constexpr uint32_t kFirstFontHandle = 16;   // handles 16...31 hold ROM fonts at reset

constexpr uint64_t kMillisecondsPerSecond = 1000;
constexpr uint32_t kPaletteEntries = 256;   // 8-bit palette indices

// Chip ID bytes at kChipIdAddress after reset (spec §1).
constexpr uint8_t kChipId[4] = {0x08, 0x12, 0x01, 0x00};

// --- Registers (spec §3) ---------------------------------------------------------------

// Logical registers. Their addresses come from the chip table (arch §12), so a unit never
// hard-codes an offset.
enum class Reg : uint8_t
{
    Id, Frames, Clock, Frequency, RenderMode, SnapY, Snapshot, SnapFormat, CpuReset,
    TapCrc, TapMask, Hcycle, Hoffset, Hsize, Hsync0, Hsync1, Vcycle, Voffset, Vsize,
    Vsync0, Vsync1, Dlswap, Rotate, Outbits, Dither, Swizzle, Cspread, PclkPol, Pclk,
    TagX, TagY, Tag, VolPb, VolSound, Sound, Play, GpioDir, Gpio, GpioxDir, Gpiox,
    IntFlags, IntEn, IntMask, PlaybackStart, PlaybackLength, PlaybackReadptr,
    PlaybackFreq, PlaybackFormat, PlaybackLoop, PlaybackPlay, PwmHz, PwmDuty, Macro0,
    Macro1, CmdRead, CmdWrite, CmdDl, TouchMode, TouchAdcMode, TouchCharge, TouchSettle,
    TouchOversample, TouchRzthresh, TouchRawXy, TouchRz, TouchScreenXy, TouchTagXy,
    TouchTag, TouchTag1Xy, TouchTag1, TouchTag2Xy, TouchTag2, TouchTag3Xy, TouchTag3,
    TouchTag4Xy, TouchTag4, TouchTransformA, TouchTransformB, TouchTransformC,
    TouchTransformD, TouchTransformE, TouchTransformF, TouchConfig, CtouchTouch4X,
    BistEn, Trim, AnaComp, SpiWidth, TouchDirectXy, TouchDirectZ1Z2, Datestamp0,
    Datestamp1, Datestamp2, Datestamp3, CmdbSpace, CmdbWrite, AdaptiveFramerate,
    Tracker, Tracker1, Tracker2, Tracker3, Tracker4, MediafifoRead, MediafifoWrite,
    Undocumented0A4, Undocumented0F0, Undocumented194, Undocumented558, Undocumented560,
    Count
};
constexpr size_t kRegCount = static_cast<size_t>(Reg::Count);

// What happens when a register is written or read.
enum class RegEffect : uint8_t
{
    None,          // stored
    Timing,        // scan timing changed
    Dlswap,        // swap request
    CpuReset,      // engine reset control
    CmdWrite,      // coprocessor kick
    CmdRead,       // coprocessor read pointer (recovery)
    CmdDl,         // display list offset set by the host
    Play,          // sound effect start
    PlaybackPlay,  // audio playback start
    MediafifoWrite // media FIFO data arrived
};

enum RegFlags : uint8_t
{
    RegReadOnly = 1,      // host writes ignored
    RegDrawing = 2,       // drawing reads it: catch up the picture before a write
    RegClearOnRead = 4,   // REG_INT_FLAGS
    RegComputed = 8,      // value computed on read (REG_CLOCK, REG_CMDB_SPACE, ...)
    RegWriteOnly = 16     // reads return 0 (REG_CMDB_WRITE)
};

struct RegInfo
{
    Reg reg;
    uint32_t address;     // absolute
    uint32_t resetValue;
    uint32_t mask;        // writable / valid bits
    uint8_t flags;
    RegEffect effect;
    const char* name;
};

// --- The chip table (arch §12): one row per model -----------------------------------------

struct ChipTable
{
    EveModel model;
    const char* name;
    uint32_t ramGSize;
    uint32_t ramDlBase, ramRegBase, ramCmdBase, specialBase;
    uint8_t chipId[4];
    const RegInfo* registers; // kRegCount entries, indexed by Reg
};

const ChipTable& GetChipTable(EveModel model);

// --- Regions (arch §4.5, §8.1) ---------------------------------------------------------

enum RegionId : uint8_t
{
    RegionRamG, RegionDl0, RegionDl1, RegionReg, RegionCmd, RegionSpecial, RegionInflight,
    RegionCount
};

struct Region
{
    const char* name;
    uint8_t* base;
    uint32_t size;
    uint64_t* dirty;
    uint32_t pageCount;

    void MarkDirty(uint32_t offset) { MarkPage(offset >> kPageShift); }
    void MarkPage(uint32_t page) { dirty[page / kPagesPerWord] |= uint64_t{1} << (page % kPagesPerWord); }
    void MarkDirtyRange(uint32_t offset, uint32_t length);
};

// --- Control state (arch §8.1): trivially copyable, saved whole -------------------------------

enum class PowerMode : uint8_t { Active, Standby, Sleep, PowerDown };

enum class SpiPhase : uint8_t
{
    Idle,        // CS_N high
    Header,      // collecting the first three bytes
    ReadDummy,   // dummy byte(s) of a read
    ReadData,    // data out
    WriteData,   // data in
    HostCommand, // host command bytes
    Ignore       // undefined prefix or access refused: ignore until CS_N high
};

struct SpiState
{
    SpiPhase phase;
    uint8_t selected;
    uint8_t headerCount;
    uint8_t dummyLeft;
    uint8_t header[3];
    uint8_t writeWrapsInCmd;   // the write started inside RAM_CMD
    uint8_t reserved0[3];
    uint32_t address;          // current 22-bit address
    uint32_t bytes;            // data bytes in this transaction
};

// The single write path's pending register side effect (spec §3: it runs when the
// register's last byte is written or the transaction ends).
struct BusState
{
    uint32_t pendingRegister;  // register address + 1 whose side effect is pending, 0 = none
    uint32_t pendingOldValue;  // its value before the first byte of this write
    uint8_t cmdbFill;          // bytes collected for REG_CMDB_WRITE
    uint8_t cmdbWord[4];
    uint8_t reserved0[3];
};

struct PowerState
{
    PowerMode mode;
    uint8_t externalClock;     // CLKEXT selected
    uint8_t multiplier;        // CLKSEL [5:0], 0 = default
    uint8_t pllRange;          // CLKSEL [7:6]
    uint8_t romsPowerDown;     // PD_ROMS byte
    uint8_t reserved0[3];
    uint32_t systemClockHz;    // when active
};

struct ScanState
{
    uint64_t clocksSinceReset; // REG_CLOCK basis
    uint64_t idReadyAt;        // clocksSinceReset value at which REG_ID reads 0x7C
    uint64_t frames;           // REG_FRAMES (low 32 bits visible)
    uint64_t completedFrames;  // EveCompletedFrames
    uint32_t line;             // line in frame, 0..VCYCLE-1
    uint32_t lineClock;        // system clocks into the line
    uint8_t activeDl;          // index (0/1) of the active display list region
    uint8_t dlswapPending;     // 0, 1 (line) or 2 (frame)
    uint8_t reserved0[6];
};

// Per-handle bitmap parameters of the graphics engine (spec §6.2). Not part of the
// graphics context: they persist across lines, frames and lists. The _H commands set the
// high bits separately.
struct BitmapHandle
{
    uint32_t source;           // BITMAP_SOURCE
    uint8_t format;            // BITMAP_LAYOUT
    uint8_t filter, wrapX, wrapY; // BITMAP_SIZE
    uint16_t strideLow;        // BITMAP_LAYOUT linestride, 10 bits
    uint16_t layoutHeightLow;  // BITMAP_LAYOUT height, 9 bits
    uint16_t widthLow;         // BITMAP_SIZE width, 9 bits
    uint16_t heightLow;        // BITMAP_SIZE height, 9 bits
    uint8_t strideHigh;        // BITMAP_LAYOUT_H, 2 bits
    uint8_t layoutHeightHigh;  // BITMAP_LAYOUT_H, 2 bits
    uint8_t widthHigh;         // BITMAP_SIZE_H, 2 bits
    uint8_t heightHigh;        // BITMAP_SIZE_H, 2 bits
};

// The graphics context (spec §6.3, [PG §4.1 Table 5]).
struct GraphicsContext
{
    uint32_t clearColorRgb;    // 0xRRGGBB
    uint32_t colorRgb;
    uint32_t paletteSource;
    int32_t transform[6];      // A, B, D, E: 8.8; C, F: 15.8
    int32_t translateX, translateY; // 1/16 pixel
    uint16_t pointSize;        // radius, 1/16 pixel
    uint16_t lineWidth;        // 1/16 pixel
    uint16_t scissorX, scissorY;
    uint16_t scissorWidth, scissorHeight;
    uint8_t clearColorA, colorA;
    uint8_t alphaFunc, alphaRef;
    uint8_t stencilFunc, stencilRef, stencilFuncMask;
    uint8_t stencilWriteMask;
    uint8_t stencilFail, stencilPass;
    uint8_t blendSrc, blendDst;
    uint8_t clearStencil, clearTag;
    uint8_t tag, tagMask;
    uint8_t colorMask;         // bit 3 R, 2 G, 1 B, 0 A
    uint8_t handle, cell;
    uint8_t vertexFormat;
    uint8_t reserved0[2];
};

struct AudioState
{
    uint64_t effectEndsAt;     // clocksSinceReset when the sound effect ends, 0 = none
    uint64_t playbackClocks;   // clocks since playback start
    uint64_t playbackSamples;  // samples consumed at the last update
    uint8_t effectPlaying;
    uint8_t playbackPlaying;
    uint8_t reserved0[6];
};

// --- Coprocessor (spec §7, arch §7) -----------------------------------------------------

constexpr uint32_t kCoproMaxParams = 16;   // fixed parameter words of the longest command
constexpr uint32_t kMatrixSize = 6;        // coefficients a..f

enum class CoproFault : uint8_t
{
    None,
    UnknownCommand,       // a code the emulator does not implement (design rule, spec §7.4)
    DisplayListOverflow,  // more than 2048 display list commands [PG §5.6]
    InvalidStream,        // CMD_INFLATE data is not a valid stream
    InvalidImage,         // CMD_LOADIMAGE / video data the chip rejects
    InflightOverflow,     // the operation needs more than kInflightCapacity
    Count
};

// What the step planned by the coprocessor does when its cost has elapsed.
enum class CoproStep : uint8_t
{
    None,
    DisplayListWord,  // copy one ring word to RAM_DL
    Start,            // read a command and its fixed parameters
    Body              // the next piece of the command in flight
};

// AVI stream state of CMD_PLAYVIDEO / CMD_VIDEOSTART / CMD_VIDEOFRAME.
struct VideoState
{
    uint32_t framePeriodUs;        // avih dwMicroSecPerFrame
    uint32_t totalFrames;          // avih dwTotalFrames
    uint32_t width, height;        // avih dwWidth / dwHeight
    uint32_t videoStream;          // index of the 'vids' stream: chunks "NNdc" / "NNdb"
    uint32_t frameIndex;           // frames shown or loaded so far
    uint32_t moviRemaining;        // bytes of the movi list not yet read
    uint32_t skipRemaining;        // bytes of a chunk being skipped
    uint32_t chunkSize;            // size of the chunk being collected (without its pad byte)
    uint32_t parserStage;          // AVI parser stage
    uint32_t riffRemaining;        // bytes of the AVI file not yet read
    uint64_t startClock;           // clocksSinceReset when playback started (frame pacing)
    uint8_t ready;                 // the header has been read (CMD_VIDEOSTART)
    uint8_t inMovi;                // the parser is inside the movi list
    uint8_t reserved0[6];
};

// Persistent coprocessor state [PG §5.7 Table 12] plus the execution state. The command
// in flight is described completely by this record and the INFLIGHT region, so a
// restore continues it exactly (arch §7).
struct CoproState
{
    // Coprocessor state of the PG (reset by CMD_COLDSTART and REG_CPURESET).
    int32_t matrix[kMatrixSize];   // a..f, 16.16
    uint32_t bgColor, fgColor, gradColor;
    uint32_t scratchHandle;
    uint32_t numberBase;
    uint32_t mediaFifoBase, mediaFifoSize;
    uint32_t fontPointers[kHandleCount]; // font metric block per handle
    uint8_t fontFirstChar[kHandleCount]; // CMD_SETFONT2 first character per handle
    uint32_t inflateEnd;           // CMD_GETPTR
    uint32_t imageAddress, imageWidth, imageHeight; // CMD_GETPROPS

    // Execution.
    EveCoproPhase phase;
    CoproFault fault;
    CoproStep step;                // planned step, applied when stall reaches 0
    uint8_t reserved0;
    uint32_t faultCommand;
    uint64_t stall;                // clocks until the planned step applies
    uint32_t stepUnits;            // size of the planned step (bytes, words)
    uint32_t ringByteOffset;       // bytes of the word at REG_CMD_READ already consumed

    // The command in flight: the in-flight record header (arch §7.4).
    uint32_t command;              // 0 = none
    uint32_t commandAddress;       // ring offset of the command word
    uint32_t params[kCoproMaxParams];
    uint32_t done;                 // progress: bytes, words or pixels done
    uint32_t total;                // size of the whole operation, when known
    uint32_t crc;                  // CMD_MEMCRC running value
    uint32_t inflightUsed;         // bytes of the INFLIGHT region the command uses
    uint32_t inputBytes;           // input recorded in INFLIGHT (fallback inflate, images)
    uint8_t decoderDone;           // the decoder reported the end of its stream
    uint8_t displayListFull;       // REG_CMD_DL wrapped past command 2047
    uint8_t generated;             // display list words of the command are in INFLIGHT
    uint8_t stage;                 // CMD_LOADIMAGE / video: collecting, writing pixels, emitting
    uint32_t scanPos;              // image parser: offset of the next PNG chunk / JPEG marker
    uint8_t scanEntropy;           // JPEG parser: inside entropy-coded data
    uint8_t reserved2[3];
    uint32_t imageFormat;          // chosen bitmap format of the image in flight
    uint32_t flightWidth, flightHeight; // its size (GETPROPS values are set when it completes)
    uint32_t writeAddress;         // where the decoded pixels of the image in flight go
    uint32_t paletteEntries;       // PLTE entries of the indexed image in flight
    VideoState video;
};

// Line budget metrics of the last completed frame (line-budget-metrics design §3.1).
constexpr uint32_t kMetricsLines = 4096; // VSIZE is 12 bits (= kMaxLines)
struct FrameMetricsState
{
    uint64_t frame;
    uint32_t valid;
    uint32_t lines;
    uint32_t hardBudget;
    uint32_t softBudget;
    uint32_t worstLine;
    uint32_t worstClocks;
    uint64_t totalClocks;
    uint32_t linesOverSoft;
    uint32_t linesOverHard;
    uint16_t lineClocks[kMetricsLines];
};

// The control state, saved whole by EveSaveState. Coprocessor, graphics and in-flight
// parts are added by their units (see eve-copro.h).
struct ControlState
{
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t model;
    uint64_t totalClocks;      // EveTotalClocks: since creation, kept across resets
    SpiState spi;
    BusState bus;
    PowerState power;
    ScanState scan;
    AudioState audio;
    CoproState copro;
    BitmapHandle handles[kHandleCount];
    GraphicsContext context;   // carried between lines unless reset per line (spec V1)
    FrameMetricsState metrics; // the last completed frame's line costs (replaced at frame end)
};
static_assert(std::is_trivially_copyable<ControlState>::value, "ControlState must be trivially copyable");

constexpr uint32_t kStateMagic = 0x31455645; // "EVE1"
constexpr uint32_t kStateVersion = 8; // 8: FrameMetricsState

} // namespace EveLib

// --- The chip context (one allocation in EveCreate) -------------------------------------

struct EveChip
{
    const EveLib::ChipTable* table;
    EveLib::ControlState state;

    // Configuration.
    uint32_t externalClockHz;
    const uint8_t* romImage;
    uint32_t romBase;          // first ROM address covered by romImage
    uint32_t romSize;

    // Regions.
    EveLib::Region regions[EveLib::RegionCount];
    std::unique_ptr<uint8_t[]> memory;     // backing store of all regions
    std::unique_ptr<uint64_t[]> dirtyBits; // backing store of all dirty bitmaps

    // Decoders (arch §6).
    const EveInflateDecoder* inflate;
    const EveImageDecoder* png;
    const EveImageDecoder* jpeg;

    // Coprocessor costs (eve-tunables.h); a test may change them.
    struct Costs
    {
        uint32_t command;
        uint32_t displayListWord;
        uint32_t memoryPerByte;
        uint32_t memcrcPerByte;
        uint32_t inflatePerOutputByte;
        uint32_t loadImagePerPixel;
    } costs;

    // Derived coprocessor state (never saved, rebuilt after a restore).
    bool coproBusy;                        // re-entrancy guard of the coprocessor pump
    std::unique_ptr<uint8_t[]> decoderState; // inflate state of a decoder that is not plain
    std::unique_ptr<uint8_t[]> workBuffer;   // decoder output before it goes to memory
    std::unique_ptr<uint8_t[]> inputBuffer;  // ring data handed to a decoder in one piece
    std::unique_ptr<uint8_t[]> imageBuffer;  // decoded image (canonical layout), CMD_LOADIMAGE
    uint32_t imagePalette[EveLib::kPaletteEntries]; // its palette, for indexed PNG
    bool imageDecoded;                       // imageBuffer holds the image in flight

    // Derived drawing state.
    std::unique_ptr<uint8_t[]> lineColor;    // line buffers (kMaxLineWidth pixels)
    std::unique_ptr<uint8_t[]> lineStencil;
    std::unique_ptr<uint8_t[]> lineTag;
    std::unique_ptr<uint32_t[]> lineTexels;  // a span's decoded texels (kMaxLineWidth)
    std::unique_ptr<uint8_t[]> probeColor;   // the same for EveProbePixel
    std::unique_ptr<uint8_t[]> probeStencil;
    std::unique_ptr<uint8_t[]> probeTag;
    std::unique_ptr<EveLineCost[]> lineCosts; // per visible line of the last frame
    uint32_t overflowLines;                  // lines over budget in the current frame
    uint32_t lineBudgetMargin;               // soft budget = hard budget minus this percent (host setting)
    uint32_t drawnLines;                     // lines of the current frame already drawn (catch-up)
    bool bitmapFastPath;                     // false: every bitmap pixel through the general path

    // Output.
    uint32_t* framebuffer;
    uint32_t stridePixels;
    uint32_t widthCapacity;
    uint32_t heightCapacity;
    int drawing;
};

namespace EveLib
{

// --- Register access (eve-registers.cpp) ----------------------------------------------------

const RegInfo& RegisterInfo(const EveChip& chip, Reg reg);
// The register at an absolute address, or nullptr for a reserved address.
const RegInfo* FindRegister(const EveChip& chip, uint32_t address);
// Stored value of a register (no read hook).
uint32_t RegGet(const EveChip& chip, Reg reg);
// Store a register value as the chip itself does (no side effect).
void RegSet(EveChip& chip, Reg reg, uint32_t value);
void ResetRegisters(EveChip& chip);
// Value a host read sees (computed registers); clear-on-read is applied by the caller.
uint32_t RegReadValue(const EveChip& chip, const RegInfo& info);
// Run the side effect of a register after a host or coprocessor write.
void CommitRegister(EveChip& chip, const RegInfo& info, uint32_t oldValue);
// Flush a pending register side effect (end of transaction or of a coprocessor chunk).
void FlushPendingRegister(EveChip& chip);

// --- Memory (eve-memory.cpp): the single write path --------------------------------------------

bool InitRegions(EveChip& chip);              // false: out of memory
void ClearRegions(EveChip& chip);
// Host or coprocessor byte write with every side effect (arch §8.2).
void BusWrite(EveChip& chip, uint32_t address, uint8_t value);
// Host or coprocessor byte read. sideEffects = false for inspection (no clear-on-read).
uint8_t BusRead(EveChip& chip, uint32_t address);
uint8_t BusPeek(const EveChip& chip, uint32_t address);
// Pointers into the regions for internal bulk access.
uint8_t* RamG(EveChip& chip);
const uint8_t* RamG(const EveChip& chip);
uint8_t* PendingDl(EveChip& chip);
const uint8_t* ActiveDl(const EveChip& chip);
Region& PendingDlRegion(EveChip& chip);
Region& ActiveDlRegion(EveChip& chip);
uint8_t RomByte(const EveChip& chip, uint32_t address);

inline uint32_t LoadLe32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline void StoreLe32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

// --- Chip (eve-chip.cpp) -------------------------------------------------------------------

void PowerOnReset(EveChip& chip);
void InitClock(EveChip& chip);                // system clock from the power state
// Core reset (RST_PULSE, clock switch, power-down exit): keeps host command settings.
void CoreReset(EveChip& chip, bool clearRamG);
void SetLastError(const char* text);

// --- SPI (eve-spi.cpp) ---------------------------------------------------------------------

void SpiSelect(EveChip& chip, bool selected);
uint8_t SpiExchange(EveChip& chip, uint8_t mosi);

// --- Timing (eve-timing.cpp) -----------------------------------------------------------------

bool ClockRunning(const EveChip& chip);
uint32_t LineClocks(const EveChip& chip);    // HCYCLE x PCLK, 0 when the scan is stopped
bool ScanRunning(const EveChip& chip);
void Advance(EveChip& chip, uint64_t clocks);
uint64_t ClocksToNextEvent(const EveChip& chip);
void RaiseInterrupt(EveChip& chip, uint32_t bits);
bool IntAsserted(const EveChip& chip);
void RequestSwap(EveChip& chip, uint32_t mode);
void ApplySwap(EveChip& chip);
void TimingChanged(EveChip& chip);
void GetTiming(const EveChip& chip, EveTiming& out);

// --- Audio (eve-audio.cpp) -------------------------------------------------------------------

void AudioReset(EveChip& chip);
void AudioStartEffect(EveChip& chip);
void AudioStartPlayback(EveChip& chip);
void AudioAdvance(EveChip& chip, uint64_t clocks); // time moved; end what completes
uint64_t AudioClocksToNextEvent(const EveChip& chip);
uint32_t AudioReadPointer(const EveChip& chip);

// --- Coprocessor (eve-copro.cpp) ---------------------------------------------------------------

void CoproReset(EveChip& chip);               // coprocessor state to defaults
void CoproHold(EveChip& chip, bool hold);     // REG_CPURESET bit 0
void CoproKick(EveChip& chip);                // new ring data or media FIFO data
void CoproRun(EveChip& chip, uint64_t clocks); // spend clocks, apply what completes
uint64_t CoproClocksToNextEvent(const EveChip& chip);
void CoproSwapDone(EveChip& chip);            // a pending swap completed
uint32_t CmdbSpace(const EveChip& chip);
void CmdbAppendWord(EveChip& chip, uint32_t word);
void GetCoproView(const EveChip& chip, EveCoproView& out);

// --- Drawing (eve-dl.cpp) ----------------------------------------------------------------------

void DrawingReset(EveChip& chip);             // handles and derived drawing state
uint32_t FrameLinesDue(const EveChip& chip);  // visible lines of the frame in flight passed so far
void CatchUp(EveChip& chip);                  // draw every line sampled up to now
void FrameStart(EveChip& chip);               // new frame: nothing drawn yet
void DisplayListSwapped(EveChip& chip);       // a new active list
void DrawingInvalidate(EveChip& chip);        // derived drawing state is stale (restore)
void GetLineCost(const EveChip& chip, uint32_t line, EveLineCost& out);
void FoldFrameMetrics(EveChip& chip);          // the frame's line costs into state.metrics (frame end)
bool ProbePixel(const EveChip& chip, uint32_t x, uint32_t y, EvePixelSource& out);
void LoadRomFontHandle(EveChip& chip, uint32_t handle, uint32_t font);
void SetHandleFromMetrics(BitmapHandle& h, uint32_t format, uint32_t stride, uint32_t width, uint32_t height,
                          uint32_t source);
bool InitDrawing(EveChip& chip);               // allocate the derived drawing buffers

// --- State (eve-state.cpp) ---------------------------------------------------------------------

void StateLoaded(EveChip& chip);              // after EveLoadState
void MemoryRestored(EveChip& chip);           // after the host wrote the regions back
void CoproStateLoaded(EveChip& chip);         // restart an operation in flight

// --- Decoders (eve-decoders.cpp) -------------------------------------------------------------

// Select the decoder of each kind (arch §6.2); false + EveLastError when one is missing.
bool ResolveDecoders(EveChip& chip, const EveDecoders* decoders);

} // namespace EveLib
