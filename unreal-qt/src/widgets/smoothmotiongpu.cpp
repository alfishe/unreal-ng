#include "smoothmotiongpu.h"

#include <QOpenGLContext>

#include <algorithm>

using smoothmotion::MotionField;
using smoothmotion::Picture;

namespace
{
constexpr int kSearchChunk = 9;  // the search order's vectors per fragment of pass 1 (range 8: 289 in 17 chunks)

// The quad over the viewport from the vertex index (a strip). Fragment (x, y) is pixel (x, y), rows top first as the
// pictures are uploaded and read back: nothing is turned over
const char* const kVertexShader = R"(
#version 330
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

// The reference's helpers (smoothmotion.cpp): Picture::At clamped to the edges, Shift = (w c + 128) >> 8 (GLSL shifts
// a signed int arithmetically, as C++20 does)
const char* const kCommon = R"(
#version 330
uniform ivec2 size;
uniform usampler2D frameA;
uniform usampler2D frameB;
uvec4 At(usampler2D t, ivec2 p) { return texelFetch(t, clamp(p, ivec2(0), size - 1), 0); }
int Shift(int w, int c) { return (w * c + 128) >> 8; }
bool Same(uvec4 a, uvec4 b) { return all(equal(a, b)); }
uvec4 Mix(uvec4 a, uvec4 b, int w) { return (a * uint(256 - w) + b * uint(w) + 128u) >> 8u; }
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
const int kBlock = 8;
const int kAccept = 4;
int Cost(ivec2 blk, ivec2 v, int limit) {
    ivec2 s = ivec2(Shift(128, v.x), Shift(128, v.y));
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
uniform ivec2 votes[4];
uniform int voteCount;
out ivec4 result;
ivec2 cands[13];
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
uniform ivec2 commonVectors[2];
uniform int commonCount;
uniform int weight;
uniform bool useMask;
uniform usampler2D mask;
uniform usampler2D base;
out uvec4 color;
ivec2 cands[12];
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
    ivec2 blk = p / 8;
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
        if (weight > 256 && v != ivec2(0) && Same(At(frameA, sa), At(frameB, sa)) && Same(At(frameA, sb), At(frameB, sb)))
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
        color = weight > 256 ? texelFetch(frameB, p, 0) : Mix(texelFetch(frameA, p, 0), texelFetch(frameB, p, 0), weight);
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

// Per vector, block column and pixel row (tiles blocks.x x size.y): the pixels it fits | those it may take << 8 |
// any with a cover balance << 16
const char* const kRowCountsShader = R"(
out ivec4 result;
void main() {
    ivec2 f = ivec2(gl_FragCoord.xy);
    ivec2 tileSize = ivec2(blocks.x, size.y);
    int c = TileOf(f, tileSize);
    ivec2 at = f - f / tileSize * tileSize;
    int fit = 0;
    int use = 0;
    bool balance = false;
    if (c < vectorCount)
        for (int x = at.x * 8; x < min(at.x * 8 + 8, size.x); x++) {
            uint bits = SumsOf(c, ivec2(x, at.y)).b;
            fit += int(bits & 1u);
            use += int(bits >> 1);
            balance = balance || ((Bits(c, ivec2(x, at.y)) >> 3) & 3u) != 1u;
        }
    result = ivec4(fit | (use << 8) | (balance ? 1 << 16 : 0), 0, 0, 0);
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
        for (int y = blk.y * 8; y < min(blk.y * 8 + 8, size.y); y++)
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
    ivec2 blk = p / 8;
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
    ivec2 p0 = blk * 8;
    ivec2 p1 = min(p0 + 8, size);
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
    ivec2 blk = p / 8;
    ivec2 b0 = blk * 8;
    ivec2 b1 = min(b0 + 8, size);
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
    ivec2 side = (p - b0) / 4;
    for (int d = 0; d < 4; d++) {
        ivec2 nb = blk + dirs[d];
        if (any(lessThan(nb, ivec2(0))) || any(greaterThanEqual(nb * 8, size)) || (d < 2 ? side.x : side.y) != d % 2)
            continue;
        ivec2 n0 = nb * 8;
        ivec2 n1 = min(n0 + 8, size);
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
    int nx0 = (bi + di) * 8, ny0 = (bj + dj) * 8;
    int nx1 = min(nx0 + 8, size.x), ny1 = min(ny0 + 8, size.y);
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
    int x0 = nx0, x1 = nx0 + 8, y0 = ny0, y1 = ny0 + 8;
    if (di < 0)
        x0 = x1 - 4;
    if (di > 0)
        x1 = x0 + 4;
    if (dj < 0)
        y0 = y1 - 4;
    if (dj > 0)
        y1 = y0 + 4;
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

// The picture: per pixel cover, the block's vector, carryFirst, the best score, the nearer frame
const char* const kLayersShader = R"(
uniform isampler2D blockInfo;
uniform isampler2D allowed;
uniform int maxAllowed;
uniform usampler2D colour;
uniform isampler2D rowCounts;
out uvec4 color;
int Allowed(ivec2 blk, int k) { return texelFetch(allowed, ivec2(blk.x * maxAllowed + k, blk.y), 0).r; }
// the cover's balance of c round q over the pixels of q's colour; the rows of blocks without one skipped
bool Coverable(int c, ivec2 q) {
    uint col = texelFetch(colour, q, 0).r;
    if (col == 255u)
        return false;
    int sum = 0;
    ivec2 origin = Tile(c, ivec2(0));
    ivec2 rows = ivec2(c % tileCols, c / tileCols) * ivec2(blocks.x, size.y);
    int x0 = max(q.x - 16, 0), x1 = min(q.x + 16, size.x - 1);
    for (int y = max(q.y - 16, 0); y <= min(q.y + 16, size.y - 1); y++)
        for (int bx = x0 / 8; bx <= x1 / 8; bx++) {
            if ((texelFetch(rowCounts, rows + ivec2(bx, y), 0).r & (1 << 16)) == 0)
                continue;
            for (int x = max(bx * 8, x0); x <= min(bx * 8 + 7, x1); x++)
                if (texelFetch(colour, ivec2(x, y), 0).r == col)
                    sum += int((texelFetch(fitBits, origin + ivec2(x, y), 0).r >> 3) & 3u) - 1;
        }
    return sum > 0;
}
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 blk = p / 8;
    ivec4 info = texelFetch(blockInfo, blk, 0);
    int bv = info.x;
    bool tied = info.y != 0;
    bool explains = info.z != 0;
    int count = info.w;
    uvec4 here = texelFetch(frameA, p, 0);
    uvec4 later = texelFetch(frameB, p, 0);
    uvec4 before = texelFetch(frameP, p, 0);
    if (Same(here, later) && !(bv >= 0 && Vec(bv) != ivec2(0) && Usable(bv, p)) && Same(before, here)) {
        int over = -1;
        int overSupport = 0;
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
            if (!Same(At(frameP, sa - v), va) || !Coverable(c, p))
                continue;
            if (over < 0 || support > overSupport) {
                over = c;
                overSupport = support;
            }
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
            color = weight < 128 ? At(frameA, sa) : At(frameB, sa + v);
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
        color = weight < 128 ? here : later;
    else
        color = At(frameA, p - Shifted(Vec(best)));
}
)";

QOpenGLShaderProgram* Build(const QString& fragment, QString& log)
{
    auto* program = new QOpenGLShaderProgram();
    if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
        !program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) || !program->link())
    {
        log += program->log();
        delete program;
        return nullptr;
    }
    return program;
}
}  // namespace

SmoothMotionGPU::~SmoothMotionGPU()
{
    // GL objects need their context: destroy() with it current; here only what is left in CPU memory
    for (QOpenGLShaderProgram* program : programs())
        delete program;
}

std::vector<QOpenGLShaderProgram*> SmoothMotionGPU::programs() const
{
    return {_blend, _searchChunks, _search, _refine, _compose, _changed, _rowSums, _columnSums, _fit,
            _explained, _rowCounts, _counts, _blockColours, _top, _own, _continues, _layers};
}

bool SmoothMotionGPU::create()
{
    destroy();
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
    _explained = Build(frames + kExplainedShader, _log);
    _rowCounts = Build(fields + kRowCountsShader, _log);
    _counts = Build(fields + kCountsShader, _log);
    _blockColours = Build(fields + kBlockColoursShader, _log);
    _top = Build(fields + kTopShader, _log);
    _own = Build(fields + kOwnShader, _log);
    _continues = Build(fields + kContinuesShader, _log);
    _layers = Build(fields + kLayersShader, _log);
    const std::vector<QOpenGLShaderProgram*> built = programs();
    if (std::find(built.begin(), built.end(), nullptr) != built.end() || !_vao.create())
    {
        destroy();
        return false;
    }
    _gl = context->extraFunctions();
    _gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &_maxSize);
    _gl->glGenFramebuffers(1, &_framebuffer);
    _gl->glGenTextures(TextureCount, _textures);
    for (auto& size : _sizes)
        size[0] = size[1] = 0;

    // Every texture complete from the start: a sampler the pass does not read (the mask without one) still has one
    const int32_t zero[4] = {};
    for (int t = 0; t < TextureCount; t++)
    {
        const auto texture = static_cast<Texture>(t);
        if (texture == Mask || texture == Colour || texture == Changed || texture == Explained || texture == FitBits)
            allocate(texture, 1, 1, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, zero);
        else if (texture == RowSums || texture == StillSums || texture == ExplainedSums)
            allocate(texture, 1, 1, GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT, zero);
        else if (texture == Sums)
            allocate(texture, 1, 1, GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, zero);
        else if (texture == Order || texture == Asks || texture == Vectors)
            allocate(texture, 1, 1, GL_RG32I, GL_RG_INTEGER, GL_INT, zero);
        else if (texture == AskAt || texture == Allowed || texture == RowCounts || texture == BlockColours)
            allocate(texture, 1, 1, GL_R32I, GL_RED_INTEGER, GL_INT, zero);
        else if (texture == FieldChunks || texture == FieldOne || texture == FieldPing || texture == FieldPong || texture == Counts ||
                 texture == Top || texture == OwnAnswers || texture == Edges || texture == EdgeAnswers ||
                 texture == BlockInfo)
            allocate(texture, 1, 1, GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, zero);
        else
            allocate(texture, 1, 1, GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, zero);
    }
    _orderRange = -1;
    return true;
}

void SmoothMotionGPU::destroy()
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
    _framebuffer = 0;
    for (QOpenGLShaderProgram* program : programs())
        delete program;
    _blend = _searchChunks = _search = _refine = _compose = nullptr;
    _changed = _rowSums = _columnSums = _fit = _explained = nullptr;
    _rowCounts = _counts = _blockColours = _top = _own = _continues = _layers = nullptr;
    if (_vao.isCreated())
        _vao.destroy();
    _gl = nullptr;
}

void SmoothMotionGPU::allocate(Texture texture, int width, int height, unsigned internalFormat, unsigned format,
                               unsigned type, const void* data)
{
    _gl->glActiveTexture(GL_TEXTURE0);
    _gl->glBindTexture(GL_TEXTURE_2D, _textures[texture]);
    _gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    _gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    if (_sizes[texture][0] != width || _sizes[texture][1] != height)
    {
        // Integer textures are complete with nearest sampling and no mipmaps only
        _gl->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), width, height, 0, format, type, data);
        _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        _sizes[texture][0] = width;
        _sizes[texture][1] = height;
    }
    else if (data)
    {
        _gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, format, type, data);
    }
}

std::vector<uint32_t>* SmoothMotionGPU::frameCopy(Texture texture)
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

bool SmoothMotionGPU::holds(Texture texture, const Picture& picture)
{
    const std::vector<uint32_t>* copy = frameCopy(texture);
    const size_t size = static_cast<size_t>(picture.width) * picture.height;
    return copy && _sizes[texture][0] == picture.width && _sizes[texture][1] == picture.height && copy->size() == size &&
           std::equal(copy->begin(), copy->end(), picture.pixels);
}

void SmoothMotionGPU::uploadPicture(Texture texture, const Picture& picture)
{
    // A frame keeps a copy: one already there is not uploaded again
    if (holds(texture, picture))
        return;
    allocate(texture, picture.width, picture.height, GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, picture.pixels);
    if (std::vector<uint32_t>* copy = frameCopy(texture))
        copy->assign(picture.pixels, picture.pixels + static_cast<size_t>(picture.width) * picture.height);
}

void SmoothMotionGPU::uploadFrames(const Picture& p, const Picture& a, const Picture& b)
{
    // The frames move on by one from refresh to refresh: a frame another texture holds moves over with its texture
    const Texture roles[3] = {FrameP, FrameA, FrameB};
    const Picture* pictures[3] = {&p, &a, &b};
    for (int r = 0; r < 3; r++)
    {
        if (!holds(roles[r], *pictures[r]))
            for (int other = r + 1; other < 3; other++)
                if (holds(roles[other], *pictures[r]))
                {
                    std::swap(_textures[roles[r]], _textures[roles[other]]);
                    std::swap(_sizes[roles[r]], _sizes[roles[other]]);
                    std::swap(*frameCopy(roles[r]), *frameCopy(roles[other]));
                    break;
                }
        uploadPicture(roles[r], *pictures[r]);
    }
}

void SmoothMotionGPU::uploadMask(const uint8_t* mask, int width, int height)
{
    allocate(Mask, width, height, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, mask);
}

void SmoothMotionGPU::bindTexture(int unit, Texture texture)
{
    _gl->glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
    _gl->glBindTexture(GL_TEXTURE_2D, _textures[texture]);
}

bool SmoothMotionGPU::drawInto(QOpenGLShaderProgram* program, Texture target, int width, int height)
{
    _gl->glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    _gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _textures[target], 0);
    const bool complete = _gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete)
    {
        _gl->glViewport(0, 0, width, height);
        _gl->glDisable(GL_BLEND);
        _gl->glDisable(GL_SCISSOR_TEST);
        _gl->glDisable(GL_DITHER);
        _gl->glUniform2i(program->uniformLocation("size"), _sizes[FrameA][0], _sizes[FrameA][1]);
        _vao.bind();
        _gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        _vao.release();
    }
    else
    {
        _log = "incomplete framebuffer";
    }
    return complete;
}

bool SmoothMotionGPU::readField(int blocksWide, int blocksHigh, std::vector<MotionField::Block>& out)
{
    // From the target drawInto left bound
    std::vector<int32_t> values(static_cast<size_t>(blocksWide) * blocksHigh * 4);
    _gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    _gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    _gl->glReadPixels(0, 0, blocksWide, blocksHigh, GL_RGBA_INTEGER, GL_INT, values.data());
    out.resize(static_cast<size_t>(blocksWide) * blocksHigh);
    for (size_t i = 0; i < out.size(); i++)
    {
        out[i].v = {values[i * 4], values[i * 4 + 1]};
        out[i].matched = values[i * 4 + 2] != 0;
    }
    return _gl->glGetError() == GL_NO_ERROR;
}

bool SmoothMotionGPU::readPicture(int width, int height, uint32_t* out)
{
    // An unsigned integer buffer reads as RGBA_INTEGER / UNSIGNED_INT everywhere (bytes are not guaranteed)
    _readBack.resize(static_cast<size_t>(width) * height * 4);
    _gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    _gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    _gl->glReadPixels(0, 0, width, height, GL_RGBA_INTEGER, GL_UNSIGNED_INT, _readBack.data());
    auto* bytes = reinterpret_cast<uint8_t*>(out);
    for (size_t i = 0; i < _readBack.size(); i++)
        bytes[i] = static_cast<uint8_t>(_readBack[i]);
    return _gl->glGetError() == GL_NO_ERROR;
}

bool SmoothMotionGPU::Estimate(const Picture& a, const Picture& b, int range, MotionField& field)
{
    if (!_fieldsOnGpu || !_gl || a.width <= 0 || a.height <= 0)
        return false;
    GLint previousFramebuffer = 0;
    GLint viewport[4] = {};
    _gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    _gl->glGetIntegerv(GL_VIEWPORT, viewport);
    while (_gl->glGetError() != GL_NO_ERROR)
    {
    }

    const int blocksWide = (a.width + MotionField::kBlock - 1) / MotionField::kBlock;
    const int blocksHigh = (a.height + MotionField::kBlock - 1) / MotionField::kBlock;
    uploadPicture(FrameA, a);
    uploadPicture(FrameB, b);
    if (_orderRange != range)
    {
        const std::vector<MotionField::Vector>& order = MotionField::SearchOrder(range);
        std::vector<int32_t> values;
        for (const MotionField::Vector& v : order)
        {
            values.push_back(v.x);
            values.push_back(v.y);
        }
        _sizes[Order][0] = 0;  // a new length: allocated again
        allocate(Order, static_cast<int>(order.size()), 1, GL_RG32I, GL_RG_INTEGER, GL_INT, values.data());
        _orderRange = range;
        _orderCount = static_cast<int>(order.size());
    }
    for (Texture t : {FieldOne, FieldPing, FieldPong})
        allocate(t, blocksWide, blocksHigh, GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, nullptr);

    // Pass 1: the chunks, then the blocks
    const int chunks = (_orderCount + kSearchChunk - 1) / kSearchChunk;
    allocate(FieldChunks, blocksWide * chunks, blocksHigh, GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, nullptr);
    _searchChunks->bind();
    bindTexture(0, FrameA);
    bindTexture(1, FrameB);
    bindTexture(2, Order);
    _searchChunks->setUniformValue("frameA", 0);
    _searchChunks->setUniformValue("frameB", 1);
    _searchChunks->setUniformValue("order", 2);
    _searchChunks->setUniformValue("orderCount", _orderCount);
    _searchChunks->setUniformValue("blocksWide", blocksWide);
    _searchChunks->setUniformValue("chunk", kSearchChunk);
    bool ok = drawInto(_searchChunks, FieldChunks, blocksWide * chunks, blocksHigh);
    _searchChunks->release();
    _search->bind();
    bindTexture(2, FieldChunks);
    _search->setUniformValue("chunks", 2);
    _search->setUniformValue("chunkCount", chunks);
    _search->setUniformValue("blocksWide", blocksWide);
    ok = ok && drawInto(_search, FieldOne, blocksWide, blocksHigh);
    std::vector<MotionField::Block> prev;
    ok = ok && readField(blocksWide, blocksHigh, prev);
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
        bindTexture(0, FrameA);
        bindTexture(1, FrameB);
        bindTexture(2, source);
        bindTexture(3, FieldOne);
        _refine->setUniformValue("frameA", 0);
        _refine->setUniformValue("frameB", 1);
        _refine->setUniformValue("prevField", 2);
        _refine->setUniformValue("firstField", 3);
        _gl->glUniform2i(_refine->uniformLocation("blocks"), blocksWide, blocksHigh);
        _refine->setUniformValue("voteCount", static_cast<int>(votes.size()));
        _gl->glUniform2iv(_refine->uniformLocation("votes"), static_cast<GLsizei>(MotionField::kGlobalVectors), packed);
        ok = drawInto(_refine, target, blocksWide, blocksHigh) && readField(blocksWide, blocksHigh, prev);
        _refine->release();
        source = target;
        target = target == FieldPing ? FieldPong : FieldPing;
    }

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
    while (_gl->glGetError() != GL_NO_ERROR)
    {
    }
    uploadPicture(FrameA, a);
    uploadPicture(FrameB, b);
    allocate(Output, a.width, a.height, GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, nullptr);
    _blend->bind();
    bindTexture(0, FrameA);
    bindTexture(1, FrameB);
    _blend->setUniformValue("frameA", 0);
    _blend->setUniformValue("frameB", 1);
    _blend->setUniformValue("weight", w);
    const bool ok = drawInto(_blend, Output, a.width, a.height) && readPicture(a.width, a.height, out);
    _blend->release();
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
    while (_gl->glGetError() != GL_NO_ERROR)
    {
    }
    uploadPicture(FrameA, a);
    uploadPicture(FrameB, b);
    // The field as the CPU holds it (a field estimated on the CPU, or this one's read back)
    std::vector<int32_t> values;
    values.reserve(static_cast<size_t>(field.BlocksWide()) * field.BlocksHigh() * 4);
    for (int j = 0; j < field.BlocksHigh(); j++)
        for (int i = 0; i < field.BlocksWide(); i++)
        {
            const MotionField::Block& block = field.At(i, j);
            values.insert(values.end(), {block.v.x, block.v.y, block.matched ? 1 : 0, 0});
        }
    allocate(FieldOne, field.BlocksWide(), field.BlocksHigh(), GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, values.data());
    if (mask)
    {
        uploadMask(mask, a.width, a.height);
        uploadPicture(Base, Picture{a.width, a.height, out});
    }
    allocate(Output, a.width, a.height, GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, nullptr);

    GLint common[2 * MotionField::kComposeCommon] = {};
    const std::vector<MotionField::Vector>& vectors = field.Common();
    const size_t commonCount = std::min(vectors.size(), MotionField::kComposeCommon);
    for (size_t k = 0; k < commonCount; k++)
    {
        common[k * 2] = vectors[k].x;
        common[k * 2 + 1] = vectors[k].y;
    }
    _compose->bind();
    bindTexture(0, FrameA);
    bindTexture(1, FrameB);
    bindTexture(2, FieldOne);
    bindTexture(3, Mask);
    bindTexture(4, Base);
    _compose->setUniformValue("frameA", 0);
    _compose->setUniformValue("frameB", 1);
    _compose->setUniformValue("field", 2);
    _compose->setUniformValue("mask", 3);
    _compose->setUniformValue("base", 4);
    _compose->setUniformValue("useMask", static_cast<GLint>(mask != nullptr));
    _gl->glUniform2i(_compose->uniformLocation("blocks"), field.BlocksWide(), field.BlocksHigh());
    _compose->setUniformValue("weight", w);
    _compose->setUniformValue("commonCount", static_cast<int>(commonCount));
    _gl->glUniform2iv(_compose->uniformLocation("commonVectors"), static_cast<GLsizei>(MotionField::kComposeCommon), common);
    const bool ok = drawInto(_compose, Output, a.width, a.height) && readPicture(a.width, a.height, out);
    _compose->release();
    _gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    _gl->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    _gl->glActiveTexture(GL_TEXTURE0);
    return ok;
}

bool SmoothMotionGPU::windowSums(Texture source, unsigned mask0, int radius0, unsigned mask1, int radius1, bool flags,
                                 Texture target, int width, int height)
{
    _rowSums->bind();
    bindTexture(0, source);
    _rowSums->setUniformValue("source", 0);
    _gl->glUniform2i(_rowSums->uniformLocation("masks"), static_cast<GLint>(mask0), static_cast<GLint>(mask1));
    _gl->glUniform2i(_rowSums->uniformLocation("radii"), radius0, radius1);
    bool ok = drawInto(_rowSums, RowSums, width, height);
    _rowSums->release();
    _columnSums->bind();
    bindTexture(0, RowSums);
    bindTexture(1, FitBits);
    bindTexture(2, StillSums);
    bindTexture(3, ExplainedSums);
    _columnSums->setUniformValue("source", 0);
    _columnSums->setUniformValue("fitBits", 1);
    _columnSums->setUniformValue("stillSums", 2);
    _columnSums->setUniformValue("explainedSums", 3);
    _columnSums->setUniformValue("flags", static_cast<GLint>(flags));
    _columnSums->setUniformValue("tileCols", _tileCols);
    _gl->glUniform2i(_columnSums->uniformLocation("radii"), radius0, radius1);
    ok = ok && drawInto(_columnSums, target, width, height);
    _columnSums->release();
    return ok;
}

bool SmoothMotionGPU::readInts(int width, int height, std::vector<int32_t>& out)
{
    // From the target drawInto left bound
    out.resize(static_cast<size_t>(width) * height * 4);
    _gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    _gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    _gl->glReadPixels(0, 0, width, height, GL_RGBA_INTEGER, GL_INT, out.data());
    return _gl->glGetError() == GL_NO_ERROR;
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
    while (_gl->glGetError() != GL_NO_ERROR)
    {
    }
    const bool ok = composeLayers(p, a, b, prev, next, w, out);
    _gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    _gl->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    _gl->glActiveTexture(GL_TEXTURE0);
    return ok;
}

bool SmoothMotionGPU::composeLayers(const Picture& p, const Picture& a, const Picture& b, const MotionField& prev,
                                    const MotionField& next, int w, uint32_t* out)
{
    using smoothmotion::LayerFlow;
    LayerFlow::Prepare(prev, next, a.width, a.height, _layerSet);
    const int width = a.width, height = a.height;
    const int n = static_cast<int>(_layerSet.all.size());
    const int bw = _layerSet.blocksWide, bh = _layerSet.blocksHigh;
    const int groups = (n + 3) / 4;
    size_t maxAllowed = 1;
    for (const auto& allowed : _layerSet.allowed)
        maxAllowed = std::max(maxAllowed, allowed.size());
    // The vectors' tiles, room for a multiple of four (fewer reallocations); past the GL's limits: the CPU
    const int room = groups * 4;
    _tileCols = std::max(1, std::min(room, _maxSize / width));
    const int tileRows = (room + _tileCols - 1) / _tileCols;
    const int tiledWidth = _tileCols * width, tiledHeight = tileRows * height;
    if (tiledHeight > _maxSize || n > _maxSize || bh * groups > _maxSize || bw * static_cast<int>(maxAllowed) > _maxSize)
        return false;
    _n = n;
    _weight = w;
    _answered = true;
    _layerCounts = {n, 0, 0};

    uploadFrames(p, a, b);
    _ints.clear();
    for (const auto& v : _layerSet.all)
        _ints.insert(_ints.end(), {v.x, v.y});
    allocate(Vectors, n, 1, GL_RG32I, GL_RG_INTEGER, GL_INT, _ints.data());
    for (Texture t : {Changed, Explained})
        allocate(t, width, height, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);
    for (Texture t : {StillSums, ExplainedSums})
        allocate(t, width, height, GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT, nullptr);
    allocate(RowSums, tiledWidth, tiledHeight, GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT, nullptr);
    allocate(FitBits, tiledWidth, tiledHeight, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);
    allocate(Sums, tiledWidth, tiledHeight, GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, nullptr);
    allocate(RowCounts, _tileCols * bw, tiledHeight, GL_R32I, GL_RED_INTEGER, GL_INT, nullptr);
    allocate(Counts, bw, bh * groups, GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, nullptr);

    // The changes and the still windows round them
    _changed->bind();
    bindTexture(0, FrameA);
    bindTexture(1, FrameB);
    _changed->setUniformValue("frameA", 0);
    _changed->setUniformValue("frameB", 1);
    bool ok = drawInto(_changed, Changed, width, height);
    _changed->release();
    ok = ok && windowSums(Changed, 1, LayerFlow::kRadius, 1, LayerFlow::kSupportRadius, false, StillSums, width, height);

    // Per vector what it fits; the changes a moving vector explains and the windows round them; per vector its
    // score, support and what it may take
    if (ok)
    {
        _fit->bind();
        bindLayerFields(_fit);
        ok = drawInto(_fit, FitBits, tiledWidth, tiledHeight);
        _fit->release();
    }
    if (ok)
    {
        _explained->bind();
        bindLayerFields(_explained);
        ok = drawInto(_explained, Explained, width, height);
        _explained->release();
    }
    ok = ok && windowSums(Explained, 0, 0, 1, LayerFlow::kSupportRadius, false, ExplainedSums, width, height);
    ok = ok && windowSums(FitBits, 2, LayerFlow::kRadius, 4, LayerFlow::kSupportRadius, true, Sums, tiledWidth,
                          tiledHeight);

    // The counts per block, read back for the block decisions
    if (ok)
    {
        _rowCounts->bind();
        bindLayerFields(_rowCounts);
        ok = drawInto(_rowCounts, RowCounts, _tileCols * bw, tiledHeight);
        _rowCounts->release();
    }
    if (ok)
    {
        _counts->bind();
        bindLayerFields(_counts);
        bindTexture(8, RowCounts);
        _counts->setUniformValue("rowCounts", 8);
        ok = drawInto(_counts, Counts, bw, bh * groups) && readInts(bw, bh * groups, _ints);
        _counts->release();
    }
    if (!ok)
        return false;
    const size_t blocks = static_cast<size_t>(bw) * bh;
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
    std::vector<int> blockVector;
    std::vector<uint8_t> tied;
    LayerFlow::Decide(_layerSet, _fitCounts.data(), _usableCounts.data(), *this, blockVector, tied);
    if (!_answered)
        return false;

    // The picture
    _ints.assign(blocks * 4, 0);
    std::vector<int32_t> allowedOf(blocks * maxAllowed, 0);
    for (size_t block = 0; block < blocks; block++)
    {
        const int bv = blockVector[block];
        const auto& allowed = _layerSet.allowed[block];
        const bool moving = bv >= 0 && !(_layerSet.all[bv] == MotionField::Vector{});
        const bool explains = moving && !tied[block] &&
                              LayerFlow::Explains(a, b, static_cast<int>(block % bw), static_cast<int>(block / bw),
                                                  _layerSet.all[bv]);
        _ints[block * 4] = bv;
        _ints[block * 4 + 1] = tied[block];
        _ints[block * 4 + 2] = explains;
        _ints[block * 4 + 3] = static_cast<int32_t>(allowed.size());
        std::copy(allowed.begin(), allowed.end(), allowedOf.begin() + static_cast<ptrdiff_t>(block * maxAllowed));
    }
    allocate(BlockInfo, bw, bh, GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, _ints.data());
    // A block's allowed vectors in a row of maxAllowed, the blocks row by row
    allocate(Allowed, bw * static_cast<int>(maxAllowed), bh, GL_R32I, GL_RED_INTEGER, GL_INT, allowedOf.data());
    std::vector<uint32_t> palette;
    _colourAt.resize(static_cast<size_t>(width) * height);
    LayerFlow::Palette(a, palette, _colourAt.data());
    allocate(Colour, width, height, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, _colourAt.data());
    allocate(Output, width, height, GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, nullptr);
    _layers->bind();
    bindLayerFields(_layers);
    bindTexture(8, BlockInfo);
    bindTexture(9, Allowed);
    bindTexture(10, Colour);
    bindTexture(11, RowCounts);
    _layers->setUniformValue("blockInfo", 8);
    _layers->setUniformValue("allowed", 9);
    _layers->setUniformValue("colour", 10);
    _layers->setUniformValue("rowCounts", 11);
    _layers->setUniformValue("maxAllowed", static_cast<int>(maxAllowed));
    ok = drawInto(_layers, Output, width, height) && readPicture(width, height, out);
    _layers->release();
    return ok;
}

void SmoothMotionGPU::bindLayerFields(QOpenGLShaderProgram* program)
{
    // Units 0..7: the frames, the vectors and the per-pixel evidence
    bindTexture(0, FrameA);
    bindTexture(1, FrameB);
    bindTexture(2, FrameP);
    bindTexture(3, Vectors);
    bindTexture(4, FitBits);
    bindTexture(5, Sums);
    bindTexture(6, StillSums);
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
        allocate(Asks, asks, 1, GL_RG32I, GL_RG_INTEGER, GL_INT, _ints.data());
        allocate(AskAt, bw, bh, GL_R32I, GL_RED_INTEGER, GL_INT, _askOf.data());
        allocate(Top, width, height, GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, nullptr);
        allocate(OwnAnswers, _n, asks, GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, nullptr);
        _top->bind();
        bindLayerFields(_top);
        bindTexture(8, AskAt);
        bindTexture(9, Counts);
        _top->setUniformValue("askAt", 8);
        _top->setUniformValue("counts", 9);
        ok = drawInto(_top, Top, width, height);
        _top->release();
        _own->bind();
        bindLayerFields(_own);
        bindTexture(8, Asks);
        bindTexture(9, Top);
        _own->setUniformValue("asks", 8);
        _own->setUniformValue("top", 9);
        ok = ok && drawInto(_own, OwnAnswers, _n, asks);
        _own->release();
    }
    // The regions rule: per edge and vector
    if (ok && count > 0)
    {
        _ints.clear();
        for (const auto& e : edges)
            _ints.insert(_ints.end(), {e.bi, e.bj, (e.di + 1) + 3 * (e.dj + 1), 0});
        allocate(Edges, count, 1, GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, _ints.data());
        allocate(EdgeAnswers, count, _n, GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, nullptr);
        allocate(BlockColours, width, height, GL_R32I, GL_RED_INTEGER, GL_INT, nullptr);
        _blockColours->bind();
        bindLayerFields(_blockColours);
        ok = drawInto(_blockColours, BlockColours, width, height);
        _blockColours->release();
        _continues->bind();
        bindLayerFields(_continues);
        bindTexture(8, Edges);
        bindTexture(9, BlockColours);
        _continues->setUniformValue("edges", 8);
        _continues->setUniformValue("blockColours", 9);
        ok = ok && drawInto(_continues, EdgeAnswers, count, _n) && readInts(count, _n, _edgeAnswers);
        _continues->release();
    }
    // Read back once the GPU did all
    if (ok && asks > 0)
    {
        _gl->glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
        _gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _textures[OwnAnswers], 0);
        ok = readInts(_n, asks, _ownAnswers);
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
