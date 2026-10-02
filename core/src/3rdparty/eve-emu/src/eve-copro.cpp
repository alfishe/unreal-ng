// eve-emu - ring reader, dispatch, cost model, faults (spec §7.1-7.4, arch §8.3).
//
// The coprocessor is a state machine. It plans its next step (a display list word, the
// start of a command, or the next piece of the command in flight) together with the
// step's cost, waits until the cost has elapsed, then applies the step. With all costs
// zero it runs everything that is in the ring at once.
#include "eve-copro.h"

#include <initializer_list>

namespace EveLib
{

namespace
{

enum class CommandKind : uint8_t
{
    Unknown,   // fault (design rule: a code the emulator does not implement)
    Core,      // handled here
    Memory,    // eve-copro-mem.cpp
    Matrix,    // eve-copro-matrix.cpp
    Text,      // eve-copro-text.cpp
    Media,     // eve-copro-media.cpp
    NoEffect,  // touch related: completes without effect (spec §7.5)
    Hang,      // never completes (BT8XX: CMD_HAMMERAUX)
    HangInPlace // never completes, REG_CMD_READ stays on its code (BT8XX: CMD_EXECUTE)
};

struct CommandInfo
{
    uint8_t fixedWords; // parameter words after the command word
    CommandKind kind;
};

constexpr uint32_t kCommandCodes = 0x100; // 0xFFFFFF00 ... 0xFFFFFFFF

constexpr uint32_t CodeIndex(uint32_t code)
{
    return code & ~kCommandPrefixMask;
}

struct CommandTable
{
    CommandInfo info[kCommandCodes];
};

constexpr CommandTable BuildCommandTable()
{
    CommandTable t{};
    auto set = [&t](uint32_t code, uint8_t words, CommandKind kind) { t.info[CodeIndex(code)] = {words, kind}; };
    set(kCmdDlstart, 0, CommandKind::Core);
    set(kCmdSwap, 0, CommandKind::Core);
    set(kCmdInterrupt, 1, CommandKind::Core);
    set(kCmdBgcolor, 1, CommandKind::Core);
    set(kCmdFgcolor, 1, CommandKind::Core);
    set(kCmdGradcolor, 1, CommandKind::Core);
    set(kCmdColdstart, 0, CommandKind::Core);
    set(kCmdSetbase, 1, CommandKind::Core);
    set(kCmdSetscratch, 1, CommandKind::Core);
    set(kCmdGetprops, 3, CommandKind::Core);
    set(kCmdSetrotate, 1, CommandKind::Core);
    set(kCmdMemcrc, 3, CommandKind::Memory);
    set(kCmdRegread, 2, CommandKind::Memory);
    set(kCmdMemwrite, 2, CommandKind::Memory);
    set(kCmdMemset, 3, CommandKind::Memory);
    set(kCmdMemzero, 2, CommandKind::Memory);
    set(kCmdMemcpy, 3, CommandKind::Memory);
    set(kCmdAppend, 2, CommandKind::Memory);
    set(kCmdInflate, 1, CommandKind::Memory);
    set(kCmdGetptr, 1, CommandKind::Memory);
    set(kCmdCalibrate, 1, CommandKind::NoEffect);
    set(kCmdTrack, 3, CommandKind::NoEffect);
    // FT80x layout: six points, six targets, result. Not in the FT81x PG (TO VERIFY).
    set(kCmdTouchTransform, 13, CommandKind::NoEffect);
    // Undocumented codes (spec §7.5, V15), as BT8XX runs them (golden cases
    // cmd-undocumented-*): these complete without effect (parameters, if any, unknown:
    // none taken); CMD_HAMMERAUX hangs past its code, CMD_EXECUTE hangs on its code;
    // 0x45, 0x46, 0x4E, 0x50, 0x80 fault. Codes not measured fault (TO VERIFY).
    constexpr uint32_t kCmdHammeraux = 0xFFFFFF04, kCmdExecute = 0xFFFFFF07;
    constexpr uint32_t kNoEffectCodes[] = {0xFFFFFF03, 0xFFFFFF35, 0xFFFFFF3D, 0xFFFFFF42, 0xFFFFFF44,
                                           0xFFFFFF47, 0xFFFFFF48, 0xFFFFFF49, 0xFFFFFF4A, 0xFFFFFF4B,
                                           0xFFFFFF4C, 0xFFFFFF4D, 0xFFFFFF4F, 0xFFFFFF60, 0xFFFFFFFF};
    for (uint32_t code : kNoEffectCodes)
        set(code, 0, CommandKind::NoEffect);
    set(kCmdHammeraux, 0, CommandKind::Hang);
    set(kCmdExecute, 0, CommandKind::HangInPlace);
    set(kCmdLoadidentity, 0, CommandKind::Matrix);
    set(kCmdTranslate, 2, CommandKind::Matrix);
    set(kCmdScale, 2, CommandKind::Matrix);
    set(kCmdRotate, 1, CommandKind::Matrix);
    set(kCmdSetmatrix, 0, CommandKind::Matrix);
    set(kCmdGetmatrix, 6, CommandKind::Matrix);
    set(kCmdText, 2, CommandKind::Text);
    set(kCmdNumber, 3, CommandKind::Text);
    set(kCmdSetfont, 2, CommandKind::Text);
    set(kCmdSetfont2, 3, CommandKind::Text);
    set(kCmdRomfont, 2, CommandKind::Text);
    set(kCmdSetbitmap, 3, CommandKind::Text);
    set(kCmdGradient, 4, CommandKind::Text);
    set(kCmdMediafifo, 2, CommandKind::Media);
    set(kCmdLoadimage, 2, CommandKind::Media);
    set(kCmdPlayvideo, 1, CommandKind::Media);
    set(kCmdVideostart, 0, CommandKind::Media);
    set(kCmdVideoframe, 2, CommandKind::Media);
    return t;
}

constexpr CommandTable kCommands = BuildCommandTable();

const CommandInfo& InfoFor(uint32_t code)
{
    static constexpr CommandInfo kUnknown{0, CommandKind::Unknown};
    if ((code & kCommandPrefixMask) != kCommandPrefix || CodeIndex(code) >= kCommandCodes)
        return kUnknown;
    return kCommands.info[CodeIndex(code)];
}

uint32_t RingWrite(const EveChip& chip)
{
    return RegGet(chip, Reg::CmdWrite) & kRingPointerMask;
}

void SetRingRead(EveChip& chip, uint32_t offset)
{
    RegSet(chip, Reg::CmdRead, offset & kRingPointerMask);
}

uint32_t RingByteAt(const EveChip& chip, uint32_t offset)
{
    return chip.regions[RegionCmd].base[offset & kRamCmdMask];
}

// --- Core commands ----------------------------------------------------------------------

bool SwapPending(const EveChip& chip)
{
    return RegGet(chip, Reg::Dlswap) != 0;
}

bool CoreBegin(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    switch (c.command)
    {
    case kCmdSwap:
        WriteRegister32(chip, Reg::Dlswap, 2);
        return true;
    case kCmdBgcolor:
        c.bgColor = c.params[0] & 0xFFFFFF;
        return true;
    case kCmdFgcolor:
        c.fgColor = c.params[0] & 0xFFFFFF;
        return true;
    case kCmdGradcolor:
        c.gradColor = c.params[0] & 0xFFFFFF;
        return true;
    case kCmdColdstart:
        CoproDefaults(chip);
        return true;
    case kCmdSetbase:
        c.numberBase = c.params[0];
        return true;
    case kCmdSetscratch:
        c.scratchHandle = c.params[0];
        return true;
    case kCmdGetprops:
        WriteRingResult(chip, 0, c.imageAddress);
        WriteRingResult(chip, 1, c.imageWidth);
        WriteRingResult(chip, 2, c.imageHeight);
        return true;
    case kCmdSetrotate:
        WriteRegister32(chip, Reg::Rotate, c.params[0]);
        return true;
    default:
        return false; // DLSTART, INTERRUPT: planned steps
    }
}

StepPlan CorePlan(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    switch (c.command)
    {
    case kCmdDlstart:
        // Waits until the current display list is scanned out [PG §5.11].
        if (SwapPending(chip))
        {
            c.phase = EVE_COPRO_WAITING_SWAP;
            return NotReady();
        }
        return StepPlan{true, 0, 0};
    case kCmdInterrupt:
    {
        // The delay is measured by the host's statement of the clock (REG_FREQUENCY).
        const uint64_t frequency = RegGet(chip, Reg::Frequency);
        return StepPlan{true, 0, c.params[0] * frequency / kMillisecondsPerSecond};
    }
    default:
        return StepPlan{true, 0, 0};
    }
}

void CoreApply(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    if (c.command == kCmdDlstart)
    {
        RegSet(chip, Reg::CmdDl, 0);
        c.displayListFull = 0;
    }
    else if (c.command == kCmdInterrupt)
        RaiseInterrupt(chip, kIntCmdFlag);
    CompleteCommand(chip);
}

// --- Dispatch ---------------------------------------------------------------------------

bool BeginCommand(EveChip& chip)
{
    switch (InfoFor(chip.state.copro.command).kind)
    {
    case CommandKind::Core:
        return CoreBegin(chip);
    case CommandKind::Memory:
        return MemBegin(chip);
    case CommandKind::Matrix:
        return MatrixBegin(chip);
    case CommandKind::Text:
        return TextBegin(chip);
    case CommandKind::Media:
        return MediaBegin(chip);
    case CommandKind::NoEffect:
        return true;
    case CommandKind::Hang:
    case CommandKind::HangInPlace:
        return false;
    default:
        return true;
    }
}

StepPlan PlanBody(EveChip& chip)
{
    switch (InfoFor(chip.state.copro.command).kind)
    {
    case CommandKind::Core:
        return CorePlan(chip);
    case CommandKind::Memory:
        return MemPlan(chip);
    case CommandKind::Matrix:
        return EmitPlan(chip);
    case CommandKind::Text:
        return TextPlan(chip);
    case CommandKind::Media:
        return MediaPlan(chip);
    case CommandKind::Hang:
    case CommandKind::HangInPlace:
        chip.state.copro.phase = EVE_COPRO_EXECUTING;
        return NotReady();
    default:
        return StepPlan{true, 0, 0};
    }
}

void ApplyBody(EveChip& chip, uint32_t units)
{
    switch (InfoFor(chip.state.copro.command).kind)
    {
    case CommandKind::Core:
        CoreApply(chip);
        break;
    case CommandKind::Memory:
        MemApply(chip, units);
        break;
    case CommandKind::Matrix:
        EmitApply(chip, units);
        break;
    case CommandKind::Text:
        TextApply(chip, units);
        break;
    case CommandKind::Media:
        MediaApply(chip, units);
        break;
    default:
        CompleteCommand(chip);
        break;
    }
}

StepPlan PlanNext(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    if (c.command != 0)
    {
        c.step = CoproStep::Body;
        return PlanBody(chip);
    }
    const uint32_t words = RingWordsAvailable(chip);
    if (words == 0)
    {
        c.phase = EVE_COPRO_IDLE;
        return NotReady();
    }
    const uint32_t word = RingWordAt(chip, 0);
    if ((word & kCommandPrefixMask) != kCommandPrefix)
    {
        // Anything that is not 0xFFFFFFxx is a display list word [PG §5.3].
        c.step = CoproStep::DisplayListWord;
        return StepPlan{true, 1, chip.costs.displayListWord};
    }
    const CommandInfo& info = InfoFor(word);
    if (info.kind != CommandKind::Unknown && words < 1u + info.fixedWords)
    {
        c.phase = EVE_COPRO_WAITING_DATA;
        return NotReady();
    }
    c.step = CoproStep::Start;
    return StepPlan{true, 1u + info.fixedWords, chip.costs.command};
}

void ApplyStep(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    const CoproStep step = c.step;
    const uint32_t units = c.stepUnits;
    c.step = CoproStep::None;
    c.stepUnits = 0;
    switch (step)
    {
    case CoproStep::DisplayListWord:
    {
        const uint32_t word = RingWordAt(chip, 0);
        SetRingRead(chip, RingRead(chip) + kRingWordBytes);
        if (WriteDlWord(chip, word) && RingWordsAvailable(chip) == 0)
            RaiseInterrupt(chip, kIntCmdEmpty);
        break;
    }
    case CoproStep::Start:
    {
        const uint32_t code = RingWordAt(chip, 0);
        const CommandInfo& info = InfoFor(code);
        c.command = code;
        c.commandAddress = RingRead(chip);
        if (info.kind == CommandKind::Unknown)
        {
            CoproFaultNow(chip, CoproFault::UnknownCommand);
            break;
        }
        for (uint32_t i = 0; i < info.fixedWords; ++i)
            c.params[i] = RingWordAt(chip, 1 + i);
        if (info.kind != CommandKind::HangInPlace)
            SetRingRead(chip, RingRead(chip) + kRingWordBytes * units);
        c.ringByteOffset = 0;
        if (BeginCommand(chip))
            CompleteCommand(chip);
        break;
    }
    case CoproStep::Body:
        ApplyBody(chip, units);
        break;
    case CoproStep::None:
        break;
    }
}

// Run the coprocessor with a budget of clocks: apply every step whose cost elapses within
// it, plan the next one, stop when the coprocessor waits or the budget is spent.
void Pump(EveChip& chip, uint64_t budget)
{
    if (chip.coproBusy)
        return; // a write of the coprocessor's own step kicked it
    chip.coproBusy = true;
    CoproState& c = chip.state.copro;
    for (;;)
    {
        if (c.phase == EVE_COPRO_FAULT || c.phase == EVE_COPRO_RESET)
            break;
        if (c.step != CoproStep::None)
        {
            if (c.stall > budget)
            {
                c.stall -= budget;
                break;
            }
            budget -= c.stall;
            c.stall = 0;
            ApplyStep(chip);
            FlushPendingRegister(chip);
            continue;
        }
        const StepPlan plan = PlanNext(chip);
        if (!plan.ready)
        {
            c.step = CoproStep::None;
            break;
        }
        c.phase = EVE_COPRO_EXECUTING;
        c.stepUnits = plan.units;
        c.stall = plan.cost;
    }
    chip.coproBusy = false;
}

} // namespace

// --- Ring access ------------------------------------------------------------------------------

uint32_t RingRead(const EveChip& chip)
{
    return RegGet(chip, Reg::CmdRead) & kRingPointerMask;
}

uint32_t RingWordsAvailable(const EveChip& chip)
{
    return ((RingWrite(chip) - RingRead(chip)) & kRamCmdMask) / kRingWordBytes;
}

uint32_t RingWordAt(const EveChip& chip, uint32_t wordIndex)
{
    const uint32_t offset = (RingRead(chip) + kRingWordBytes * wordIndex) & kRamCmdMask;
    return LoadLe32(chip.regions[RegionCmd].base + offset);
}

uint32_t RingDataAvailable(const EveChip& chip)
{
    return RingWordsAvailable(chip) * kRingWordBytes - chip.state.copro.ringByteOffset;
}

uint8_t RingDataByte(const EveChip& chip, uint32_t index)
{
    return static_cast<uint8_t>(RingByteAt(chip, RingRead(chip) + chip.state.copro.ringByteOffset + index));
}

uint32_t RingDataCopy(const EveChip& chip, uint8_t* out, uint32_t size)
{
    const uint32_t count = size < RingDataAvailable(chip) ? size : RingDataAvailable(chip);
    const uint32_t start = (RingRead(chip) + chip.state.copro.ringByteOffset) & kRamCmdMask;
    const uint32_t first = count < kRamCmdSize - start ? count : kRamCmdSize - start;
    std::memcpy(out, chip.regions[RegionCmd].base + start, first);
    std::memcpy(out + first, chip.regions[RegionCmd].base, count - first);
    return count;
}

void ConsumeRingData(EveChip& chip, uint32_t bytes)
{
    // Never past REG_CMD_WRITE: the coprocessor does not run ahead of the host (spec §7.2).
    const uint32_t available = RingDataAvailable(chip);
    bytes = bytes < available ? bytes : available;
    CoproState& c = chip.state.copro;
    const uint32_t consumed = c.ringByteOffset + bytes;
    SetRingRead(chip, RingRead(chip) + (consumed / kRingWordBytes) * kRingWordBytes);
    c.ringByteOffset = consumed % kRingWordBytes;
}

void AlignRing(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    if (c.ringByteOffset != 0)
        ConsumeRingData(chip, kRingWordBytes - c.ringByteOffset);
}

void WriteRingResult(EveChip& chip, uint32_t paramIndex, uint32_t value)
{
    // Output parameters go back into the ring at the parameter's own position [PG §5.16].
    Region& cmd = chip.regions[RegionCmd];
    const uint32_t offset = (chip.state.copro.commandAddress + kRingWordBytes * (1 + paramIndex)) & kRamCmdMask;
    StoreLe32(cmd.base + offset, value);
    cmd.MarkDirty(offset);
}

// --- Effects -----------------------------------------------------------------------------------

bool WriteDlWord(EveChip& chip, uint32_t word)
{
    // REG_CMD_DL has 13 bits: after command 2047 it wraps to 0, and the next word is a
    // display list longer than 2048 commands, a fault [PG §5.6].
    CoproState& c = chip.state.copro;
    const uint32_t cmdDl = RegGet(chip, Reg::CmdDl);
    if (c.displayListFull)
    {
        CoproFaultNow(chip, CoproFault::DisplayListOverflow);
        return false;
    }
    for (uint32_t i = 0; i < kRingWordBytes; ++i)
        BusWrite(chip, kRamDlBase + cmdDl + i, static_cast<uint8_t>(word >> (8 * i)));
    RegSet(chip, Reg::CmdDl, cmdDl + kRingWordBytes);
    if (cmdDl + kRingWordBytes >= kRamDlSize)
        c.displayListFull = 1;
    return true;
}

void WriteRegister32(EveChip& chip, Reg reg, uint32_t value)
{
    const uint32_t address = RegisterInfo(chip, reg).address;
    for (uint32_t i = 0; i < kRingWordBytes; ++i)
        BusWrite(chip, address + i, static_cast<uint8_t>(value >> (8 * i)));
    FlushPendingRegister(chip);
}

void CompleteCommand(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    AlignRing(chip);
    c.command = 0;
    c.commandAddress = 0;
    std::memset(c.params, 0, sizeof(c.params));
    c.done = 0;
    c.total = 0;
    c.crc = 0;
    c.inflightUsed = 0;
    c.inputBytes = 0;
    c.decoderDone = 0;
    c.generated = 0;
    c.stage = 0;
    c.scanPos = 0;
    c.scanEntropy = 0;
    // INT_CMDEMPTY: the last command in the ring has completed [PG §5.4].
    if (RingWordsAvailable(chip) == 0)
        RaiseInterrupt(chip, kIntCmdEmpty);
}

void CoproFaultNow(EveChip& chip, CoproFault fault)
{
    CoproState& c = chip.state.copro;
    c.phase = EVE_COPRO_FAULT;
    c.fault = fault;
    c.faultCommand = c.command;
    c.step = CoproStep::None;
    c.stall = 0;
    c.command = 0;
    c.ringByteOffset = 0;
    RegSet(chip, Reg::CmdRead, kFaultReadPointer);
    // BT8XX leaves a word at the start of RAM_CMD that depends on the fault (golden cases
    // fault-ring-*, cmd-inflate-raw), likely a ROM address; other faults leave RAM_CMD as
    // it is (TO VERIFY).
    constexpr uint32_t kUnknownCommandMark = 0x04F2, kInvalidStreamMark = 0x2074;
    constexpr uint32_t kBitsPerByte = 8;
    const uint32_t mark = fault == CoproFault::UnknownCommand  ? kUnknownCommandMark
                          : fault == CoproFault::InvalidStream ? kInvalidStreamMark
                                                               : 0;
    if (mark != 0)
        for (uint32_t i = 0; i < kRingWordBytes; ++i)
            BusWrite(chip, kRamCmdBase + i, static_cast<uint8_t>(mark >> (kBitsPerByte * i)));
    RaiseInterrupt(chip, kIntCmdEmpty);
}

void CoproDefaults(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    for (int32_t& m : c.matrix)
        m = 0;
    c.matrix[0] = kFixedOne;
    c.matrix[4] = kFixedOne;
    c.bgColor = kDefaultBgColor;
    c.fgColor = kDefaultFgColor;
    c.gradColor = kDefaultGradColor;
    c.scratchHandle = kDefaultScratchHandle;
    c.numberBase = kDefaultNumberBase;
    c.mediaFifoBase = 0;
    c.mediaFifoSize = 0;
    // Handles 16...31 -> ROM fonts 16...31 (spec §7.6).
    const uint32_t root = static_cast<uint32_t>(RomByte(chip, kRomFontRootAddress)) |
                          (static_cast<uint32_t>(RomByte(chip, kRomFontRootAddress + 1)) << 8) |
                          (static_cast<uint32_t>(RomByte(chip, kRomFontRootAddress + 2)) << 16) |
                          (static_cast<uint32_t>(RomByte(chip, kRomFontRootAddress + 3)) << 24);
    for (uint32_t handle = 0; handle < kHandleCount; ++handle)
    {
        c.fontPointers[handle] =
            handle >= kFirstFontHandle ? root + kFontMetricSize * (handle - kFirstRomFont) : 0;
        c.fontFirstChar[handle] = 0;
    }
}

// --- Generated display list words ------------------------------------------------------------

bool QueueDlWord(EveChip& chip, uint32_t word)
{
    CoproState& c = chip.state.copro;
    Region& inflight = chip.regions[RegionInflight];
    const uint32_t offset = c.total * kRingWordBytes;
    if (offset + kRingWordBytes > inflight.size)
    {
        CoproFaultNow(chip, CoproFault::InflightOverflow);
        return false;
    }
    StoreLe32(inflight.base + offset, word);
    inflight.MarkDirty(offset);
    ++c.total;
    c.inflightUsed = c.total * kRingWordBytes;
    c.generated = 1;
    return true;
}

StepPlan EmitPlan(EveChip& chip)
{
    const CoproState& c = chip.state.copro;
    const uint32_t units = StepUnits(c.total - c.done, chip.costs.displayListWord);
    return StepPlan{true, units, Cost(units, chip.costs.displayListWord)};
}

void EmitApply(EveChip& chip, uint32_t units)
{
    CoproState& c = chip.state.copro;
    const uint8_t* words = chip.regions[RegionInflight].base;
    for (uint32_t i = 0; i < units; ++i)
    {
        if (!WriteDlWord(chip, LoadLe32(words + kRingWordBytes * (c.done + i))))
            return;
    }
    c.done += units;
    if (c.done >= c.total)
        CompleteCommand(chip);
}

uint32_t ReadMemory32(const EveChip& chip, uint32_t address)
{
    uint32_t value = 0;
    for (uint32_t i = 0; i < kRingWordBytes; ++i)
        value |= static_cast<uint32_t>(BusPeek(chip, address + i)) << (8 * i);
    return value;
}

// --- Entry points ------------------------------------------------------------------------------

uint32_t CmdbSpace(const EveChip& chip)
{
    const uint32_t write = RegGet(chip, Reg::CmdWrite);
    const uint32_t read = RegGet(chip, Reg::CmdRead);
    return (kRamCmdSize - kRingWordBytes - ((write - read) & kRamCmdMask)) & (kRamCmdSize - 1);
}

void CmdbAppendWord(EveChip& chip, uint32_t word)
{
    const uint32_t write = RingWrite(chip);
    Region& cmd = chip.regions[RegionCmd];
    StoreLe32(cmd.base + write, word);
    cmd.MarkDirty(write);
    RegSet(chip, Reg::CmdWrite, (write + kRingWordBytes) & kRamCmdMask);
    CoproKick(chip);
}

void CoproReset(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    c = CoproState{};
    CoproDefaults(chip);
    c.phase = (RegGet(chip, Reg::CpuReset) & 1) ? EVE_COPRO_RESET : EVE_COPRO_IDLE;
    // An idle coprocessor with an empty ring reports INT_CMDEMPTY (BT8XX: REG_INT_FLAGS =
    // 0x20 after reset, golden case reset-registers).
    if (c.phase == EVE_COPRO_IDLE && RingWordsAvailable(chip) == 0)
        RaiseInterrupt(chip, kIntCmdEmpty);
}

void CoproHold(EveChip& chip, bool hold)
{
    CoproState& c = chip.state.copro;
    if (hold)
    {
        // The engine does nothing while held; the command in flight is abandoned.
        c.phase = EVE_COPRO_RESET;
        c.step = CoproStep::None;
        c.stall = 0;
        c.command = 0;
        c.ringByteOffset = 0;
        return;
    }
    // On release it starts at REG_CMD_READ with its state reset [PG §5.6, §5.7].
    const uint32_t cmdDl = RegGet(chip, Reg::CmdDl);
    CoproReset(chip);
    if (kRecoveryKeepsCmdDl)
        RegSet(chip, Reg::CmdDl, cmdDl);
    CoproKick(chip);
}

void CoproKick(EveChip& chip)
{
    if (ClockRunning(chip))
        Pump(chip, 0);
}

void CoproRun(EveChip& chip, uint64_t clocks)
{
    Pump(chip, clocks);
}

uint64_t CoproClocksToNextEvent(const EveChip& chip)
{
    const CoproState& c = chip.state.copro;
    return c.step != CoproStep::None ? c.stall : UINT64_MAX;
}

void CoproSwapDone(EveChip& chip)
{
    if (chip.state.copro.phase == EVE_COPRO_WAITING_SWAP)
        CoproKick(chip);
}

void GetCoproView(const EveChip& chip, EveCoproView& out)
{
    static const char* const kFaultText[] = {"", "unknown or unimplemented command", "display list overflow",
                                             "invalid inflate stream", "invalid image", "operation too large"};
    static_assert(sizeof(kFaultText) / sizeof(kFaultText[0]) == static_cast<size_t>(CoproFault::Count),
                  "fault text per CoproFault");
    const CoproState& c = chip.state.copro;
    out.phase = c.phase;
    out.command = c.command;
    out.commandAddress = c.commandAddress;
    out.costLeft = c.stall;
    out.faultCommand = c.faultCommand;
    out.faultReason = kFaultText[static_cast<size_t>(c.fault)];
    for (size_t i = 0; i < kMatrixSize; ++i)
        out.matrix[i] = c.matrix[i];
    for (size_t i = 0; i < kHandleCount; ++i)
        out.fontPointers[i] = c.fontPointers[i];
    out.scratchHandle = c.scratchHandle;
    out.numberBase = c.numberBase;
    out.mediaFifoBase = c.mediaFifoBase;
    out.mediaFifoSize = c.mediaFifoSize;
    out.bgColor = c.bgColor;
    out.fgColor = c.fgColor;
    out.gradColor = c.gradColor;
}

void CoproStateLoaded(EveChip& chip)
{
    chip.coproBusy = false;
    MediaStateLoaded(chip);
    if (chip.state.copro.command == kCmdInflate)
        InflateRestore(chip);
}

} // namespace EveLib
