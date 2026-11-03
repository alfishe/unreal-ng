#include "smoothmotion.h"

#include "smoothlayers.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include "common/threadpool.h"

namespace smoothmotion
{

namespace
{
/// The whole-pixel shift of a vector component at the weight w: round(w c / 256), halves up
int Shift(int w, int c)
{
    return (w * c + kOne / 2) >> 8;
}

/// Where the output at w reads A and B for the vector v: an object moving by v from A to B is at p - shift in A and
/// at p + v - shift in B when the output shows it at p
struct Warp
{
    int ax, ay, bx, by;
    Warp(MotionField::Vector v, int w)
    {
        const int sx = Shift(w, v.x);
        const int sy = Shift(w, v.y);
        ax = -sx;
        ay = -sy;
        bx = v.x - sx;
        by = v.y - sy;
    }
};

/// ASCII, case-insensitive (an ini's "Interpolate" is "interpolate")
bool SameName(const char* a, const char* b)
{
    for (; *a && *b; a++, b++)
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
            return false;
    return *a == *b;
}
}  // namespace

const char* ModeName(Mode mode)
{
    switch (mode)
    {
        case Mode::Blend:
            return "blend";
        case Mode::Interpolate:
            return "interpolate";
        case Mode::Off:
            break;
    }
    return "off";
}

bool ParseMode(const char* text, Mode& mode)
{
    if (!text)
        return false;
    for (Mode m : {Mode::Off, Mode::Blend, Mode::Interpolate})
        if (SameName(text, ModeName(m)))
        {
            mode = m;
            return true;
        }
    return false;
}

const char* GameModeName(GameMode mode)
{
    switch (mode)
    {
        case GameMode::Cadence:
            return "cadence";
        case GameMode::Carry:
            return "carry";
        case GameMode::Off:
            break;
    }
    return "off";
}

bool ParseGameMode(const char* text, GameMode& mode)
{
    if (!text)
        return false;
    for (GameMode m : {GameMode::Off, GameMode::Cadence, GameMode::Carry})
        if (SameName(text, GameModeName(m)))
        {
            mode = m;
            return true;
        }
    return false;
}

uint32_t Mix(uint32_t a, uint32_t b, int w)
{
    if (a == b)
        return a;
    uint32_t out = 0;
    for (int c = 0; c < 32; c += 8)
    {
        const uint32_t va = (a >> c) & 0xFF;
        const uint32_t vb = (b >> c) & 0xFF;
        const uint32_t v = (va * static_cast<uint32_t>(kOne - w) + vb * static_cast<uint32_t>(w) + kOne / 2) >> 8;
        out |= v << c;
    }
    return out;
}

int WeightAt(int64_t t, int64_t ta, int64_t tb)
{
    if (tb <= ta)
        return t >= tb ? kOne : 0;
    if (t <= ta)
        return 0;
    if (t >= tb)
        return kOne;
    const int64_t span = tb - ta;
    return static_cast<int>(((t - ta) * kOne + span / 2) / span);
}

void Blend(const Picture& a, const Picture& b, int w, uint32_t* out)
{
    const size_t n = static_cast<size_t>(a.width) * a.height;
    for (size_t p = 0; p < n; p++)
        out[p] = Mix(a.pixels[p], b.pixels[p], w);
}

const std::vector<MotionField::Vector>& MotionField::SearchOrder(int range)
{
    static std::mutex mutex;
    static std::map<int, std::vector<Vector>> orders;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = orders.find(range);
    if (it != orders.end())
        return it->second;
    std::vector<Vector> order;
    for (int y = -range; y <= range; y++)
        for (int x = -range; x <= range; x++)
            order.push_back({x, y});
    std::stable_sort(order.begin(), order.end(),
                     [](Vector p, Vector q) { return p.x * p.x + p.y * p.y < q.x * q.x + q.y * q.y; });
    return orders.emplace(range, std::move(order)).first->second;
}

void MotionField::Estimate(const Picture& a, const Picture& b, int range, ThreadPool* pool)
{
    _blocksWide = (a.width + kBlock - 1) / kBlock;
    _blocksHigh = (a.height + kBlock - 1) / kBlock;
    const size_t count = static_cast<size_t>(_blocksWide) * _blocksHigh;
    const std::vector<Vector>& order = SearchOrder(range);
    // The pixels of block (i, j) where A and B warped to the middle by v disagree, counted up to limit + 1
    const auto cost = [&](int i, int j, Vector v, int limit) {
        const Warp k(v, kOne / 2);
        const int x0 = i * kBlock;
        const int y0 = j * kBlock;
        const int x1 = std::min(x0 + kBlock, a.width);
        const int y1 = std::min(y0 + kBlock, a.height);
        int bad = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if (a.At(x + k.ax, y + k.ay) != b.At(x + k.bx, y + k.by) && ++bad > limit)
                    return bad;
        return bad;
    };

    // The block rows j0..j1 of a pass, over the pool's threads (each block writes only its own)
    const auto rows = [&](const std::function<void(int, int)>& pass) {
        if (pool)
            pool->ParallelFor(_blocksHigh, kRowParts, pass);
        else
            pass(0, _blocksHigh);
    };

    // Pass 1, per block: the first vector of the search order with the fewest mismatches, at most kAccept
    std::vector<Block> first(count);
    rows([&](int j0, int j1) {
    for (int j = j0; j < j1; j++)
        for (int i = 0; i < _blocksWide; i++)
        {
            int best = kAccept + 1;
            Vector bestV{};
            for (const Vector& v : order)
            {
                const int bad = cost(i, j, v, best - 1);
                if (bad < best)
                {
                    best = bad;
                    bestV = v;
                    if (best == 0)
                        break;
                }
            }
            Block& block = first[static_cast<size_t>(j) * _blocksWide + i];
            block.matched = best <= kAccept;
            block.v = block.matched ? bestV : Vector{};
        }
    });

    // Refinement passes, per block from the previous pass alone: zero, the common vectors (most first) and the eight
    // neighbours' (row by row); the fewest mismatches within kAccept, the first of equals; none of them: the block's
    // own from pass 1. A flat or periodic area agrees under many vectors, and the nearest one the search finds first
    // is not the motion: the common motion spreads from block to block, one neighbour per pass
    std::vector<Block> prev = first;
    std::vector<Block> next(count);
    for (int pass = 0; pass < kRefinePasses; pass++)
    {
        const std::vector<Vector> votes = CommonVectors(prev, kGlobalVectors, range);

        rows([&](int j0, int j1) {
        for (int j = j0; j < j1; j++)
            for (int i = 0; i < _blocksWide; i++)
            {
                Vector cands[1 + kGlobalVectors + 8];
                int n = 0;
                const auto add = [&](Vector v) {
                    for (int c = 0; c < n; c++)
                        if (cands[c] == v)
                            return;
                    cands[n++] = v;
                };
                add(Vector{});
                for (const Vector& vote : votes)
                    add(vote);
                for (int dj = -1; dj <= 1; dj++)
                    for (int di = -1; di <= 1; di++)
                    {
                        const int bi = i + di, bj = j + dj;
                        if ((di || dj) && bi >= 0 && bj >= 0 && bi < _blocksWide && bj < _blocksHigh &&
                            prev[static_cast<size_t>(bj) * _blocksWide + bi].matched)
                            add(prev[static_cast<size_t>(bj) * _blocksWide + bi].v);
                    }
                int best = kAccept + 1;
                Vector bestV{};
                for (int c = 0; c < n && best > 0; c++)
                {
                    const int bad = cost(i, j, cands[c], best - 1);
                    if (bad < best)
                    {
                        best = bad;
                        bestV = cands[c];
                    }
                }
                Block& block = next[static_cast<size_t>(j) * _blocksWide + i];
                if (best <= kAccept)
                    block = {bestV, true};
                else
                    block = first[static_cast<size_t>(j) * _blocksWide + i];
            }
        });
        std::swap(prev, next);
    }

    Assign(_blocksWide, _blocksHigh, std::move(prev), range);
}

std::vector<MotionField::Vector> MotionField::CommonVectors(const std::vector<Block>& blocks, size_t limit, int range)
{
    const std::vector<Vector>& order = SearchOrder(range);
    const auto orderIndex = [&](Vector v) {
        return static_cast<int>(std::find(order.begin(), order.end(), v) - order.begin());
    };
    std::vector<std::pair<Vector, int>> votes;
    for (const Block& block : blocks)
    {
        if (!block.matched || block.v == Vector{})
            continue;
        auto it = std::find_if(votes.begin(), votes.end(), [&](const auto& e) { return e.first == block.v; });
        if (it == votes.end())
            votes.push_back({block.v, 1});
        else
            it->second++;
    }
    std::sort(votes.begin(), votes.end(), [&](const auto& p, const auto& q) {
        return p.second != q.second ? p.second > q.second : orderIndex(p.first) < orderIndex(q.first);
    });
    std::vector<Vector> common;
    for (size_t c = 0; c < votes.size() && c < limit; c++)
        common.push_back(votes[c].first);
    return common;
}

void MotionField::Assign(int blocksWide, int blocksHigh, std::vector<Block> blocks, int range)
{
    _blocksWide = blocksWide;
    _blocksHigh = blocksHigh;
    _blocks = std::move(blocks);
    // The motions most of the picture took, for Compose
    _common = CommonVectors(_blocks, kComposeCommon, range);
    _stats = {};
    for (const Block& block : _blocks)
    {
        _stats.blocks++;
        if (!block.matched)
            _stats.unmatched++;
        else if (block.v == Vector{})
            _stats.still++;
        else
            _stats.moved++;
    }
}

void MotionField::Compose(const Picture& a, const Picture& b, int w, uint32_t* out, const uint8_t* mask) const
{
    for (int j = 0; j < _blocksHigh; j++)
        for (int i = 0; i < _blocksWide; i++)
        {
            Vector cands[10 + kComposeCommon];
            int n = 0;
            const auto add = [&](Vector v) {
                for (int c = 0; c < n; c++)
                    if (cands[c] == v)
                        return;
                cands[n++] = v;
            };
            const auto addBlock = [&](int bi, int bj) {
                if (bi >= 0 && bj >= 0 && bi < _blocksWide && bj < _blocksHigh && At(bi, bj).matched)
                    add(At(bi, bj).v);
            };
            addBlock(i, j);
            add(Vector{});
            addBlock(i - 1, j);
            addBlock(i + 1, j);
            addBlock(i, j - 1);
            addBlock(i, j + 1);
            addBlock(i - 1, j - 1);
            addBlock(i + 1, j - 1);
            addBlock(i - 1, j + 1);
            addBlock(i + 1, j + 1);
            for (const Vector& v : _common)
                add(v);

            const int x1 = std::min((i + 1) * kBlock, a.width);
            const int y1 = std::min((j + 1) * kBlock, a.height);
            for (int y = j * kBlock; y < y1; y++)
                for (int x = i * kBlock; x < x1; x++)
                {
                    const size_t p = static_cast<size_t>(y) * a.width + x;
                    if (mask && !mask[p])
                        continue;
                    uint32_t pixel = 0;
                    int bestScore = 0;
                    for (int c = 0; c < n; c++)
                    {
                        const Warp k(cands[c], w);
                        const uint32_t va = a.At(x + k.ax, y + k.ay);
                        if (va != b.At(x + k.bx, y + k.by))
                            continue;
                        if (w > kOne && !(cands[c] == Vector{}))
                        {
                            const int sax = x + k.ax, say = y + k.ay, sbx = x + k.bx, sby = y + k.by;
                            if (a.At(sax, say) == b.At(sax, say) && a.At(sbx, sby) == b.At(sbx, sby))
                                continue;
                        }
                        int score = 0;
                        for (int dy = -1; dy <= 1; dy++)
                            for (int dx = -1; dx <= 1; dx++)
                                score += a.At(x + dx + k.ax, y + dy + k.ay) == b.At(x + dx + k.bx, y + dy + k.by);
                        if (score > bestScore)
                        {
                            bestScore = score;
                            pixel = va;
                        }
                    }
                    if (bestScore > 0)
                        out[p] = pixel;
                    else
                        out[p] = w > kOne ? b.pixels[p] : Mix(a.pixels[p], b.pixels[p], w);
                }
        }
}

SmoothMotion::SmoothMotion() : _layers(std::make_unique<LayerFlow>()) {}

SmoothMotion::~SmoothMotion() = default;

ThreadPool* SmoothMotion::Pool()
{
    if (!_pool)
    {
        // Half the cores, at most kMaxThreads with the caller (the window's thread): the rest stays the machine's.
        // UNREAL_SMOOTH_THREADS (1..32) sets it, 1: the caller alone
        const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
        size_t threads = std::clamp<size_t>(cores / 2, 1, kMaxThreads);
        if (const char* env = std::getenv("UNREAL_SMOOTH_THREADS"))
            if (const long wanted = std::strtol(env, nullptr, 10); wanted >= 1 && wanted <= 32)
                threads = static_cast<size_t>(wanted);
        _pool = std::make_unique<ThreadPool>(threads - 1, "smooth", ThreadPool::Priority::Interactive);
        _layers->SetPool(_pool.get());
    }
    return _pool.get();
}

void SmoothMotion::SetMode(Mode mode)
{
    _mode = mode;
}

void SmoothMotion::SetGame(const GameVideo* game)
{
    if ((game != nullptr) == _hasGame && (!game || *game == _game))
        return;
    _hasGame = game != nullptr;
    _game = game ? *game : GameVideo{};
    _pictures.clear();
    _fields[0] = {};
    _fields[1] = {};
    BuildMask();
}

void SmoothMotion::Reset()
{
    _frames.clear();
    _pictures.clear();
    _periods.clear();
    _fields[0] = {};
    _fields[1] = {};
}

void SmoothMotion::BuildMask()
{
    _mask.clear();
    if (!_hasGame || _width <= 0 || _height <= 0)
        return;
    _mask.assign(static_cast<size_t>(_width) * _height, 0);
    const auto fill = [&](const Rect& r, uint8_t value) {
        const int x0 = std::clamp(r.x, 0, _width);
        const int y0 = std::clamp(r.y, 0, _height);
        const int x1 = std::clamp(r.x + r.width, 0, _width);
        const int y1 = std::clamp(r.y + r.height, 0, _height);
        for (int y = y0; y < y1; y++)
            std::memset(&_mask[static_cast<size_t>(y) * _width + x0], value, static_cast<size_t>(std::max(0, x1 - x0)));
    };
    fill(_game.window.Empty() ? Rect{0, 0, _width, _height} : _game.window, 1);
    for (const Rect& r : _game.exclude)
        fill(r, 0);
}

bool SmoothMotion::ChangedInWindow(const uint32_t* a, const uint32_t* b) const
{
    const size_t n = static_cast<size_t>(_width) * _height;
    for (size_t p = 0; p < n; p++)
        if (_mask[p] && a[p] != b[p])
            return true;
    return false;
}

void SmoothMotion::Push(uint64_t serial, int64_t targetNs, const uint32_t* pixels, int width, int height)
{
    if (width <= 0 || height <= 0 || !pixels)
        return;
    if (width != _width || height != _height)
    {
        Reset();
        _width = width;
        _height = height;
        BuildMask();
    }
    if (!_frames.empty() && targetNs > _frames.back().targetNs)
    {
        _periods.push_back(targetNs - _frames.back().targetNs);
        if (_periods.size() > 8)
            _periods.erase(_periods.begin());
    }

    const size_t size = static_cast<size_t>(width) * height;
    Stored frame;
    if (_frames.size() >= kFrames)
    {
        frame = std::move(_frames.front());
        _frames.erase(_frames.begin());
    }
    frame.serial = serial;
    frame.targetNs = targetNs;
    frame.pixels.assign(pixels, pixels + size);
    frame.changed = size;
    frame.flicker = 0;
    frame.smooth.clear();
    if (!_frames.empty())
    {
        Stored& before = _frames.back();
        frame.changed = 0;
        for (size_t p = 0; p < size; p++)
            frame.changed += before.pixels[p] != pixels[p] ? 1 : 0;
        if (_frames.size() >= 4)
            FindFlicker(_frames[_frames.size() - 4], _frames[_frames.size() - 3], _frames[_frames.size() - 2], before,
                        pixels);
    }

    // A game's picture: a frame whose play area differs from the frame before it
    if (_hasGame && (_frames.empty() || ChangedInWindow(_frames.back().pixels.data(), pixels)))
    {
        Stored picture;
        if (_pictures.size() >= kPictures)
        {
            picture = std::move(_pictures.front());
            _pictures.erase(_pictures.begin());
        }
        picture.serial = serial;
        picture.targetNs = targetNs;
        picture.pixels.assign(pixels, pixels + size);
        _pictures.push_back(std::move(picture));
        _stats.pictures++;
    }
    _frames.push_back(std::move(frame));
    _stats.frames++;
}

void SmoothMotion::FindFlicker(const Stored& x, const Stored& y, const Stored& z, Stored& a, const uint32_t* b)
{
    // Per 8x8 block: the pixels that change from A to B, and those of them alternating y = A, z = B
    const int block = MotionField::kBlock;
    for (int by = 0; by < _height; by += block)
    {
        for (int bx = 0; bx < _width; bx += block)
        {
            const int x1 = std::min(bx + block, _width);
            const int y1 = std::min(by + block, _height);
            int changed = 0;
            int alternating = 0;
            for (int py = by; py < y1; py++)
            {
                for (int px = bx; px < x1; px++)
                {
                    const size_t p = static_cast<size_t>(py) * _width + px;
                    if (a.pixels[p] == b[p])
                        continue;
                    changed++;
                    if (x.pixels[p] == b[p] && z.pixels[p] == b[p] && y.pixels[p] == a.pixels[p])
                        alternating++;
                }
            }
            if (changed == 0 || alternating * kFlickerDenominator < changed * kFlickerNumerator)
                continue;
            if (a.smooth.empty())
                a.smooth.assign(static_cast<size_t>(_width) * _height, 1);
            for (int py = by; py < y1; py++)
                std::memset(&a.smooth[static_cast<size_t>(py) * _width + bx], 0, static_cast<size_t>(x1 - bx));
            a.flicker += static_cast<size_t>(changed);
        }
    }
}

int64_t SmoothMotion::FramePeriodNs() const
{
    if (_periods.empty())
        return 20'000'000;
    std::vector<int64_t> sorted = _periods;
    std::sort(sorted.begin(), sorted.end());
    return sorted[sorted.size() / 2];
}

const MotionField& SmoothMotion::FieldFor(const Stored& a, const Stored& b, int range)
{
    for (CachedField& cached : _fields)
        if (cached.from == a.serial && cached.to == b.serial && cached.range == range)
            return cached.field;
    CachedField& slot = _fields[_nextField];
    _nextField = (_nextField + 1) % 2;
    slot.from = a.serial;
    slot.to = b.serial;
    slot.range = range;
    if (_device && _device->Estimate(a.View(_width, _height), b.View(_width, _height), range, slot.field))
        _stats.onDevice++;
    else
        slot.field.Estimate(a.View(_width, _height), b.View(_width, _height), range, Pool());
    _stats.estimates++;
    return slot.field;
}

SmoothMotion::Result SmoothMotion::Plan(int64_t tNs) const
{
    Result r;
    if (_frames.empty())
        return r;
    // The frame due: the newest whose target is at or before the time (the oldest when all are later)
    size_t due = 0;
    for (size_t i = 0; i < _frames.size(); i++)
        if (_frames[i].targetNs <= tNs)
            due = i;
    const Stored& a = _frames[due];
    r.kind = Kind::Frame;
    r.serial = r.from = r.to = a.serial;

    if (_mode == Mode::Interpolate && _hasGame)
    {
        if (_game.mode == GameMode::Off)
            return r;
        // The game's picture on screen at the time
        size_t p0 = _pictures.size();
        for (size_t i = 0; i < _pictures.size(); i++)
            if (_pictures[i].targetNs <= tNs)
                p0 = i;
        if (p0 == _pictures.size())
            return r;
        const Stored& cur = _pictures[p0];
        const int64_t frameNs = FramePeriodNs();
        if (_game.mode == GameMode::Cadence)
        {
            if (p0 + 1 >= _pictures.size())
                return r;
            const Stored& next = _pictures[p0 + 1];
            const int64_t start = std::max(cur.targetNs, next.targetNs - kLookaheadFrames * frameNs);
            const int w = WeightAt(tNs, start, next.targetNs);
            if (w <= 0 || w >= kOne)
                return r;
            return {Kind::Cadence, a.serial, cur.serial, next.serial, w};
        }
        if (p0 == 0)
            return r;
        const Stored& prev = _pictures[p0 - 1];
        const int64_t pictureNs = _game.picturePeriod * frameNs;
        // The motion stopped (the pictures further apart than a period), or no picture came for a period: the frame
        // as it is
        if (cur.targetNs - prev.targetNs > pictureNs + frameNs / 2 || tNs - cur.targetNs > pictureNs + frameNs / 2)
            return r;
        const int w = kOne + WeightAt(tNs, cur.targetNs, cur.targetNs + pictureNs);
        if (w <= kOne)
            return r;
        return {Kind::Carry, a.serial, prev.serial, cur.serial, w};
    }

    if (_mode == Mode::Off || due + 1 >= _frames.size())
        return r;
    const Stored& b = _frames[due + 1];
    const int w = WeightAt(tNs, a.targetNs, b.targetNs);
    const size_t size = static_cast<size_t>(_width) * _height;
    if (w <= 0 || std::memcmp(a.pixels.data(), b.pixels.data(), size * sizeof(uint32_t)) == 0)
        return r;
    // Nothing but flicker
    if (a.flicker >= b.changed)
        return r;
    return {_mode == Mode::Blend ? Kind::Blend : Kind::Interpolate, a.serial, a.serial, b.serial, w};
}

bool SmoothMotion::Draw(const Result& r, uint32_t* out)
{
    if (r.kind == Kind::None || !out)
        return false;
    const bool pictures = r.kind == Kind::Cadence || r.kind == Kind::Carry;
    const auto find = [](const std::vector<Stored>& in, uint64_t serial) -> const Stored* {
        for (const Stored& s : in)
            if (s.serial == serial)
                return &s;
        return nullptr;
    };
    const Stored* a = find(pictures ? _pictures : _frames, r.from);
    const Stored* b = find(pictures ? _pictures : _frames, r.to);
    if (!a || !b)
        return false;
    _stats.renders++;
    if (!a->smooth.empty() && (r.kind == Kind::Blend || r.kind == Kind::Interpolate))
        _stats.heldFlicker++;
    const Picture pa = a->View(_width, _height);
    const Picture pb = b->View(_width, _height);
    switch (r.kind)
    {
        case Kind::Frame:
            std::memcpy(out, a->pixels.data(), a->pixels.size() * sizeof(uint32_t));
            break;
        case Kind::Blend:
            if (_device && _device->Blend(pa, pb, r.weight, out))
                _stats.onDevice++;
            else
                Blend(pa, pb, r.weight, out);
            // Flickering pixels: the frame due
            for (size_t p = 0; p < a->smooth.size(); p++)
                if (!a->smooth[p])
                    out[p] = a->pixels[p];
            _stats.blended++;
            break;
        case Kind::Interpolate:
        {
            // The frame before a for the still and steady checks (a itself when it is the first)
            const Stored* p = a;
            for (size_t i = 1; i < _frames.size(); i++)
                if (&_frames[i] == a)
                    p = &_frames[i - 1];
            ComposeLayers(*p, *a, *b, r.weight, out);
            // Flickering pixels: the frame due
            for (size_t q = 0; q < a->smooth.size(); q++)
                if (!a->smooth[q])
                    out[q] = a->pixels[q];
            _stats.interpolated++;
            break;
        }
        case Kind::Cadence:
        case Kind::Carry:
            // Outside the game's window the frame due, as it is
            if (const Stored* due = find(_frames, r.serial))
                std::memcpy(out, due->pixels.data(), due->pixels.size() * sizeof(uint32_t));
            Compose(FieldFor(*a, *b, MotionField::kRangePictures), pa, pb, r.weight, out, _mask.data());
            _stats.interpolated++;
            break;
        case Kind::None:
            return false;
    }
    return true;
}

void SmoothMotion::Compose(const MotionField& field, const Picture& a, const Picture& b, int w, uint32_t* out,
                           const uint8_t* mask)
{
    if (_device && _device->Compose(field, a, b, w, out, mask))
        _stats.onDevice++;
    else
        field.Compose(a, b, w, out, mask);
}

void SmoothMotion::ComposeLayers(const Stored& p, const Stored& a, const Stored& b, int w, uint32_t* out)
{
    // The field p -> a first: in order, it is the last pair's a -> b, still cached
    const MotionField* prev = &_still;
    if (&p != &a)
        prev = &FieldFor(p, a, MotionField::kRangeFrames);
    else if (_still.BlocksWide() != (_width + MotionField::kBlock - 1) / MotionField::kBlock ||
             _still.BlocksHigh() != (_height + MotionField::kBlock - 1) / MotionField::kBlock)
    {
        const int blocksWide = (_width + MotionField::kBlock - 1) / MotionField::kBlock;
        const int blocksHigh = (_height + MotionField::kBlock - 1) / MotionField::kBlock;
        _still.Assign(blocksWide, blocksHigh,
                      std::vector<MotionField::Block>(static_cast<size_t>(blocksWide) * blocksHigh, {{}, true}),
                      MotionField::kRangeFrames);
    }
    const MotionField& next = FieldFor(a, b, MotionField::kRangeFrames);
    const Picture pp = p.View(_width, _height), pa = a.View(_width, _height), pb = b.View(_width, _height);
    if (_device && _device->ComposeLayers(pp, pa, pb, *prev, next, w, out))
        _stats.onDevice++;
    else
    {
        Pool();
        _layers->Compose(pp, pa, pb, *prev, next, w, out);
    }
}

SmoothMotion::Result SmoothMotion::Render(int64_t tNs, uint32_t* out)
{
    const Result r = Plan(tNs);
    if (!Draw(r, out))
        return {};
    return r;
}

}  // namespace smoothmotion
