#include "smoothmotion.h"

#include <bit>

#include "smoothlayers.h"
#include "smoothlite.h"

#include <algorithm>
#include <atomic>
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
        case Mode::Auto:
            return "auto";
        case Mode::InterpolateLite:
            return "interpolate-lite";
        case Mode::Off:
            break;
    }
    return "off";
}

bool ParseMode(const char* text, Mode& mode)
{
    if (!text)
        return false;
    for (Mode m : {Mode::Off, Mode::Blend, Mode::Interpolate, Mode::Auto, Mode::InterpolateLite})
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
    // Two channels to a 32-bit word, one in each 16-bit half: a half's sum is at most 255 * 256 + 128 and never
    // carries into the next one. Two multiplies a channel pair instead of one per channel (a baseline x86-64 build has
    // no 32-bit vector multiply: SSE4.1 pmulld), and no branch for a == b, which gives a either way
    constexpr uint32_t kPair = 0x00FF00FFu;
    const uint32_t wa = static_cast<uint32_t>(kOne - w);
    const uint32_t wb = static_cast<uint32_t>(w);
    const uint32_t even = ((a & kPair) * wa + (b & kPair) * wb + 0x00800080u) >> 8;
    const uint32_t odd = ((a >> 8) & kPair) * wa + ((b >> 8) & kPair) * wb + 0x00800080u;
    return (even & kPair) | (odd & ~kPair);
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

/// Flickering pixels (`smooth` 0): the frame due. Without a branch: a held pixel's branch mispredicts where
/// flicker and motion mix
void KeepHeld(const std::vector<uint8_t>& smooth, const uint32_t* frame, uint32_t* out)
{
    for (size_t p = 0; p < smooth.size(); p++)
    {
        const uint32_t held = 0u - static_cast<uint32_t>(smooth[p] == 0);
        out[p] = (out[p] & ~held) | (frame[p] & held);
    }
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
        // The block's two warped copies inside the pictures (every block but the ones near an edge): rows compared
        // whole through their pointers, the count looked at once a row. It may pass the limit by more than one
        // then; the callers ask only whether it did. (A clamped read per pixel was 30 % of an interpolated
        // picture's time: 290 vectors by 64 pixels a block)
        if (x0 + k.ax >= 0 && x0 + k.bx >= 0 && x1 + k.ax <= a.width && x1 + k.bx <= b.width && y0 + k.ay >= 0 &&
            y0 + k.by >= 0 && y1 + k.ay <= a.height && y1 + k.by <= b.height)
        {
            const int n = x1 - x0;
            for (int y = y0; y < y1; y++)
            {
                const uint32_t* const rowA = a.pixels + static_cast<size_t>(y + k.ay) * a.width + (x0 + k.ax);
                const uint32_t* const rowB = b.pixels + static_cast<size_t>(y + k.by) * b.width + (x0 + k.bx);
                int row = 0;
                for (int x = 0; x < n; x++)
                    row += rowA[x] != rowB[x];
                bad += row;
                if (bad > limit)
                    return bad;
            }
            return bad;
        }
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
    if (!_ownThreads)
        return nullptr;
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
    // Auto is the front end's: it decides when the engine runs, the engine interpolates
    _mode = mode == Mode::Auto ? Mode::Interpolate : mode;
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
    if (_lite)
        _lite->Reset();
    _movesNow.clear();
    _movesBefore.clear();
    _movesTwoAgo.clear();
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
    static std::atomic<uint64_t> nextId{0};
    frame.serial = serial;
    frame.targetNs = targetNs;
    frame.id = ++nextId;
    frame.pixels.assign(pixels, pixels + size);
    frame.changed = size;
    frame.flicker = 0;
    frame.smooth.clear();
    frame.sameTwoAgo.clear();
    if (_frames.size() >= 4)
        frame.changed = FindFlicker(_frames[_frames.size() - 4], _frames[_frames.size() - 3],
                                    _frames[_frames.size() - 2], _frames.back(), pixels, frame.sameTwoAgo);
    else if (!_frames.empty())
    {
        const uint32_t* before = _frames.back().pixels.data();
        frame.changed = 0;
        for (size_t p = 0; p < size; p++)
            frame.changed += before[p] != pixels[p] ? 1 : 0;
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
        picture.id = frame.id;  // the frame's pixels
        picture.pixels.assign(pixels, pixels + size);
        _pictures.push_back(std::move(picture));
        _stats.pictures++;
    }
    _frames.push_back(std::move(frame));
    _stats.frames++;
}

size_t SmoothMotion::FindFlicker(const Stored& x, const Stored& y, const Stored& z, Stored& a, const uint32_t* b,
                                 std::vector<uint8_t>& sameB)
{
    // Per 8x8 block: the pixels that change from A to B, those of them alternating (x = B, y = A, z = B), and the
    // pixels of B that are as in Z. Row by row, a block's 8 pixels at a time with no branch (the compiler's vectors)
    const int block = MotionField::kBlock;
    const int blocksWide = (_width + block - 1) / block, blocksHigh = (_height + block - 1) / block;
    _flickerChanged.assign(static_cast<size_t>(blocksWide) * blocksHigh, 0);
    _flickerAlternating.assign(_flickerChanged.size(), 0);
    _flickerSameZ.assign(_flickerChanged.size(), 0);
    const int whole = _width / block;
    size_t total = 0;
    for (int py = 0; py < _height; py++)
    {
        const size_t row = static_cast<size_t>(py) * _width;
        const uint32_t *pa = a.pixels.data() + row, *pb = b + row, *px = x.pixels.data() + row,
                       *py_ = y.pixels.data() + row, *pz = z.pixels.data() + row;
        int* changedRow = _flickerChanged.data() + static_cast<size_t>(py / block) * blocksWide;
        int* alternatingRow = _flickerAlternating.data() + static_cast<size_t>(py / block) * blocksWide;
        int* sameRow = _flickerSameZ.data() + static_cast<size_t>(py / block) * blocksWide;
        const auto pixel = [&](int i, int& changed, int& alternating, int& same) {
            const int differs = pa[i] != pb[i];
            changed += differs;
            alternating += differs & (px[i] == pb[i]) & (pz[i] == pb[i]) & (py_[i] == pa[i]);
            same += pz[i] == pb[i];
        };
        for (int bi = 0; bi < whole; bi++)
        {
            int changed = 0, alternating = 0, same = 0;
            for (int i = bi * block; i < bi * block + block; i++)
                pixel(i, changed, alternating, same);
            changedRow[bi] += changed;
            alternatingRow[bi] += alternating;
            sameRow[bi] += same;
            total += static_cast<size_t>(changed);
        }
        if (whole < blocksWide)
        {
            int changed = 0, alternating = 0, same = 0;
            for (int i = whole * block; i < _width; i++)
                pixel(i, changed, alternating, same);
            changedRow[whole] += changed;
            alternatingRow[whole] += alternating;
            sameRow[whole] += same;
            total += static_cast<size_t>(changed);
        }
    }
    sameB.assign(_flickerChanged.size(), 0);
    for (int bj = 0; bj < blocksHigh; bj++)
        for (int bi = 0; bi < blocksWide; bi++)
        {
            const int pixels = (std::min((bi + 1) * block, _width) - bi * block) *
                               (std::min((bj + 1) * block, _height) - bj * block);
            const size_t at = static_cast<size_t>(bj) * blocksWide + bi;
            sameB[at] = _flickerSameZ[at] == pixels ? 1 : 0;
        }
    const auto flickers = [](int changed, int alternating) {
        return changed > 0 && alternating * kFlickerDenominator >= changed * kFlickerNumerator;
    };
    // Per block the verdict, split over the pool by block rows (each block writes only its own); then the held
    // blocks applied in order
    _flickerHeld.assign(_flickerChanged.size(), 0);
    // MovesBy's answers of the last two calls: flicker repeats two frames apart, so a block whose surroundings in A
    // and B are what they were in Y and Z (two calls ago: the pair y, z) has the answer it had then
    std::swap(_movesTwoAgo, _movesBefore);
    std::swap(_movesBefore, _movesNow);
    _movesNow.assign(_flickerChanged.size(), -1);
    const bool remembered = _movesTwoAgo.size() == _flickerChanged.size();
    // The 3 x 3 blocks round a block (MovesBy's reach, cut by the frame's edges) the same as two frames before: by
    // the blocks' flags, A's from when it came (against Y), B's from the counts above (against Z)
    const bool flagsA = a.sameTwoAgo.size() == _flickerChanged.size();
    const auto sameAround = [&](const std::vector<uint8_t>& same, int bi, int bj) {
        for (int j = std::max(bj - 1, 0); j <= std::min(bj + 1, blocksHigh - 1); j++)
            for (int i = std::max(bi - 1, 0); i <= std::min(bi + 1, blocksWide - 1); i++)
                if (!same[static_cast<size_t>(j) * blocksWide + i])
                    return false;
        return true;
    };
    const auto rows = [&](int j0, int j1) {
        for (int bj = j0; bj < j1; bj++)
            for (int bi = 0; bi < blocksWide; bi++)
            {
                const size_t at = static_cast<size_t>(bj) * blocksWide + bi;
                if (!flickers(_flickerChanged[at], _flickerAlternating[at]))
                    continue;
                // a shift that explains the block's changes both ways (b(p) = a(p - d), a(p) = b(p + d)) makes it a
                // look-alike of motion: dots a scroll's step apart, passing the block one a frame (Aleste's Y). Then
                // the 3 x 3 blocks round it alternate as well, else it is motion: the row's ends and the letter's
                // other dots round it do not. So the shifts are asked only when the blocks round it do not alternate
                // (MovesBy's answer decides nothing otherwise)
                int changed = 0, alternating = 0;
                for (int j = std::max(bj - 1, 0); j <= std::min(bj + 1, blocksHigh - 1); j++)
                    for (int i = std::max(bi - 1, 0); i <= std::min(bi + 1, blocksWide - 1); i++)
                    {
                        changed += _flickerChanged[static_cast<size_t>(j) * blocksWide + i];
                        alternating += _flickerAlternating[static_cast<size_t>(j) * blocksWide + i];
                    }
                if (!flickers(changed, alternating))
                {
                    const int bx = bi * block, by = bj * block;
                    const int x1 = std::min(bx + block, _width), y1 = std::min(by + block, _height);
                    bool moves;
                    if (remembered && flagsA && _movesTwoAgo[at] >= 0 && sameAround(a.sameTwoAgo, bi, bj) &&
                        sameAround(sameB, bi, bj))
                        moves = _movesTwoAgo[at] != 0;
                    else
                        moves = MovesBy(a.pixels.data(), b, bx, by, x1, y1, _flickerChanged[at]);
                    _movesNow[at] = moves ? 1 : 0;
                    if (moves)
                        continue;
                }
                _flickerHeld[at] = 1;
            }
    };
    ThreadPool* pool = nullptr;
    if (blocksHigh > 1 && std::count_if(_flickerChanged.begin(), _flickerChanged.end(), [](int c) { return c > 0; }) >
                              kFlickerParallelBlocks)
        pool = Pool();
    if (pool)
        pool->ParallelFor(blocksHigh, std::min(blocksHigh, MotionField::kRowParts), rows);
    else
        rows(0, blocksHigh);
    for (int bj = 0; bj < blocksHigh; bj++)
        for (int bi = 0; bi < blocksWide; bi++)
        {
            const size_t at = static_cast<size_t>(bj) * blocksWide + bi;
            if (!_flickerHeld[at])
                continue;
            const int bx = bi * block, by = bj * block;
            const int x1 = std::min(bx + block, _width), y1 = std::min(by + block, _height);
            if (a.smooth.empty())
                a.smooth.assign(static_cast<size_t>(_width) * _height, 1);
            for (int py = by; py < y1; py++)
                std::memset(&a.smooth[static_cast<size_t>(py) * _width + bx], 0, static_cast<size_t>(x1 - bx));
            a.flicker += static_cast<size_t>(_flickerChanged[at]);
        }
    return total;
}

namespace
{
/// The ones of a word. std::popcount without a popcnt instruction in the build's baseline (gcc on x86-64) is a
/// library call: here a few word operations instead
inline int Ones(uint64_t word)
{
#if defined(__POPCNT__) || defined(__aarch64__) || defined(_M_ARM64) || defined(_MSC_VER)
    return std::popcount(word);
#else
    word = word - ((word >> 1) & 0x5555555555555555ull);
    word = (word & 0x3333333333333333ull) + ((word >> 2) & 0x3333333333333333ull);
    word = (word + (word >> 4)) & 0x0F0F0F0F0F0F0F0Full;
    return static_cast<int>((word * 0x0101010101010101ull) >> 56);
#endif
}

/// MovesBy on bit masks, the same answer. The 24 x 24 pixels round the block (its reach each way) of both frames as
/// planes of the colours' indices among the colours met there; per column shift the block's 8 columns of every row as
/// a byte, so the block read at a shift (dx, dy) is 8 bytes from row 8 + dy: one word a plane. A pixel outside the
/// frame explains nothing (the validity plane), as the pixel loop skips it. A plane's shifted bytes are made when a
/// shift first needs it: most shifts fail on the first plane.
/// false: more than kLocalColours colours there, the pixel loop decides
class BlockShifts
{
public:
    static constexpr int kReach = MotionField::kBlock;
    static constexpr int kSide = 3 * MotionField::kBlock;
    static constexpr int kLocalColours = 64;
    static constexpr int kPlanes = 6;

    bool Build(const uint32_t* a, const uint32_t* b, int width, int height, int bx, int by)
    {
        // colour -> index: open addressing on a multiplicative hash (any one-to-one numbering gives the same answer)
        static constexpr int kSlots = 2 * kLocalColours;
        uint32_t keys[kSlots];
        int8_t values[kSlots];
        std::memset(values, -1, sizeof(values));
        int count = 0;
        const auto indexOf = [&](uint32_t colour) -> int {
            for (uint32_t slot = (colour * 0x9E3779B1u) >> 25;; slot = (slot + 1) & (kSlots - 1))
            {
                if (values[slot] < 0)
                {
                    if (count == kLocalColours)
                        return -1;
                    keys[slot] = colour;
                    values[slot] = static_cast<int8_t>(count);
                    return count++;
                }
                if (keys[slot] == colour)
                    return values[slot];
            }
        };
        _inside = bx >= kReach && by >= kReach && bx + MotionField::kBlock + kReach <= width &&
                  by + MotionField::kBlock + kReach <= height;
        // the colours' indices, a byte a pixel (0 outside the frame: the validity rows mask them)
        uint8_t indexA[kSide][kSide], indexB[kSide][kSide];
        for (int r = 0; r < kSide; r++)
        {
            const int y = by - kReach + r;
            _validRows[r] = 0;
            if (y < 0 || y >= height)
            {
                std::memset(indexA[r], 0, kSide);
                std::memset(indexB[r], 0, kSide);
                continue;
            }
            const int c0 = std::max(0, kReach - bx), c1 = std::min(kSide, width - bx + kReach);
            if (c0 > 0)
            {
                std::memset(indexA[r], 0, static_cast<size_t>(c0));
                std::memset(indexB[r], 0, static_cast<size_t>(c0));
            }
            if (c1 < kSide)
            {
                std::memset(indexA[r] + c1, 0, static_cast<size_t>(kSide - c1));
                std::memset(indexB[r] + c1, 0, static_cast<size_t>(kSide - c1));
            }
            const uint32_t* rowA = a + static_cast<size_t>(y) * width + (bx - kReach);
            const uint32_t* rowB = b + static_cast<size_t>(y) * width + (bx - kReach);
            uint32_t lastA = 0, lastB = 0;
            int lastIndexA = -1, lastIndexB = -1;
            for (int c = c0; c < c1; c++)
            {
                if (lastIndexA < 0 || rowA[c] != lastA)
                {
                    lastA = rowA[c];
                    if ((lastIndexA = indexOf(lastA)) < 0)
                        return false;
                }
                if (lastIndexB < 0 || rowB[c] != lastB)
                {
                    lastB = rowB[c];
                    if ((lastIndexB = indexOf(lastB)) < 0)
                        return false;
                }
                indexA[r][c] = static_cast<uint8_t>(lastIndexA);
                indexB[r][c] = static_cast<uint8_t>(lastIndexB);
            }
            if (c1 > c0)
                _validRows[r] = ((1u << (c1 - c0)) - 1) << c0;
        }
        _planes = 1;
        while ((1 << _planes) < count)
            _planes++;
        // per plane and row the 24 columns' bits, 8 at a time: byte i's bit p lands at bit i (no carries: every
        // product term has its own bit)
        const auto bits = [](const uint8_t* bytes, int plane) {
            uint64_t word = 0;
            for (int i = 0; i < 8; i++)
                word |= static_cast<uint64_t>(bytes[i]) << (8 * i);
            return static_cast<uint32_t>((((word >> plane) & 0x0101010101010101ull) * 0x0102040810204080ull) >> 56);
        };
        for (int plane = 0; plane < _planes; plane++)
            for (int r = 0; r < kSide; r++)
            {
                _rowsA[plane][r] = bits(indexA[r], plane) | bits(indexA[r] + 8, plane) << 8 |
                                   bits(indexA[r] + 16, plane) << 16;
                _rowsB[plane][r] = bits(indexB[r], plane) | bits(indexB[r] + 8, plane) << 8 |
                                   bits(indexB[r] + 16, plane) << 16;
            }
        // the block itself and its changed pixels
        uint64_t differs = 0;
        for (int plane = 0; plane < _planes; plane++)
        {
            _blockA[plane] = RowsWord(_rowsA[plane], kReach, kReach);
            _blockB[plane] = RowsWord(_rowsB[plane], kReach, kReach);
            differs |= _blockA[plane] ^ _blockB[plane];
        }
        _changed = differs & RowsWord(_validRows, kReach, kReach);
        if (!_inside)
            for (int shift = 0; shift <= 2 * kReach; shift++)
                for (int r = 0; r < kSide; r++)
                    _valid[shift][r] = static_cast<uint8_t>(_validRows[r] >> shift);
        _shifted = 0;
        return true;
    }

    /// The shift (dx, dy) explains at least `wanted` of the block's changed pixels both ways: b(p) = a(p - d) and
    /// a(p) = b(p + d). Plane by plane the candidates only shrink: it stops once they are fewer than wanted
    bool Explains(int dx, int dy, int wanted)
    {
        uint64_t both = _changed;
        if (!_inside)
            both &= Word(_valid[kReach - dx], kReach - dy) & Word(_valid[kReach + dx], kReach + dy);
        for (int plane = 0; plane < _planes; plane++)
        {
            if (!(_shifted & (1u << plane)))
                Shift(plane);
            both &= ~(_blockB[plane] ^ Word(_a[plane][kReach - dx], kReach - dy)) &
                    ~(_blockA[plane] ^ Word(_b[plane][kReach + dx], kReach + dy));
            if (Ones(both) < wanted)
                return false;
        }
        return true;
    }

private:
    /// Eight rows from `row`, a byte each
    static uint64_t Word(const uint8_t* rows, int row)
    {
        uint64_t word;
        std::memcpy(&word, rows + row, sizeof(word));
        return word;
    }
    /// Eight rows from `row` of 24-bit rows, the 8 columns from `column`, a byte each
    static uint64_t RowsWord(const uint32_t* rows, int row, int column)
    {
        uint64_t word = 0;
        for (int i = 0; i < 8; i++)
            word |= static_cast<uint64_t>((rows[row + i] >> column) & 0xFF) << (8 * i);
        return word;
    }
    void Shift(int plane)
    {
        for (int shift = 0; shift <= 2 * kReach; shift++)
            for (int r = 0; r < kSide; r++)
            {
                _a[plane][shift][r] = static_cast<uint8_t>(_rowsA[plane][r] >> shift);
                _b[plane][shift][r] = static_cast<uint8_t>(_rowsB[plane][r] >> shift);
            }
        _shifted |= 1u << plane;
    }

    int _planes = 1;
    unsigned _shifted = 0;  ///< planes whose shifted bytes are made
    uint32_t _rowsA[kPlanes][kSide], _rowsB[kPlanes][kSide], _validRows[kSide];
    uint8_t _a[kPlanes][2 * kReach + 1][kSide];
    uint8_t _b[kPlanes][2 * kReach + 1][kSide];
    uint8_t _valid[2 * kReach + 1][kSide];
    uint64_t _blockA[kPlanes], _blockB[kPlanes];
    uint64_t _changed = 0;
    bool _inside = false;
};
}  // namespace

bool SmoothMotion::MovesBy(const uint32_t* a, const uint32_t* b, int bx, int by, int x1, int y1, int changed) const
{
    // explained * kFlickerDenominator >= changed * kFlickerNumerator, in whole pixels
    const int wanted = (changed * kFlickerNumerator + kFlickerDenominator - 1) / kFlickerDenominator;
    if (BlockShifts shifts; shifts.Build(a, b, _width, _height, bx, by))
    {
        for (int dy = -BlockShifts::kReach; dy <= BlockShifts::kReach; dy++)
            for (int dx = -BlockShifts::kReach; dx <= BlockShifts::kReach; dx++)
                if ((dx != 0 || dy != 0) && shifts.Explains(dx, dy, wanted))
                    return true;
        return false;
    }
    // The block's changed pixels once; per shift the count stops as soon as it is reached or out of reach
    constexpr int kPixels = MotionField::kBlock * MotionField::kBlock;
    int xs[kPixels], ys[kPixels];
    int count = 0;
    for (int py = by; py < y1; py++)
        for (int px = bx; px < x1; px++)
            if (a[static_cast<size_t>(py) * _width + px] != b[static_cast<size_t>(py) * _width + px])
            {
                xs[count] = px;
                ys[count++] = py;
            }
    // explained * kFlickerDenominator >= changed * kFlickerNumerator, in whole pixels
    const int needed = (changed * kFlickerNumerator + kFlickerDenominator - 1) / kFlickerDenominator;
    const int reach = MotionField::kBlock;
    for (int dy = -reach; dy <= reach; dy++)
        for (int dx = -reach; dx <= reach; dx++)
        {
            if (dx == 0 && dy == 0)
                continue;
            int explained = 0;
            for (int k = 0; k < count && explained < needed && explained + (count - k) >= needed; k++)
            {
                const int px = xs[k], py = ys[k];
                const int fx = px - dx, fy = py - dy, tx = px + dx, ty = py + dy;
                if (fx < 0 || fy < 0 || fx >= _width || fy >= _height || tx < 0 || ty < 0 || tx >= _width ||
                    ty >= _height)
                    continue;
                const size_t p = static_cast<size_t>(py) * _width + px;
                explained += b[p] == a[static_cast<size_t>(fy) * _width + fx] &&
                             a[p] == b[static_cast<size_t>(ty) * _width + tx];
            }
            if (explained >= needed)
                return true;
        }
    return false;
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

    if ((_mode == Mode::Interpolate || _mode == Mode::InterpolateLite) && _hasGame)
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
            KeepHeld(a->smooth, a->pixels.data(), out);
            _stats.blended++;
            break;
        case Kind::Interpolate:
        {
            bool drawn = false;
            if (Lite())
            {
                if (!_lite)
                    _lite = std::make_unique<LiteFlow>();
                drawn = _lite->Compose(pa, pb, r.weight, out);
                if (drawn)
                    _stats.lite++;
            }
            if (!drawn)
            {
                // The frame before a for the still and steady checks (a itself when it is the first)
                const Stored* p = a;
                for (size_t i = 1; i < _frames.size(); i++)
                    if (&_frames[i] == a)
                        p = &_frames[i - 1];
                ComposeLayers(*p, *a, *b, r.weight, out);
            }
            KeepHeld(a->smooth, a->pixels.data(), out);
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
