#pragma once

/// @file portreadwatch.h
/// @brief The guest's input port reads, counted per class per frame (retro-devices.md §6): the evidence the
/// library's heuristic picks the auto connector from (mapping-and-profiles.md §6.1 step 2). One table increment
/// per IN while it runs, nothing when off (the CPU tests the IN-result hook flag it already tests). The machine's
/// thread counts and folds; any thread reads the evidence.

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
    Count
};

constexpr size_t kPortReadClasses = static_cast<size_t>(PortReadClass::Count);
constexpr uint32_t kPortReadWindow = 50;  ///< frames folded into the evidence

/// "kempston", "kempston2", "fuller", "kmouse", "row.fe" .. "row.7f", "rows.multi"
const char* PortReadClassName(PortReadClass cls);

/// What the watch saw over its last window
struct PortReadEvidence
{
    bool active = false;
    uint32_t framesObserved = 0;  ///< since the watch started
    uint32_t window = 0;          ///< frames in the window (up to kPortReadWindow)
    std::array<uint16_t, kPortReadClasses> framesWithReads{};  ///< frames of the window with a read of the class
    std::array<float, kPortReadClasses> readsPerFrame{};       ///< average over the window

    uint16_t Frames(PortReadClass cls) const
    {
        return framesWithReads[static_cast<size_t>(cls)];
    }
    float Reads(PortReadClass cls) const
    {
        return readsPerFrame[static_cast<size_t>(cls)];
    }
};

class PortReadWatch
{
public:
    /// The CPU, after every IN (Z80::portReadWatch)
    void OnRead(uint16_t port)
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

    std::array<uint16_t, kPortReadClasses> _frame{};  ///< the frame being counted (machine thread only)
    mutable std::mutex _mutex;                        ///< guards what follows
    std::array<std::array<uint16_t, kPortReadClasses>, kPortReadWindow> _ring{};
    uint32_t _frames = 0;
};
