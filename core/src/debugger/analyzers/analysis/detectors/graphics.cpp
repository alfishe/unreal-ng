/// The graphics detector: fonts, sprites and tiles, from the pictures the drawing passes drew (L1 drawn frames,
/// FramePieces), else from what the blit loops (media.h) drew.
///
/// Sprite banks first (SpriteBanks: records of a size header and the rows, back to back): their records are claimed
/// whole, the frames inside them are theirs. A frame inside a generated table (a mirror table) is no picture.
///
/// Every frame start, grouped by shape (bytes a row, rows, mask, pitch), sorted, becomes objects: frames a
/// constant step apart (the frame size, or up to 8 bytes more: a tile's attribute before its rows) are one object;
/// a start inside the previous frame is a partial draw and is dropped. Two neighbouring frames split where the code
/// named one of them with a pointer (an immediate, a page byte, a word whose value addressed it) and named neither
/// pair with one pointer, and at the edge of a buffer (the records detector's copy destinations). A record array
/// with the same step around the frames gives the object its extent (frames the replay did not draw included).
///
/// Kinds: a tile when a loop around the blit walked a map one byte a pass; a font when 8 x 8 frames sit on the
/// lattice of one character set: base + 8 * code (base + code for a set stored a pixel row a page on) for the
/// characters of the text the detectors found (at least three, three quarters of the frames, not one run of
/// consecutive codes; no font when the walk around each of its blits read a map and no text), the claim then covers
/// the codes drawn, the ROM's set all 96 characters 32..127 (neighbouring objects on one lattice of glyphs, gaps of at
/// most 32 codes, are tried as one set too: a pointer into the set splits its glyphs, and each part alone is a run of
/// consecutive codes); 8 x 8 frames in RAM the ROM's character loop
/// (PR_ALL) drew are a font with no text needed: the 21 user-defined graphics at (UDG), the 96 characters at (CHARS) +
/// 256, or else the frames drawn;
/// a sprite otherwise. An object inside a buffer is also claimed in each of the buffer's sources at the same offset:
/// observed where the copy provenance of the bytes the blit read shows that source, inferred otherwise.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <tuple>

#include "debugger/analyzers/analysis/detectors/detectors.h"
#include "debugger/analyzers/analysis/detectors/media.h"

namespace analysis
{

namespace
{

using media::Blit;

constexpr uint16_t kPrAll = 0x0B7F;     ///< the 48K ROM's PR_ALL: a character's 8 rows to the display
constexpr uint16_t kPrAllEnd = 0x0BDB;  ///< PO_ATTR follows

struct Shape
{
    uint16_t rowBytes = 0;
    uint16_t height = 0;
    bool mask = false;
    bool columns = false;  ///< stored a column at a time (the bytes of one column top to bottom, then the next)
    uint16_t pitch = 0;    ///< bytes from one row's (column's) start to the next; 0: the row's (column's) own bytes
    bool operator<(const Shape& o) const
    {
        return std::tie(rowBytes, height, mask, columns, pitch) <
               std::tie(o.rowBytes, o.height, o.mask, o.columns, o.pitch);
    }
    /// The bytes one frame spans
    uint32_t Bytes() const
    {
        const uint32_t pair = mask ? 2u : 1u;
        const uint32_t lines = columns ? rowBytes : height;
        const uint32_t length = columns ? height : rowBytes;
        const uint32_t step = pitch != 0 ? pitch : length * pair;
        return lines == 0 ? 0 : (lines - 1u) * step + length * pair;
    }
    /// The bytes of one line (a row, or a column in layout "columns"), the mask bytes included
    uint32_t Line() const { return (columns ? height : rowBytes) * (mask ? 2u : 1u); }
    /// From one glyph of a character set to the next: 8 bytes back to back, or one byte (each pixel row a page on)
    uint32_t GlyphStep() const { return pitch != 0 ? 1u : 8u; }
    /// From a frame's first byte to its last
    uint32_t Tail() const { return Bytes() - 1u; }
    /// The offsets of a frame's bytes from its start (the mask bytes included)
    std::vector<uint32_t> Offsets() const
    {
        const uint32_t pair = mask ? 2u : 1u;
        const uint32_t lines = columns ? rowBytes : height;
        const uint32_t length = columns ? height : rowBytes;
        const uint32_t step = pitch != 0 ? pitch : length * pair;
        std::vector<uint32_t> out;
        for (uint32_t l = 0; l < lines; ++l)
            for (uint32_t c = 0; c < length * pair; ++c)
                out.push_back(l * step + c);
        return out;
    }
    /// Two frames @p shift bytes apart share a byte (frames of a page-strided set interleave: one byte apart, each
    /// line a page further)
    bool Overlaps(uint32_t shift) const
    {
        if (shift >= Bytes())
            return false;
        const std::vector<uint32_t> offsets = Offsets();
        const std::set<uint32_t> own(offsets.begin(), offsets.end());
        return std::any_of(offsets.begin(), offsets.end(), [&](uint32_t o) { return own.count(o + shift) != 0; });
    }
};

/// One frame start: the blits that drew it, the instructions that wrote its bytes to the screen
struct Frame
{
    std::set<size_t> blits;
    std::set<PhysKey> writers;
    uint64_t draws = 0;
    bool blank = false;  ///< one value in every byte
};

struct PointerRange
{
    uint32_t lo = 0;
    uint32_t hi = 0;
};

struct Object
{
    uint16_t first = 0;     ///< the first frame's pixel rows
    uint16_t last = 0;
    uint32_t step = 0;      ///< 0: one frame
    uint32_t seen = 1;      ///< frames drawn
    uint32_t filled = 0;    ///< frames between drawn ones
    bool sparse = false;    ///< 8 x 8 frames with gaps (a character set's glyphs)
    std::set<size_t> blits;
    std::set<PhysKey> writers;
    std::vector<uint16_t> drawn;  ///< the frames drawn
    uint64_t draws = 0;
};

/// Source bytes drawn as one picture
struct Piece
{
    PhysKey first = 0;
    PhysKey last = 0;
    uint32_t stride = 1;  ///< bytes between neighbours along a row (column); 2: a mask byte between graphic bytes
    uint32_t pitch = 0;   ///< bytes from one row's (column's) start to the next
    uint16_t width = 0;   ///< bytes a row
    uint16_t height = 0;
    bool columns = false;
    std::set<PhysKey> writers;
    uint64_t draws = 0;
};

constexpr uint32_t kMinPieceBytes = 4;
constexpr uint32_t kMaxHole = 8;     ///< frames never drawn between two drawn ones of an interleaved set
constexpr size_t kMinGlyphs = 8;  ///< different glyphs drawn: a font of the game's own codes
constexpr uint16_t kRomCharset = 0x3D00;  ///< the 48K ROM's character set, codes 32..127

constexpr uint32_t kMinBankSeeds = 8;  ///< records drawn whose header bytes agree on a record format
constexpr int kMaxBanks = 4;  ///< sprite banks of different formats in one image
constexpr uint32_t kMaxAnchorChain = 64;  ///< records a pointer's chain crosses before a known one
constexpr uint32_t kMinTableWords = 4;  ///< pointers to records back to back: a table of them
constexpr uint32_t kMaxImmediateChain = 4;  ///< records a lone LD rr,nn's chain crosses before a known one
constexpr uint32_t kMaxGlyphRows = 16;

/// The header before each record of a sprite bank: @c bytes of them, the record's width in bytes times @c scale at
/// @c widthAt (scale 0: every record @c width bytes wide, the header holds no width), its height in pixel lines at
/// @c heightAt; the rows follow, back to back, @c pair bytes a column (2: a mask byte before each graphic byte)
struct BankFormat
{
    uint32_t bytes = 0;
    uint32_t widthAt = 0;
    uint32_t heightAt = 0;
    uint32_t scale = 0;
    uint32_t pair = 1;
    uint32_t width = 0;
};

struct BankRecord
{
    uint16_t header = 0;
    uint16_t width = 0;  ///< bytes a row
    uint16_t height = 0;
    bool drawn = false;
    uint32_t Bytes(const BankFormat& f) const { return f.bytes + static_cast<uint32_t>(width) * height * f.pair; }
};

/// A neighbour step most of a byte's neighbours agreed on, at two or more frame ends (one is a redraw caught halfway:
/// the new frame's rows over the old one's), the majority count a quarter of the neighbours seen (the step is about
/// 60 % of them or more)
bool Steady(const DrawStep& s)
{
    return s.count >= 2 && s.delta > 0 && static_cast<uint64_t>(s.count) * 4 >= s.seen;
}

/// The drawn bytes (sorted by key) cut into pictures. A byte's steady steps right and down link it to the bytes they
/// name; a linked set is a piece. Its most common pair of steps is its layout: right 1 (2: a mask byte between) and down
/// the row pitch, stored in rows; down 1 (2) and right the column pitch, in columns; down alone, one byte wide. The
/// bytes sit on the lattice of the pitch: the cells drawn give the width and height (a window of a wider picture, the
/// part a scrolled landscape showed, is its own extent). Pieces as wide as the screen are pictures (the screens
/// detector's), not graphics; a piece far from filling its rectangle is no picture
std::vector<Piece> Pieces(const std::vector<DrawnByte>& drawn)
{
    const size_t n = drawn.size();
    std::vector<size_t> parent(n);
    for (size_t i = 0; i < n; ++i)
        parent[i] = i;
    const auto root = [&](size_t i) {
        while (parent[i] != i)
            i = parent[i] = parent[parent[i]];
        return i;
    };
    const auto index = [&](PhysKey key) {
        const auto it = std::lower_bound(drawn.begin(), drawn.end(), key,
                                         [](const DrawnByte& d, PhysKey k) { return d.key < k; });
        return it != drawn.end() && it->key == key ? static_cast<size_t>(it - drawn.begin()) : n;
    };
    // A step right longer than a mask's links columns only when the columns are whole: every byte between the two
    // drawn too (drawn is sorted by key, one entry a key). The letters of a text sit side by side, their glyphs anywhere
    // in the font; a column picture is not windowed (one scrolled up and down is a known limit)
    const auto linked = [&](size_t i, const DrawStep& step) {
        if (!Steady(step) || KeyOffset(drawn[i].key) + static_cast<uint32_t>(step.delta) >= 0x4000)
            return n;
        const PhysKey to = drawn[i].key + static_cast<uint32_t>(step.delta);
        const size_t j = index(to);
        return j < n && (&step == &drawn[i].down || step.delta <= 2 || j - i == static_cast<size_t>(step.delta)) ? j : n;
    };
    // The pair of steps most of a set's bytes took, both steps first (an edge byte knows one of them)
    const auto modeOf = [&](const std::vector<size_t>& members) {
        std::map<std::pair<int, int>, uint32_t> pairs;
        for (const size_t i : members)
        {
            const DrawnByte& d = drawn[i];
            const int r = linked(i, d.right) < n ? d.right.delta : 0;
            const int v = linked(i, d.down) < n ? d.down.delta : 0;
            if (r != 0 || v != 0)
                ++pairs[{r, v}];
        }
        std::pair<int, int> mode{0, 0};
        uint32_t best = 0;
        for (const bool both : {true, false})
        {
            for (const auto& [pair, count] : pairs)
                if ((pair.first != 0 && pair.second != 0) == both && count > best)
                {
                    mode = pair;
                    best = count;
                }
            if (best != 0)
                break;
        }
        return mode;
    };
    const auto unite = [&](const std::vector<std::pair<int, int>>* modes) {
        for (size_t i = 0; i < n; ++i)
            parent[i] = i;
        for (size_t i = 0; i < n; ++i)
            for (const DrawStep* step : {&drawn[i].right, &drawn[i].down})
                if (const size_t j = linked(i, *step); j < n)
                    if (!modes || step->delta == (step == &drawn[i].right ? (*modes)[i].first : (*modes)[i].second))
                        parent[root(i)] = root(j);
        std::map<size_t, std::vector<size_t>> sets;
        for (size_t i = 0; i < n; ++i)
            sets[root(i)].push_back(i);
        return sets;
    };
    // Linked by every step, then only by the steps of the set's layout: tiles a map stacks are drawn one under the
    // other, the last row of one above the first of the next (a step of the tile's size, not the row's)
    std::vector<std::pair<int, int>> modes(n);
    for (const auto& [_, members] : unite(nullptr))
    {
        const std::pair<int, int> mode = modeOf(members);
        for (const size_t i : members)
            modes[i] = mode;
    }
    std::vector<Piece> out;
    for (const auto& [_, members] : unite(&modes))
    {
        if (members.size() < kMinPieceBytes)
            continue;
        const std::pair<int, int> mode = modeOf(members);
        const auto [r, v] = mode;
        Piece piece;
        uint32_t stride = 0, pitch = 0;
        if ((r == 1 || r == 2) && v >= r)
            stride = static_cast<uint32_t>(r), pitch = static_cast<uint32_t>(v);
        else if ((v == 1 || v == 2) && r > v)
            stride = static_cast<uint32_t>(v), pitch = static_cast<uint32_t>(r), piece.columns = true;
        else if (r == 0 && (v == 1 || v == 2))
            stride = pitch = static_cast<uint32_t>(v);  // one byte wide
        else
            continue;
        // The lattice: a line starts at a byte the one stride bytes back does not step to along the line (none there,
        // or the end of the line before: rows stored back to back); the most common residue of those modulo the pitch
        std::map<uint32_t, uint32_t> starts;
        for (const size_t i : members)
        {
            const PhysKey k = drawn[i].key;
            const size_t before = KeyOffset(k) >= stride ? index(k - stride) : n;
            if (before == n || linked(before, piece.columns ? drawn[before].down : drawn[before].right) != i)
                ++starts[KeyOffset(k) % pitch];
        }
        uint32_t start = 0, startCount = 0;
        for (const auto& [residue, count] : starts)
            if (count > startCount)
                start = residue, startCount = count;
        struct Cell
        {
            int64_t line = 0;
            uint32_t cell = 0;
            size_t member = 0;
        };
        std::vector<Cell> cells;
        std::map<int64_t, std::pair<uint64_t, uint32_t>> lineDraws;  // line -> draws, bytes
        for (const size_t i : members)
        {
            const int64_t at = static_cast<int64_t>(KeyOffset(drawn[i].key)) - start;
            const int64_t line = at >= 0 ? at / pitch : (at - pitch + 1) / static_cast<int64_t>(pitch);
            const int64_t within = at - line * pitch;
            if (within % stride != 0)
                continue;
            cells.push_back(Cell{line, static_cast<uint32_t>(within / stride), i});
            lineDraws[line].first += drawn[i].draws;
            ++lineDraws[line].second;
        }
        // Lines no step across links (down a row, right a column), drawn a different number of times or one twice as
        // wide as the other, are two pictures: frames stored back to back, the last row of one beside the first of the
        // next only by the spill byte a shifted sprite reads past its row (the byte after the last frame, read so)
        std::set<int64_t> crossed;
        for (const Cell& c : cells)
            if (const size_t j = linked(c.member, piece.columns ? drawn[c.member].right : drawn[c.member].down); j < n)
                if (KeyOffset(drawn[j].key) == KeyOffset(drawn[c.member].key) + pitch)
                    crossed.insert(c.line);
        const auto mean = [&](int64_t line) {
            const auto& [draws, count] = lineDraws[line];
            return static_cast<double>(draws) / count;
        };
        std::map<int64_t, int64_t> runOf;  // line -> the first line of its run
        int64_t run = 0;
        for (auto it = lineDraws.begin(); it != lineDraws.end(); ++it)
        {
            if (it == lineDraws.begin())
                run = it->first;
            else if (const int64_t before = std::prev(it)->first; before + 1 == it->first && !crossed.count(before))
            {
                const double a = mean(before), b = mean(it->first);
                const uint32_t wa = lineDraws[before].second, wb = it->second.second;
                if (std::max(a, b) >= 2 * std::min(a, b) || std::max(wa, wb) >= 2 * std::min(wa, wb))
                    run = it->first;
            }
            runOf[it->first] = run;
        }
        std::map<int64_t, std::vector<const Cell*>> runs;
        for (const Cell& c : cells)
            runs[runOf[c.line]].push_back(&c);
        for (const auto& [_, part] : runs)
        {
            if (part.size() < kMinPieceBytes)
                continue;
            int64_t firstLine = part.front()->line;
            uint32_t lines = 0, length = 0;
            for (const Cell* c : part)
                firstLine = std::min(firstLine, c->line);
            for (const Cell* c : part)
            {
                lines = std::max(lines, static_cast<uint32_t>(c->line - firstLine) + 1);
                length = std::max(length, c->cell + 1);
            }
            const auto bytes = static_cast<uint32_t>(part.size());
            const uint32_t across = piece.columns ? lines : length;
            const uint32_t down = piece.columns ? length : lines;
            if (across >= 32 || down <= 1 || down > 192 || bytes * 4 < across * down || length * stride > pitch)
                continue;
            const int64_t base = static_cast<int64_t>(start) + firstLine * pitch;
            if (base < 0 || base + (lines - 1) * pitch + (length - 1) * stride >= 0x4000)
                continue;
            Piece cut = piece;
            const PhysKey page = MakeKey(KeyPage(drawn[members.front()].key), 0);
            cut.first = page + static_cast<uint32_t>(base);
            cut.last = cut.first + (lines - 1) * pitch + (length - 1) * stride;
            cut.stride = stride;
            cut.pitch = pitch == length * stride ? 0 : pitch;
            cut.width = static_cast<uint16_t>(across);
            cut.height = static_cast<uint16_t>(down);
            for (const Cell* c : part)
            {
                cut.draws += drawn[c->member].draws;
                if (drawn[c->member].writer != kNoKey && cut.writers.size() < 8)
                    cut.writers.insert(drawn[c->member].writer);
            }
            out.push_back(cut);
        }
    }
    std::sort(out.begin(), out.end(), [](const Piece& a, const Piece& b) { return a.first < b.first; });
    return out;
}

/// A byte the run rewrites as it draws it: a buffer (a copy of a level's data, a save area, a line composed for each
/// draw), not a picture's source. Copied in by a block copy, or written at least once for every four draws
bool BufferByte(const L1Result& l1, PhysKey key)
{
    const ByteUse* u = l1.UseAt(key);
    constexpr uint16_t kWrites = use::kWrite | use::kBlockWrite | use::kWordWrite | use::kIncDec | use::kOtherWrite;
    if (u == nullptr || (u->flags & kWrites) == 0)
        return false;
    if ((u->flags & use::kBlockWrite) != 0)
        return true;
    const auto it = std::lower_bound(l1.drawn.begin(), l1.drawn.end(), key,
                                     [](const DrawnByte& d, PhysKey k) { return d.key < k; });
    const uint64_t draws = it != l1.drawn.end() && it->key == key ? it->draws : 0;
    return static_cast<uint64_t>(u->writes) * 4 >= draws;
}

/// @p key's address in the start's paging; a page it did not show lies past 64 K, 16 K each (lines a page on run past
/// $C000 into the next page as the Z80 sees them)
uint32_t Linear(const L1Result& l1, PhysKey key)
{
    for (uint32_t b = 0; b < 4; ++b)
        if (l1.startBanks[b] == KeyPage(key))
            return b * 0x4000u + KeyOffset(key);
    return 0x10000u + KeyPage(key) * 0x4000u + KeyOffset(key);
}

PhysKey KeyAt(const L1Result& l1, uint32_t linear)
{
    if (linear < 0x10000u)
        return MakeKey(l1.startBanks[linear >> 14], static_cast<uint16_t>(linear));
    return MakeKey(static_cast<uint16_t>((linear - 0x10000u) >> 14), static_cast<uint16_t>(linear));
}

/// The drawing passes' frames (l1valueflow.h) cut into pictures. A frame's columns are pieces of a column: lines of one
/// row step from a base; the pieces of one row step that share a byte are one segment (a sprite drawn clipped, a tile
/// drawn from a later line: the longest run of lines seen). Frames sit side by side by chance (the tiles of a map, a
/// sprite next to a wall), so two segments join left to right only when every frame that drew them side by side joined
/// them so and neither ever had another neighbour; a chain of joined segments with one shape and one column step is a
/// picture. Its layout: column step 1 (2 with the mask between) and the row step its pitch, stored in rows; row step 1
/// (2) and the columns back to back, in columns; one column alone, one byte wide. Any other column step is the glyphs
/// of one text printed the same way every time (each glyph has one neighbour, never a picture): one column each. A
/// picture most of whose bytes are a buffer's is none; nor is one whose lines go up through memory (stored bottom up: a
/// known limit), one line high, or one as wide as the screen. Addresses are the start's (Linear): a set stored a pixel
/// row a page on runs past $C000 as the Z80 sees it
std::vector<Piece> FramePieces(const L1Result& l1, const std::set<PhysKey>& starts)
{
    // A frame cut in two by a pass's end (a screen byte rewritten while it drew): the lower part's source goes on from
    // the upper's last line, both drawn as often, last seen one under the other on the screen
    std::vector<DrawnFrame> frames = l1.drawnFrames;
    {
        using Lattice = std::tuple<uint32_t, uint8_t, int16_t, int16_t, int16_t, uint32_t, uint8_t, uint8_t, uint8_t>;
        const auto latticeOf = [](const DrawnFrame& f, uint32_t base, uint8_t y) {
            return Lattice{base, f.width, f.rowStep, f.colStep, f.maskDelta, f.count, f.x, y, f.screen};
        };
        std::map<Lattice, size_t> below;
        for (size_t i = 0; i < frames.size(); ++i)
            if (frames[i].height > 1 && frames[i].rowStep > 0)
                below.emplace(latticeOf(frames[i], Linear(l1, frames[i].base), frames[i].y), i);
        std::vector<bool> merged(frames.size(), false);
        for (size_t i = 0; i < frames.size(); ++i)
        {
            DrawnFrame& f = frames[i];
            while (!merged[i] && f.height > 1 && f.rowStep > 0 && f.y + f.height < 256)
            {
                const auto it = below.find(latticeOf(f, Linear(l1, f.base) + f.height * static_cast<uint32_t>(f.rowStep),
                                                     static_cast<uint8_t>(f.y + f.height)));
                if (it == below.end() || it->second == i || merged[it->second] ||
                    f.height + frames[it->second].height > 192)
                    break;
                merged[it->second] = true;
                f.height = static_cast<uint8_t>(f.height + frames[it->second].height);
                f.bytes = static_cast<uint16_t>(f.bytes + frames[it->second].bytes);
            }
        }
        size_t kept = 0;
        for (size_t i = 0; i < frames.size(); ++i)
            if (!merged[i])
                frames[kept++] = frames[i];
        frames.resize(kept);
    }
    struct Column
    {
        uint32_t base = 0;  ///< Linear
        int32_t row = 0;
        uint32_t lines = 0;
        int32_t maskDelta = 0;
        bool operator<(const Column& o) const
        {
            return std::tie(base, row, lines, maskDelta) < std::tie(o.base, o.row, o.lines, o.maskDelta);
        }
    };
    struct Use
    {
        uint64_t draws = 0;
        std::set<PhysKey> writers;
    };
    // The frames' columns
    std::map<Column, Use> columns;
    // A column's bytes in the 64 K the start showed, or in the one page it did not show
    const auto columnOf = [&l1](const DrawnFrame& f, uint32_t c, Column& out) {
        const int64_t base = Linear(l1, f.base);
        const int64_t lo = base < 0x10000 ? 0 : base - KeyOffset(f.base);
        const int64_t hi = base < 0x10000 ? 0x10000 : lo + 0x4000;
        const int64_t at = base + static_cast<int64_t>(c) * f.colStep;
        const int64_t top = at + static_cast<int64_t>(f.height - 1) * f.rowStep;
        if (at < lo || at >= hi || top < lo || top >= hi)
            return false;
        out = Column{static_cast<uint32_t>(at), f.height > 1 ? f.rowStep : 0, f.height, f.maskDelta};
        return true;
    };
    // A column over the bytes of a column left of it in one frame is a shifted sprite's spill (the byte read past the
    // row, its bits shifted in): the frame ends before it
    std::vector<uint32_t> widths;
    widths.reserve(frames.size());
    for (const DrawnFrame& f : frames)
    {
        uint32_t width = f.width;
        for (uint32_t c = 1; c < width && f.height > 1 && f.rowStep != 0; ++c)
        {
            const int64_t d = static_cast<int64_t>(c) * f.colStep;
            for (uint32_t k = 0; k < c; ++k)
            {
                const int64_t apart = d - static_cast<int64_t>(k) * f.colStep;
                if (apart % f.rowStep == 0 && std::abs(apart / f.rowStep) < f.height)
                {
                    width = c;
                    break;
                }
            }
        }
        widths.push_back(width);
    }
    for (size_t i = 0; i < frames.size(); ++i)
        for (uint32_t c = 0; c < widths[i]; ++c)
            if (Column col; columnOf(frames[i], c, col))
            {
                const DrawnFrame& f = frames[i];
                Use& u = columns[col];
                u.draws += f.count;
                if (f.writer != kNoKey && u.writers.size() < 8)
                    u.writers.insert(f.writer);
            }

    // Segments: one row step's columns merged while they share a byte, cut again where more draws began or ended a
    // column than ran through it (tiles stacked on a map, the next one's bytes right after: a column of both, now and
    // then), and where a blit loop began reading a frame (tiles stacked so on every cell: the loop drew them one by
    // one)
    struct Segment
    {
        uint32_t first = 0;  ///< Linear
        uint32_t step = 0;  ///< 0: one line
        uint32_t lines = 1;
        int32_t maskDelta = 0;
        Use use;
    };
    std::vector<Segment> segments;
    std::map<Column, size_t> segmentOf;
    {
        std::map<std::pair<uint32_t, uint32_t>, std::vector<std::pair<Column, const Use*>>> lattices;
        for (const auto& [col, u] : columns)
            if (col.row > 0)
                lattices[{static_cast<uint32_t>(col.row), col.base % static_cast<uint32_t>(col.row)}].emplace_back(col,
                                                                                                                  &u);
        for (auto& [lattice, items] : lattices)
        {
            const uint32_t step = lattice.first;
            std::sort(items.begin(), items.end(),
                      [](const auto& a, const auto& b) { return a.first.base < b.first.base; });
            // Clusters of columns sharing bytes
            std::vector<std::vector<std::pair<Column, const Use*>>> clusters;
            uint32_t hi = 0;
            for (const auto& item : items)
            {
                const uint32_t top = item.first.base + (item.first.lines - 1) * step;
                if (clusters.empty() || item.first.base > hi)
                    clusters.emplace_back();
                hi = clusters.back().empty() ? top : std::max(hi, top);
                clusters.back().push_back(item);
            }
            for (const auto& cluster : clusters)
            {
                const uint32_t lo = cluster.front().first.base;
                uint32_t lines = 0;
                for (const auto& [col, u] : cluster)
                    lines = std::max(lines, (col.base - lo) / step + col.lines);
                std::vector<uint64_t> cross(lines + 1, 0), split(lines + 1, 0);
                for (const auto& [col, u] : cluster)
                {
                    const uint32_t first = (col.base - lo) / step, end = first + col.lines;
                    split[first] += u->draws;
                    split[end] += u->draws;
                    for (uint32_t b = first + 1; b < end; ++b)
                        cross[b] += u->draws;
                }
                std::vector<size_t> segmentAt(lines);
                uint32_t from = 0;
                for (uint32_t b = 1; b <= lines; ++b)
                    if (b == lines || split[b] > cross[b] || starts.count(KeyAt(l1, lo + b * step)) != 0)
                    {
                        segments.push_back(Segment{lo + from * step, step, b - from, cluster.front().first.maskDelta, {}});
                        for (uint32_t l = from; l < b; ++l)
                            segmentAt[l] = segments.size() - 1;
                        from = b;
                    }
                for (const auto& [col, u] : cluster)
                {
                    const size_t at = segmentAt[(col.base - lo) / step];
                    segments[at].use.draws += u->draws;
                    segments[at].use.writers.insert(u->writers.begin(), u->writers.end());
                    segmentOf[col] = at;
                }
            }
        }
        // Single lines: part of the segment holding the byte, else one of their own
        std::map<uint32_t, size_t> holder;
        for (size_t i = 0; i < segments.size(); ++i)
            for (uint32_t l = 0; l < segments[i].lines; ++l)
                holder.emplace(segments[i].first + l * segments[i].step, i);
        for (const auto& [col, u] : columns)
        {
            if (col.row != 0)
                continue;
            const auto it = holder.find(col.base);
            size_t i = 0;
            if (it != holder.end())
                i = it->second;
            else
            {
                segments.push_back(Segment{col.base, 0, 1, col.maskDelta, {}});
                i = segments.size() - 1;
                holder.emplace(col.base, i);
            }
            segments[i].use.draws += u.draws;
            segments[i].use.writers.insert(u.writers.begin(), u.writers.end());
            segmentOf[col] = i;
        }
    }

    // Joins: a segment's right neighbour, when it is the only one either ever had
    constexpr size_t kNone = ~size_t{0};
    std::vector<std::set<size_t>> right(segments.size()), left(segments.size());
    for (size_t i = 0; i < frames.size(); ++i)
    {
        const DrawnFrame& f = frames[i];
        size_t before = kNone;
        for (uint32_t c = 0; c < widths[i]; ++c)
        {
            Column col;
            const auto it = columnOf(f, c, col) ? segmentOf.find(col) : segmentOf.end();
            const size_t s = it != segmentOf.end() ? it->second : kNone;
            if (before != kNone && s != kNone && before != s)
            {
                right[before].insert(s);
                left[s].insert(before);
            }
            before = s;
        }
    }
    std::vector<size_t> next(segments.size(), kNone);
    std::vector<bool> joined(segments.size(), false);
    for (size_t i = 0; i < segments.size(); ++i)
        if (right[i].size() == 1 && left[*right[i].begin()].size() == 1)
        {
            next[i] = *right[i].begin();
            joined[next[i]] = true;
        }

    std::vector<Piece> out;
    const auto emit = [&](const std::vector<size_t>& chain) {
        const Segment& s = segments[chain.front()];
        const uint32_t cols = static_cast<uint32_t>(chain.size());
        int64_t colStep = 0;
        bool even = true;
        for (size_t k = 1; k < chain.size(); ++k)
        {
            const Segment& a = segments[chain[k - 1]];
            const Segment& b = segments[chain[k]];
            const int64_t d = static_cast<int64_t>(b.first) - static_cast<int64_t>(a.first);
            even = even && b.step == s.step && b.lines == s.lines && (b.first < 0x10000u) == (a.first < 0x10000u) &&
                   (k == 1 || d == colStep);
            colStep = d;
        }
        if (!even)
            return false;
        const bool masked = s.maskDelta == 1 || s.maskDelta == -1;
        Piece p;
        p.first = KeyAt(l1, s.first);
        p.height = static_cast<uint16_t>(s.lines);
        p.width = static_cast<uint16_t>(cols);
        if (cols == 1)
        {
            if (s.step == 2 && masked)
                p.stride = 2, p.pitch = 0;
            else
                p.stride = 1, p.pitch = s.lines > 1 && s.step != 1 ? s.step : 0;
        }
        else if (colStep == 1 || (colStep == 2 && masked))
        {
            p.stride = static_cast<uint32_t>(colStep);
            p.pitch = s.lines > 1 && s.step != cols * p.stride ? s.step : 0;
        }
        else if ((s.step == 1 || (s.step == 2 && masked)) && s.lines > 1 &&
                 static_cast<uint64_t>(colStep) == static_cast<uint64_t>(s.lines) * s.step)
        {
            p.columns = true;
            p.stride = s.step;
            p.pitch = 0;
            p.width = static_cast<uint16_t>(cols);
            p.height = static_cast<uint16_t>(s.lines);
        }
        else
            return false;
        // The bytes: a buffer's for the most part, no picture
        uint32_t bytes = 0, buffered = 0;
        uint32_t last = s.first;
        for (const size_t k : chain)
            for (uint32_t l = 0; l < segments[k].lines; ++l)
            {
                const uint32_t at = segments[k].first + l * segments[k].step;
                last = std::max(last, at);
                ++bytes;
                buffered += BufferByte(l1, KeyAt(l1, at)) ? 1 : 0;
            }
        // As in Pieces: a strip as wide as the screen is a picture (the screens detector's), one line no frame
        const uint32_t across = p.width, down = p.height;
        if (2 * buffered > bytes || bytes < kMinPieceBytes || across >= 32 || down <= 1 || down > 192)
            return true;
        p.last = KeyAt(l1, last);
        for (const size_t k : chain)
        {
            p.draws += segments[k].use.draws;
            for (const PhysKey w : segments[k].use.writers)
                if (p.writers.size() < 8)
                    p.writers.insert(w);
        }
        out.push_back(p);
        return true;
    };
    for (size_t i = 0; i < segments.size(); ++i)
    {
        if (joined[i])
            continue;
        std::vector<size_t> chain{i};
        while (next[chain.back()] != kNone && chain.size() < 32 &&
               std::find(chain.begin(), chain.end(), next[chain.back()]) == chain.end())
            chain.push_back(next[chain.back()]);
        if (!emit(chain))
            for (const size_t k : chain)
                emit({k});
    }
    // A frame drawn whole, its rows back to back (plain, or a mask byte before each graphic byte), is a piece too: the
    // tallest one from each base (a sprite drawn clipped at every height cuts its columns into many segments)
    {
        using Whole = std::tuple<PhysKey, uint8_t, int16_t>;  // base, width, mask offset
        std::map<Whole, const DrawnFrame*> tallest;
        for (const DrawnFrame& f : frames)
        {
            const int32_t pair = f.maskDelta == 0 ? 1 : f.maskDelta == -1 ? 2 : 0;
            if (pair == 0 || f.height < 2 || f.width == 0 || f.width >= 32 || f.rowStep != f.width * pair ||
                (f.width > 1 && f.colStep != pair) || Linear(l1, f.base) >= 0x10000)
                continue;
            const DrawnFrame*& t = tallest[Whole{f.base, f.width, f.maskDelta}];
            if (t == nullptr || f.height > t->height)
                t = &f;
        }
        for (const auto& [whole, f] : tallest)
        {
            const uint32_t pair = f->maskDelta == 0 ? 1u : 2u;
            const uint32_t base = static_cast<uint32_t>(Linear(l1, f->base));
            const uint32_t last = base + (f->height - 1u) * f->rowStep + (f->width - 1u) * pair;
            if (last > 0xFFFF)
                continue;
            uint32_t bytes = 0, buffered = 0;
            for (uint32_t l = 0; l < f->height; ++l)
                for (uint32_t c = 0; c < f->width; ++c)
                {
                    ++bytes;
                    buffered += BufferByte(l1, KeyAt(l1, base + l * f->rowStep + c * pair)) ? 1 : 0;
                }
            if (2 * buffered > bytes || bytes < kMinPieceBytes)
                continue;
            Piece p;
            p.first = f->base;
            p.last = KeyAt(l1, last);
            p.stride = pair;
            p.width = f->width;
            p.height = f->height;
            p.draws = f->count;
            if (f->writer != kNoKey)
                p.writers.insert(f->writer);
            out.push_back(p);
        }
    }
    std::sort(out.begin(), out.end(), [](const Piece& a, const Piece& b) { return a.first < b.first; });
    return out;
}

std::vector<PointerRange> PointerRanges(const L1Result& l1)
{
    std::vector<PointerRange> out;
    for (const PointerUse& p : l1.pointers)
    {
        if (p.code)
            continue;
        int64_t base = p.value;
        int64_t top = p.value;
        if (p.form == PointerForm::ImmediateHigh)
            base = top = static_cast<int64_t>(p.value) * 256;
        else if (p.form == PointerForm::Word)
        {
            base = p.minValue;
            top = p.maxValue;
        }
        const int64_t lo = base + p.minDelta;
        const int64_t hi = top + p.maxDelta;
        if (lo < 0 || hi > 0xFFFF || hi < lo || hi - lo > 0x3FFF)
            continue;
        out.push_back(PointerRange{static_cast<uint32_t>(lo), static_cast<uint32_t>(hi)});
    }
    return out;
}

class Builder
{
public:
    Builder(DetectorContext& context, const std::vector<Blit>& blits)
        : _context(context), _blits(blits), _pointers(PointerRanges(context.l1))
    {
        _buffers = media::Buffers(context);
        for (const Region& r : context.out.store.Regions())
        {
            if (r.kind == RegionKind::Code && r.detector == DetectorId::Signatures && r.label == "PR_ALL")
                _romPrint = r.lo;
            if (r.kind == RegionKind::Text)
            {
                _textRanges.emplace_back(r.z80, r.Z80End());
                for (const char c : r.geometry.text)
                {
                    const auto code = static_cast<uint8_t>(static_cast<unsigned char>(c) & 0x7F);
                    if (code >= 32 && code < 128)
                        _text.insert(code);
                }
            }
        }
    }

    void Run()
    {
        std::map<Shape, std::map<uint16_t, Frame>> frames;
        if (_context.l1.drawn.empty())
        {
            // No value provenance (an L1 built without it): the frames the blit loops began reading at
            for (size_t i = 0; i < _blits.size(); ++i)
            {
                const Blit& b = _blits[i];
                for (const uint16_t s : b.starts)
                    frames[Shape{b.rowBytes, b.height, b.mask, false}][s].blits.insert(i);
            }
        }
        else
        {
            // The pieces in the pages the start's paging shows, then those of each page it did not (a bank switched
            // in later: scenery in another 128 K page), seen at bank 3
            std::set<PhysKey> starts;
            for (const Blit& b : _blits)
                for (const uint16_t z80 : b.starts)
                    starts.insert(_context.Key(z80));
            _pieces = _context.l1.drawnFrames.empty() ? Pieces(Pictures(_context.l1.drawn))
                                                      : FramePieces(_context.l1, starts);
            std::set<int> views{-1};
            for (const Piece& p : _pieces)
                if (std::find(std::begin(_context.l1.startBanks), std::end(_context.l1.startBanks), KeyPage(p.first)) ==
                    std::end(_context.l1.startBanks))
                    views.insert(KeyPage(p.first));
            for (const int view : views)
            {
                _view = view;
                std::map<Shape, std::map<uint16_t, Frame>> viewFrames;
                DrawnFrames(viewFrames);
                ClaimFrames(viewFrames);
            }
            _view = -1;
            return;
        }
        ClaimFrames(frames);
    }

    void ClaimFrames(const std::map<Shape, std::map<uint16_t, Frame>>& drawnFrames)
    {
        _pending.clear();
        _fonts.clear();
        // A sprite bank's records are claimed whole, drawn or not: the frames inside them are its
        std::map<Shape, std::map<uint16_t, Frame>> rest;
        // Nor is a frame inside a generated table (a mirror table read by the bytes of a sprite drawn flipped)
        std::map<uint16_t, bool> generated;  // 256-byte page -> its bytes follow a generator
        const auto inTable = [&](uint16_t start) {
            const auto page = static_cast<uint16_t>(start >> 8);
            const auto it = generated.find(page);
            if (it != generated.end())
                return it->second;
            uint8_t bytes[256];
            bool known = true;
            for (uint32_t i = 0; i < 256 && known; ++i)
                known = EndAt(static_cast<uint16_t>(page << 8 | i), bytes[i]);
            return generated[page] = known && !MatchGenerator(bytes, sizeof(bytes)).empty();
        };
        const bool banks = _view < 0 && SpriteBanks(drawnFrames);
        for (const auto& [shape, starts] : drawnFrames)
            for (const auto& [start, frame] : starts)
                if ((!banks || _inBank[start] == 0) && !inTable(start))
                    rest[shape].emplace(start, frame);
        const std::map<Shape, std::map<uint16_t, Frame>>& frames = rest;
        for (const auto& [shape, starts] : frames)
            Objects(shape, starts);
        // A font's glyphs drawn side by side form pieces of other shapes (a word printed as one strip): no sprite;
        // frames of one value each are no picture
        for (const Pending& p : _pending)
        {
            const uint32_t hi = static_cast<uint32_t>(p.o.last) + p.size - 1;
            const auto shape = frames.find(p.shape);
            const bool blank =
                shape != frames.end() && std::all_of(p.o.drawn.begin(), p.o.drawn.end(), [&](uint16_t start) {
                    const auto f = shape->second.find(start);
                    return f != shape->second.end() && f->second.blank;
                });
            if (!blank && std::none_of(_fonts.begin(), _fonts.end(),
                                       [&](const auto& f) { return p.o.first >= f.first && hi <= f.second; }))
                Claim(p.shape, p.o, p.size);
        }
    }

private:
    DetectorContext& _context;
    const std::vector<Blit>& _blits;
    std::vector<PointerRange> _pointers;
    std::vector<media::BufferCopy> _buffers;
    std::set<uint8_t> _text;
    std::vector<std::pair<uint16_t, uint16_t>> _textRanges;  ///< the text claims, [lo, hi] (Z80)
    std::set<std::tuple<PhysKey, PhysKey, uint8_t>> _claimed;
    PhysKey _romPrint = kNoKey;  ///< the ROM's character loop (PR_ALL), when its signature matched
    std::vector<std::pair<uint32_t, uint32_t>> _fonts;  ///< the fonts claimed, [lo, hi] (Z80)
    struct Pending
    {
        Shape shape;
        Object o;
        uint32_t size = 0;
    };
    std::vector<Pending> _pending;  ///< the sprites and tiles, claimed once every shape's fonts are known
    std::vector<Piece> _pieces;
    int _view = -1;  ///< a page the start's paging did not show, seen at bank 3 ($C000); -1: the start's paging
    BankFormat _format;
    std::vector<uint8_t> _inBank;      ///< per Z80 address: 1 inside a bank record (its header included)
    std::vector<uint32_t> _codeBefore;  ///< executed bytes below each Z80 address (a record holds none)

    /// The record at @p at under the bank's format: false when its header gives no size (zero, over 32 bytes or
    /// 192 lines, a width the scale does not divide) or its bytes run past 64 K or over executed code
    bool Record(uint32_t at, BankRecord& r) const
    {
        const BankFormat& f = _format;
        if (at < 0x4000 || at + f.bytes > 0xFFFF)
            return false;
        const uint32_t w = f.scale == 0 ? f.width : _context.Start(static_cast<uint16_t>(at + f.widthAt));
        const uint8_t h = _context.Start(static_cast<uint16_t>(at + f.heightAt));
        const uint32_t scale = f.scale == 0 ? 1 : f.scale;
        if (w == 0 || w % scale != 0 || w / scale > 32 || h == 0 || h > 192)
            return false;
        r = BankRecord{static_cast<uint16_t>(at), static_cast<uint16_t>(w / scale), h, false};
        const uint32_t end = at + r.Bytes(f);
        return end <= 0x10000 && _codeBefore[end] == _codeBefore[at];
    }

    /// A record drawn whole: its rows (pair bytes a column) from @c data
    struct BankSeed
    {
        uint32_t data = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        uint32_t pair = 1;
        bool operator<(const BankSeed& o) const
        {
            return std::tie(data, width, height, pair) < std::tie(o.data, o.width, o.height, o.pair);
        }
    };

    /// The seeds whose record's chain (kMaxAnchorChain records at most) reaches another seed's record, under @p format
    uint32_t Links(const BankFormat& format, const std::set<BankSeed>& seeds)
    {
        const BankFormat kept = _format;
        _format = format;
        std::set<uint32_t> headers;
        for (const BankSeed& seed : seeds)
        {
            BankRecord r;
            if (seed.pair == format.pair && seed.data >= format.bytes && _inBank[seed.data] == 0 &&
                Record(seed.data - format.bytes, r) && r.width == seed.width && r.height == seed.height)
                headers.insert(r.header);
        }
        uint32_t links = 0;
        for (const uint32_t header : headers)
        {
            BankRecord r;
            uint32_t at = header;
            for (uint32_t n = 0; n < kMaxAnchorChain && at < 0x10000 && _inBank[at] == 0 && Record(at, r); ++n)
            {
                at += r.Bytes(_format);
                if (headers.count(at) != 0)
                {
                    ++links;
                    break;
                }
            }
        }
        _format = kept;
        return links;
    }

    /// Sprites stored as records of a bank: each one a header that gives its size, then its rows, the next record
    /// right after it (Dizzy's). The seeds are the frames drawn whole: rows back to back, plain bytes or a mask byte
    /// before each graphic byte (the drawing passes' frames, and the pictures' frames). A header format: the bytes
    /// before the rows give the width (in bytes, times 1, 2 or 8) and the height, or the height alone and every record
    /// one width; one kMinBankSeeds or more seeds agree with is a candidate. The bank's is the candidate whose chains
    /// link the most seeds (from a seed's record, the records after it while their headers give a size, up to another
    /// seed's: kMaxAnchorChain at most), then the one most seeds agree with, the shortest header first; a header of the
    /// height alone needs a link (the byte before a frame of one width matches its height often enough by chance).
    /// Then the format the seeds outside that bank agree with, and so on (kMaxBanks: the scenery in one bank, the hero
    /// with his mask in another)
    bool SpriteBanks(const std::map<Shape, std::map<uint16_t, Frame>>& frames)
    {
        _inBank.assign(0x10000, 0);
        std::set<BankSeed> seeds;
        // The pictures' frames (glyphs printed side by side are one drawn frame, its columns apart: one piece each)
        for (const auto& [shape, starts] : frames)
            if (!shape.mask && !shape.columns && shape.pitch == 0 && shape.rowBytes <= 32 && shape.height > 1)
                for (const auto& [start, frame] : starts)
                    if (start >= 0x4005)
                        seeds.insert(BankSeed{start, shape.rowBytes, shape.height, 1});
        for (const DrawnFrame& f : _context.l1.drawnFrames)
        {
            const int64_t base = Linear(_context.l1, f.base);
            if (base < 0x4005 || base >= 0x10000 || f.height < 2 || f.width > 32)
                continue;
            uint32_t pair = 0;
            if (f.maskDelta == 0 && (f.colStep == 1 || f.width == 1) && f.rowStep == f.width)
                pair = 1;
            else if (f.maskDelta == -1 && (f.colStep == 2 || f.width == 1) && f.rowStep == 2 * f.width)
                pair = 2;
            if (pair != 0)
                seeds.insert(BankSeed{static_cast<uint32_t>(base) - (pair - 1), f.width, f.height, pair});
        }
        _codeBefore.assign(0x10001, 0);
        for (uint32_t a = 0; a < 0x10000; ++a)
            _codeBefore[a + 1] = _codeBefore[a] + (DetectorContext::Code(_context.Use(static_cast<uint16_t>(a))) ? 1 : 0);
        bool any = false;
        for (int bank = 0; bank < kMaxBanks; ++bank)
        {
            // (bytes, width at, height at, scale, pair, width): scale 0 a width of its own, width at none (bytes)
            std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>, uint32_t> votes;
            for (const BankSeed& seed : seeds)
            {
                if (_inBank[seed.data] != 0)
                    continue;
                for (uint32_t k = 2; k <= 4; ++k)
                    for (uint32_t hi = 0; hi < k; ++hi)
                    {
                        if (_context.Start(static_cast<uint16_t>(seed.data - k + hi)) != seed.height)
                            continue;
                        ++votes[{k, k, hi, 0, seed.pair, seed.width}];
                        for (uint32_t wi = 0; wi < k; ++wi)
                            for (const uint32_t c : {1u, 2u, 8u})
                                if (wi != hi &&
                                    _context.Start(static_cast<uint16_t>(seed.data - k + wi)) == seed.width * c)
                                    ++votes[{k, wi, hi, c, seed.pair, 0}];
                    }
            }
            uint32_t best = 0, bestLinks = 0;
            for (const auto& [key, count] : votes)  // the shortest header first (std::map order)
            {
                if (count < kMinBankSeeds)
                    continue;
                const BankFormat format{std::get<0>(key), std::get<1>(key), std::get<2>(key),
                                        std::get<3>(key), std::get<4>(key), std::get<5>(key)};
                const uint32_t links = Links(format, seeds);
                if ((format.scale == 0 && links == 0) || links < bestLinks || (links == bestLinks && count <= best))
                    continue;
                best = count;
                bestLinks = links;
                _format = format;
            }
            if (best < kMinBankSeeds || !SpriteBank(seeds))
                break;
            any = true;
        }
        return any;
    }

    /// The bank of the seeds in _format. From each record drawn the records after it while their headers give a size
    /// (none over executed code or another bank); a record before the first one drawn is reached through a pointer: a
    /// word whose value starts a chain of records that ends on a known one, in a table of such words (kMinTableWords or
    /// more, a word apart or two: a null entry between) or the operand of a LD rr,nn whose chain is short
    /// (kMaxImmediateChain), to a fixed point. Runs of records of one size are claimed together: a font when its
    /// records are glyphs (one or two plain bytes wide, kMaxGlyphRows lines at most) that kMinGlyphs or more of them
    /// were printed side by side (letters of a word: two of its records the columns of one drawn frame), and so is a
    /// run of glyphs printed next to a font's (its own codes past the letters); a sprite run otherwise
    bool SpriteBank(const std::set<BankSeed>& seeds)
    {
        // The records drawn, then the ones after each
        std::map<uint16_t, BankRecord> records;
        const auto mark = [&](const BankRecord& r) {
            for (uint32_t a = r.header; a < r.header + r.Bytes(_format); ++a)
                _inBank[a] = 1;
        };
        for (const BankSeed& seed : seeds)
        {
            BankRecord r;
            if (seed.pair == _format.pair && seed.data >= _format.bytes && _inBank[seed.data] == 0 &&
                Record(seed.data - _format.bytes, r) && r.width == seed.width && r.height == seed.height)
            {
                r.drawn = true;
                records[r.header] = r;
            }
        }
        if (records.empty())
            return false;
        const auto chain = [&](uint32_t at) {
            BankRecord r;
            while (at < 0x10000 && records.count(static_cast<uint16_t>(at)) == 0 && _inBank[at] == 0 && Record(at, r))
            {
                records.emplace(r.header, r);
                at += r.Bytes(_format);
            }
        };
        for (const auto& [header, r] : std::map<uint16_t, BankRecord>(records))
        {
            mark(r);
            chain(header + r.Bytes(_format));
        }
        for (const auto& [header, r] : records)
            mark(r);
        // Pointers to the records before: chains that end on a known record
        for (int round = 0; round < 8; ++round)
        {
            std::map<uint16_t, std::vector<BankRecord>> found;  // the word's address -> its chain
            for (uint32_t at = 0x4000; at + 1 < 0x10000; ++at)
            {
                const uint32_t p = _context.Start(static_cast<uint16_t>(at)) |
                                   (_context.Start(static_cast<uint16_t>(at + 1)) << 8);
                if (p < 0x4000 || _inBank[p] != 0)
                    continue;
                std::vector<BankRecord> path;
                BankRecord r;
                uint32_t to = p;
                while (path.size() < kMaxAnchorChain && to < 0x10000 && (path.empty() || _inBank[to] == 0) &&
                       Record(to, r))
                {
                    path.push_back(r);
                    to += r.Bytes(_format);
                    if (to < 0x10000 && records.count(static_cast<uint16_t>(to)) != 0)
                    {
                        found.emplace(static_cast<uint16_t>(at), path);
                        break;
                    }
                }
            }
            std::vector<const std::vector<BankRecord>*> accepted;
            for (auto it = found.begin(); it != found.end();)
            {
                auto end = std::next(it);
                while (end != found.end() && end->first - std::prev(end)->first <= 4 &&
                       (end->first - std::prev(end)->first) % 2 == 0)
                    ++end;
                const auto words = static_cast<uint32_t>(std::distance(it, end));
                for (auto w = it; w != end; ++w)
                {
                    const uint8_t op = _context.Start(static_cast<uint16_t>(w->first - 1));
                    const bool immediate = (op == 0x01 || op == 0x11 || op == 0x21) &&
                                           (_context.Use(static_cast<uint16_t>(w->first - 1)).flags & use::kOpcode) != 0;
                    if (words >= kMinTableWords || (immediate && w->second.size() <= kMaxImmediateChain))
                        accepted.push_back(&w->second);
                }
                it = end;
            }
            bool added = false;
            for (const std::vector<BankRecord>* path : accepted)
                for (const BankRecord& r : *path)
                    if (_inBank[r.header] == 0 && records.emplace(r.header, r).second)
                        added = true;
            if (!added)
                break;
            for (const auto& [header, r] : records)
                mark(r);
        }

        // Glyphs printed side by side: two records the columns of one frame
        std::map<uint32_t, uint16_t> rowOf;  // a byte of a record's first row -> its header
        for (const auto& [header, r] : records)
            for (uint32_t c = 0; c < r.width; ++c)
                rowOf[header + _format.bytes + c * _format.pair + _format.pair - 1] = header;
        std::map<uint16_t, std::set<uint16_t>> beside;
        for (const DrawnFrame& f : _context.l1.drawnFrames)
        {
            if (f.width < 2 || f.colStep == 0 || f.height > kMaxGlyphRows)
                continue;
            const int64_t base = Linear(_context.l1, f.base);
            uint32_t before = 0x10000;
            for (uint32_t c = 0; c < f.width; ++c)
            {
                const int64_t at = base + static_cast<int64_t>(c) * f.colStep;
                const auto it = at >= 0 && at < 0x10000 ? rowOf.find(static_cast<uint32_t>(at)) : rowOf.end();
                const uint32_t header = it != rowOf.end() ? it->second : 0x10000;
                if (before < 0x10000 && header < 0x10000 && before != header)
                {
                    beside[static_cast<uint16_t>(before)].insert(static_cast<uint16_t>(header));
                    beside[static_cast<uint16_t>(header)].insert(static_cast<uint16_t>(before));
                }
                before = header;
            }
        }

        // Runs of one size, back to back in one 16 K bank
        std::vector<std::vector<const BankRecord*>> runs;
        for (const auto& [header, r] : records)
        {
            const uint32_t end = header + r.Bytes(_format) - 1;
            if ((header >> 14) != (end >> 14))
                continue;  // a record across two banks: no claim holds it
            const BankRecord* last = runs.empty() ? nullptr : runs.back().back();
            if (last == nullptr || last->header + last->Bytes(_format) != header || last->width != r.width ||
                last->height != r.height || (last->header >> 14) != (header >> 14))
                runs.emplace_back();
            runs.back().push_back(&r);
        }
        std::map<uint16_t, size_t> runOf;
        for (size_t i = 0; i < runs.size(); ++i)
            for (const BankRecord* r : runs[i])
                runOf[r->header] = i;
        const auto glyphs = [&](size_t i) {
            const BankRecord& r = *runs[i].front();
            return _format.pair == 1 && r.width <= 2 && r.height >= 5 && r.height <= kMaxGlyphRows;
        };
        std::vector<bool> font(runs.size(), false);
        std::vector<size_t> printedWith(runs.size(), runs.size());
        for (size_t i = 0; i < runs.size(); ++i)
        {
            if (!glyphs(i))
                continue;
            size_t printed = 0;
            for (const BankRecord* r : runs[i])
            {
                const auto it = beside.find(r->header);
                printed += it != beside.end() && std::any_of(it->second.begin(), it->second.end(),
                                                              [&](uint16_t o) { return runOf[o] == i; })
                               ? 1
                               : 0;
            }
            font[i] = printed >= kMinGlyphs;
        }
        for (size_t i = 0; i < runs.size(); ++i)
        {
            if (font[i] || !glyphs(i))
                continue;
            std::map<size_t, uint32_t> links;
            for (const BankRecord* r : runs[i])
                if (const auto it = beside.find(r->header); it != beside.end())
                    for (const uint16_t o : it->second)
                        if (font[runOf[o]])
                            ++links[runOf[o]];
            for (const auto& [j, n] : links)
                if (n >= 2 && (printedWith[i] == runs.size() || n > links[printedWith[i]]))
                    printedWith[i] = j;
        }

        size_t bankRecords = records.size(), bankDrawn = 0;
        for (const auto& [header, r] : records)
            bankDrawn += r.drawn ? 1 : 0;
        for (size_t i = 0; i < runs.size(); ++i)
        {
            const std::vector<const BankRecord*>& run = runs[i];
            const BankRecord& r = *run.front();
            const uint32_t size = r.Bytes(_format);
            const uint32_t drawn = static_cast<uint32_t>(
                std::count_if(run.begin(), run.end(), [](const BankRecord* x) { return x->drawn; }));
            const bool isFont = font[i] || printedWith[i] < runs.size();
            Region region;
            region.kind = isFont ? RegionKind::Font : RegionKind::Sprite;
            region.detector = DetectorId::Graphics;
            region.status = drawn != 0 ? RegionStatus::Observed : RegionStatus::Inferred;
            region.geometry.width = static_cast<uint16_t>(r.width * 8);
            region.geometry.height = r.height;
            region.geometry.frames = static_cast<uint16_t>(run.size());
            region.geometry.stride = static_cast<uint16_t>(size);
            region.geometry.prefix = static_cast<uint16_t>(_format.bytes);
            region.geometry.layout = "rows";
            region.geometry.mask = _format.pair == 2;
            region.confidence = drawn != 0 ? confidence::FromCount(700, 950, drawn, 2)
                                           : confidence::FromCount(600, 850, bankDrawn, 16);
            const std::string size2 =
                std::to_string(r.width * 8) + " x " + std::to_string(r.height) + (_format.pair == 2 ? ", masked" : "");
            const std::string header = std::to_string(_format.bytes) + "-byte header before each";
            if (isFont)
                region.title = "Font, " + std::to_string(run.size()) + " characters of " + size2 + " (" + header +
                               (font[i] ? "" : "; printed beside the font at " + Hex(runs[printedWith[i]].front()->header)) +
                               ")";
            else
                region.title = (run.size() == 1 ? "Sprite, " + size2 : "Sprites, " + std::to_string(run.size()) +
                                                                            " of " + size2) +
                               " (" + header + ")";
            region.evidence.push_back(analysis::Evidence{
                "bank", static_cast<uint16_t>(records.begin()->first), bankRecords,
                "a sprite bank of " + std::to_string(bankRecords) + " records, " + std::to_string(bankDrawn) +
                    " of them drawn: each a header (" +
                    (_format.scale == 0 ? "every record " + std::to_string(_format.width) + " bytes wide"
                                        : "width x " + std::to_string(_format.scale) + " at +" +
                                              std::to_string(_format.widthAt)) +
                    ", height at +" + std::to_string(_format.heightAt) + "), then its rows" + (_format.pair == 2 ? std::string(" (a mask byte before each graphic byte)") : "") +
                    ", the next record right after"});
            region.evidence.push_back(analysis::Evidence{"frames", r.header, drawn,
                                                         std::to_string(drawn) + " of the " +
                                                             std::to_string(run.size()) + " records drawn"});
            if (isFont)
                region.evidence.push_back(analysis::Evidence{"glyphs", r.header, 0,
                                                             "records printed side by side, letters of a word"});
            Add(std::move(region), r.header, static_cast<uint16_t>(run.back()->header + size - 1));
            if (isFont)
                _fonts.emplace_back(r.header, run.back()->header + size - 1);
        }
        return true;
    }

    /// The physical key of a Z80 address in the view
    PhysKey KeyAt(uint16_t z80) const
    {
        return _view >= 0 && z80 >= 0xC000 ? MakeKey(static_cast<uint16_t>(_view), z80) : _context.Key(z80);
    }
    const ByteUse* UseAt(uint16_t z80) const { return _context.l1.UseAt(KeyAt(z80)); }
    /// The byte at the end (else the start) of the replay; false when the page was mapped at neither
    bool EndAt(uint16_t z80, uint8_t& value) const
    {
        return _context.l1.EndByte(KeyAt(z80), value) || _context.l1.StartByte(KeyAt(z80), value);
    }

    /// The frames of the pieces the drawn bytes form (Z80, through the start's paging), the largest first: a piece
    /// inside a larger frame already taken is a partial draw of it (a sprite clipped at the screen's edge)
    void DrawnFrames(std::map<Shape, std::map<uint16_t, Frame>>& frames) const
    {
        std::map<uint16_t, std::set<size_t>> blitsOf;  // source reader (Z80) -> blits
        for (size_t i = 0; i < _blits.size(); ++i)
            for (const uint16_t r : _blits[i].readers)
                blitsOf[r].insert(i);
        struct Start
        {
            Shape shape;
            uint16_t start = 0;
            uint32_t size = 0;
            const Piece* piece = nullptr;
            bool blank = false;
        };
        std::vector<Start> starts;
        for (const Piece& p : _pieces)
        {
            int bank = -1;
            if (_view >= 0)
                bank = KeyPage(p.first) == _view ? 3 : -1;
            else
                for (int b = 0; b < 4; ++b)
                    if (_context.l1.startBanks[b] == KeyPage(p.first))
                        bank = b;
            if (bank < 0 || p.stride > 2)
                continue;
            const auto z80 = static_cast<uint32_t>(bank * 0x4000 + KeyOffset(p.first));
            // One value in every byte is no picture on its own (a back buffer's blank, never drawn into); among frames
            // of its shape with a picture it is one (a tile set's space)
            const uint32_t lines = p.columns ? p.width : p.height, length = p.columns ? p.height : p.width;
            const uint32_t lineStep = p.pitch != 0 ? p.pitch : length * p.stride;
            const uint32_t lastZ80 = z80 + (lines - 1) * lineStep + (length - 1) * p.stride;
            uint8_t first = 0, value = 0;
            bool blank = EndAt(static_cast<uint16_t>(z80), first);
            for (uint32_t l = 0; l < lines && blank; ++l)
                for (uint32_t c = 0; c < length && blank; ++c)
                    blank = EndAt(static_cast<uint16_t>(z80 + l * lineStep + c * p.stride), value) && value == first;
            // Bytes the replay never read are no source of a picture (the provenance lost a value on its way: a table
            // lookup, a register kept across draws)
            constexpr uint16_t kReads = use::kRead | use::kStackRead | use::kBlockRead | use::kWordRead;
            uint32_t read = 0;
            for (uint32_t l = 0; l < lines; ++l)
                for (uint32_t c = 0; c < length; ++c)
                {
                    const ByteUse* u = UseAt(static_cast<uint16_t>(z80 + l * lineStep + c * p.stride));
                    read += u == nullptr || (u->flags & kReads) != 0 ? 1 : 0;  // a page not tracked: unknown
                }
            if (read * 2 < lines * length)
                continue;

            Shape shape{p.width, p.height, p.stride == 2, p.columns, static_cast<uint16_t>(p.pitch)};
            uint32_t start = z80;
            if (p.stride == 2)
            {
                // The byte between graphic bytes the drawing read: a mask, before or after each graphic byte
                const ByteUse* below = z80 > 0 ? UseAt(static_cast<uint16_t>(z80 - 1)) : nullptr;
                const ByteUse* above = lastZ80 < 0xFFFF ? UseAt(static_cast<uint16_t>(lastZ80 + 1)) : nullptr;
                // (any data read: a sprite drawn by POP reads its bytes off the stack)
                const bool before = below != nullptr && (below->flags & kReads) != 0;
                const bool after = above != nullptr && (above->flags & kReads) != 0;
                if (!before && !after)
                    continue;
                start = before ? z80 - 1 : z80;
            }
            const uint32_t size = shape.Bytes();
            if (start + size - 1 > 0xFFFF)
                continue;
            starts.push_back(Start{shape, static_cast<uint16_t>(start), size, &p, blank});
        }
        std::stable_sort(starts.begin(), starts.end(), [](const Start& a, const Start& b) { return a.size > b.size; });
        std::set<uint32_t> taken;  // the bytes of the frames kept
        for (const Start& s : starts)
        {
            const std::vector<uint32_t> offsets = s.shape.Offsets();
            if (std::any_of(offsets.begin(), offsets.end(), [&](uint32_t o) { return taken.count(s.start + o) != 0; }))
                continue;
            for (const uint32_t o : offsets)
                taken.insert(s.start + o);
            Frame& f = frames[s.shape][s.start];
            f.blank = s.blank;
            f.writers.insert(s.piece->writers.begin(), s.piece->writers.end());
            f.draws += s.piece->draws;
            // The blits that read the frame's bytes (the drawing, not the copy of a back buffer to the screen)
            for (const PhysKey key : {s.piece->first, s.piece->last})
                if (const ByteUse* u = _context.l1.UseAt(key))
                    for (const PhysKey reader : {u->firstReader, u->lastReader})
                        if (const Instruction* ins = reader != kNoKey ? _context.l1.InstructionAt(reader) : nullptr)
                        {
                            const auto it2 = blitsOf.find(ins->z80);
                            if (it2 != blitsOf.end())
                                f.blits.insert(it2->second.begin(), it2->second.end());
                        }
        }
    }

    bool Split(uint32_t a, uint32_t b) const
    {
        bool one = false, both = false;
        for (const PointerRange& p : _pointers)
        {
            const bool inA = p.lo <= a && a <= p.hi;
            const bool inB = p.lo <= b && b <= p.hi;
            both = both || (inA && inB);
            one = one || (inA != inB);
        }
        if (one && !both)
            return true;
        for (const media::BufferCopy& r : _buffers)
        {
            const bool inA = r.lo <= a && a <= r.hi;
            const bool inB = r.lo <= b && b <= r.hi;
            if (inA != inB)
                return true;
        }
        return false;
    }

    void Objects(const Shape& shape, const std::map<uint16_t, Frame>& starts)
    {
        const uint32_t size = shape.Bytes();
        const bool glyphs = shape.rowBytes == 1 && shape.height == 8 && !shape.mask;
        std::vector<Object> objects;
        for (const auto& [start, frame] : starts)
        {
            if (!objects.empty())
            {
                Object& cur = objects.back();
                const uint32_t gap = static_cast<uint32_t>(start) - cur.last;
                bool joined = false;
                if (cur.step == 0)
                {
                    // Frames of an interleaved set (each line a page on) sit a line's width apart, those between
                    // never drawn
                    const uint32_t line = shape.Line();
                    if (shape.pitch != 0 && gap < size && gap % line == 0 && !shape.Overlaps(gap) &&
                        gap / line <= (glyphs ? 32u : kMaxHole + 1u) && !Split(cur.last, start))
                    {
                        cur.step = line;
                        cur.filled += gap / line - 1;
                        cur.sparse = glyphs;
                        joined = true;
                    }
                    else if (((gap >= size && gap <= size + 8) || (gap < size && !shape.Overlaps(gap))) &&
                             !Split(cur.last, start))
                    {
                        cur.step = gap;
                        joined = true;
                    }
                    else if (glyphs && shape.pitch == 0 && gap % 8 == 0 && gap <= 32 * 8 && !Split(cur.last, start))
                    {
                        cur.step = 8;
                        cur.sparse = true;
                        cur.filled += gap / 8 - 1;
                        joined = true;
                    }
                    else if (gap < size)
                        continue;  // inside the frame before: a partial draw
                }
                else
                {
                    // Frames of an interleaved set (each line a page further) leave the frames never drawn between
                    const uint32_t n = gap / cur.step;
                    const bool interleaved = cur.step < size;
                    if (gap % cur.step == 0 && (n == 1 || (cur.sparse && n <= 32) || (interleaved && n <= kMaxHole + 1)) &&
                        !Split(cur.last, start))
                    {
                        cur.filled += n - 1;
                        joined = true;
                    }
                    else if (gap < cur.step)
                        continue;  // off the lattice
                }
                if (joined)
                {
                    cur.last = start;
                    ++cur.seen;
                    cur.blits.insert(frame.blits.begin(), frame.blits.end());
                    cur.writers.insert(frame.writers.begin(), frame.writers.end());
                    cur.drawn.push_back(start);
                    cur.draws += frame.draws;
                    continue;
                }
            }
            Object o;
            o.first = o.last = start;
            o.blits = frame.blits;
            o.writers = frame.writers;
            o.drawn.push_back(start);
            o.draws = frame.draws;
            objects.push_back(std::move(o));
        }
        // Character sets first: the frames of one font can fall into several objects (a pointer splits them), each
        // inside the set the others name is one of its glyphs
        std::vector<std::pair<uint32_t, uint32_t>> fonts;
        std::vector<bool> done(objects.size(), false);
        for (size_t i = 0; i < objects.size(); ++i)
        {
            const Object& o = objects[i];
            std::pair<uint32_t, uint32_t> range;
            if (glyphs &&
                (((o.sparse || o.step == shape.GlyphStep()) && Font(shape, o, range)) || RomCharacters(shape, o, range)))
            {
                fonts.push_back(range);
                _fonts.push_back(range);
                done[i] = true;
            }
        }
        if (glyphs)
        {
            // Neighbouring objects on one lattice of glyphs, the gaps the size of a character set's unused codes (any
            // gap in the ROM's set: its lattice is known)
            const uint32_t g = shape.GlyphStep();
            const auto romSet = [&](const Object& o) {
                return shape.pitch == 0 && o.first >= kRomCharset && o.last + shape.Tail() < 0x4000 &&
                       (o.first - kRomCharset) % 8 == 0;
            };
            size_t i = 0;
            while (i < objects.size())
            {
                Object group = objects[i];
                size_t j = i + 1;
                while (j < objects.size() &&
                       (((objects[j].step == 0 || objects[j].step == g) && (group.step == 0 || group.step == g) &&
                         (objects[j].first - group.last) % g == 0 && objects[j].first - group.last <= 32u * g) ||
                        (romSet(group) && romSet(objects[j]) && objects[j].step % 8 == 0 && group.step % 8 == 0)))
                {
                    group.filled += (objects[j].first - group.last) / g - 1 + objects[j].filled;
                    group.seen += objects[j].seen;
                    group.last = objects[j].last;
                    group.blits.insert(objects[j].blits.begin(), objects[j].blits.end());
                    group.writers.insert(objects[j].writers.begin(), objects[j].writers.end());
                    group.drawn.insert(group.drawn.end(), objects[j].drawn.begin(), objects[j].drawn.end());
                    group.draws += objects[j].draws;
                    group.step = g;
                    group.sparse = true;
                    ++j;
                }
                std::pair<uint32_t, uint32_t> range;
                if (std::none_of(done.begin() + static_cast<std::ptrdiff_t>(i),
                                 done.begin() + static_cast<std::ptrdiff_t>(j), [](bool d) { return d; }) &&
                    ((j > i + 1 && Font(shape, group, range)) || GlyphSet(shape, group, range)))
                {
                    fonts.push_back(range);
                    _fonts.push_back(range);
                }
                i = j;
            }
        }
        for (size_t i = 0; i < objects.size(); ++i)
        {
            Object& o = objects[i];
            if (done[i] || std::any_of(fonts.begin(), fonts.end(), [&](const auto& f) {
                    return o.first >= f.first && o.last + shape.Tail() <= f.second &&
                           (o.first - f.first) % shape.GlyphStep() == 0;
                }))
                continue;
            if (o.sparse)
            {
                // Not a character set: each run of adjacent frames is an object of its own
                std::vector<uint16_t> drawn;
                for (const auto& [start, frame] : starts)
                    if (start >= o.first && start <= o.last && (start - o.first) % shape.GlyphStep() == 0)
                        drawn.push_back(start);
                Object part;
                for (size_t i = 0; i < drawn.size(); ++i)
                {
                    if (i == 0 || drawn[i] - drawn[i - 1] != shape.GlyphStep())
                    {
                        if (i != 0)
                            _pending.push_back(Pending{shape, part, size});
                        part = Object();
                        part.first = part.last = drawn[i];
                        part.blits = o.blits;
                        part.writers = o.writers;
                        part.drawn = {drawn[i]};
                        continue;
                    }
                    part.drawn.push_back(drawn[i]);
                    part.last = drawn[i];
                    part.step = shape.GlyphStep();
                    ++part.seen;
                }
                if (!drawn.empty())
                    _pending.push_back(Pending{shape, part, size});
                continue;
            }
            _pending.push_back(Pending{shape, o, size});
        }
    }

    /// The walk around every blit of @p o read a map, no text: the frames are picked by map cells (tiles drawn by a
    /// print routine), not by character codes
    bool PickedByMap(const Object& o) const
    {
        const std::vector<const Blit*> blits = BlitsOf(o);
        if (blits.empty())
            return false;  // no blit loop (an unrolled LDI chain): no map read either
        for (const Blit* b : blits)
        {
            if (b->mapRead == nullptr || b->mapRead->rangesMore)
                return false;
            for (const Interval& r : b->mapRead->ranges)
            {
                const uint32_t lo = r.z80;
                const uint32_t hi = lo + (r.hi - r.lo);
                for (const auto& [tlo, thi] : _textRanges)
                    if (lo <= thi && tlo <= hi)
                        return false;
            }
        }
        return true;
    }

    /// A character set: the frames on base + 8 * code for the text's characters (base + code: each pixel row a page
    /// on). The claim spans the codes drawn: the rest of the 96 is a guess (a set's frames that fit the text's codes
    /// on some base by chance; a font that starts past the space), named in the title only. The ROM's set is 96
    bool Font(const Shape& shape, const Object& o, std::pair<uint32_t, uint32_t>& range)
    {
        if (_text.empty() || o.seen < 3 || PickedByMap(o))
            return false;
        const int64_t g = shape.GlyphStep();
        const std::vector<uint16_t>& drawn = o.drawn;
        int64_t bestBase = -1;
        uint32_t bestCount = 0;
        for (const uint16_t s : drawn)
            for (const uint8_t c : _text)
            {
                const int64_t base = static_cast<int64_t>(s) - g * c;
                if (base < 0)
                    continue;
                uint32_t count = 0;
                for (const uint16_t t : drawn)
                {
                    const int64_t code = (static_cast<int64_t>(t) - base) / g;
                    count += (code >= 32 && code < 128 && _text.count(static_cast<uint8_t>(code)) != 0) ? 1 : 0;
                }
                if (count > bestCount || (count == bestCount && base < bestBase))
                {
                    bestCount = count;
                    bestBase = base;
                }
            }
        // Frames drawn (not filled) that are text characters: at least three, three quarters of them, and not one run
        // of codes (frames side by side fit consecutive codes on some base: an animation, a strip of tiles)
        uint32_t drawnText = 0, drawnAll = 0;
        std::set<int64_t> codes;
        for (const uint16_t s : o.drawn)
            if (s >= o.first && s <= o.last && (s - o.first) % g == 0)
            {
                ++drawnAll;
                const int64_t code = (static_cast<int64_t>(s) - bestBase) / g;
                const bool text = code >= 32 && code < 128 && _text.count(static_cast<uint8_t>(code)) != 0;
                drawnText += text ? 1 : 0;
                if (text)
                    codes.insert(code);
            }
        if (bestBase < 0 || drawnText < 3 || drawnText * 4 < drawnAll * 3 || codes.size() < 3 ||
            *codes.rbegin() - *codes.begin() + 1 == static_cast<int64_t>(codes.size()))
            return false;
        const uint32_t setLo = static_cast<uint32_t>(bestBase + 32 * g);
        const uint32_t setHi = static_cast<uint32_t>(bestBase + 127 * g) + shape.Tail();
        if (setHi > 0xFFFF || (setLo >> 14) != (setHi >> 14) || o.first < setLo || o.last + shape.Tail() > setHi)
            return false;
        const bool rom = setHi < 0x4000;
        const uint32_t lo = rom ? setLo : o.first;
        const uint32_t hi = rom ? setHi : o.last + shape.Tail();
        const auto firstCode = static_cast<uint32_t>((lo - bestBase) / g);
        const auto lastCode = static_cast<uint32_t>((static_cast<int64_t>(hi - shape.Tail()) - bestBase) / g);
        Region r;
        r.kind = RegionKind::Font;
        r.detector = DetectorId::Graphics;
        r.status = RegionStatus::Observed;
        r.geometry.width = 8;
        r.geometry.height = 8;
        r.geometry.frames = static_cast<uint16_t>(lastCode - firstCode + 1);
        r.geometry.stride = static_cast<uint16_t>(g);
        r.geometry.pitch = shape.pitch;
        r.geometry.layout = "rows";
        r.confidence = confidence::FromCount(700, 950, drawnText, 8);
        r.title = rom ? "Font, 96 characters of 8 x 8 (codes 32-127)"
                      : "Font, " + std::to_string(r.geometry.frames) + " characters of 8 x 8 (codes " +
                            std::to_string(firstCode) + "-" + std::to_string(lastCode) + " of a set at " +
                            Hex(static_cast<uint16_t>(bestBase)) + " + " + std::to_string(g) + " x code)";
        Evidence(r, o, shape);
        r.evidence.push_back(analysis::Evidence{"characters", static_cast<uint16_t>(bestBase + 32 * g), drawnText,
                                                "glyphs drawn at base + step x the code of a text character"});
        Add(std::move(r), static_cast<uint16_t>(lo), static_cast<uint16_t>(hi));
        range = {lo, hi};
        return true;
    }

    /// The routine that writes @p o to the screen also reads its frames: printed straight to the screen, not composed
    /// in a back buffer the screen is copied from (a playfield of tiles)
    bool PrintedDirectly(const Object& o) const
    {
        std::set<const Routine*> writers;
        for (const PhysKey w : o.writers)
            if (const Routine* routine = _context.l1.RoutineOf(w))
                writers.insert(routine);
        for (const uint16_t s : o.drawn)
            if (const ByteUse* u = UseAt(s))
                for (const PhysKey reader : {u->firstReader, u->lastReader})
                    if (reader != kNoKey && writers.count(_context.l1.RoutineOf(reader)) != 0)
                        return true;
        return false;
    }

    /// Glyphs of a game's own codes (no text the detectors found names them): 8 x 8 frames on one lattice (8 bytes a
    /// glyph, or the frames' own step: a byte or more before each), eight or more different ones drawn, printed
    /// straight to the screen and not picked by a map (a tile set). The claim spans the frames from the first to the
    /// last; glyphs of the ROM's character set that the game's own code printed, all 96 of it
    bool GlyphSet(const Shape& shape, const Object& o, std::pair<uint32_t, uint32_t>& range)
    {
        const std::set<uint16_t> glyphs(o.drawn.begin(), o.drawn.end());
        if (glyphs.size() < kMinGlyphs || PickedByMap(o) || !PrintedDirectly(o))
            return false;
        const bool rom = o.first < 0x4000;
        if (rom && (shape.pitch != 0 || o.first < kRomCharset || (o.first - kRomCharset) % 8 != 0 ||
                    o.last + shape.Tail() >= 0x4000 ||
                    std::any_of(o.writers.begin(), o.writers.end(), [&](PhysKey w) {
                        uint16_t z80 = 0;
                        return !_context.Z80Of(w, z80) || z80 < 0x4000;
                    })))
            return false;  // drawn by the ROM (its print routine), or no glyphs of its set
        const uint32_t step = !o.sparse && o.step > 8 && shape.pitch == 0 ? o.step : shape.GlyphStep();
        const uint32_t prefix = step > 8 ? step - 8 : 0;
        if (o.first < prefix)
            return false;
        const uint32_t lo = rom ? kRomCharset : o.first - prefix;
        const uint32_t hi = rom ? 0x3FFFu : static_cast<uint32_t>(o.last) + shape.Tail();
        if (hi > 0xFFFF || (lo >> 14) != (hi >> 14))
            return false;
        Region r;
        r.kind = RegionKind::Font;
        r.detector = DetectorId::Graphics;
        r.status = RegionStatus::Observed;
        r.geometry.width = 8;
        r.geometry.height = 8;
        r.geometry.frames = static_cast<uint16_t>(rom ? 96 : (o.last - o.first) / step + 1);
        r.geometry.stride = static_cast<uint16_t>(step);
        r.geometry.prefix = static_cast<uint16_t>(prefix);
        r.geometry.pitch = shape.pitch;
        r.geometry.layout = "rows";
        r.confidence = confidence::FromCount(500, 800, glyphs.size(), 16);
        r.title = rom ? "Font, 96 characters of 8 x 8 (codes 32-127): the ROM's, printed by the game's own code"
                      : "Font, " + std::to_string(r.geometry.frames) + " characters of 8 x 8 (the game's own codes)";
        Evidence(r, o, shape);
        r.evidence.push_back(analysis::Evidence{"glyphs", o.first, static_cast<uint32_t>(glyphs.size()),
                                                "different glyphs drawn, one lattice of glyphs"});
        Add(std::move(r), static_cast<uint16_t>(lo), static_cast<uint16_t>(hi));
        range = {lo, hi};
        return true;
    }

    /// Characters in RAM the ROM drew (its character loop, PR_ALL): the user-defined graphics at (UDG) + 8 x (code -
    /// 144), 21 of them, or the character set at (CHARS) + 8 x code, 96; what lies in neither, the frames drawn
    bool RomCharacters(const Shape& shape, const Object& o, std::pair<uint32_t, uint32_t>& range)
    {
        if (_romPrint == kNoKey || o.first < 0x4000 || shape.pitch != 0)
            return false;
        for (const PhysKey w : o.writers)
            if (KeyPage(w) != KeyPage(_romPrint) || KeyOffset(w) < kPrAll || KeyOffset(w) >= kPrAllEnd)
                return false;
        for (const Blit* b : BlitsOf(o))
        {
            if (KeyPage(b->loop->header) != KeyPage(_romPrint) || b->loop->z80 < kPrAll || b->loop->z80 >= kPrAllEnd)
                return false;
        }
        if (o.writers.empty() && o.blits.empty())
            return false;
        const auto word = [&](uint16_t at) {
            return static_cast<uint32_t>(_context.End(at) | (_context.End(static_cast<uint16_t>(at + 1)) << 8));
        };
        const uint32_t udg = word(0x5C7B), chars = word(0x5C36);
        const uint32_t end = static_cast<uint32_t>(o.last) + 7;
        uint32_t lo = o.first, hi = end, frames = (o.last - o.first) / 8 + 1;
        std::string what = "Characters";
        if (o.first >= udg && end < udg + 21 * 8)
        {
            lo = udg, hi = udg + 21 * 8 - 1, frames = 21;
            what = "User-defined graphics, 21 characters of 8 x 8 (codes 144-164)";
        }
        else if (o.first >= chars + 32 * 8 && end < chars + 128 * 8)
        {
            lo = chars + 32 * 8, hi = chars + 128 * 8 - 1, frames = 96;
            what = "Font, 96 characters of 8 x 8 (codes 32-127)";
        }
        if (hi > 0xFFFF || (lo >> 14) != (hi >> 14))
            return false;
        Region r;
        r.kind = RegionKind::Font;
        r.detector = DetectorId::Graphics;
        r.status = RegionStatus::Observed;
        r.geometry.width = 8;
        r.geometry.height = 8;
        r.geometry.frames = frames;
        r.geometry.stride = 8;
        r.geometry.layout = "rows";
        r.confidence = confidence::FromCount(700, 900, o.seen, 8);
        r.title = what + ", printed through the ROM";
        Evidence(r, o, shape);
        Add(std::move(r), static_cast<uint16_t>(lo), static_cast<uint16_t>(hi));
        range = {lo, hi};
        return true;
    }

    std::vector<const Blit*> BlitsOf(const Object& o) const
    {
        std::vector<const Blit*> out;
        for (const size_t i : o.blits)
            out.push_back(&_blits[i]);
        return out;
    }

    void Evidence(Region& r, const Object& o, const Shape& shape) const
    {
        if (!o.writers.empty())
        {
            const Instruction* ins = _context.l1.InstructionAt(*o.writers.begin());
            const Routine* routine = _context.l1.RoutineOf(*o.writers.begin());
            r.evidence.push_back(analysis::Evidence{
                "drawn", o.first, o.draws,
                "bytes written to the screen " + std::to_string(o.draws) + " times" +
                    (ins != nullptr ? ", by " + Hex(ins->z80) : std::string()) +
                    (routine != nullptr ? " in routine " + Hex(routine->z80) : std::string()) + "; " +
                    std::to_string(shape.height) + (shape.columns ? " rows, stored in columns of " : " rows of ") +
                    std::to_string(shape.rowBytes) + " bytes" + (shape.mask ? " (mask, graphic)" : "")});
        }
        for (const Blit* b : BlitsOf(o))
        {
            const Routine& routine = _context.l1.routines[b->loop->routine];
            r.evidence.push_back(analysis::Evidence{
                "blit", b->loop->z80, b->instances,
                std::to_string(shape.height) + " rows of " + std::to_string(b->rowBytes) + " bytes" +
                    (b->mask ? " (mask, graphic)" : "") + ", source stride " + std::to_string(b->srcStride) +
                    ", in routine " + Hex(routine.z80)});
            if (r.evidence.size() >= 4)
                break;
        }
    }

    static std::string Hex(uint16_t v)
    {
        char text[8];
        std::snprintf(text, sizeof(text), "$%04X", v);
        return text;
    }

    void Claim(const Shape& shape, const Object& o, uint32_t size)
    {
        if (o.first < 0x4000)
            return;  // the ROM's bytes (its character set drawn as glyphs that are no font of the game)
        uint32_t step = o.step != 0 ? o.step : size;
        uint32_t prefix = step > size ? step - size : 0;
        uint32_t lo = o.first - prefix;
        uint32_t hi = static_cast<uint32_t>(o.last) + size - 1;
        uint32_t frames = (o.last - o.first) / step + 1;
        if (hi > 0xFFFF || o.first < prefix)
            return;
        // A record array with the same step around the frames: its extent
        for (const Region* rec : _context.out.store.Overlapping(KeyAt(static_cast<uint16_t>(lo)),
                                                                KeyAt(static_cast<uint16_t>(hi))))
            if (rec->kind == RegionKind::RecordArray && rec->geometry.stride == step && rec->z80 <= lo &&
                rec->Z80End() >= hi && (lo - rec->z80) % step == 0)
            {
                lo = rec->z80;
                hi = rec->Z80End();
                frames = rec->geometry.count != 0 ? rec->geometry.count : (hi - lo + 1) / step;
                break;
            }
        if ((lo >> 14) != (hi >> 14))
            return;
        bool tile = false;
        for (const Blit* b : BlitsOf(o))
            tile = tile || b->mapLoop != nullptr;
        Region r;
        r.kind = tile ? RegionKind::Tile : RegionKind::Sprite;
        r.detector = DetectorId::Graphics;
        r.status = o.filled != 0 ? RegionStatus::Inferred : RegionStatus::Observed;
        r.geometry.width = static_cast<uint16_t>(shape.rowBytes * 8);
        r.geometry.height = shape.height;
        r.geometry.frames = static_cast<uint16_t>(frames);
        r.geometry.stride = static_cast<uint16_t>(step);
        r.geometry.prefix = static_cast<uint16_t>(prefix);
        r.geometry.mask = shape.mask;
        r.geometry.layout = shape.columns ? "columns" : "rows";
        r.geometry.pitch = shape.pitch;
        uint64_t dominant = 0, instances = 0;
        for (const Blit* b : BlitsOf(o))
        {
            dominant += b->dominant;
            instances += b->instances;
        }
        uint16_t conf = confidence::FromCount(650, 950, o.seen, 2);
        if (instances != 0 && dominant * 10 < instances * 9)
            conf = confidence::Scale(conf, 9, 10);
        r.confidence = conf;
        Evidence(r, o, shape);
        r.evidence.push_back(analysis::Evidence{"frames", o.first, o.seen,
                                                "frames drawn" + std::string(o.filled ? ", " + std::to_string(o.filled) +
                                                                                            " between them not drawn"
                                                                                      : "")});
        const uint16_t live = static_cast<uint16_t>(lo);
        const uint16_t liveEnd = static_cast<uint16_t>(hi);
        Region copy = r;
        Add(std::move(r), live, liveEnd);

        // Inside a buffer: the same object in each of the buffer's sources, in the page a copy read it from (a
        // source no copy names, one the lattice inferred: the start's paging)
        for (const media::BufferCopy& buffer : _buffers)
        {
            if (_view >= 0)
                break;
            if (!(buffer.lo <= live && buffer.hi >= liveEnd))
                continue;
            std::vector<std::pair<uint16_t, uint16_t>> origins;
            for (const Blit* b : BlitsOf(o))
            {
                const auto runs = media::Runs(_context, b->origins, 0);
                origins.insert(origins.end(), runs.begin(), runs.end());
            }
            for (const uint16_t instance : buffer.instances)
            {
                const uint32_t at = static_cast<uint32_t>(instance) + (live - buffer.lo);
                const uint32_t end = at + (liveEnd - live);
                if (end > 0xFFFF)
                    continue;
                bool shown = false;
                for (const auto& [olo, ohi] : origins)
                    shown = shown || (olo <= end && ohi >= at);
                Region r2 = copy;
                r2.status = shown ? r2.status : RegionStatus::Inferred;
                r2.confidence = shown ? r2.confidence : confidence::Scale(r2.confidence, 3, 4);
                r2.evidence.push_back(analysis::Evidence{"copy", buffer.lo, 0,
                                                         std::string("copied to the buffer at ") + Hex(buffer.lo) +
                                                             (shown ? ", the blit read its bytes there"
                                                                    : ", drawn from the buffer")});
                Add(std::move(r2), static_cast<uint16_t>(at), static_cast<uint16_t>(end), SourcePage(at, live));
            }
        }
    }

    /// The drawn bytes less a working buffer's: a byte drawn more than twice a frame (both screens: twice) and written
    /// as often as it is drawn holds a value computed for each draw (a line buffer a routine fills, shifts and prints
    /// row after row), no picture of its own
    std::vector<DrawnByte> Pictures(const std::vector<DrawnByte>& drawn) const
    {
        std::vector<DrawnByte> out;
        out.reserve(drawn.size());
        for (const DrawnByte& d : drawn)
        {
            const ByteUse* u = _context.l1.UseAt(d.key);
            if (u != nullptr && d.draws > 2ull * d.frames && u->writes >= d.draws)
                continue;
            out.push_back(d);
        }
        return out;
    }

    /// The page a block copy read @p at from to put it at @p to (-1: no copy did)
    int SourcePage(uint32_t at, uint32_t to) const
    {
        for (const BlockCopy& c : _context.l1.copies)
        {
            const uint32_t src = c.dir > 0 ? c.src : static_cast<uint16_t>(c.src - (c.length - 1));
            const uint32_t dst = c.dir > 0 ? c.dst : static_cast<uint16_t>(c.dst - (c.length - 1));
            if (at >= src && at < src + c.length && to >= dst && at - src == to - dst)
                return KeyPage(c.srcKey);
        }
        return -1;
    }

    /// Claims @p r on @p lo..hi, in @p page when one is given (else the view's, else the start's paging)
    void Add(Region r, uint16_t lo, uint16_t hi, int page = -1)
    {
        const auto key = [&](uint16_t z80) {
            return page >= 0 ? MakeKey(static_cast<uint16_t>(page), z80) : KeyAt(z80);
        };
        if (!_claimed.insert({key(lo), key(hi), static_cast<uint8_t>(r.kind)}).second)
            return;
        if (_view < 0)
            r.users = _context.UsersOf(lo, hi);
        _context.Claim(std::move(r), key(lo), lo, hi);
    }
};

}  // namespace

void DetectGraphics(DetectorContext& context)
{
    const media::DisplayInfo display = media::FindDisplay(context);
    const std::vector<media::Blit> blits = media::FindBlits(context, display);
    Builder(context, blits).Run();
}

}  // namespace analysis
