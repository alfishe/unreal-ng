#pragma once

/// @file gscardanalysis.h
/// @brief The card CPU's analysis data of a General Sound card with a real Z80 (classic GS, the ZX-MultiSound's GS,
/// NeoGS): the code / data log (coverage, CDL), the RAM heat map and the reads of RAM never written
/// (gs-debugger requirements §4.13 X8, X10, X11; gui-card-debugger.md A5.2a tabs Coverage and Heat map).
///
/// **Keyed by the physical byte.** Two spaces: ROM (the classic 32 KB ROM; the NeoGS 512 KB flash) and RAM (the card's
/// fitted RAM). A CPU address is resolved through the card's mapping at the access, so NeoGS code copied from flash
/// into a RAM page and run there is that RAM page's code.
///
/// **Code / data log (X8).** One flag byte per physical byte (Flag). Marks are only ever OR-ed in, so runs
/// ("sessions") merge until a reset; the `Written` stamp is the exception: a card reset clears it (X11).
///
/// **Heat map (X10).** The card RAM in cells of `CellBytes()` bytes (16 bytes up to 1 MB of RAM, then whatever keeps
/// 65,536 cells: 32 at 2 MB, 64 at 4 MB), one heat byte per cell and channel (Channel). An access adds kHeatBump
/// (saturating at 255); the heat loses `decay` per host frame. The decay is lazy: each cell keeps the frame it was last
/// touched (stored + 1, 0 = never), the heat is decayed when the cell is touched again or read, so a frame end costs
/// nothing. Integer only.
///
/// **Never-written reads (X11).** A CPU read (data or instruction) of a RAM byte whose `Written` stamp is clear is
/// recorded once per byte with the instruction's PC, up to kMaxUninitRows rows; stamps start clear at a card reset and
/// when the analysis is created.
///
/// **Cost.** Nothing exists until the analysis is switched on (GeneralSoundCard::setAnalysisOn): the arrays are
/// allocated then (bounded by the card's ROM + RAM sizes, about 7 MB for a 4 MB NeoGS) and the card binds its
/// analysis bus callbacks at its next run. Off, the card runs its plain callbacks: no test on the memory path.
///
/// **Threads.** The machine thread writes; readers (the reports) read the arrays on their own thread without
/// stopping the machine. As in CoverageAnalyzer, the bytes are debug data: a reader sees each byte before or after a
/// mark, never anything else that matters; the never-written rows are guarded by a mutex (rare: once per byte).

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "3rdparty/unreal-z80/z80cpu.h"
#include "common/zeroinitbuffer.h"

class GSCardAnalysis
{
public:
    /// The CDL flags of a byte
    enum Flag : uint8_t
    {
        Exec = 0x01,     ///< the first byte of an executed instruction (an M1 opcode fetch at the instruction's PC)
        Operand = 0x02,  ///< another instruction-stream byte (prefix, opcode after a prefix, displacement, operand)
        Read = 0x04,     ///< read as data by the card CPU
        Write = 0x08,    ///< written (the card CPU, or a DMA master; a ROM / flash byte: the write was attempted)
        Jump = 0x10,     ///< an instruction reached other than by falling through (a jump, an interrupt entry)
        Sub = 0x20,      ///< an instruction reached by CALL / RST: a subroutine entry
        Dac = 0x40,      ///< fetched by a read of the DAC window #6000-#7FFF
        Written = 0x80,  ///< RAM written since the last card reset (the X11 stamp)
    };
    static constexpr uint8_t kCodeFlags = Exec | Operand;

    enum Space : uint8_t
    {
        Rom = 0,  ///< the ROM (classic, MultiSound) or the flash (NeoGS)
        Ram = 1,
    };

    /// The heat map's channels (the bus masters and kinds of access)
    enum Channel : uint8_t
    {
        Fetch = 0,  ///< card CPU instruction-stream reads
        DataRead,   ///< card CPU data reads
        DataWrite,  ///< card CPU writes
        DacFetch,   ///< reads of the DAC window
        SdDma,      ///< NeoGS SD-DMA writes and MP3-DMA reads
        ZxDma,      ///< NeoGS ZX-DMA reads and writes (the host's)
        ChannelCount
    };
    static const char* ChannelName(Channel channel);
    static bool ParseChannel(const std::string& name, Channel& out);

    static constexpr uint8_t kHeatBump = 32;
    static constexpr uint8_t kDefaultDecay = 8;  ///< a saturated cell goes dark in 32 frames
    static constexpr size_t kMaxUninitRows = 1024;

    /// One never-written read
    struct UninitRead
    {
        uint32_t offset = 0;  ///< the RAM byte (physical offset)
        uint16_t address = 0; ///< the CPU address it was read at
        uint16_t pc = 0;      ///< the instruction that read it
        uint64_t frame = 0;   ///< the host frame
        bool fetch = false;   ///< an instruction-stream read (code run from RAM never written)
    };

    /// `frameSource` the host's frame counter (EmulatorState::frame_counter), read on the machine thread at an
    /// access and by the reports; nullptr: frame 0 always
    GSCardAnalysis(size_t romBytes, size_t ramBytes, const uint64_t* frameSource);

    /// region <Machine thread: the marks (the card's analysis bus callbacks)>

    /// A card CPU read of `address` that landed on (`space`, `offset`); `m1` the core's instruction-stream flag
    inline void CpuRead(Space space, uint32_t offset, uint16_t address, uint8_t value, int m1, Z80CPU* cpu)
    {
        uint8_t* flag = FlagAt(space, offset);
        if (!flag) [[unlikely]]
            return;
        if (m1)
        {
            const uint16_t start = Z80CpuInstructionPc(cpu);
            if (address == start)
                InstructionStart(flag, address, value);
            else
            {
                *flag |= Operand;
                if (address == static_cast<uint16_t>(start + 1))
                    _op1 = value;
            }
            _nextSequential = static_cast<uint16_t>(address + 1);
            if (space == Ram)
            {
                Heat(Fetch, offset);
                if (!(*flag & Written)) [[unlikely]]
                    NoteUninit(offset, address, start, true);
            }
            return;
        }
        *flag |= Read;
        if (space == Ram)
        {
            Heat(DataRead, offset);
            if (!(*flag & Written)) [[unlikely]]
                NoteUninit(offset, address, Z80CpuInstructionPc(cpu), false);
        }
    }

    /// A card CPU write to (`space`, `offset`); `landed` false when the byte was dropped (a ROM window, write-protected
    /// RAM) or went to the flash chip's command decoder
    inline void CpuWrite(Space space, uint32_t offset, bool landed)
    {
        uint8_t* flag = FlagAt(space, offset);
        if (!flag) [[unlikely]]
            return;
        *flag |= landed && space == Ram ? static_cast<uint8_t>(Write | Written) : static_cast<uint8_t>(Write);
        if (space == Ram)
            Heat(DataWrite, offset);
    }

    /// A read of the DAC window that latched (`space`, `offset`) into a DAC
    inline void DacRead(Space space, uint32_t offset)
    {
        uint8_t* flag = FlagAt(space, offset);
        if (!flag) [[unlikely]]
            return;
        *flag |= Dac;
        if (space == Ram)
            Heat(DacFetch, offset);
    }

    /// Another bus master moved `length` RAM bytes from `offset` (wrapping at the RAM size): writes stamp the bytes
    void BusMaster(Channel channel, uint32_t offset, uint32_t length, bool write);

    /// The idle loop at RAM `offset` was slept through (GSCardRunner): its head cell keeps glowing in the fetch channel
    void IdleLoop(Space space, uint32_t offset)
    {
        if (space == Ram && offset < _ramBytes)
            Heat(Fetch, offset);
    }

    /// The card CPU was reset: the Written stamps and the never-written rows start again; the code / data marks stay
    void CardReset();

    /// endregion </Machine thread>

    /// region <Any thread: control and reads>

    /// Every mark, heat and row cleared (the analysis stays allocated)
    void Clear();
    void SetDecay(uint8_t perFrame) { _decay = perFrame; }
    uint8_t Decay() const { return _decay; }

    size_t RomBytes() const { return _romBytes; }
    size_t RamBytes() const { return _ramBytes; }
    size_t SpaceBytes(Space space) const { return space == Rom ? _romBytes : _ramBytes; }
    const uint8_t* Flags(Space space) const { return space == Rom ? _romFlags.data() : _ramFlags.data(); }
    size_t MemoryBytes() const;

    uint32_t CellBytes() const { return 1u << _cellShift; }
    uint32_t Cells() const { return _cells; }
    uint64_t Frame() const { return _frameSource ? *_frameSource : 0; }
    /// The heat of a cell decayed to the frame `now`
    uint8_t HeatAt(Channel channel, uint32_t cell, uint64_t now) const;
    /// The last frame the cell was touched in the channel; false: never
    bool LastTouch(Channel channel, uint32_t cell, uint64_t& frame) const;
    uint64_t Accesses(Channel channel) const { return _accesses[channel]; }

    /// The never-written rows (a copy), the rows not kept past kMaxUninitRows, the card resets seen
    std::vector<UninitRead> UninitRows(uint64_t& dropped) const;
    uint64_t CardResets() const { return _cardResets; }
    uint64_t CreatedFrame() const { return _createdFrame; }
    uint64_t LastResetFrame() const { return _lastResetFrame; }

    /// endregion </Any thread>

private:
    uint8_t* FlagAt(Space space, uint32_t offset)
    {
        if (space == Rom)
            return offset < _romBytes ? _romFlags.data() + offset : nullptr;
        return offset < _ramBytes ? _ramFlags.data() + offset : nullptr;
    }

    /// The first byte of an instruction: Exec, and how it was reached (the previous instruction's opcode decides a
    /// CALL / RST entry from a return or a jump)
    void InstructionStart(uint8_t* flag, uint16_t address, uint8_t opcode)
    {
        uint8_t mark = Exec;
        if (_havePrevious && address != _nextSequential && address != _previousStart)
            mark |= ReachedBy(_op0, _op1);
        *flag |= mark;
        _havePrevious = true;
        _previousStart = address;
        _op0 = opcode;
        _op1 = 0;
    }
    /// The mark for an instruction reached away from the fall-through, by the instruction with these first bytes
    static uint8_t ReachedBy(uint8_t op0, uint8_t op1);

    void Heat(Channel channel, uint32_t offset)
    {
        const size_t index = static_cast<size_t>(channel) * _cells + (offset >> _cellShift);
        const uint32_t stamp = static_cast<uint32_t>(Frame()) + 1;
        uint8_t heat = _heat[index];
        const uint32_t last = _heatFrame[index];
        if (last != stamp)
        {
            heat = Decayed(heat, stamp - last);
            _heatFrame[index] = stamp;
        }
        _heat[index] = heat > 255 - kHeatBump ? uint8_t(255) : static_cast<uint8_t>(heat + kHeatBump);
        _accesses[channel]++;
    }
    uint8_t Decayed(uint8_t heat, uint32_t frames) const
    {
        const uint64_t loss = static_cast<uint64_t>(frames) * _decay;
        return loss >= heat ? uint8_t(0) : static_cast<uint8_t>(heat - loss);
    }

    void NoteUninit(uint32_t offset, uint16_t address, uint16_t pc, bool fetch);

    const size_t _romBytes;
    const size_t _ramBytes;
    const uint64_t* const _frameSource;
    ZeroInitBuffer<uint8_t> _romFlags;
    ZeroInitBuffer<uint8_t> _ramFlags;

    // The way the last instruction was reached (machine thread)
    bool _havePrevious = false;
    uint16_t _previousStart = 0;
    uint16_t _nextSequential = 0;
    uint8_t _op0 = 0;
    uint8_t _op1 = 0;

    // Heat map
    uint32_t _cellShift = 4;
    uint32_t _cells = 0;
    uint8_t _decay = kDefaultDecay;
    ZeroInitBuffer<uint8_t> _heat;        ///< [channel * cells + cell]
    ZeroInitBuffer<uint32_t> _heatFrame;  ///< the frame last touched + 1 (0: never)
    uint64_t _accesses[ChannelCount] = {};

    // Never-written reads
    ZeroInitBuffer<uint8_t> _uninitSeen;  ///< a bit per RAM byte: its first read is recorded
    mutable std::mutex _uninitMutex;
    std::vector<UninitRead> _uninitRows;
    uint64_t _uninitDropped = 0;
    uint64_t _cardResets = 0;
    uint64_t _createdFrame = 0;
    uint64_t _lastResetFrame = 0;
};
