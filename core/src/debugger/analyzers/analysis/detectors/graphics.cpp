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
/// a map and no text), the claim then covers the 96 characters 32..127; 8 x 8 frames in RAM the ROM's character loop
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
    bool operator<(const Shape& o) const
    {
        return std::tie(rowBytes, height, mask) < std::tie(o.rowBytes, o.height, o.mask);
    }
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
};

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
        std::map<Shape, std::map<uint16_t, std::set<size_t>>> frames;
        for (size_t i = 0; i < _blits.size(); ++i)
        {
            const Blit& b = _blits[i];
            for (const uint16_t s : b.starts)
                frames[Shape{b.rowBytes, b.height, b.mask}][s].insert(i);
        }
        for (const auto& [shape, starts] : frames)
            Objects(shape, starts);
    }

private:
    DetectorContext& _context;
    const std::vector<Blit>& _blits;
    std::vector<PointerRange> _pointers;
    std::vector<media::BufferCopy> _buffers;
    std::set<uint8_t> _text;
    std::vector<std::pair<uint16_t, uint16_t>> _textRanges;  ///< the text claims, [lo, hi] (Z80)
    std::set<std::tuple<uint16_t, uint16_t, uint8_t>> _claimed;
    PhysKey _romPrint = kNoKey;  ///< the ROM's character loop (PR_ALL), when its signature matched

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

    void Objects(const Shape& shape, const std::map<uint16_t, std::set<size_t>>& starts)
    {
        const uint32_t size = static_cast<uint32_t>(shape.rowBytes) * shape.height * (shape.mask ? 2u : 1u);
        const bool glyphs = shape.rowBytes == 1 && shape.height == 8 && !shape.mask;
        std::vector<Object> objects;
        for (const auto& [start, blits] : starts)
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
                    cur.blits.insert(blits.begin(), blits.end());
                    continue;
                }
            }
            Object o;
            o.first = o.last = start;
            o.blits = blits;
            objects.push_back(std::move(o));
        }
        for (Object& o : objects)
        {
            if (glyphs && (((o.sparse || o.step == 8) && Font(shape, o)) || RomCharacters(shape, o)))
                continue;
            if (o.sparse)
            {
                // Not a character set: each run of adjacent frames is an object of its own
                std::vector<uint16_t> drawn;
                for (const auto& [start, blits] : starts)
                    if (start >= o.first && start <= o.last && (start - o.first) % 8 == 0)
                        drawn.push_back(start);
                Object part;
                for (size_t i = 0; i < drawn.size(); ++i)
                {
                    if (i == 0 || drawn[i] - drawn[i - 1] != 8)
                    {
                        if (i != 0)
                            Claim(shape, part, size);
                        part = Object();
                        part.first = part.last = drawn[i];
                        part.blits = o.blits;
                        continue;
                    }
                    part.last = drawn[i];
                    part.step = 8;
                    ++part.seen;
                }
                if (!drawn.empty())
                    Claim(shape, part, size);
                continue;
            }
            Claim(shape, o, size);
        }
    }

    /// The walk around every blit of @p o read a map, no text: the frames are picked by map cells (tiles drawn by a
    /// print routine), not by character codes
    bool PickedByMap(const Object& o) const
    {
        for (const Blit* b : BlitsOf(o))
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
    bool Font(const Shape& shape, const Object& o)
    {
        if (_text.empty() || o.seen < 3 || PickedByMap(o))
            return false;
        std::vector<uint16_t> drawn;
        for (uint32_t a = o.first; a <= o.last; a += 8)
            drawn.push_back(static_cast<uint16_t>(a));
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
        for (const Blit* b : BlitsOf(o))
            for (const uint16_t s : b->starts)
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
        return true;
    }

    /// Characters in RAM the ROM drew (its character loop, PR_ALL): the user-defined graphics at (UDG) + 8 x (code -
    /// 144), 21 of them, or the character set at (CHARS) + 8 x code, 96; what lies in neither, the frames drawn
    bool RomCharacters(const Shape& shape, const Object& o)
    {
        if (_romPrint == kNoKey || o.first < 0x4000)
            return false;
        for (const Blit* b : BlitsOf(o))
        {
            if (KeyPage(b->loop->header) != KeyPage(_romPrint) || b->loop->z80 < kPrAll || b->loop->z80 >= kPrAllEnd)
                return false;
        }
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
        uint32_t step = o.step != 0 ? o.step : size;
        uint32_t prefix = step > size ? step - size : 0;
        uint32_t lo = o.first - prefix;
        uint32_t hi = static_cast<uint32_t>(o.last) + size - 1;
        uint32_t frames = (o.last - o.first) / step + 1;
        if (hi > 0xFFFF || o.first < prefix)
            return;
        // A record array with the same step around the frames: its extent
        for (const Region* rec : _context.out.store.Overlapping(_context.Key(static_cast<uint16_t>(lo)),
                                                                _context.Key(static_cast<uint16_t>(hi))))
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
        r.geometry.layout = "rows";
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

        // Inside a buffer: the same object in each of the buffer's sources
        for (const media::BufferCopy& buffer : _buffers)
        {
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
        if (!_claimed.insert({lo, hi, static_cast<uint8_t>(r.kind)}).second)
            return;
        r.users = _context.UsersOf(lo, hi);
        _context.Claim(std::move(r), lo, hi);
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
