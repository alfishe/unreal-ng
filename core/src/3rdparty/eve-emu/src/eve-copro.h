// eve-emu - coprocessor internals shared by eve-copro*.cpp (spec §7).
#pragma once

#include "eve-internal.h"

namespace EveLib
{

// Command codes [PG §5.11-5.67], cross-checked with [SDK ft812.h].
enum : uint32_t
{
    kCmdDlstart = 0xFFFFFF00,
    kCmdSwap = 0xFFFFFF01,
    kCmdInterrupt = 0xFFFFFF02,
    kCmdBgcolor = 0xFFFFFF09,
    kCmdFgcolor = 0xFFFFFF0A,
    kCmdGradient = 0xFFFFFF0B,
    kCmdText = 0xFFFFFF0C,
    kCmdButton = 0xFFFFFF0D,
    kCmdKeys = 0xFFFFFF0E,
    kCmdProgress = 0xFFFFFF0F,
    kCmdSlider = 0xFFFFFF10,
    kCmdScrollbar = 0xFFFFFF11,
    kCmdToggle = 0xFFFFFF12,
    kCmdGauge = 0xFFFFFF13,
    kCmdClock = 0xFFFFFF14,
    kCmdCalibrate = 0xFFFFFF15,
    kCmdSpinner = 0xFFFFFF16,
    kCmdStop = 0xFFFFFF17,
    kCmdMemcrc = 0xFFFFFF18,
    kCmdRegread = 0xFFFFFF19,
    kCmdMemwrite = 0xFFFFFF1A,
    kCmdMemset = 0xFFFFFF1B,
    kCmdMemzero = 0xFFFFFF1C,
    kCmdMemcpy = 0xFFFFFF1D,
    kCmdAppend = 0xFFFFFF1E,
    kCmdSnapshot = 0xFFFFFF1F,
    kCmdTouchTransform = 0xFFFFFF20,
    kCmdInflate = 0xFFFFFF22,
    kCmdGetptr = 0xFFFFFF23,
    kCmdLoadimage = 0xFFFFFF24,
    kCmdGetprops = 0xFFFFFF25,
    kCmdLoadidentity = 0xFFFFFF26,
    kCmdTranslate = 0xFFFFFF27,
    kCmdScale = 0xFFFFFF28,
    kCmdRotate = 0xFFFFFF29,
    kCmdSetmatrix = 0xFFFFFF2A,
    kCmdSetfont = 0xFFFFFF2B,
    kCmdTrack = 0xFFFFFF2C,
    kCmdDial = 0xFFFFFF2D,
    kCmdNumber = 0xFFFFFF2E,
    kCmdScreensaver = 0xFFFFFF2F,
    kCmdSketch = 0xFFFFFF30,
    kCmdLogo = 0xFFFFFF31,
    kCmdColdstart = 0xFFFFFF32,
    kCmdGetmatrix = 0xFFFFFF33,
    kCmdGradcolor = 0xFFFFFF34,
    kCmdSetrotate = 0xFFFFFF36,
    kCmdSnapshot2 = 0xFFFFFF37,
    kCmdSetbase = 0xFFFFFF38,
    kCmdMediafifo = 0xFFFFFF39,
    kCmdPlayvideo = 0xFFFFFF3A,
    kCmdSetfont2 = 0xFFFFFF3B,
    kCmdSetscratch = 0xFFFFFF3C,
    kCmdRomfont = 0xFFFFFF3F,
    kCmdVideostart = 0xFFFFFF40,
    kCmdVideoframe = 0xFFFFFF41,
    kCmdSetbitmap = 0xFFFFFF43,
};

constexpr uint32_t kCommandPrefixMask = 0xFFFFFF00; // coprocessor commands are 0xFFFFFFxx
constexpr uint32_t kCommandPrefix = 0xFFFFFF00;
constexpr uint32_t kRingWordBytes = 4;
constexpr uint32_t kRingPointerMask = 0xFFC;        // ring pointers are word offsets
constexpr uint32_t kFaultReadPointer = 0xFFF;       // REG_CMD_READ after a fault [PG §5.6]

// Interrupt flags the coprocessor raises (spec §5.4).
constexpr uint32_t kIntCmdEmpty = 0x20;
constexpr uint32_t kIntCmdFlag = 0x40;

// Default coprocessor state [PG §5.7 Table 12].
constexpr uint32_t kDefaultBgColor = 0x002040;
constexpr uint32_t kDefaultFgColor = 0x003870;
constexpr uint32_t kDefaultGradColor = 0xFFFFFF;
constexpr uint32_t kDefaultScratchHandle = 15;
constexpr uint32_t kDefaultNumberBase = 10;
constexpr int32_t kFixedOne = 0x10000; // 1.0 in 16.16

// Granularity of a long command when its cost is not zero: the coprocessor applies it in
// pieces of this many bytes, so REG_CMD_READ and memory move as the cost elapses. With all
// costs zero a command runs in one piece.
constexpr uint32_t kCoproStepBytes = 256;
// Size of the buffer that takes decoder output before it is written to memory.
constexpr uint32_t kWorkBufferSize = 4096;

struct StepPlan
{
    bool ready;      // false: the command waits (data, swap)
    uint32_t units;  // size of the step
    uint64_t cost;   // clocks before it applies
};

inline StepPlan NotReady()
{
    return StepPlan{false, 0, 0};
}

inline uint64_t Cost(uint64_t units, uint32_t perUnit)
{
    return units * perUnit;
}

// Step size: everything when the per-unit cost is 0, else kCoproStepBytes at most.
inline uint32_t StepUnits(uint32_t remaining, uint32_t perUnit)
{
    return (perUnit == 0 || remaining < kCoproStepBytes) ? remaining : kCoproStepBytes;
}

// --- Ring access (eve-copro.cpp) ---------------------------------------------------------

uint32_t RingRead(const EveChip& chip);           // REG_CMD_READ as a ring offset
uint32_t RingWordsAvailable(const EveChip& chip); // whole words between READ and WRITE
uint32_t RingWordAt(const EveChip& chip, uint32_t wordIndex); // word at READ + 4 x index
// Data bytes of the command in flight that are in the ring (after the consumed part).
uint32_t RingDataAvailable(const EveChip& chip);
uint8_t RingDataByte(const EveChip& chip, uint32_t index);
// Copy up to size data bytes; the ring may wrap. Returns the bytes copied.
uint32_t RingDataCopy(const EveChip& chip, uint8_t* out, uint32_t size);
void ConsumeRingData(EveChip& chip, uint32_t bytes); // REG_CMD_READ moves past whole words
void AlignRing(EveChip& chip);                        // skip the padding of the last word
void WriteRingResult(EveChip& chip, uint32_t paramIndex, uint32_t value);

// --- Effects (eve-copro.cpp) ---------------------------------------------------------------

bool WriteDlWord(EveChip& chip, uint32_t word);   // false: faulted
void WriteRegister32(EveChip& chip, Reg reg, uint32_t value); // with side effects
void CompleteCommand(EveChip& chip);
void CoproFaultNow(EveChip& chip, CoproFault fault);
void CoproDefaults(EveChip& chip);

// --- Generated display list words (eve-copro.cpp) ----------------------------------------
//
// A command that produces display list words writes them into the INFLIGHT region first
// (c.total of them), then emits them as their cost elapses (c.done so far).

bool QueueDlWord(EveChip& chip, uint32_t word);   // false: faulted (INFLIGHT full)
StepPlan EmitPlan(EveChip& chip);
void EmitApply(EveChip& chip, uint32_t units);     // completes the command at the end
uint32_t ReadMemory32(const EveChip& chip, uint32_t address); // RAM_G or ROM, no side effect

// --- Matrix commands (eve-copro-matrix.cpp) ----------------------------------------------

bool MatrixBegin(EveChip& chip);                  // true: complete

// --- Text and fonts (eve-copro-text.cpp) -------------------------------------------------

bool TextBegin(EveChip& chip);                    // true: complete
StepPlan TextPlan(EveChip& chip);
void TextApply(EveChip& chip, uint32_t units);

// --- Media FIFO and images (eve-copro-media.cpp) --------------------------------------------

bool MediaBegin(EveChip& chip);                   // true: complete
StepPlan MediaPlan(EveChip& chip);
void MediaApply(EveChip& chip, uint32_t units);
void MediaStateLoaded(EveChip& chip);

// --- Memory commands and INFLATE (eve-copro-mem.cpp) --------------------------------------

bool MemBegin(EveChip& chip);                     // true: complete
StepPlan MemPlan(EveChip& chip);
void MemApply(EveChip& chip, uint32_t units);
uint8_t* InflateState(EveChip& chip);             // the decoder state of the command in flight
void InflateRestore(EveChip& chip);               // re-run a non-plain decoder after a restore
uint32_t Crc32Update(uint32_t crc, const uint8_t* data, size_t size);

} // namespace EveLib
