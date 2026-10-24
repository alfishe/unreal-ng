#pragma once

/// @file portreadwatch.h
/// @brief The guest's input port reads, counted per class per frame (retro-devices.md §6): the evidence the
/// library's heuristic picks the auto connector from (mapping-and-profiles.md §6.1 step 2). One table increment
/// per IN while it runs, nothing when off (the CPU tests the IN-result hook flag it already tests). The machine's
/// thread counts and folds; any thread reads the evidence.
///
/// Besides the ports: the bits of a half-row the code tests after the read (row bits: the code after the IN is
/// read, not run: the next `AND n`, `BIT b,r` or a right shift into a carry test within 8 instructions, once per
/// code address), reads of the AY's R14 (the chip counts them: the Timex sticks, the 128 keypad, the AY mouse and
/// the Magnum gun sit on port A) and of the Next's NR #B2 (the MD pads' X Y Z MODE: the port decoder counts them).

#include <array>
#include <cstdint>
#include <mutex>

/// The counted classes. Rows are the half-rows by their high byte: Row0 = #FEFE (CAPS..V) .. Row7 = #7FFE
/// (SPACE..B); RowsMulti = a #FE read with several address lines low
enum class PortReadClass : uint8_t
{
    Kempston,   ///< #1F
    Kempston2,  ///< #37 (Next)
    Fuller,     ///< #7F
    KMouse,     ///< #FADF / #FBDF / #FFDF
    Row0,
    Row1,
    Row2,
    Row3,
    Row4,
    Row5,
    Row6,
    Row7,
    RowsMulti,
    AyR14,   ///< the AY's data port read with R14 (I/O port A) selected
    NextB2,  ///< the Next's NR #B2 read through #253B (MD pads: X Y Z MODE)
    Count
};

constexpr size_t kPortReadClasses = static_cast<size_t>(PortReadClass::Count);
constexpr uint32_t kPortReadWindow = 50;  ///< frames folded into the evidence

/// "kempston", "kempston2", "fuller", "kmouse", "row.fe" .. "row.7f", "rows.multi", "ay.r14", "nr.b2"
const char* PortReadClassName(PortReadClass cls);

/// What the watch saw over its last window
struct PortReadEvidence
{
    bool active = false;
    uint32_t framesObserved = 0;  ///< since the watch started
    uint32_t window = 0;          ///< frames in the window (up to kPortReadWindow)
    std::array<uint16_t, kPortReadClasses> framesWithReads{};  ///< frames of the window with a read of the class
    std::array<float, kPortReadClasses> readsPerFrame{};       ///< average over the window
    /// Per half-row (Row0 .. Row7): the bits the code tested after its reads in the window (row-bits.<half-row>);
    /// 0 when none was seen (the code shifts in a way the sampler does not follow, or reads the row elsewhere)
    std::array<uint8_t, 8> rowBits{};

    uint16_t Frames(PortReadClass cls) const
    {
        return framesWithReads[static_cast<size_t>(cls)];
    }
    float Reads(PortReadClass cls) const
    {
        return readsPerFrame[static_cast<size_t>(cls)];
    }
    /// The tested bits of a half-row class (Row0 .. Row7); 0 for any other class
    uint8_t RowBits(PortReadClass cls) const
    {
        const unsigned row = static_cast<unsigned>(cls) - static_cast<unsigned>(PortReadClass::Row0);
        return row < rowBits.size() ? rowBits[row] : uint8_t{0};
    }
};

/// The bits of the IN's result the code after it tests (row bits, retro-devices.md §6): the code at @p next (the
/// instruction after the IN) read through @p read, without running it, up to 8 instructions. The IN is the one
/// ending at @p next (`IN A,(n)` or `IN r,(C)`); an `AND n` of A, a `BIT b` of the register and a right shift of A
/// followed by a carry test count; the scan stops at an unconditional jump, a call, a return, another IN. 0 when
/// nothing tests it there
using PortReadCodeReader = uint8_t (*)(const void* context, uint16_t address);
uint8_t TestedBitsAfterIn(PortReadCodeReader read, const void* context, uint16_t next);

class PortReadWatch
{
public:
    /// The CPU, after every IN (Z80::portReadWatch); @p next is the address after the IN instruction, for the row
    /// bits (with a code reader)
    void OnRead(uint16_t port, uint16_t next = 0)
    {
        const uint8_t low = static_cast<uint8_t>(port);
        if ((low & 0x01) == 0)
        {
            const uint8_t rows = static_cast<uint8_t>(~(port >> 8));
            if (rows == 0)
                return;  // no half-row selected: an EAR read
            if ((rows & (rows - 1)) != 0)
            {
                Count(PortReadClass::RowsMulti);
                return;
            }
            unsigned row = 0;
            while ((rows >> row) != 1)
                ++row;
            Count(static_cast<PortReadClass>(static_cast<unsigned>(PortReadClass::Row0) + row));
            if (_read != nullptr)
                SampleRowBits(row, next);
            return;
        }
        if (port == 0xFADF || port == 0xFBDF || port == 0xFFDF)
            Count(PortReadClass::KMouse);
        else if (low == 0x1F)
            Count(PortReadClass::Kempston);
        else if (low == 0x37)
            Count(PortReadClass::Kempston2);
        else if (low == 0x7F)
            Count(PortReadClass::Fuller);
    }

    /// The AY chip, on a bus read of R14; the Next's port decoder, on a read of NR #B2
    void OnRegisterRead(PortReadClass cls)
    {
        Count(cls);
    }
    /// Where the row bits read the guest's code (the machine's memory as the CPU sees it, no side effects);
    /// nullptr (the default) samples no row bits. Set on the machine's thread
    void SetCodeReader(PortReadCodeReader read, const void* context)
    {
        _read = read;
        _readContext = context;
        _sampledAt.fill(kNotSampled);
    }

    /// The machine's thread, at the frame boundary: the frame's counts go into the window
    void EndFrame();
    /// Clear the window and count from zero
    void Reset();
    PortReadEvidence Evidence() const;

private:
    void Count(PortReadClass cls)
    {
        uint16_t& counter = _frame[static_cast<size_t>(cls)];
        if (counter != UINT16_MAX)
            ++counter;
    }

    /// The row bits of a read of half-row @p row whose IN ends at @p next: one scan per code address (a polling
    /// loop reads at the same address thousands of times a frame)
    void SampleRowBits(unsigned row, uint16_t next);

    static constexpr uint32_t kNotSampled = 0x10000;

    std::array<uint16_t, kPortReadClasses> _frame{};  ///< the frame being counted (machine thread only)
    std::array<uint8_t, 8> _frameRowBits{};           ///< the frame's row bits (machine thread only)
    PortReadCodeReader _read = nullptr;
    const void* _readContext = nullptr;
    std::array<uint32_t, 8> _sampledAt{kNotSampled, kNotSampled, kNotSampled, kNotSampled,
                                       kNotSampled, kNotSampled, kNotSampled, kNotSampled};  ///< per row: the last scan
    std::array<uint8_t, 8> _sampledBits{};                                                    ///< and what it found
    mutable std::mutex _mutex;                        ///< guards what follows
    std::array<std::array<uint16_t, kPortReadClasses>, kPortReadWindow> _ring{};
    std::array<std::array<uint8_t, 8>, kPortReadWindow> _ringRowBits{};
    uint32_t _frames = 0;
};
