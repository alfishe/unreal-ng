#pragma once

/// @file mediaundolog.h
/// @brief What a medium's units were before the guest (or the user's write-protect switch) changed them while a
/// time travel history exists, so a seek can set the medium to the version it had at any recorded point (TTD gaps
/// 11-12; owner decision 2026-10-10: an undo log, not the storage manager's versioned change layer).
///
/// A version is the medium's count of changes (MediaManager: a frame that wrote it, a write-protect toggle). The log
/// keeps, per version, each changed unit as it was before its first change in that version: a block sector, a floppy
/// track (its whole stream: a write may reformat it), the write-protect switch. Going back restores those and keeps
/// what the units held, so going forward again restores that. A change made after going back drops the versions
/// above (the history's future is gone, as TTD's own). The oldest versions go first past the budget: the medium can
/// no longer go back to them, and a seek there reports it.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <utility>
#include <variant>
#include <vector>

#include "unrealdisk/diskimage.h"

class MediaUndoLog
{
public:
    enum class Unit : uint8_t
    {
        Sector,   ///< a block medium's sector (index: the LBA)
        Track,    ///< a floppy track (index: cylinder * 2 + side)
        Protect,  ///< the slot's write-protect switch (index 0)
    };
    /// A unit's contents: a sector's bytes, a track's stream, the switch
    using Image = std::variant<std::vector<uint8_t>, unrealdisk::DiskImage::RawTrack, bool>;

    /// The medium the log sets back and forth
    class IUnits
    {
    public:
        virtual ~IUnits() = default;
        virtual bool Read(Unit unit, uint64_t index, Image& out) = 0;
        virtual bool Write(Unit unit, uint64_t index, const Image& image) = 0;
    };

    static constexpr size_t kDefaultBudget = size_t(64) << 20;

    explicit MediaUndoLog(size_t budget = kDefaultBudget) : _budget(budget) {}

    /// Nothing kept: the medium cannot go back before @p head
    void Reset(uint64_t head);
    /// A change after a seek back: the versions above @p head are gone
    void DropAbove(uint64_t head);
    /// The medium reached @p version (live: a change, whether it kept units or not)
    void Reach(uint64_t version);
    /// Whether @p unit already has its image in @p version (a later change of it in the same version keeps nothing)
    bool Has(uint64_t version, Unit unit, uint64_t index) const;
    /// @p unit as it was before its first change in @p version; @p after: what it became, when the change says so (a
    /// switch). @p frame: the frame the version belongs to
    void Keep(uint64_t version, uint64_t frame, Unit unit, uint64_t index, Image before,
              std::optional<Image> after = std::nullopt);

    /// Set the medium from @p head to @p target. False, with nothing changed: the log does not reach @p target
    bool Move(uint64_t head, uint64_t target, IUnits& units);
    /// Whether Move(@p head, @p target) would go ahead
    bool Reaches(uint64_t head, uint64_t target) const;

    /// The version after @p head, when it holds the write-protect switch alone and belongs to @p frame: a replay
    /// reaching that frame boundary sets the switch as the recording did (guest writes it runs again itself)
    std::optional<bool> SwitchAt(uint64_t head, uint64_t frame) const;

    /// The oldest and the newest version Move reaches
    uint64_t Floor() const { return _floor; }
    uint64_t Top() const { return _top; }
    size_t Entries() const { return _entries.size(); }
    size_t Bytes() const { return _bytes; }

private:
    struct Entry
    {
        uint64_t version = 0;
        uint64_t frame = 0;
        Unit unit = Unit::Sector;
        uint64_t index = 0;
        Image before;
        std::optional<Image> after;  ///< set when the entry went back once (or by Keep for a switch)
    };

    static size_t SizeOf(const Image& image);
    size_t SizeOf(const Entry& entry) const;
    /// Past the budget: the oldest versions go, and the floor rises
    void Trim();

    std::deque<Entry> _entries;  ///< ascending versions
    std::set<std::pair<Unit, uint64_t>> _newest;  ///< the units kept in the newest version (Has)
    uint64_t _newestVersion = 0;
    uint64_t _floor = 0;
    uint64_t _top = 0;
    size_t _bytes = 0;
    size_t _budget;
};
