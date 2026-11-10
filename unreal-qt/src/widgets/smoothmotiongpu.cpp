#include "smoothmotiongpu.h"

#include <QOpenGLContext>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "widgets/gpupasstimes.h"

using smoothmotion::MotionField;
using smoothmotion::Picture;

namespace
{
using smoothmotion::LayerFlow;

constexpr int kSearchChunk = 9;  // the search order's vectors per fragment of pass 1 (range 8: 289 in 17 chunks)

// The shaders' constants are the C++ ones (ShaderHeader); what their packing and arrays assume
static_assert(smoothmotion::kOne == 1 << 8, "Shift and Mix: (w c + kOne / 2) >> 8");
static_assert(MotionField::kBlock * MotionField::kBlock < 128, "BlockColours: a block's count in 7 bits");
static_assert(LayerFlow::kSupportRadius == 16, "CoverMasks: a row's 2 r pixels in one 32-bit word");
static_assert((2 * LayerFlow::kSupportRadius + 1) * (2 * LayerFlow::kSupportRadius + 1) < 4096,
              "Top: a support in 12 bits");
static_assert(LayerFlow::kMaxVectors < (1u << 19), "Top: a vector index above the support's 12 bits");

/// The texel reads of a fragment of the picture pass at most: per allowed vector its checks, then the cover rule's
/// rows (the masks of every candidate and the row's colours)
constexpr int kLayersReads = static_cast<int>(LayerFlow::kMaxAllowed) * 16 +
                             (2 * LayerFlow::kSupportRadius + 1) *
                                 (2 * static_cast<int>(LayerFlow::kMaxAllowed) + 2 * LayerFlow::kSupportRadius + 1);

/// The constants every fragment shader shares, from the C++ ones, after the version line
QString ShaderHeader()
{
    return QString::asprintf("#version 330\n"
                             "#define kBlock %d\n"
                             "#define kHalfBlock %d\n"
                             "#define kAccept %d\n"
                             "#define kGlobalVectors %d\n"
                             "#define kRefineCandidates %d\n"
                             "#define kComposeCommon %d\n"
                             "#define kComposeCandidates %d\n"
                             "#define kOne %d\n"
                             "#define kHalfOne %d\n"
                             "#define kOneShift 8\n"
                             "#define kSupportRadius %d\n"
                             "#define kMaxAllowed %d\n"
                             "#define kNoColour %du\n",
                             MotionField::kBlock, MotionField::kBlock / 2, MotionField::kAccept,
                             static_cast<int>(MotionField::kGlobalVectors),
                             static_cast<int>(1 + MotionField::kGlobalVectors + 8),
                             static_cast<int>(MotionField::kComposeCommon),
                             static_cast<int>(10 + MotionField::kComposeCommon), smoothmotion::kOne,
                             smoothmotion::kOne / 2, LayerFlow::kSupportRadius,
                             static_cast<int>(LayerFlow::kMaxAllowed), 255);
}

// The quad over the viewport from the vertex index (a strip). Fragment (x, y) is pixel (x, y), rows top first as the
// pictures are uploaded and read back: nothing is turned over
const char* const kVertexShader = R"(
#version 330
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

// The reference's helpers (smoothmotion.cpp): Picture::At clamped to the edges, Shift = (w c + kOne / 2) >> 8 (GLSL shifts
// a signed int arithmetically, as C++20 does); Ones: the bits set (bitCount needs GLSL 4.0)
const char* const kCommon = R"(
uniform ivec2 size;
uniform usampler2D frameA;
uniform usampler2D frameB;
uvec4 At(usampler2D t, ivec2 p) { return texelFetch(t, clamp(p, ivec2(0), size - 1), 0); }
int Shift(int w, int c) { return (w * c + kHalfOne) >> kOneShift; }
bool Same(uvec4 a, uvec4 b) { return all(equal(a, b)); }
uvec4 Mix(uvec4 a, uvec4 b, int w) { return (a * uint(kOne - w) + b * uint(w) + uint(kHalfOne)) >> uint(kOneShift); }
int Ones(uint v) {
    v = v - ((v >> 1) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    return int((((v + (v >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24);
}
)";

const char* const kBlendShader = R"(
uniform int weight;
out uvec4 color;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    color = Mix(texelFetch(frameA, p, 0), texelFetch(frameB, p, 0), weight);
}
)";

// MotionField::Estimate's cost: the pixels of block `blk` where A and B warped to the middle by v disagree, counted up
// to limit + 1
const char* const kCost = R"(
int Cost(ivec2 blk, ivec2 v, int limit) {
    ivec2 s = ivec2(Shift(kHalfOne, v.x), Shift(kHalfOne, v.y));
    ivec2 ka = -s;
    ivec2 kb = v - s;
    ivec2 p0 = blk * kBlock;
    ivec2 p1 = min(p0 + kBlock, size);
    int bad = 0;
    for (int y = p0.y; y < p1.y; y++)
        for (int x = p0.x; x < p1.x; x++)
            if (!Same(At(frameA, ivec2(x, y) + ka), At(frameB, ivec2(x, y) + kb)) && ++bad > limit)
                return bad;
    return bad;
}
)";

// Pass 1 in two draws, the search order cut into chunks so every block searches in parallel: per block and chunk the
// first vector of the chunk with the fewest mismatches (capped at kAccept + 1); then per block the first chunk with
// the fewest. The first vector of the whole order with the fewest, as the reference's sequential search
const char* const kSearchChunkShader = R"(
uniform isampler2D order;
uniform int orderCount;
uniform int blocksWide;
uniform int chunk;
out ivec4 result;
void main() {
    ivec2 at = ivec2(gl_FragCoord.xy);
    ivec2 blk = ivec2(at.x % blocksWide, at.y);
    int first = at.x / blocksWide * chunk;
    int last = min(first + chunk, orderCount);
    int best = kAccept + 1;
    ivec2 bestV = ivec2(0);
    for (int i = first; i < last && best > 0; i++) {
        ivec2 v = texelFetch(order, ivec2(i, 0), 0).xy;
        int bad = Cost(blk, v, best - 1);
        if (bad < best) {
            best = bad;
            bestV = v;
        }
    }
    result = ivec4(bestV, best, 0);
}
)";

const char* const kSearchShader = R"(
uniform isampler2D chunks;
uniform int chunkCount;
uniform int blocksWide;
out ivec4 result;
void main() {
    ivec2 blk = ivec2(gl_FragCoord.xy);
    int best = kAccept + 1;
    ivec2 bestV = ivec2(0);
    for (int c = 0; c < chunkCount && best > 0; c++) {
        ivec4 r = texelFetch(chunks, ivec2(c * blocksWide + blk.x, blk.y), 0);
        if (r.z < best) {
            best = r.z;
            bestV = r.xy;
        }
    }
    bool matched = best <= kAccept;
    result = ivec4(matched ? bestV : ivec2(0), matched ? 1 : 0, 0);
}
)";

// A refinement pass from the previous one: zero, the votes, the eight neighbours' (row by row), the first of the
// fewest mismatches within kAccept; none: pass 1's
const char* const kRefineShader = R"(
uniform isampler2D prevField;
uniform isampler2D firstField;
uniform ivec2 blocks;
uniform ivec2 votes[kGlobalVectors];
uniform int voteCount;
out ivec4 result;
ivec2 cands[kRefineCandidates];
int n = 0;
void Add(ivec2 v) {
    for (int c = 0; c < n; c++)
        if (cands[c] == v)
            return;
    cands[n++] = v;
}
void main() {
    ivec2 blk = ivec2(gl_FragCoord.xy);
    Add(ivec2(0));
    for (int k = 0; k < voteCount; k++)
        Add(votes[k]);
    for (int dj = -1; dj <= 1; dj++)
        for (int di = -1; di <= 1; di++) {
            ivec2 q = blk + ivec2(di, dj);
            if ((di != 0 || dj != 0) && all(greaterThanEqual(q, ivec2(0))) && all(lessThan(q, blocks))) {
                ivec4 b = texelFetch(prevField, q, 0);
                if (b.z != 0)
                    Add(b.xy);
            }
        }
    int best = kAccept + 1;
    ivec2 bestV = ivec2(0);
    for (int c = 0; c < n && best > 0; c++) {
        int bad = Cost(blk, cands[c], best - 1);
        if (bad < best) {
            best = bad;
            bestV = cands[c];
        }
    }
    result = best <= kAccept ? ivec4(bestV, 1, 0) : texelFetch(firstField, blk, 0);
}
)";

// MotionField::Compose per pixel: of the candidates (own, zero, left, right, up, down, the diagonals, the common
// vectors) the one the warped A and B agree on with the most of the 3x3 around, the first of equals; none: the blend,
// or B past it. Extrapolating, a moving vector only draws pixels whose sources changed
const char* const kComposeShader = R"(
uniform isampler2D field;
uniform ivec2 blocks;
uniform ivec2 commonVectors[kComposeCommon];
uniform int commonCount;
uniform int weight;
uniform bool useMask;
uniform usampler2D mask;
uniform usampler2D base;
out uvec4 color;
ivec2 cands[kComposeCandidates];
int n = 0;
void Add(ivec2 v) {
    for (int c = 0; c < n; c++)
        if (cands[c] == v)
            return;
    cands[n++] = v;
}
void AddBlock(ivec2 q) {
    if (all(greaterThanEqual(q, ivec2(0))) && all(lessThan(q, blocks))) {
        ivec4 b = texelFetch(field, q, 0);
        if (b.z != 0)
            Add(b.xy);
    }
}
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    if (useMask && texelFetch(mask, p, 0).r == 0u) {
        color = texelFetch(base, p, 0);
        return;
    }
    ivec2 blk = p / kBlock;
    AddBlock(blk);
    Add(ivec2(0));
    AddBlock(blk + ivec2(-1, 0));
    AddBlock(blk + ivec2(1, 0));
    AddBlock(blk + ivec2(0, -1));
    AddBlock(blk + ivec2(0, 1));
    AddBlock(blk + ivec2(-1, -1));
    AddBlock(blk + ivec2(1, -1));
    AddBlock(blk + ivec2(-1, 1));
    AddBlock(blk + ivec2(1, 1));
    for (int k = 0; k < commonCount; k++)
        Add(commonVectors[k]);
    uvec4 pixel = uvec4(0u);
    int bestScore = 0;
    for (int c = 0; c < n; c++) {
        ivec2 v = cands[c];
        ivec2 s = ivec2(Shift(weight, v.x), Shift(weight, v.y));
        ivec2 sa = p - s;
        ivec2 sb = p + v - s;
        uvec4 va = At(frameA, sa);
        if (!Same(va, At(frameB, sb)))
            continue;
        if (weight > kOne && v != ivec2(0) && Same(At(frameA, sa), At(frameB, sa)) && Same(At(frameA, sb), At(frameB, sb)))
            continue;
        int score = 0;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (Same(At(frameA, sa + ivec2(dx, dy)), At(frameB, sb + ivec2(dx, dy))))
                    score++;
        if (score > bestScore) {
            bestScore = score;
            pixel = va;
        }
    }
    if (bestScore > 0)
        color = pixel;
    else
        color = weight > kOne ? texelFetch(frameB, p, 0) : Mix(texelFetch(frameA, p, 0), texelFetch(frameB, p, 0), weight);
}
)";

// LayerFlow (smoothlayers.cpp), its per-pixel passes. The per-vector images are tiles of one texture, tileCols a row,
// every vector in one draw: vector c's pixel q at Tile(c, q)
const char* const kLayerFrames = R"(
uniform usampler2D frameP;
uniform isampler2D vectors;
uniform int vectorCount;
uniform int weight;
uniform ivec2 blocks;
uniform int tileCols;
ivec2 Vec(int c) { return texelFetch(vectors, ivec2(c, 0), 0).xy; }
ivec2 Shifted(ivec2 v) { return ivec2(Shift(weight, v.x), Shift(weight, v.y)); }
bool Slower(ivec2 u, ivec2 v) { return u.x * u.x + u.y * u.y < v.x * v.x + v.y * v.y; }
ivec2 Tile(int c, ivec2 q) { return ivec2(c % tileCols, c / tileCols) * size + q; }
// The fragment's tile (vector) and pixel in a tiled target of tiles `tileSize`
int TileOf(ivec2 f, ivec2 tileSize) { ivec2 t = f / tileSize; return t.y * tileCols + t.x; }
)";

// The evidence of the vectors: fit bits (fits, fitting, changes, the cover's balance + 1) and sums (score, support,
// fits | usable << 1)
const char* const kLayerFields = R"(
uniform usampler2D fitBits;
uniform usampler2D sums;
uint Bits(int c, ivec2 q) { return texelFetch(fitBits, Tile(c, q), 0).r; }
bool Fits(int c, ivec2 q) { return (Bits(c, q) & 1u) != 0u; }
uvec4 SumsOf(int c, ivec2 q) { return texelFetch(sums, Tile(c, q), 0); }
int Score(int c, ivec2 q) { return int(SumsOf(c, q).r); }
int Support(int c, ivec2 q) { return int(SumsOf(c, q).g); }
bool Usable(int c, ivec2 q) { return (SumsOf(c, q).b & 2u) != 0u; }
)";

const char* const kChangedShader = R"(
out uvec4 result;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    result = uvec4(Same(texelFetch(frameA, p, 0), texelFetch(frameB, p, 0)) ? 0u : 1u, 0u, 0u, 0u);
}
)";

// WindowSums, the row: per channel the pixels of the tile with the mask's bits round the pixel, clipped to the tile
const char* const kRowSumsShader = R"(
uniform usampler2D source;
uniform ivec2 masks;
uniform ivec2 radii;
out uvec4 result;
void main() {
    ivec2 f = ivec2(gl_FragCoord.xy);
    ivec2 origin = f / size * size;
    ivec2 q = f - origin;
    int r = max(radii.x, radii.y);
    uvec2 sum = uvec2(0u);
    for (int x = max(q.x - r, 0); x <= min(q.x + r, size.x - 1); x++) {
        int bits = int(texelFetch(source, origin + ivec2(x, q.y), 0).r);
        int d = abs(x - q.x);
        if (d <= radii.x && (bits & masks.x) != 0)
            sum.x++;
        if (d <= radii.y && (bits & masks.y) != 0)
            sum.y++;
    }
    result = uvec4(sum, 0u, 0u);
}
)";

// WindowSums, the column over the row sums. With flags (the vectors' tiles): what the vector fits and may take, a
// moving one with support reaching the bar (the window's changes no moving vector explains)
const char* const kColumnSumsShader = R"(
uniform usampler2D source;
uniform ivec2 radii;
uniform bool flags;
uniform usampler2D fitBits;
uniform usampler2D stillSums;
uniform usampler2D explainedSums;
uniform int tileCols;
out uvec4 result;
void main() {
    ivec2 f = ivec2(gl_FragCoord.xy);
    ivec2 tile = f / size;
    ivec2 origin = tile * size;
    ivec2 q = f - origin;
    int r = max(radii.x, radii.y);
    uvec2 sum = uvec2(0u);
    for (int y = max(q.y - r, 0); y <= min(q.y + r, size.y - 1); y++) {
        uvec2 row = texelFetch(source, origin + ivec2(q.x, y), 0).rg;
        int d = abs(y - q.y);
        if (d <= radii.x)
            sum.x += row.x;
        if (d <= radii.y)
            sum.y += row.y;
    }
    uint bits = 0u;
    if (flags) {
        bool fits = (texelFetch(fitBits, f, 0).r & 1u) != 0u;
        int bar = max(int(texelFetch(stillSums, q, 0).g) - int(texelFetch(explainedSums, q, 0).g), 0);
        bool usable = fits && (tile.y * tileCols + tile.x == 0 || (sum.y > 0u && int(sum.y) >= bar));
        bits = (fits ? 1u : 0u) | (usable ? 2u : 0u);
    }
    result = uvec4(sum, bits, 0u);
}
)";

// Per vector: fits (still3, stopped), fitting, changes, and the cover's balance + 1
const char* const kFitShader = R"(
uniform usampler2D stillSums;
out uvec4 result;
int Balance(ivec2 q, ivec2 v) {
    uvec4 here = At(frameA, q), from = At(frameA, q - v);
    if (!Same(At(frameP, q), here) || Same(from, At(frameB, q - v)) || Same(from, here))
        return 0;
    uvec4 later = At(frameB, q);
    if (Same(later, from))
        return 1;
    return Same(later, here) && Same(At(frameP, q - 2 * v), from) ? -1 : 0;
}
void main() {
    ivec2 f = ivec2(gl_FragCoord.xy);
    int c = TileOf(f, size);
    if (c >= vectorCount) {
        result = uvec4(0u);
        return;
    }
    ivec2 x = f - f / size * size;
    ivec2 v = Vec(c);
    ivec2 s = Shifted(v);
    uvec4 va = At(frameA, x - s), vb = At(frameB, x - s);
    bool fit = Same(va, At(frameB, x - s + v)) &&
               (Same(At(frameA, x), At(frameB, x + v)) || Same(At(frameB, x), At(frameA, x - v)));
    bool calm = texelFetch(stillSums, x, 0).r == 0u;
    bool fits = fit && (v != ivec2(0) || Same(At(frameP, x), va) || calm);
    bool changes = fit && !Same(va, vb) && Same(vb, At(frameA, x - s - v));
    uint bits = (fits ? 1u : 0u) | (fit ? 2u : 0u) | (changes ? 4u : 0u) | (uint(Balance(x, v) + 1) << 3);
    result = uvec4(bits, 0u, 0u, 0u);
}
)";

// A moving vector explains the change
const char* const kExplainedShader = R"(
uniform usampler2D fitBits;
out uvec4 result;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    uint e = 0u;
    for (int c = 1; c < vectorCount; c++)
        e |= (texelFetch(fitBits, Tile(c, p), 0).r >> 2) & 1u;
    result = uvec4(e, 0u, 0u, 0u);
}
)";

// Per vector, block column and pixel row (tiles blocks.x x size.y): the pixels it fits | those it may take << 8
const char* const kRowCountsShader = R"(
out ivec4 result;
void main() {
    ivec2 f = ivec2(gl_FragCoord.xy);
    ivec2 tileSize = ivec2(blocks.x, size.y);
    int c = TileOf(f, tileSize);
    ivec2 at = f - f / tileSize * tileSize;
    int fit = 0;
    int use = 0;
    if (c < vectorCount)
        for (int x = at.x * kBlock; x < min(at.x * kBlock + kBlock, size.x); x++) {
            uint bits = SumsOf(c, ivec2(x, at.y)).b;
            fit += int(bits & 1u);
            use += int(bits >> 1);
        }
    result = ivec4(fit | (use << 8), 0, 0, 0);
}
)";

// Per block, four vectors a row of blocks: the pixels each fits | those it may take << 8
const char* const kCountsShader = R"(
uniform isampler2D rowCounts;
out ivec4 result;
void main() {
    ivec2 f = ivec2(gl_FragCoord.xy);
    int group = f.y / blocks.y;
    ivec2 blk = ivec2(f.x, f.y - group * blocks.y);
    ivec4 r = ivec4(0);
    for (int k = 0; k < 4; k++) {
        int c = group * 4 + k;
        if (c >= vectorCount)
            break;
        ivec2 origin = ivec2(c % tileCols, c / tileCols) * ivec2(blocks.x, size.y);
        int sum = 0;
        for (int y = blk.y * kBlock; y < min(blk.y * kBlock + kBlock, size.y); y++)
            sum += texelFetch(rowCounts, origin + ivec2(blk.x, y), 0).r & 0xFFFF;
        r[k] = sum;
    }
    result = r;
}
)";

// The aliases rule, per pixel of an asked block: the three fitting vectors (of those fitting some of the block) with
// the most support, vector << 12 | support, -1
const char* const kTopShader = R"(
uniform isampler2D askAt;
uniform isampler2D counts;
out ivec4 result;
int FitCount(int u, ivec2 blk) {
    return texelFetch(counts, ivec2(blk.x, blk.y + (u / 4) * blocks.y), 0)[u % 4] & 255;
}
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 blk = p / kBlock;
    if (texelFetch(askAt, blk, 0).r < 0) {
        result = ivec4(-1);
        return;
    }
    int ids[3] = int[3](-1, -1, -1);
    int supports[3] = int[3](-1, -1, -1);
    for (int u = 0; u < vectorCount; u++) {
        if (FitCount(u, blk) == 0 || !Fits(u, p))
            continue;
        int tv = u;
        int ts = Support(u, p);
        for (int k = 0; k < 3; k++)
            if (ts > supports[k]) {
                int v0 = ids[k];
                int s0 = supports[k];
                ids[k] = tv;
                supports[k] = ts;
                tv = v0;
                ts = s0;
            }
    }
    for (int k = 0; k < 3; k++)
        result[k] = ids[k] < 0 ? -1 : ids[k] << 12 | supports[k];
    result.w = 0;
}
)";

// The aliases rule's own(c, s) per ask and vector s: (own(s, none), own(the ask's c, s))
const char* const kOwnShader = R"(
uniform isampler2D asks;
uniform isampler2D top;
out ivec4 result;
int Own(ivec2 blk, int c, int s) {
    ivec2 p0 = blk * kBlock;
    ivec2 p1 = min(p0 + kBlock, size);
    int count = 0;
    for (int y = p0.y; y < p1.y; y++)
        for (int x = p0.x; x < p1.x; x++) {
            ivec2 q = ivec2(x, y);
            if (!Fits(c, q) || (s >= 0 && Fits(s, q)))
                continue;
            int mine = Support(c, q);
            ivec4 t = texelFetch(top, q, 0);
            bool other = false;
            for (int k = 0; k < 3; k++) {
                int tv = t[k] < 0 ? -1 : t[k] >> 12;
                int ts = t[k] < 0 ? -1 : t[k] & 4095;
                other = other || (tv >= 0 && tv != c && tv != s && ts > mine);
            }
            if (!other)
                count++;
        }
    return count;
}
void main() {
    ivec2 f = ivec2(gl_FragCoord.xy);
    ivec2 ask = texelFetch(asks, ivec2(f.y, 0), 0).xy;
    ivec2 blk = ivec2(ask.x % blocks.x, ask.x / blocks.x);
    result = ivec4(Own(blk, f.x, -1), Own(blk, ask.y, f.x), 0, 0);
}
)";

// Per pixel, its colour within its block and the blocks next to it: the block's pixels of it | the first of them
// (row by row) << 7 | present in the block left, right, above, below << 8..11 (of the halves an edge asks: left
// of the left half, right of the right one)
const char* const kBlockColoursShader = R"(
out ivec4 result;
uvec4 A(int x, int y) { return texelFetch(frameA, ivec2(x, y), 0); }
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 blk = p / kBlock;
    ivec2 b0 = blk * kBlock;
    ivec2 b1 = min(b0 + kBlock, size);
    uvec4 colour = A(p.x, p.y);
    int count = 0;
    bool first = true;
    for (int y = b0.y; y < b1.y; y++)
        for (int x = b0.x; x < b1.x; x++)
            if (Same(A(x, y), colour)) {
                count++;
                if (y < p.y || (y == p.y && x < p.x))
                    first = false;
            }
    int present = 0;
    ivec2 dirs[4] = ivec2[4](ivec2(-1, 0), ivec2(1, 0), ivec2(0, -1), ivec2(0, 1));
    ivec2 side = (p - b0) / kHalfBlock;
    for (int d = 0; d < 4; d++) {
        ivec2 nb = blk + dirs[d];
        if (any(lessThan(nb, ivec2(0))) || any(greaterThanEqual(nb * kBlock, size)) || (d < 2 ? side.x : side.y) != d % 2)
            continue;
        ivec2 n0 = nb * kBlock;
        ivec2 n1 = min(n0 + kBlock, size);
        bool found = false;
        for (int y = n0.y; y < n1.y && !found; y++)
            for (int x = n0.x; x < n1.x && !found; x++)
                found = Same(A(x, y), colour);
        if (found)
            present |= 1 << d;
    }
    result = ivec4(count | (first ? 1 << 7 : 0) | present << 8, 0, 0, 0);
}
)";

// The regions rule's continued per edge and vector (the neighbour's, once the aliases rule decided it): every colour
// of the neighbour's half at the edge is the block's, and the vector fits three quarters of its content (the colours
// other than its paper, the most frequent, the first met of equals)
const char* const kContinuesShader = R"(
uniform isampler2D edges;
uniform isampler2D blockColours;
out ivec4 result;
void main() {
    ivec4 e = texelFetch(edges, ivec2(int(gl_FragCoord.x), 0), 0);
    int bi = e.x, bj = e.y, di = e.z % 3 - 1, dj = e.z / 3 - 1, nv = int(gl_FragCoord.y);
    int nx0 = (bi + di) * kBlock, ny0 = (bj + dj) * kBlock;
    int nx1 = min(nx0 + kBlock, size.x), ny1 = min(ny0 + kBlock, size.y);
    uvec4 paper = uvec4(0u);
    int most = 0;
    for (int y = ny0; y < ny1; y++)
        for (int x = nx0; x < nx1; x++) {
            int info = texelFetch(blockColours, ivec2(x, y), 0).r;
            if ((info & (1 << 7)) != 0 && (info & 127) > most) {
                most = info & 127;
                paper = texelFetch(frameA, ivec2(x, y), 0);
            }
        }
    int x0 = nx0, x1 = nx0 + kBlock, y0 = ny0, y1 = ny0 + kBlock;
    if (di < 0)
        x0 = x1 - kHalfBlock;
    if (di > 0)
        x1 = x0 + kHalfBlock;
    if (dj < 0)
        y0 = y1 - kHalfBlock;
    if (dj > 0)
        y1 = y0 + kHalfBlock;
    x1 = min(x1, size.x);
    y1 = min(y1, size.y);
    // the block is the neighbour's neighbour the other way
    int toBlock = di > 0 ? 0 : di < 0 ? 1 : dj > 0 ? 2 : 3;
    int content = 0, foreign = 0, carried = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            if ((texelFetch(blockColours, ivec2(x, y), 0).r & (1 << (8 + toBlock))) == 0)
                foreign++;
            if (!Same(texelFetch(frameA, ivec2(x, y), 0), paper)) {
                content++;
                if (Fits(nv, ivec2(x, y)))
                    carried++;
            }
        }
    result = ivec4(content > 0 && foreign == 0 && carried * 4 >= content * 3 ? 1 : 0, 0, 0, 0);
}
)";

// The cover rule's masks per vector: of the pixels x - 16 .. x + 15 of the row (bit i: x - 16 + i), those whose
// balance wins (+1) and loses (-1); out of the picture none
const char* const kCoverMasksShader = R"(
out uvec4 result;
void main() {
    ivec2 f = ivec2(gl_FragCoord.xy);
    int c = TileOf(f, size);
    if (c >= vectorCount) {
        result = uvec4(0u);
        return;
    }
    ivec2 origin = f / size * size;
    ivec2 q = f - origin;
    uint wins = 0u, losses = 0u;
    for (int i = 0; i < 2 * kSupportRadius; i++) {
        int x = q.x - kSupportRadius + i;
        if (x < 0 || x >= size.x)
            continue;
        uint balance = (texelFetch(fitBits, origin + ivec2(x, q.y), 0).r >> 3) & 3u;
        if (balance == 2u)
            wins |= 1u << uint(i);
        else if (balance == 0u)
            losses |= 1u << uint(i);
    }
    result = uvec4(wins, losses, 0u, 0u);
}
)";

// The picture: per pixel cover, the block's vector, carryFirst, the best score, the nearer frame. The cover rule's
// balance of a vector round q over the pixels of q's colour, per row of the window: the vector's masks (CoverMasks,
// the window's last pixel from its fit bits) and the row's pixels of q's colour, read once for every candidate
const char* const kLayersShader = R"(
uniform isampler2D blockInfo;
uniform isampler2D allowed;
uniform usampler2D colour;
uniform usampler2D coverMasks;
out uvec4 color;
int Allowed(ivec2 blk, int k) { return texelFetch(allowed, ivec2(blk.x * kMaxAllowed + k, blk.y), 0).r; }
int Balance(int c, ivec2 q) { return int((Bits(c, q) >> 3) & 3u) - 1; }
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 blk = p / kBlock;
    ivec4 info = texelFetch(blockInfo, blk, 0);
    int bv = info.x;
    bool tied = info.y != 0;
    bool explains = info.z != 0;
    int count = info.w;
    uvec4 here = texelFetch(frameA, p, 0);
    uvec4 later = texelFetch(frameB, p, 0);
    uvec4 before = texelFetch(frameP, p, 0);
    if (Same(here, later) && !(bv >= 0 && Vec(bv) != ivec2(0) && Usable(bv, p)) && Same(before, here)) {
        // the candidates: moving, with support, their steady content arriving over the pixel
        int cands[kMaxAllowed];
        int supports[kMaxAllowed];
        int sums[kMaxAllowed];
        int m = 0;
        for (int k = 0; k < count; k++) {
            int c = Allowed(blk, k);
            ivec2 v = Vec(c);
            int support = Support(c, p);
            if (v == ivec2(0) || support == 0)
                continue;
            ivec2 sa = p - Shifted(v);
            uvec4 va = At(frameA, sa);
            if (!Same(va, At(frameB, sa + v)) || Same(va, At(frameB, sa)) || Same(va, here))
                continue;
            if (!Same(At(frameP, sa - v), va))
                continue;
            cands[m] = c;
            supports[m] = support;
            sums[m] = 0;
            m++;
        }
        uint col = texelFetch(colour, p, 0).r;
        if (m > 0 && col != kNoColour) {
            int edgeX = p.x + kSupportRadius;
            for (int y = max(p.y - kSupportRadius, 0); y <= min(p.y + kSupportRadius, size.y - 1); y++) {
                bool read = false;
                uint same = 0u;
                bool sameEdge = false;
                for (int j = 0; j < m; j++) {
                    uvec2 masks = texelFetch(coverMasks, Tile(cands[j], ivec2(p.x, y)), 0).rg;
                    int edge = edgeX < size.x ? Balance(cands[j], ivec2(edgeX, y)) : 0;
                    if (masks.x == 0u && masks.y == 0u && edge == 0)
                        continue;
                    if (!read) {
                        read = true;
                        for (int i = 0; i < 2 * kSupportRadius; i++) {
                            int x = p.x - kSupportRadius + i;
                            if (x >= 0 && x < size.x && texelFetch(colour, ivec2(x, y), 0).r == col)
                                same |= 1u << uint(i);
                        }
                        sameEdge = edgeX < size.x && texelFetch(colour, ivec2(edgeX, y), 0).r == col;
                    }
                    sums[j] += Ones(masks.x & same) - Ones(masks.y & same) + (sameEdge ? edge : 0);
                }
            }
        }
        int over = -1;
        int overSupport = 0;
        for (int j = 0; j < m; j++)
            if (sums[j] > 0 && (over < 0 || supports[j] > overSupport)) {
                over = cands[j];
                overSupport = supports[j];
            }
        if (over >= 0) {
            color = At(frameA, p - Shifted(Vec(over)));
            return;
        }
    }
    if (bv >= 0 && (Usable(bv, p) || (tied && Fits(bv, p)))) {
        color = At(frameA, p - Shifted(Vec(bv)));
        return;
    }
    if (bv >= 0 && Vec(bv) != ivec2(0) && !tied && explains && !(Same(before, here) && !Same(here, later))) {
        int mine = Support(bv, p);
        bool other = false;
        for (int k = 0; k < count; k++) {
            int c = Allowed(blk, k);
            other = other || (c != bv && Usable(c, p) && (Slower(Vec(c), Vec(bv)) || Support(c, p) > mine));
        }
        if (!other) {
            ivec2 v = Vec(bv);
            ivec2 sa = p - Shifted(v);
            color = weight < kHalfOne ? At(frameA, sa) : At(frameB, sa + v);
            return;
        }
    }
    int best = -1;
    int bestScore = -1;
    for (int k = 0; k < count; k++) {
        int c = Allowed(blk, k);
        if (!Usable(c, p))
            continue;
        int score = Score(c, p);
        if (score > bestScore || (score == bestScore && Slower(Vec(c), Vec(best)))) {
            best = c;
            bestScore = score;
        }
    }
    if (best < 0)
        color = weight < kHalfOne ? here : later;
    else
        color = At(frameA, p - Shifted(Vec(best)));
}
)";

QOpenGLShaderProgram* Build(const QString& fragment, QString& log)
{
    auto* program = new QOpenGLShaderProgram();
    if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
        !program->addShaderFromSourceCode(QOpenGLShader::Fragment, ShaderHeader() + fragment) || !program->link())
    {
        log += program->log();
        delete program;
        return nullptr;
    }
    return program;
}

// The most texel reads a fragment of each pass does: DrawInto cuts a draw into stripes by it
constexpr int kBlockPixels = MotionField::kBlock * MotionField::kBlock;
constexpr int kPixelReads = 3;  // changed, blend
constexpr int kSearchChunkReads = kSearchChunk * (2 * kBlockPixels + 1);
constexpr int kRefineReads = static_cast<int>(1 + MotionField::kGlobalVectors + 8) * 2 * kBlockPixels + 9;
constexpr int kComposeReads = static_cast<int>(10 + MotionField::kComposeCommon) * 24 + 12;
constexpr int kWindowReads = 2 * LayerFlow::kSupportRadius + 1 + 3;  // a window's sum, the flags
constexpr int kFitReads = 12;
constexpr int kVectorReads = static_cast<int>(LayerFlow::kMaxVectors) * 3;  // explained, top
constexpr int kRowCountsReads = 2 * MotionField::kBlock;
constexpr int kCountsReads = 4 * MotionField::kBlock;
constexpr int kCoverMasksReads = 2 * LayerFlow::kSupportRadius;
constexpr int kOwnReads = 2 * kBlockPixels * 6;
constexpr int kBlockColoursReads = 5 * kBlockPixels;
constexpr int kContinuesReads = 4 * kBlockPixels;
constexpr int kMaxErrors = 16;  // GL errors cleared at most: a lost context reports one for ever
}  // namespace

SmoothMotionGPU::~SmoothMotionGPU()
{
    // GL objects need their context: Destroy() with it current; here only what is left in CPU memory
    for (QOpenGLShaderProgram* program : Programs())
        delete program;
}

std::vector<QOpenGLShaderProgram*> SmoothMotionGPU::Programs() const
{
    return {_blend, _searchChunks, _search, _refine, _compose, _changed, _rowSums, _columnSums, _fit, _coverMasks,
            _explained, _rowCounts, _counts, _blockColours, _top, _own, _continues, _layers};
}

SmoothMotionGPU::Format SmoothMotionGPU::FormatOf(Texture texture)
{
    switch (texture)
    {
        case Mask:
        case Colour:
        case Changed:
        case Explained:
        case FitBits:
            return {GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE};
        case RowSums:
        case StillSums:
        case ExplainedSums:
            return {GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT};
        case Sums:
            return {GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT};
        case CoverMasks:
            return {GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT};
        case Order:
        case Asks:
        case Vectors:
            return {GL_RG32I, GL_RG_INTEGER, GL_INT};
        case AskAt:
        case Allowed:
        case RowCounts:
        case BlockColours:
            return {GL_R32I, GL_RED_INTEGER, GL_INT};
        case FieldChunks:
        case FieldOne:
        case FieldPing:
        case FieldPong:
        case Counts:
        case Top:
        case OwnAnswers:
        case Edges:
        case EdgeAnswers:
        case BlockInfo:
            return {GL_RGBA32I, GL_RGBA_INTEGER, GL_INT};
        default:
            return {GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE};
    }
}

bool SmoothMotionGPU::Create()
{
    Destroy();
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context)
    {
        _log = "no current context";
        return false;
    }
    _log.clear();
    const QString common = QString::fromLatin1(kCommon);
    _blend = Build(common + kBlendShader, _log);
    _searchChunks = Build(common + kCost + kSearchChunkShader, _log);
    _search = Build(common + kCost + kSearchShader, _log);
    _refine = Build(common + kCost + kRefineShader, _log);
    _compose = Build(common + kComposeShader, _log);
    const QString frames = common + kLayerFrames;
    const QString fields = frames + kLayerFields;
    _changed = Build(common + kChangedShader, _log);
    _rowSums = Build(common + kRowSumsShader, _log);
    _columnSums = Build(common + kColumnSumsShader, _log);
    _fit = Build(frames + kFitShader, _log);
    _coverMasks = Build(fields + kCoverMasksShader, _log);
    _explained = Build(frames + kExplainedShader, _log);
    _rowCounts = Build(fields + kRowCountsShader, _log);
    _counts = Build(fields + kCountsShader, _log);
    _blockColours = Build(fields + kBlockColoursShader, _log);
    _top = Build(fields + kTopShader, _log);
    _own = Build(fields + kOwnShader, _log);
    _continues = Build(fields + kContinuesShader, _log);
    _layers = Build(fields + kLayersShader, _log);
    const std::vector<QOpenGLShaderProgram*> built = Programs();
    if (std::find(built.begin(), built.end(), nullptr) != built.end() || !_vao.create())
    {
        Destroy();
        return false;
    }
    _gl = context->extraFunctions();
    _gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &_maxSize);
    _gl->glGenFramebuffers(1, &_framebuffer);
    _gl->glGenTextures(TextureCount, _textures);
    for (auto& size : _sizes)
        size[0] = size[1] = 0;
    // UNREAL_SMOOTH_FIELDS=gpu: the motion field on the GPU too (cpu, unset: on the CPU, measured faster)
    if (const char* fields = std::getenv("UNREAL_SMOOTH_FIELDS"))
        _fieldsOnGpu = std::strcmp(fields, "gpu") == 0;
    _readBytes = true;
    ReleaseBuffers();
    return true;
}

void SmoothMotionGPU::ReleaseBuffers()
{
    if (!_gl)
        return;
    // Every texture complete at 1 x 1: a sampler the pass does not read (the mask without one) still has one
    const int32_t zero[4] = {};
    for (int t = 0; t < TextureCount; t++)
        Allocate(static_cast<Texture>(t), 1, 1, zero);
    for (auto& copy : _frameCopies)
        std::vector<uint32_t>().swap(copy);
    for (uint64_t& id : _frameIds)
        id = 0;
    std::vector<uint32_t>().swap(_readBack);
    _holdsBuffers = false;
    _layersWidth = _layersHeight = 0;
    _orderRange = -1;
}

void SmoothMotionGPU::Destroy()
{
    if (_gl)
    {
        _gl->glDeleteTextures(TextureCount, _textures);
        _gl->glDeleteFramebuffers(1, &_framebuffer);
    }
    for (unsigned& texture : _textures)
        texture = 0;
    for (auto& copy : _frameCopies)
        copy.clear();
    for (uint64_t& id : _frameIds)
        id = 0;
    _layersWidth = _layersHeight = 0;
    _framebuffer = 0;
    for (QOpenGLShaderProgram* program : Programs())
        delete program;
    _blend = _searchChunks = _search = _refine = _compose = nullptr;
    _changed = _rowSums = _columnSums = _fit = _coverMasks = _explained = nullptr;
    _rowCounts = _counts = _blockColours = _top = _own = _continues = _layers = nullptr;
    if (_vao.isCreated())
        _vao.destroy();
    _gl = nullptr;
}

void SmoothMotionGPU::Allocate(Texture texture, int width, int height, const void* data)
{
    const Format format = FormatOf(texture);
    _gl->glActiveTexture(GL_TEXTURE0);
    _gl->glBindTexture(GL_TEXTURE_2D, _textures[texture]);
    _gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    _gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    if (_sizes[texture][0] != width || _sizes[texture][1] != height)
    {
        _gl->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(format.internal), width, height, 0, format.format,
                          format.type, data);
        _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        _sizes[texture][0] = width;
        _sizes[texture][1] = height;
        _holdsBuffers = _holdsBuffers || width > 1 || height > 1;
    }
    else if (data)
    {
        _gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, format.format, format.type, data);
    }
}

void SmoothMotionGPU::Grow(Texture texture, int width, int height, const void* data)
{
    const int w = std::max(width, _sizes[texture][0]), h = std::max(height, _sizes[texture][1]);
    if (w != _sizes[texture][0] || h != _sizes[texture][1])
        Allocate(texture, w, h, nullptr);
    if (!data)
        return;
    const Format format = FormatOf(texture);
    _gl->glActiveTexture(GL_TEXTURE0);
    _gl->glBindTexture(GL_TEXTURE_2D, _textures[texture]);
    _gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    _gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    _gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, format.format, format.type, data);
}

std::vector<uint32_t>* SmoothMotionGPU::FrameCopy(Texture texture)
{
    switch (texture)
    {
        case FrameA:
            return &_frameCopies[0];
        case FrameB:
            return &_frameCopies[1];
        case FrameP:
            return &_frameCopies[2];
        default:
            return nullptr;
    }
}

uint64_t* SmoothMotionGPU::FrameId(Texture texture)
{
    switch (texture)
    {
        case FrameA:
            return &_frameIds[0];
        case FrameB:
            return &_frameIds[1];
        case FrameP:
            return &_frameIds[2];
        default:
            return nullptr;
    }
}

bool SmoothMotionGPU::Holds(Texture texture, const Picture& picture)
{
    const std::vector<uint32_t>* copy = FrameCopy(texture);
    if (!copy || _sizes[texture][0] != picture.width || _sizes[texture][1] != picture.height)
        return false;
    // A picture with an id is known by it; one without by its pixels
    if (picture.id != 0)
        return *FrameId(texture) == picture.id;
    const size_t size = static_cast<size_t>(picture.width) * picture.height;
    return *FrameId(texture) == 0 && copy->size() == size && std::equal(copy->begin(), copy->end(), picture.pixels);
}

void SmoothMotionGPU::UploadPicture(Texture texture, const Picture& picture)
{
    // A frame texture remembers what it holds: one already there is not uploaded again
    if (Holds(texture, picture))
        return;
    Allocate(texture, picture.width, picture.height, picture.pixels);
    if (std::vector<uint32_t>* copy = FrameCopy(texture))
    {
        *FrameId(texture) = picture.id;
        if (picture.id != 0)
            copy->clear();
        else
            copy->assign(picture.pixels, picture.pixels + static_cast<size_t>(picture.width) * picture.height);
    }
}

void SmoothMotionGPU::UploadFrames(const Picture& p, const Picture& a, const Picture& b)
{
    // The frames move on by one from refresh to refresh: a frame another texture holds moves over with its texture
    const Texture roles[3] = {FrameP, FrameA, FrameB};
    const Picture* pictures[3] = {&p, &a, &b};
    for (int r = 0; r < 3; r++)
    {
        if (!Holds(roles[r], *pictures[r]))
            for (int other = r + 1; other < 3; other++)
                if (Holds(roles[other], *pictures[r]))
                {
                    std::swap(_textures[roles[r]], _textures[roles[other]]);
                    std::swap(_sizes[roles[r]], _sizes[roles[other]]);
                    std::swap(*FrameCopy(roles[r]), *FrameCopy(roles[other]));
                    std::swap(*FrameId(roles[r]), *FrameId(roles[other]));
                    break;
                }
        UploadPicture(roles[r], *pictures[r]);
    }
}

void SmoothMotionGPU::UploadMask(const uint8_t* mask, int width, int height)
{
    Allocate(Mask, width, height, mask);
}

void SmoothMotionGPU::BindTexture(int unit, Texture texture)
{
    _gl->glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
    _gl->glBindTexture(GL_TEXTURE_2D, _textures[texture]);
}

void SmoothMotionGPU::ClearErrors()
{
    for (int i = 0; i < kMaxErrors && _gl->glGetError() != GL_NO_ERROR; i++)
    {
    }
}

void SmoothMotionGPU::GpuBegin()
{
    if (_times)
        _times->Begin(GpuPassTimes::Smooth);
}

void SmoothMotionGPU::GpuEnd()
{
    if (_times)
        _times->End(GpuPassTimes::Smooth);
}

bool SmoothMotionGPU::DrawInto(QOpenGLShaderProgram* program, Texture target, int width, int height,
                               int readsPerFragment)
{
    _gl->glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    _gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _textures[target], 0);
    if (_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        _log = "incomplete framebuffer";
        return false;
    }
    GpuBegin();
    _gl->glViewport(0, 0, width, height);
    _gl->glDisable(GL_BLEND);
    _gl->glDisable(GL_DITHER);
    _gl->glUniform2i(program->uniformLocation("size"), _sizes[FrameA][0], _sizes[FrameA][1]);
    _vao.bind();
    // A stripe of rows reads _maxReadsPerDraw texels at most, each submitted on its own (fragments read only their
    // inputs, so the stripes draw the same pixels as one draw)
    const int64_t rowReads = static_cast<int64_t>(width) * std::max(readsPerFragment, 1);
    const int rows = static_cast<int>(std::clamp<int64_t>(_maxReadsPerDraw / rowReads, 1, height));
    if (rows >= height)
    {
        _gl->glDisable(GL_SCISSOR_TEST);
        _gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    else
    {
        _gl->glEnable(GL_SCISSOR_TEST);
        for (int y = 0; y < height; y += rows)
        {
            _gl->glScissor(0, y, width, std::min(rows, height - y));
            _gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            _gl->glFlush();
        }
        _gl->glDisable(GL_SCISSOR_TEST);
    }
    _vao.release();
    return true;
}

bool SmoothMotionGPU::ReadField(int blocksWide, int blocksHigh, std::vector<MotionField::Block>& out)
{
    // From the target DrawInto left bound
    GpuEnd();
    _fieldValues.resize(static_cast<size_t>(blocksWide) * blocksHigh * 4);
    _gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    _gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    _gl->glReadPixels(0, 0, blocksWide, blocksHigh, GL_RGBA_INTEGER, GL_INT, _fieldValues.data());
    out.resize(static_cast<size_t>(blocksWide) * blocksHigh);
    for (size_t i = 0; i < out.size(); i++)
    {
        out[i].v = {_fieldValues[i * 4], _fieldValues[i * 4 + 1]};
        out[i].matched = _fieldValues[i * 4 + 2] != 0;
    }
    return _gl->glGetError() == GL_NO_ERROR;
}

bool SmoothMotionGPU::ReadPicture(int width, int height, uint32_t* out)
{
    // From the target DrawInto left bound. Desktop GL converts an unsigned integer buffer to any unsigned integer
    // type: bytes straight into `out`; a GL that refuses reads RGBA_INTEGER / UNSIGNED_INT (always allowed)
    GpuEnd();
    _gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    _gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    if (_readBytes)
    {
        _gl->glReadPixels(0, 0, width, height, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, out);
        if (_gl->glGetError() == GL_NO_ERROR)
            return true;
        _readBytes = false;
        ClearErrors();
    }
    _readBack.resize(static_cast<size_t>(width) * height * 4);
    _gl->glReadPixels(0, 0, width, height, GL_RGBA_INTEGER, GL_UNSIGNED_INT, _readBack.data());
    auto* bytes = reinterpret_cast<uint8_t*>(out);
    for (size_t i = 0; i < _readBack.size(); i++)
        bytes[i] = static_cast<uint8_t>(_readBack[i]);
    return _gl->glGetError() == GL_NO_ERROR;
}

bool SmoothMotionGPU::ReadInts(int width, int height, std::vector<int32_t>& out)
{
    // From the target DrawInto left bound
    GpuEnd();
    out.resize(static_cast<size_t>(width) * height * 4);
    _gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    _gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    _gl->glReadPixels(0, 0, width, height, GL_RGBA_INTEGER, GL_INT, out.data());
    return _gl->glGetError() == GL_NO_ERROR;
}

bool SmoothMotionGPU::Estimate(const Picture& a, const Picture& b, int range, MotionField& field)
{
    if (!_fieldsOnGpu || !_gl || a.width <= 0 || a.height <= 0)
        return false;
    const int blocksWide = (a.width + MotionField::kBlock - 1) / MotionField::kBlock;
    const int blocksHigh = (a.height + MotionField::kBlock - 1) / MotionField::kBlock;
    const std::vector<MotionField::Vector>& order = MotionField::SearchOrder(range);
    const int orderCount = static_cast<int>(order.size());
    const int chunks = (orderCount + kSearchChunk - 1) / kSearchChunk;
    // Past the GL's limits: the CPU
    if (orderCount > _maxSize || static_cast<int64_t>(blocksWide) * chunks > _maxSize || blocksHigh > _maxSize)
        return false;
    GLint previousFramebuffer = 0;
    GLint viewport[4] = {};
    _gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    _gl->glGetIntegerv(GL_VIEWPORT, viewport);
    ClearErrors();

    UploadPicture(FrameA, a);
    UploadPicture(FrameB, b);
    if (_orderRange != range)
    {
        _ints.clear();
        for (const MotionField::Vector& v : order)
            _ints.insert(_ints.end(), {v.x, v.y});
        Allocate(Order, orderCount, 1, _ints.data());
        _orderRange = range;
        _orderCount = orderCount;
    }
    for (Texture t : {FieldOne, FieldPing, FieldPong})
        Allocate(t, blocksWide, blocksHigh, nullptr);

    // Pass 1: the chunks, then the blocks
    Allocate(FieldChunks, blocksWide * chunks, blocksHigh, nullptr);
    _searchChunks->bind();
    BindTexture(0, FrameA);
    BindTexture(1, FrameB);
    BindTexture(2, Order);
    _searchChunks->setUniformValue("frameA", 0);
    _searchChunks->setUniformValue("frameB", 1);
    _searchChunks->setUniformValue("order", 2);
    _searchChunks->setUniformValue("orderCount", _orderCount);
    _searchChunks->setUniformValue("blocksWide", blocksWide);
    _searchChunks->setUniformValue("chunk", kSearchChunk);
    bool ok = DrawInto(_searchChunks, FieldChunks, blocksWide * chunks, blocksHigh, kSearchChunkReads);
    _searchChunks->release();
    _search->bind();
    BindTexture(2, FieldChunks);
    _search->setUniformValue("chunks", 2);
    _search->setUniformValue("chunkCount", chunks);
    _search->setUniformValue("blocksWide", blocksWide);
    ok = ok && DrawInto(_search, FieldOne, blocksWide, blocksHigh, chunks);
    std::vector<MotionField::Block> prev;
    ok = ok && ReadField(blocksWide, blocksHigh, prev);
    _search->release();

    // The refinement passes, the votes counted on the CPU between them
    Texture source = FieldOne;
    Texture target = FieldPing;
    for (int pass = 0; ok && pass < MotionField::kRefinePasses; pass++)
    {
        const std::vector<MotionField::Vector> votes =
            MotionField::CommonVectors(prev, MotionField::kGlobalVectors, range);
        GLint packed[2 * MotionField::kGlobalVectors] = {};
        for (size_t k = 0; k < votes.size(); k++)
        {
            packed[k * 2] = votes[k].x;
            packed[k * 2 + 1] = votes[k].y;
        }
        _refine->bind();
        BindTexture(0, FrameA);
        BindTexture(1, FrameB);
        BindTexture(2, source);
        BindTexture(3, FieldOne);
        _refine->setUniformValue("frameA", 0);
        _refine->setUniformValue("frameB", 1);
        _refine->setUniformValue("prevField", 2);
        _refine->setUniformValue("firstField", 3);
        _gl->glUniform2i(_refine->uniformLocation("blocks"), blocksWide, blocksHigh);
        _refine->setUniformValue("voteCount", static_cast<int>(votes.size()));
        _gl->glUniform2iv(_refine->uniformLocation("votes"), static_cast<GLsizei>(MotionField::kGlobalVectors), packed);
        ok = DrawInto(_refine, target, blocksWide, blocksHigh, kRefineReads) && ReadField(blocksWide, blocksHigh, prev);
        _refine->release();
        source = target;
        target = target == FieldPing ? FieldPong : FieldPing;
    }

    GpuEnd();
    _gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    _gl->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    _gl->glActiveTexture(GL_TEXTURE0);
    if (!ok)
        return false;
    field.Assign(blocksWide, blocksHigh, std::move(prev), range);
    return true;
}

bool SmoothMotionGPU::Blend(const Picture& a, const Picture& b, int w, uint32_t* out)
{
    if (!_gl || a.width <= 0 || a.height <= 0 || !out)
        return false;
    GLint previousFramebuffer = 0;
    GLint viewport[4] = {};
    _gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    _gl->glGetIntegerv(GL_VIEWPORT, viewport);
    ClearErrors();
    UploadPicture(FrameA, a);
    UploadPicture(FrameB, b);
    Allocate(Output, a.width, a.height, nullptr);
    _blend->bind();
    BindTexture(0, FrameA);
    BindTexture(1, FrameB);
    _blend->setUniformValue("frameA", 0);
    _blend->setUniformValue("frameB", 1);
    _blend->setUniformValue("weight", w);
    const bool ok = DrawInto(_blend, Output, a.width, a.height, kPixelReads) && ReadPicture(a.width, a.height, out);
    _blend->release();
    GpuEnd();
    _gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    _gl->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    _gl->glActiveTexture(GL_TEXTURE0);
    return ok;
}

bool SmoothMotionGPU::Compose(const MotionField& field, const Picture& a, const Picture& b, int w, uint32_t* out,
                              const uint8_t* mask)
{
    if (!_gl || a.width <= 0 || a.height <= 0 || !out || field.BlocksWide() <= 0)
        return false;
    GLint previousFramebuffer = 0;
    GLint viewport[4] = {};
    _gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    _gl->glGetIntegerv(GL_VIEWPORT, viewport);
    ClearErrors();
    UploadPicture(FrameA, a);
    UploadPicture(FrameB, b);
    // The field as the CPU holds it (a field estimated on the CPU, or this one's read back)
    _ints.clear();
    for (int j = 0; j < field.BlocksHigh(); j++)
        for (int i = 0; i < field.BlocksWide(); i++)
        {
            const MotionField::Block& block = field.At(i, j);
            _ints.insert(_ints.end(), {block.v.x, block.v.y, block.matched ? 1 : 0, 0});
        }
    Allocate(FieldOne, field.BlocksWide(), field.BlocksHigh(), _ints.data());
    if (mask)
    {
        UploadMask(mask, a.width, a.height);
        UploadPicture(Base, Picture{a.width, a.height, out});
    }
    Allocate(Output, a.width, a.height, nullptr);

    GLint common[2 * MotionField::kComposeCommon] = {};
    const std::vector<MotionField::Vector>& vectors = field.Common();
    const size_t commonCount = std::min(vectors.size(), MotionField::kComposeCommon);
    for (size_t k = 0; k < commonCount; k++)
    {
        common[k * 2] = vectors[k].x;
        common[k * 2 + 1] = vectors[k].y;
    }
    _compose->bind();
    BindTexture(0, FrameA);
    BindTexture(1, FrameB);
    BindTexture(2, FieldOne);
    BindTexture(3, Mask);
    BindTexture(4, Base);
    _compose->setUniformValue("frameA", 0);
    _compose->setUniformValue("frameB", 1);
    _compose->setUniformValue("field", 2);
    _compose->setUniformValue("mask", 3);
    _compose->setUniformValue("base", 4);
    _compose->setUniformValue("useMask", static_cast<GLint>(mask != nullptr));
    _gl->glUniform2i(_compose->uniformLocation("blocks"), field.BlocksWide(), field.BlocksHigh());
    _compose->setUniformValue("weight", w);
    _compose->setUniformValue("commonCount", static_cast<int>(commonCount));
    _gl->glUniform2iv(_compose->uniformLocation("commonVectors"), static_cast<GLsizei>(MotionField::kComposeCommon),
                      common);
    const bool ok =
        DrawInto(_compose, Output, a.width, a.height, kComposeReads) && ReadPicture(a.width, a.height, out);
    _compose->release();
    GpuEnd();
    _gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    _gl->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    _gl->glActiveTexture(GL_TEXTURE0);
    return ok;
}

bool SmoothMotionGPU::WindowSums(Texture source, unsigned mask0, int radius0, unsigned mask1, int radius1, bool flags,
                                 Texture target, int width, int height)
{
    _rowSums->bind();
    BindTexture(0, source);
    _rowSums->setUniformValue("source", 0);
    _gl->glUniform2i(_rowSums->uniformLocation("masks"), static_cast<GLint>(mask0), static_cast<GLint>(mask1));
    _gl->glUniform2i(_rowSums->uniformLocation("radii"), radius0, radius1);
    bool ok = DrawInto(_rowSums, RowSums, width, height, kWindowReads);
    _rowSums->release();
    _columnSums->bind();
    BindTexture(0, RowSums);
    BindTexture(1, FitBits);
    BindTexture(2, StillSums);
    BindTexture(3, ExplainedSums);
    _columnSums->setUniformValue("source", 0);
    _columnSums->setUniformValue("fitBits", 1);
    _columnSums->setUniformValue("stillSums", 2);
    _columnSums->setUniformValue("explainedSums", 3);
    _columnSums->setUniformValue("flags", static_cast<GLint>(flags));
    _columnSums->setUniformValue("tileCols", _tileCols);
    _gl->glUniform2i(_columnSums->uniformLocation("radii"), radius0, radius1);
    ok = ok && DrawInto(_columnSums, target, width, height, kWindowReads);
    _columnSums->release();
    return ok;
}

bool SmoothMotionGPU::AllocateLayers(int width, int height)
{
    // Every per-vector image a tile, room for kMaxVectors: allocated once per picture size, whatever a refresh's count
    _tileCols = std::max(1, std::min(static_cast<int>(LayerFlow::kMaxVectors), _maxSize / width));
    const int tileRows = (static_cast<int>(LayerFlow::kMaxVectors) + _tileCols - 1) / _tileCols;
    const int bw = (width + MotionField::kBlock - 1) / MotionField::kBlock;
    const int bh = (height + MotionField::kBlock - 1) / MotionField::kBlock;
    const int groups = (static_cast<int>(LayerFlow::kMaxVectors) + 3) / 4;
    if (static_cast<int64_t>(tileRows) * height > _maxSize || static_cast<int64_t>(bh) * groups > _maxSize ||
        static_cast<int64_t>(bw) * static_cast<int>(LayerFlow::kMaxAllowed) > _maxSize)
        return false;
    if (_layersWidth == width && _layersHeight == height)
        return true;
    const int tiledWidth = _tileCols * width, tiledHeight = tileRows * height;
    for (Texture t : {Changed, Explained, StillSums, ExplainedSums, Colour, Top, BlockColours, Output})
        Allocate(t, width, height, nullptr);
    for (Texture t : {RowSums, FitBits, Sums, CoverMasks})
        Allocate(t, tiledWidth, tiledHeight, nullptr);
    Allocate(RowCounts, _tileCols * bw, tiledHeight, nullptr);
    Allocate(Counts, bw, bh * groups, nullptr);
    Allocate(BlockInfo, bw, bh, nullptr);
    Allocate(AskAt, bw, bh, nullptr);
    Allocate(Allowed, bw * static_cast<int>(LayerFlow::kMaxAllowed), bh, nullptr);
    Grow(Vectors, static_cast<int>(LayerFlow::kMaxVectors), 1, nullptr);
    _layersWidth = width;
    _layersHeight = height;
    return true;
}

bool SmoothMotionGPU::ComposeLayers(const Picture& p, const Picture& a, const Picture& b, const MotionField& prev,
                                    const MotionField& next, int w, uint32_t* out)
{
    if (!_gl || a.width <= 0 || a.height <= 0 || !out || next.BlocksWide() <= 0)
        return false;
    GLint previousFramebuffer = 0;
    GLint viewport[4] = {};
    _gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    _gl->glGetIntegerv(GL_VIEWPORT, viewport);
    ClearErrors();
    const bool ok = ComposeLayersHere(p, a, b, prev, next, w, out);
    GpuEnd();
    _gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    _gl->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    _gl->glActiveTexture(GL_TEXTURE0);
    return ok;
}

bool SmoothMotionGPU::ComposeLayersHere(const Picture& p, const Picture& a, const Picture& b, const MotionField& prev,
                                        const MotionField& next, int w, uint32_t* out)
{
    LayerFlow::Prepare(prev, next, a.width, a.height, _layerSet);
    const int width = a.width, height = a.height;
    const int n = static_cast<int>(_layerSet.all.size());
    const int bw = _layerSet.blocksWide, bh = _layerSet.blocksHigh;
    const size_t blocks = static_cast<size_t>(bw) * bh;
    bool fitsAllowed = n <= static_cast<int>(LayerFlow::kMaxVectors);
    for (const auto& allowed : _layerSet.allowed)
        fitsAllowed = fitsAllowed && allowed.size() <= LayerFlow::kMaxAllowed;
    if (!fitsAllowed || !AllocateLayers(width, height))
        return false;
    // Only the tile rows the refresh's vectors take are drawn
    const int groups = (n + 3) / 4;
    const int tiledWidth = _tileCols * width;
    const int tiledHeight = (n + _tileCols - 1) / _tileCols * height;
    _n = n;
    _weight = w;
    _answered = true;
    _layerCounts = {n, 0, 0};

    UploadFrames(p, a, b);
    _ints.clear();
    for (const auto& v : _layerSet.all)
        _ints.insert(_ints.end(), {v.x, v.y});
    Grow(Vectors, n, 1, _ints.data());

    // The changes and the still windows round them
    _changed->bind();
    BindTexture(0, FrameA);
    BindTexture(1, FrameB);
    _changed->setUniformValue("frameA", 0);
    _changed->setUniformValue("frameB", 1);
    bool ok = DrawInto(_changed, Changed, width, height, kPixelReads);
    _changed->release();
    ok = ok && WindowSums(Changed, 1, LayerFlow::kRadius, 1, LayerFlow::kSupportRadius, false, StillSums, width, height);

    // Per vector what it fits; the changes a moving vector explains and the windows round them; per vector its
    // score, support and what it may take; the cover rule's masks
    if (ok)
    {
        _fit->bind();
        BindLayerFields(_fit);
        ok = DrawInto(_fit, FitBits, tiledWidth, tiledHeight, kFitReads);
        _fit->release();
    }
    if (ok)
    {
        _explained->bind();
        BindLayerFields(_explained);
        ok = DrawInto(_explained, Explained, width, height, kVectorReads);
        _explained->release();
    }
    ok = ok && WindowSums(Explained, 0, 0, 1, LayerFlow::kSupportRadius, false, ExplainedSums, width, height);
    ok = ok && WindowSums(FitBits, 2, LayerFlow::kRadius, 4, LayerFlow::kSupportRadius, true, Sums, tiledWidth,
                          tiledHeight);
    if (ok)
    {
        _coverMasks->bind();
        BindLayerFields(_coverMasks);
        ok = DrawInto(_coverMasks, CoverMasks, tiledWidth, tiledHeight, kCoverMasksReads);
        _coverMasks->release();
    }

    // The counts per block, read back for the block decisions
    if (ok)
    {
        _rowCounts->bind();
        BindLayerFields(_rowCounts);
        ok = DrawInto(_rowCounts, RowCounts, _tileCols * bw, tiledHeight, kRowCountsReads);
        _rowCounts->release();
    }
    if (ok)
    {
        _counts->bind();
        BindLayerFields(_counts);
        BindTexture(8, RowCounts);
        _counts->setUniformValue("rowCounts", 8);
        ok = DrawInto(_counts, Counts, bw, bh * groups, kCountsReads) && ReadInts(bw, bh * groups, _ints);
        _counts->release();
    }
    if (!ok)
        return false;
    _fitCounts.resize(blocks * n);
    _usableCounts.resize(blocks * n);
    for (int c = 0; c < n; c++)
        for (size_t block = 0; block < blocks; block++)
        {
            const size_t texel = (static_cast<size_t>(c / 4) * bh + block / bw) * bw + block % bw;
            const int32_t value = _ints[texel * 4 + c % 4];
            _fitCounts[c * blocks + block] = static_cast<uint8_t>(value & 255);
            _usableCounts[c * blocks + block] = static_cast<uint8_t>(value >> 8);
        }

    // The decisions, the reference's own code; the questions go to the passes below
    LayerFlow::Decide(_layerSet, a, _fitCounts.data(), _usableCounts.data(), *this, _blockVector, _tied, _decide);
    if (!_answered)
        return false;

    // The picture
    _ints.assign(blocks * 4, 0);
    _allowedOf.assign(blocks * LayerFlow::kMaxAllowed, 0);
    for (size_t block = 0; block < blocks; block++)
    {
        const int bv = _blockVector[block];
        const auto& allowed = _layerSet.allowed[block];
        const bool moving = bv >= 0 && !(_layerSet.all[bv] == MotionField::Vector{});
        const bool explains = moving && !_tied[block] &&
                              LayerFlow::Explains(a, b, static_cast<int>(block % bw), static_cast<int>(block / bw),
                                                  _layerSet.all[bv]);
        _ints[block * 4] = bv;
        _ints[block * 4 + 1] = _tied[block];
        _ints[block * 4 + 2] = explains;
        _ints[block * 4 + 3] = static_cast<int32_t>(allowed.size());
        std::copy(allowed.begin(), allowed.end(),
                  _allowedOf.begin() + static_cast<ptrdiff_t>(block * LayerFlow::kMaxAllowed));
    }
    Allocate(BlockInfo, bw, bh, _ints.data());
    // A block's allowed vectors in a row of kMaxAllowed, the blocks row by row
    Allocate(Allowed, bw * static_cast<int>(LayerFlow::kMaxAllowed), bh, _allowedOf.data());
    _colourAt.resize(static_cast<size_t>(width) * height);
    LayerFlow::Palette(a, _palette, _colourAt.data());
    Allocate(Colour, width, height, _colourAt.data());
    _layers->bind();
    BindLayerFields(_layers);
    BindTexture(8, BlockInfo);
    BindTexture(9, Allowed);
    BindTexture(10, Colour);
    BindTexture(11, CoverMasks);
    _layers->setUniformValue("blockInfo", 8);
    _layers->setUniformValue("allowed", 9);
    _layers->setUniformValue("colour", 10);
    _layers->setUniformValue("coverMasks", 11);
    ok = DrawInto(_layers, Output, width, height, kLayersReads) && ReadPicture(width, height, out);
    _layers->release();
    if (ok)
        LayerFlow::KeepInk(a, b, w < smoothmotion::kOne / 2 ? a : b, out);
    return ok;
}

void SmoothMotionGPU::BindLayerFields(QOpenGLShaderProgram* program)
{
    // Units 0..7: the frames, the vectors and the per-pixel evidence
    BindTexture(0, FrameA);
    BindTexture(1, FrameB);
    BindTexture(2, FrameP);
    BindTexture(3, Vectors);
    BindTexture(4, FitBits);
    BindTexture(5, Sums);
    BindTexture(6, StillSums);
    program->setUniformValue("frameA", 0);
    program->setUniformValue("frameB", 1);
    program->setUniformValue("frameP", 2);
    program->setUniformValue("vectors", 3);
    program->setUniformValue("fitBits", 4);
    program->setUniformValue("sums", 5);
    program->setUniformValue("stillSums", 6);
    program->setUniformValue("vectorCount", _n);
    program->setUniformValue("weight", _weight);
    program->setUniformValue("tileCols", _tileCols);
    _gl->glUniform2i(program->uniformLocation("blocks"), _layerSet.blocksWide, _layerSet.blocksHigh);
}

void SmoothMotionGPU::Ask(const std::vector<std::pair<size_t, int>>& blocks,
                          const std::vector<smoothmotion::LayerFlow::Edge>& edges)
{
    _layerCounts.ownAsks = static_cast<int>(blocks.size());
    _layerCounts.edges = static_cast<int>(edges.size());
    const int asks = static_cast<int>(blocks.size()), count = static_cast<int>(edges.size());
    if (!_answered || (asks == 0 && count == 0))
        return;
    if (asks > _maxSize || count > _maxSize)
    {
        _answered = false;
        return;
    }
    const int bw = _layerSet.blocksWide, bh = _layerSet.blocksHigh;
    const int width = _sizes[FrameA][0], height = _sizes[FrameA][1];
    bool ok = true;
    // The aliases rule: the top three per pixel of the asked blocks, then own(...) per ask and vector
    if (asks > 0)
    {
        _askOf.assign(static_cast<size_t>(bw) * bh, -1);
        _ints.clear();
        for (int i = 0; i < asks; i++)
        {
            _askOf[blocks[i].first] = i;
            _ints.insert(_ints.end(), {static_cast<int32_t>(blocks[i].first), blocks[i].second});
        }
        Grow(Asks, asks, 1, _ints.data());
        Allocate(AskAt, bw, bh, _askOf.data());
        Grow(OwnAnswers, _n, asks, nullptr);
        _top->bind();
        BindLayerFields(_top);
        BindTexture(8, AskAt);
        BindTexture(9, Counts);
        _top->setUniformValue("askAt", 8);
        _top->setUniformValue("counts", 9);
        ok = DrawInto(_top, Top, width, height, kVectorReads);
        _top->release();
        _own->bind();
        BindLayerFields(_own);
        BindTexture(8, Asks);
        BindTexture(9, Top);
        _own->setUniformValue("asks", 8);
        _own->setUniformValue("top", 9);
        ok = ok && DrawInto(_own, OwnAnswers, _n, asks, kOwnReads);
        _own->release();
    }
    // The regions rule: per edge and vector
    if (ok && count > 0)
    {
        _ints.clear();
        for (const auto& e : edges)
            _ints.insert(_ints.end(), {e.bi, e.bj, (e.di + 1) + 3 * (e.dj + 1), 0});
        Grow(Edges, count, 1, _ints.data());
        Grow(EdgeAnswers, count, _n, nullptr);
        _blockColours->bind();
        BindLayerFields(_blockColours);
        ok = DrawInto(_blockColours, BlockColours, width, height, kBlockColoursReads);
        _blockColours->release();
        _continues->bind();
        BindLayerFields(_continues);
        BindTexture(8, Edges);
        BindTexture(9, BlockColours);
        _continues->setUniformValue("edges", 8);
        _continues->setUniformValue("blockColours", 9);
        ok = ok && DrawInto(_continues, EdgeAnswers, count, _n, kContinuesReads) && ReadInts(count, _n, _edgeAnswers);
        _continues->release();
    }
    // Read back once the GPU did all
    if (ok && asks > 0)
    {
        _gl->glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
        _gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _textures[OwnAnswers], 0);
        ok = ReadInts(_n, asks, _ownAnswers);
    }
    _answered = ok;
}

int SmoothMotionGPU::Own(size_t block, int c, int s)
{
    // Decide asks own(s, none) and own(the ask's vector, s)
    const int ask = _answered ? _askOf[block] : -1;
    if (ask < 0)
        return 0;
    return s < 0 ? _ownAnswers[(static_cast<size_t>(ask) * _n + c) * 4]
                 : _ownAnswers[(static_cast<size_t>(ask) * _n + s) * 4 + 1];
}

bool SmoothMotionGPU::Continues(size_t index, int vector)
{
    const size_t edges = static_cast<size_t>(_layerCounts.edges);
    return _answered && _edgeAnswers[(static_cast<size_t>(vector) * edges + index) * 4] != 0;
}
