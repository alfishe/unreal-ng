// eve-emu - memory commands, INFLATE, APPEND, MEMCRC (spec §7.5).
//
// Every command here keeps its whole progress in the coprocessor record (and, for
// INFLATE, the decoder state or its consumed input in the INFLIGHT region), so a restore
// continues it from the exact point reached (arch §7.3).
#include "eve-copro.h"

namespace EveLib
{

namespace
{

// CRC-32 of zlib / IEEE 802.3 (reflected polynomial). The exact variant of CMD_MEMCRC is
// TO VERIFY (spec V16).
constexpr uint32_t kCrcPolynomial = 0xEDB88320;
constexpr uint32_t kCrcInitial = 0xFFFFFFFF;
constexpr uint32_t kByteValues = 256;
constexpr uint32_t kBitsPerByte = 8;

struct CrcTable
{
    uint32_t entry[kByteValues];
};

constexpr CrcTable BuildCrcTable()
{
    CrcTable t{};
    for (uint32_t i = 0; i < kByteValues; ++i)
    {
        uint32_t c = i;
        for (uint32_t bit = 0; bit < kBitsPerByte; ++bit)
            c = (c & 1) ? (c >> 1) ^ kCrcPolynomial : c >> 1;
        t.entry[i] = c;
    }
    return t;
}

constexpr CrcTable kCrcTable = BuildCrcTable();

uint32_t Remaining(const CoproState& c)
{
    return c.total - c.done;
}

uint32_t Peek32(const EveChip& chip, uint32_t address)
{
    uint32_t value = 0;
    for (uint32_t i = 0; i < kRingWordBytes; ++i)
        value |= static_cast<uint32_t>(BusPeek(chip, address + i)) << (kBitsPerByte * i);
    return value;
}

// --- INFLATE ----------------------------------------------------------------------------

bool PlainDecoder(const EveChip& chip)
{
    return chip.inflate->stateIsPlain != 0;
}

void MarkDecoderStateDirty(EveChip& chip)
{
    if (PlainDecoder(chip))
        chip.regions[RegionInflight].MarkDirtyRange(0, static_cast<uint32_t>(chip.inflate->stateSize));
}

bool InflateBegin(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    const size_t stateSize = chip.inflate->stateSize;
    if (PlainDecoder(chip) && stateSize > chip.regions[RegionInflight].size)
    {
        CoproFaultNow(chip, CoproFault::InflightOverflow);
        return false;
    }
    chip.inflate->Begin(InflateState(chip), chip.inflate->user);
    MarkDecoderStateDirty(chip);
    c.inflightUsed = PlainDecoder(chip) ? static_cast<uint32_t>(stateSize) : 0;
    c.inputBytes = 0;
    c.decoderDone = 0;
    return false;
}

// Keep the consumed input of a decoder whose state cannot be saved (arch §7.3).
bool RecordInput(EveChip& chip, const uint8_t* data, uint32_t size)
{
    if (PlainDecoder(chip) || size == 0)
        return true;
    CoproState& c = chip.state.copro;
    Region& inflight = chip.regions[RegionInflight];
    if (c.inputBytes + size > inflight.size)
    {
        CoproFaultNow(chip, CoproFault::InflightOverflow);
        return false;
    }
    std::memcpy(inflight.base + c.inputBytes, data, size);
    inflight.MarkDirtyRange(c.inputBytes, size);
    c.inputBytes += size;
    c.inflightUsed = c.inputBytes;
    return true;
}

struct InflateResult
{
    EveDecodeStatus status;
    uint32_t inUsed;
    uint32_t outWritten;
};

// One decoder call over at most inputLimit ring bytes, output into the work buffer.
InflateResult InflateRun(EveChip& chip, void* state, uint32_t inputLimit)
{
    const uint32_t input = RingDataCopy(chip, chip.inputBuffer.get(), inputLimit);
    size_t inUsed = 0;
    size_t outWritten = 0;
    const EveDecodeStatus status = chip.inflate->Run(state, chip.inflate->user, chip.inputBuffer.get(), input,
                                                     &inUsed, chip.workBuffer.get(), kWorkBufferSize, &outWritten);
    return InflateResult{status, static_cast<uint32_t>(inUsed), static_cast<uint32_t>(outWritten)};
}

StepPlan InflatePlan(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    if (c.decoderDone)
        return StepPlan{true, 0, 0};
    const uint32_t available = RingDataAvailable(chip);
    if (available == 0)
    {
        c.phase = EVE_COPRO_WAITING_DATA;
        return NotReady();
    }
    const uint32_t perByte = chip.costs.inflatePerOutputByte;
    if (perByte == 0 || !PlainDecoder(chip))
        return StepPlan{true, available, 0};
    // The cost depends on the output: decode the step on a copy of the state to count it.
    const size_t stateSize = chip.inflate->stateSize;
    std::memcpy(chip.decoderState.get(), InflateState(chip), stateSize);
    const InflateResult r = InflateRun(chip, chip.decoderState.get(), available);
    return StepPlan{true, available, Cost(r.outWritten, perByte)};
}

void InflateApply(EveChip& chip, uint32_t inputLimit)
{
    CoproState& c = chip.state.copro;
    const uint32_t destination = c.params[0];
    const bool oneStep = chip.costs.inflatePerOutputByte != 0 && PlainDecoder(chip);
    while (!c.decoderDone)
    {
        const uint32_t limit = oneStep ? inputLimit : RingDataAvailable(chip);
        const InflateResult r = InflateRun(chip, InflateState(chip), limit);
        MarkDecoderStateDirty(chip);
        if (r.status == EVE_DECODE_ERROR)
        {
            CoproFaultNow(chip, CoproFault::InvalidStream);
            return;
        }
        if (!RecordInput(chip, chip.inputBuffer.get(), r.inUsed))
            return;
        ConsumeRingData(chip, r.inUsed);
        for (uint32_t i = 0; i < r.outWritten; ++i)
            BusWrite(chip, destination + c.done + i, chip.workBuffer[i]);
        c.done += r.outWritten;
        if (r.status == EVE_DECODE_OK)
            c.decoderDone = 1;
        if (oneStep || (r.inUsed == 0 && r.outWritten == 0))
            break;
    }
    if (c.decoderDone)
    {
        c.inflateEnd = destination + c.done;
        CompleteCommand(chip);
    }
}

} // namespace

uint32_t Crc32Update(uint32_t crc, const uint8_t* data, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        crc = kCrcTable.entry[(crc ^ data[i]) & (kByteValues - 1)] ^ (crc >> kBitsPerByte);
    return crc;
}

uint8_t* InflateState(EveChip& chip)
{
    return PlainDecoder(chip) ? chip.regions[RegionInflight].base : chip.decoderState.get();
}

void InflateRestore(EveChip& chip)
{
    // A plain decoder state came back with the INFLIGHT region. Any other decoder is run
    // again over the recorded input, silently: its output is already in memory.
    if (PlainDecoder(chip))
        return;
    const CoproState& c = chip.state.copro;
    void* state = chip.decoderState.get();
    chip.inflate->Begin(state, chip.inflate->user);
    // Feed the recorded input and drain all output, as the live run did: the decoder ends
    // in the same position with nothing pending.
    const uint8_t* input = chip.regions[RegionInflight].base;
    uint32_t pos = 0;
    for (;;)
    {
        size_t inUsed = 0;
        size_t outWritten = 0;
        const EveDecodeStatus status = chip.inflate->Run(state, chip.inflate->user, input + pos, c.inputBytes - pos,
                                                         &inUsed, chip.workBuffer.get(), kWorkBufferSize, &outWritten);
        pos += static_cast<uint32_t>(inUsed);
        if (status == EVE_DECODE_ERROR || (inUsed == 0 && outWritten == 0))
            break;
    }
}

bool MemBegin(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    c.done = 0;
    switch (c.command)
    {
    case kCmdRegread:
        WriteRingResult(chip, 1, Peek32(chip, c.params[0]));
        return true;
    case kCmdGetptr:
        WriteRingResult(chip, 0, c.inflateEnd);
        return true;
    case kCmdMemwrite:
    case kCmdMemzero:
    case kCmdAppend:
        c.total = c.params[1]; // ptr, num
        break;
    case kCmdMemcrc:
        c.total = c.params[1]; // ptr, num, result
        c.crc = kCrcInitial;
        break;
    case kCmdMemset:
    case kCmdMemcpy:
        c.total = c.params[2]; // ptr, value / src, num
        break;
    case kCmdInflate:
        return InflateBegin(chip);
    default:
        return true;
    }
    if (c.command == kCmdAppend)
        c.total &= ~(kRingWordBytes - 1); // num must be a multiple of 4 [PG §5.15]
    if (c.total == 0)
    {
        if (c.command == kCmdMemcrc)
            WriteRingResult(chip, 2, 0);
        return true;
    }
    return false;
}

StepPlan MemPlan(EveChip& chip)
{
    CoproState& c = chip.state.copro;
    switch (c.command)
    {
    case kCmdMemwrite:
    {
        const uint32_t available = RingDataAvailable(chip);
        const uint32_t remaining = Remaining(c);
        const uint32_t units = StepUnits(available < remaining ? available : remaining, chip.costs.memoryPerByte);
        if (units == 0)
        {
            c.phase = EVE_COPRO_WAITING_DATA;
            return NotReady();
        }
        return StepPlan{true, units, Cost(units, chip.costs.memoryPerByte)};
    }
    case kCmdMemcrc:
    {
        const uint32_t units = StepUnits(Remaining(c), chip.costs.memcrcPerByte);
        return StepPlan{true, units, Cost(units, chip.costs.memcrcPerByte)};
    }
    case kCmdInflate:
        return InflatePlan(chip);
    default:
    {
        const uint32_t units = StepUnits(Remaining(c), chip.costs.memoryPerByte);
        return StepPlan{true, units, Cost(units, chip.costs.memoryPerByte)};
    }
    }
}

void MemApply(EveChip& chip, uint32_t units)
{
    CoproState& c = chip.state.copro;
    const uint32_t destination = c.params[0] + c.done;
    switch (c.command)
    {
    case kCmdMemwrite:
    {
        uint32_t left = units;
        while (left > 0)
        {
            const uint32_t n = RingDataCopy(chip, chip.workBuffer.get(), left < kWorkBufferSize ? left : kWorkBufferSize);
            ConsumeRingData(chip, n);
            for (uint32_t i = 0; i < n; ++i)
                BusWrite(chip, c.params[0] + c.done + i, chip.workBuffer[i]);
            c.done += n;
            left -= n;
        }
        break;
    }
    case kCmdMemset:
    case kCmdMemzero:
    {
        const uint8_t value = c.command == kCmdMemset ? static_cast<uint8_t>(c.params[1]) : 0;
        for (uint32_t i = 0; i < units; ++i)
            BusWrite(chip, destination + i, value);
        c.done += units;
        break;
    }
    case kCmdMemcpy:
        // Forward, byte by byte (spec V16): an overlapping copy reads bytes it changed.
        for (uint32_t i = 0; i < units; ++i)
            BusWrite(chip, destination + i, BusPeek(chip, c.params[1] + c.done + i));
        c.done += units;
        break;
    case kCmdMemcrc:
        for (uint32_t i = 0; i < units; ++i)
        {
            const uint8_t byte = BusPeek(chip, destination + i);
            c.crc = Crc32Update(c.crc, &byte, 1);
        }
        c.done += units;
        if (c.done == c.total)
            WriteRingResult(chip, 2, ~c.crc);
        break;
    case kCmdAppend:
        for (uint32_t i = 0; i < units; i += kRingWordBytes)
            if (!WriteDlWord(chip, Peek32(chip, destination + i)))
                return;
        c.done += units;
        break;
    case kCmdInflate:
        InflateApply(chip, units);
        return;
    default:
        break;
    }
    if (c.done >= c.total)
        CompleteCommand(chip);
}

} // namespace EveLib
