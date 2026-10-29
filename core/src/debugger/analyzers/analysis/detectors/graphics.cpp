/// The graphics detector: fonts, sprites and tiles, from what the blit loops (media.h) drew.
///
/// Every frame start of every blit, grouped by shape (bytes a row, rows, mask), sorted, becomes objects: frames a
/// constant step apart (the frame size, or up to 8 bytes more: a tile's attribute before its rows) are one object;
/// a start inside the previous frame is a partial draw and is dropped. Two neighbouring frames split where the code
/// named one of them with a pointer (an immediate, a page byte, a word whose value addressed it) and named neither
/// pair with one pointer, and at the edge of a buffer (the records detector's copy destinations). A record array
/// with the same step around the frames gives the object its extent (frames the replay did not draw included).
///
/// Kinds: a tile when a loop around the blit walked a map one byte a pass; a font when 8 x 8 frames sit on the
/// lattice of one character set: base + 8 * code for the characters of the text the detectors found (at least three,
/// three quarters of the frames, not one run of consecutive codes; no font when the walk around each of its blits read
/// a map and no text), the claim then covers the 96 characters 32..127 (neighbouring objects on one lattice of 8, gaps
/// of at most 32 codes, are tried as one set too: a pointer into the set splits its glyphs, and each part alone is a
/// run of consecutive codes); 8 x 8 frames in RAM the ROM's character loop
/// (PR_ALL) drew are a font with no text needed: the 21 user-defined graphics at (UDG), the 96 characters at (CHARS) +
/// 256, or else the frames drawn;
/// a sprite otherwise. An object inside a buffer is also claimed in each of the buffer's sources at the same offset:
/// observed where the copy provenance of the bytes the blit read shows that source, inferred otherwise.

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <tuple>

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
constexpr size_t kMinGlyphs = 8;  ///< different glyphs drawn: a font of the game's own codes

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
        int64_t firstLine = -1;
        uint32_t lines = 0, length = 0, bytes = 0;
        std::vector<std::pair<int64_t, uint32_t>> cells;
        for (const size_t i : members)
        {
            const int64_t at = static_cast<int64_t>(KeyOffset(drawn[i].key)) - start;
            const int64_t line = at >= 0 ? at / pitch : (at - pitch + 1) / static_cast<int64_t>(pitch);
            const int64_t within = at - line * pitch;
            if (within % stride != 0)
                continue;
            cells.emplace_back(line, static_cast<uint32_t>(within / stride));
        }
        if (cells.size() < kMinPieceBytes)
            continue;
        for (const auto& [line, cell] : cells)
            firstLine = firstLine < 0 || line < firstLine ? line : firstLine;
        for (const auto& [line, cell] : cells)
        {
            lines = std::max(lines, static_cast<uint32_t>(line - firstLine) + 1);
            length = std::max(length, cell + 1);
            ++bytes;
        }
        const uint32_t across = piece.columns ? lines : length;
        const uint32_t down = piece.columns ? length : lines;
        if (across >= 32 || down <= 1 || down > 192 || bytes * 4 < across * down || length * stride > pitch)
            continue;
        const int64_t base = static_cast<int64_t>(start) + firstLine * pitch;
        if (base < 0 || base + (lines - 1) * pitch + (length - 1) * stride >= 0x4000)
            continue;
        const PhysKey page = MakeKey(KeyPage(drawn[members.front()].key), 0);
        piece.first = page + static_cast<uint32_t>(base);
        piece.last = piece.first + (lines - 1) * pitch + (length - 1) * stride;
        piece.stride = stride;
        piece.pitch = pitch == length * stride ? 0 : pitch;
        piece.width = static_cast<uint16_t>(across);
        piece.height = static_cast<uint16_t>(down);
        for (const size_t i : members)
        {
            piece.draws += drawn[i].draws;
            if (drawn[i].writer != kNoKey && piece.writers.size() < 8)
                piece.writers.insert(drawn[i].writer);
        }
        out.push_back(piece);
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
            _pieces = Pieces(_context.l1.drawn);
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

    void ClaimFrames(const std::map<Shape, std::map<uint16_t, Frame>>& frames)
    {
        _pending.clear();
        _fonts.clear();
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
            const uint32_t lastZ80 = z80 + (p.last - p.first);
            // One value in every byte is no picture on its own (a back buffer's blank, never drawn into); among frames
            // of its shape with a picture it is one (a tile set's space)
            const uint32_t lines = p.columns ? p.width : p.height, length = p.columns ? p.height : p.width;
            const uint32_t lineStep = p.pitch != 0 ? p.pitch : length * p.stride;
            uint8_t first = 0, value = 0;
            bool blank = EndAt(static_cast<uint16_t>(z80), first);
            for (uint32_t l = 0; l < lines && blank; ++l)
                for (uint32_t c = 0; c < length && blank; ++c)
                    blank = EndAt(static_cast<uint16_t>(z80 + l * lineStep + c * p.stride), value) && value == first;

            Shape shape{p.width, p.height, p.stride == 2, p.columns, static_cast<uint16_t>(p.pitch)};
            uint32_t start = z80;
            if (p.stride == 2)
            {
                // The byte between graphic bytes the drawing read: a mask, before or after each graphic byte
                const ByteUse* below = z80 > 0 ? UseAt(static_cast<uint16_t>(z80 - 1)) : nullptr;
                const ByteUse* above = lastZ80 < 0xFFFF ? UseAt(static_cast<uint16_t>(lastZ80 + 1)) : nullptr;
                const bool before = below != nullptr && (below->flags & use::kRead) != 0;
                const bool after = above != nullptr && (above->flags & use::kRead) != 0;
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
        std::map<uint32_t, uint32_t> taken;  // lo -> hi
        for (const Start& s : starts)
        {
            const uint32_t lo = s.start, hi = s.start + s.size - 1;
            auto it = taken.upper_bound(hi);
            if (it != taken.begin() && std::prev(it)->second >= lo)
                continue;
            taken[lo] = hi;
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
                    if (gap >= size && gap <= size + 8 && !Split(cur.last, start))
                    {
                        cur.step = gap;
                        joined = true;
                    }
                    else if (glyphs && gap % 8 == 0 && gap <= 32 * 8 && !Split(cur.last, start))
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
                    const uint32_t n = gap / cur.step;
                    if (gap % cur.step == 0 && (n == 1 || (cur.sparse && n <= 32)) && !Split(cur.last, start))
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
            if (glyphs && (((o.sparse || o.step == 8) && Font(shape, o, range)) || RomCharacters(shape, o, range)))
            {
                fonts.push_back(range);
                _fonts.push_back(range);
                done[i] = true;
            }
        }
        if (glyphs)
        {
            // Neighbouring objects on one lattice of 8, the gaps the size of a character set's unused codes
            size_t i = 0;
            while (i < objects.size())
            {
                Object group = objects[i];
                size_t j = i + 1;
                while (j < objects.size() && (objects[j].step == 0 || objects[j].step == 8) &&
                       (group.step == 0 || group.step == 8) && (objects[j].first - group.last) % 8 == 0 &&
                       objects[j].first - group.last <= 32u * 8)
                {
                    group.filled += (objects[j].first - group.last) / 8 - 1 + objects[j].filled;
                    group.seen += objects[j].seen;
                    group.last = objects[j].last;
                    group.blits.insert(objects[j].blits.begin(), objects[j].blits.end());
                    group.writers.insert(objects[j].writers.begin(), objects[j].writers.end());
                    group.drawn.insert(group.drawn.end(), objects[j].drawn.begin(), objects[j].drawn.end());
                    group.draws += objects[j].draws;
                    group.step = 8;
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
                    return o.first >= f.first && o.last + 7u <= f.second && (o.first - f.first) % 8 == 0;
                }))
                continue;
            if (o.sparse)
            {
                // Not a character set: each run of adjacent frames is an object of its own
                std::vector<uint16_t> drawn;
                for (const auto& [start, frame] : starts)
                    if (start >= o.first && start <= o.last && (start - o.first) % 8 == 0)
                        drawn.push_back(start);
                Object part;
                for (size_t i = 0; i < drawn.size(); ++i)
                {
                    if (i == 0 || drawn[i] - drawn[i - 1] != 8)
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
                    part.step = 8;
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

    /// A character set: the frames on base + 8 * code for the text's characters
    bool Font(const Shape& shape, const Object& o, std::pair<uint32_t, uint32_t>& range)
    {
        if (_text.empty() || o.seen < 3 || PickedByMap(o))
            return false;
        const std::vector<uint16_t>& drawn = o.drawn;
        int64_t bestBase = -1;
        uint32_t bestCount = 0;
        for (const uint16_t s : drawn)
            for (const uint8_t c : _text)
            {
                const int64_t base = static_cast<int64_t>(s) - 8 * c;
                if (base < 0)
                    continue;
                uint32_t count = 0;
                for (const uint16_t t : drawn)
                {
                    const int64_t code = (static_cast<int64_t>(t) - base) / 8;
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
            if (s >= o.first && s <= o.last && (s - o.first) % 8 == 0)
            {
                ++drawnAll;
                const int64_t code = (static_cast<int64_t>(s) - bestBase) / 8;
                const bool text = code >= 32 && code < 128 && _text.count(static_cast<uint8_t>(code)) != 0;
                drawnText += text ? 1 : 0;
                if (text)
                    codes.insert(code);
            }
        if (bestBase < 0 || drawnText < 3 || drawnText * 4 < drawnAll * 3 || codes.size() < 3 ||
            *codes.rbegin() - *codes.begin() + 1 == static_cast<int64_t>(codes.size()))
            return false;
        const uint32_t lo = static_cast<uint32_t>(bestBase) + 32 * 8;
        const uint32_t hi = static_cast<uint32_t>(bestBase) + 128 * 8 - 1;
        if (hi > 0xFFFF || (lo >> 14) != (hi >> 14) || o.first < lo || o.last + 7u > hi)
            return false;
        Region r;
        r.kind = RegionKind::Font;
        r.detector = DetectorId::Graphics;
        r.status = RegionStatus::Observed;
        r.geometry.width = 8;
        r.geometry.height = 8;
        r.geometry.frames = 96;
        r.geometry.stride = 8;
        r.geometry.layout = "rows";
        r.confidence = confidence::FromCount(700, 950, drawnText, 8);
        r.title = "Font, 96 characters of 8 x 8 (codes 32-127)";
        Evidence(r, o, shape);
        r.evidence.push_back(analysis::Evidence{"characters", static_cast<uint16_t>(bestBase + 256), drawnText,
                                                "glyphs drawn at base + 8 x the code of a text character"});
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

    /// Glyphs of a game's own codes (no text the detectors found names them): 8 x 8 frames on one lattice, eight or more
    /// different ones drawn, printed straight to the screen and not picked by a map (a tile set). The claim spans the
    /// frames from the first to the last
    bool GlyphSet(const Shape& shape, const Object& o, std::pair<uint32_t, uint32_t>& range)
    {
        const std::set<uint16_t> glyphs(o.drawn.begin(), o.drawn.end());
        if (glyphs.size() < kMinGlyphs || PickedByMap(o) || o.first < 0x4000 || !PrintedDirectly(o))
            return false;
        const uint32_t lo = o.first;
        const uint32_t hi = static_cast<uint32_t>(o.last) + 7;
        if (hi > 0xFFFF || (lo >> 14) != (hi >> 14))
            return false;
        Region r;
        r.kind = RegionKind::Font;
        r.detector = DetectorId::Graphics;
        r.status = RegionStatus::Observed;
        r.geometry.width = 8;
        r.geometry.height = 8;
        r.geometry.frames = static_cast<uint16_t>((hi - lo + 1) / 8);
        r.geometry.stride = 8;
        r.geometry.layout = "rows";
        r.confidence = confidence::FromCount(500, 800, glyphs.size(), 16);
        r.title = "Font, " + std::to_string(r.geometry.frames) + " characters of 8 x 8 (the game's own codes)";
        Evidence(r, o, shape);
        r.evidence.push_back(analysis::Evidence{"glyphs", o.first, static_cast<uint32_t>(glyphs.size()),
                                                "different glyphs drawn, one lattice of 8 bytes"});
        Add(std::move(r), static_cast<uint16_t>(lo), static_cast<uint16_t>(hi));
        range = {lo, hi};
        return true;
    }

    /// Characters in RAM the ROM drew (its character loop, PR_ALL): the user-defined graphics at (UDG) + 8 x (code -
    /// 144), 21 of them, or the character set at (CHARS) + 8 x code, 96; what lies in neither, the frames drawn
    bool RomCharacters(const Shape& shape, const Object& o, std::pair<uint32_t, uint32_t>& range)
    {
        if (_romPrint == kNoKey || o.first < 0x4000)
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

        // Inside a buffer: the same object in each of the buffer's sources (buffers are in the start's paging)
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
                Add(std::move(r2), static_cast<uint16_t>(at), static_cast<uint16_t>(end));
            }
        }
    }

    void Add(Region r, uint16_t lo, uint16_t hi)
    {
        if (!_claimed.insert({KeyAt(lo), KeyAt(hi), static_cast<uint8_t>(r.kind)}).second)
            return;
        if (_view < 0)
            r.users = _context.UsersOf(lo, hi);
        _context.Claim(std::move(r), KeyAt(lo), lo, hi);
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
