#pragma once

/// @file memorymarkup.h
/// @brief What the memory of the main CPU holds, from every source at once (docs/inprogress/2026-11-05-software-
/// metadata-profile, §3): regions of a kind (code, data, sprite, ...), entry points and comments, in layers. Where two
/// layers claim one byte the higher wins: user > profile > analysis > runtime > bundle; inside a layer the claim made
/// later. A claim names a page place (RAM / ROM / cache page and the offsets in it, wherever it is mapped) or a CPU
/// place (whatever the CPU sees there). Queries take CPU addresses and resolve them through the paging of the moment,
/// so a region in RAM 3 shows at #C000 only while RAM 3 is there.
///
/// Two layers fill themselves (no hot-path work of their own, both from what is gathered anyway):
///   - runtime: built at each query from the L0 access facts (analysis level `facts`: fetched bytes are code, an
///     opcode fetch an instruction start, data and stack accesses data) or, without them, from the code coverage
///     (an executed address is an instruction start). It follows the running program, without a revision step.
///   - analysis: the claims and decoded instructions of the instance's last L2 run (analysis::L1Jobs::Structure),
///     taken again when a new one is there. SetLayer(Analysis, ...) puts other content in its place until cleared.
///
/// @code
///   markup::MemoryMarkup& markup = *debugManager->GetMemoryMarkup();
///   markup.Mark(0x8000, 0x80FF, analysis::RegionKind::Data, "table");     // the user layer, the pages shown now
///   for (const markup::Span& span : markup.Ranges(0x8000, 0xBFFF)) ...   // flattened by priority
///   if (markup.IsInstructionStart(0x6A00)) ...                            // an anchor for the disassembler
/// @endcode
///
/// Thread safe: the layers are behind a lock; queries read the paging as the tools do (MapZ80AddressToPhysicalPage).

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/l2model.h"

class EmulatorContext;

namespace markup
{

/// Ascending priority: a later layer wins where claims overlap
enum class Layer : uint8_t
{
    Bundle,    ///< ROM symbol bundles
    Runtime,   ///< what the running program was seen doing
    Analysis,  ///< an L2 run's claims
    Profile,   ///< the matched software profile's `memory` section
    User,      ///< marked by hand, through the debugger and the planes
};
constexpr int kLayerCount = 5;
const char* LayerName(Layer layer);
bool ParseLayer(const std::string& text, Layer& layer);

enum class Space : uint8_t
{
    Cpu,    ///< the CPU's 64K as mapped at the time of the query
    Ram,
    Rom,
    Cache,  ///< cache RAM (ZX-Evo, TS-Conf, the Sprinter's fast RAM)
};

/// An inclusive range. A page place keeps offsets inside its 16K page (0x0000-0x3FFF) and never leaves it
struct Place
{
    Space space = Space::Cpu;
    uint16_t page = 0;
    uint16_t lo = 0;
    uint16_t hi = 0;

    bool operator==(const Place&) const = default;
};

struct Claim
{
    Place place;
    analysis::RegionKind kind = analysis::RegionKind::Data;
    std::string title;
    uint16_t confidence = 1000;  ///< 0-1000, as the analysis claims
    /// The SHA-256 of the bytes (lower-case hex) when given: the claim holds only while the bytes at the place match
    /// (another level loaded, the program not unpacked yet)
    std::string sha256;
    /// The rest of a mark that crossed a window: its first byte starts nothing (no instruction start there)
    bool continues = false;
};

struct Note
{
    Place place;  ///< one address
    std::string text;
};

/// What one layer holds; `source` names where it came from ("profile:skool-daze", "user")
struct LayerContent
{
    std::string source;
    std::vector<Claim> claims;
    std::vector<Place> entries;  ///< instruction starts known for certain (one address each)
    std::vector<Note> comments;
};

/// A stretch of CPU addresses one claim wins
struct Span
{
    uint16_t lo = 0;
    uint16_t hi = 0;
    analysis::RegionKind kind = analysis::RegionKind::Data;
    Layer layer = Layer::User;
    std::string source;
    std::string title;
    uint16_t confidence = 1000;
    bool claimStart = false;  ///< `lo` is the claim's first byte (not a part after a higher claim, a window or the query)
};

struct CommentAt
{
    uint16_t address = 0;
    Layer layer = Layer::User;
    std::string text;
};

/// The analysis layer of an L2 run: its regions on their pages (the untouched filler left out: no evidence, it
/// would hide the runtime layer under it) and its decoded instructions as entries
LayerContent FromStructure(const analysis::L2Result& l2);

class MemoryMarkup
{
public:
    explicit MemoryMarkup(EmulatorContext* context);

    /// Replace a layer (empty content clears it; the analysis layer then shows the last L2 run again)
    void SetLayer(Layer layer, LayerContent content);
    void ClearLayer(Layer layer);
    /// A layer's content: for the analysis layer what shows (set, or the L2 run's); the runtime layer only what was
    /// set (the derived part exists per query)
    LayerContent GetLayer(Layer layer) const;

    /// The user layer: mark CPU addresses @p lo..@p hi as @p kind. The claim is made on the pages the CPU shows now
    /// (one claim per window the range crosses), so it follows the page; @p cpuView: on the CPU addresses instead,
    /// whatever is mapped. False for a reversed range
    bool Mark(uint16_t lo, uint16_t hi, analysis::RegionKind kind, const std::string& title = "", bool cpuView = false);
    /// The user layer: forget its claims over CPU addresses @p lo..@p hi (the lower layers show again); the part of
    /// a claim outside the range stays
    void Unmark(uint16_t lo, uint16_t hi);
    /// The user layer's comment at a CPU address, on the page shown now (empty text removes it)
    void SetComment(uint16_t address, const std::string& text);

    /// The winning claim at a CPU address, none where no layer says anything
    std::optional<Span> At(uint16_t address) const;
    /// The CPU range @p from..@p to as stretches of one winning claim each, in address order; the stretches no layer
    /// claims are absent. Neighbours of one claim are one span
    std::vector<Span> Ranges(uint16_t from, uint16_t to) const;
    /// An instruction starts at the CPU address for certain: an entry (unless a higher layer claims the byte as no
    /// code), or the first byte of a winning code claim
    bool IsInstructionStart(uint16_t address) const;
    /// The instruction starts known in @p from..@p to, ascending (entries and code claims' first bytes)
    std::vector<uint16_t> Anchors(uint16_t from, uint16_t to) const;
    /// The comments in @p from..@p to, ascending; one per address, the highest layer's
    std::vector<CommentAt> Comments(uint16_t from, uint16_t to) const;

    /// Grows on every change of a layer (a consumer redraws when it moved)
    uint64_t Revision() const;

private:
    struct Window
    {
        uint16_t lo = 0;  ///< the CPU range of a mapping slot
        uint16_t hi = 0;
        Space space = Space::Cpu;
        uint16_t page = 0;
        uint16_t offset = 0;  ///< the page offset of `lo`
        bool mapped = false;
    };
    /// The layers a query reads: the runtime one with its derived part, the analysis one set or from L2
    struct View
    {
        View() = default;
        View(const View&) = delete;  ///< layers points into runtime
        View& operator=(const View&) = delete;
        LayerContent runtime;
        const LayerContent* layers[kLayerCount] = {};
    };

    /// The CPU slots covering @p from..@p to with the page each shows now
    std::vector<Window> WindowsOf(uint16_t from, uint16_t to) const;
    /// The layers for @p windows; @p derive: with the runtime layer's derived part (claims and entries)
    void ViewOf(const std::vector<Window>& windows, bool derive, View& view) const;
    /// The runtime claims and entries of @p windows from the L0 facts or the coverage
    void DeriveRuntime(const std::vector<Window>& windows, LayerContent& out) const;
    /// Take the instance's L2 result when a new one is there (under the lock)
    void RefreshStructureLocked() const;
    /// The CPU range a place covers inside a window, false when it shows nowhere there
    static bool InWindow(const Place& place, const Window& window, uint16_t& lo, uint16_t& hi);
    /// A claim's guard holds (no guard, or the bytes at its place match)
    bool GuardHolds(const Claim& claim) const;
    std::vector<Span> RangesLocked(const View& view, const std::vector<Window>& windows, uint16_t from,
                                   uint16_t to) const;

    EmulatorContext* _context = nullptr;
    mutable std::mutex _lock;
    LayerContent _layers[kLayerCount];
    mutable std::shared_ptr<const analysis::L2Result> _structure;  ///< the L2 run the analysis layer came from
    mutable LayerContent _fromStructure;
    mutable uint64_t _revision = 0;
};

}  // namespace markup
