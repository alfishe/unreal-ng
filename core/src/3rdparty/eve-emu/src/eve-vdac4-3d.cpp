// eve-emu: the VDAC4 coprocessor's 3D commands 0xFFFFFFA0...AF (EVE_MODEL_VDAC4 only).
//
// A port of the BL616 firmware's ft_3d.c (VDAC4 board, MIT): model-view matrix stack,
// perspective, near-plane and guard-band clipping, flat / face / Gouraud light, a 24-bit
// z-buffer and the strip rasteriser that writes the target bitmap into RAM_G.
//
// Determinism: all math is IEEE single precision as in the firmware (built with
// -ffp-contract=off); sqrt is exact by IEEE, sin / cos are this file's own series, never the
// host libm.
//
// Not modelled (the games that target VDAC4 do not use them): the textured rasteriser
// (CMD_3D_TEXMESH validates its parameters and draws nothing), SSAA (format | 0x100 is accepted,
// drawn without the 2 x 2 samples), the measurement block at 0x1DFE00 and the render time
// (CMD_3D_INFO reports 0 ms).

#include "eve-copro.h"
#include "eve-internal.h"

#include <cmath>
#include <cstring>

namespace EveLib
{

namespace
{

constexpr uint32_t kVersion = 3;                   // FTC3D_VERSION
constexpr uint32_t kStripBytes = 16384;            // FTC3D_STRIP
constexpr uint32_t kTargetEnd = 0x300000;          // the 3D target and sources lie in 3 MB RAM_G
constexpr uint32_t kMeshAddressMask = 0x3FFFFC;
constexpr uint32_t kMeshCull = 1, kMeshLight = 2, kMeshSmooth = 4, kMeshStatic = 8;
constexpr float kGuard = 8192.0f;                  // guard band: +-8192 pixels from the centre
constexpr uint32_t kFormatArgb1555 = 0, kFormatL8 = 3, kFormatRgb332 = 4, kFormatArgb2 = 5,
                   kFormatRgb565 = 7;

// --- deterministic sin / cos ---------------------------------------------------------------------

// sin and cos of x (radians) in double by range reduction and Taylor series, rounded to float:
// the same bits on every host.
void SinCos(float angle, float& sine, float& cosine)
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kHalfPi = kPi / 2;
    double x = static_cast<double>(angle);
    // quadrant: x = q * pi/2 + r, |r| <= pi/4
    const double qd = std::floor(x / kHalfPi + 0.5);
    const double r = x - qd * kHalfPi;
    const int64_t q = static_cast<int64_t>(qd);
    const double r2 = r * r;
    double s = 0.0, c = 0.0, term = r;
    for (int n = 1; n < 30; n += 2) // r - r^3/3! + ...
    {
        s += term;
        term *= -r2 / ((n + 1) * (n + 2));
    }
    term = 1.0;
    for (int n = 0; n < 30; n += 2)
    {
        c += term;
        term *= -r2 / ((n + 1) * (n + 2));
    }
    double rs = s, rc = c;
    switch (static_cast<uint32_t>(q & 3))
    {
    case 1:
        rs = c;
        rc = -s;
        break;
    case 2:
        rs = -s;
        rc = -c;
        break;
    case 3:
        rs = -c;
        rc = s;
        break;
    default:
        break;
    }
    sine = static_cast<float>(rs);
    cosine = static_cast<float>(rc);
}

// --- matrices --------------------------------------------------------------------------------------

float Fixed(uint32_t v)
{
    return static_cast<float>(static_cast<int32_t>(v)) * (1.0f / 65536.0f);
}

void Identity(float* m)
{
    std::memset(m, 0, 12 * sizeof(float));
    m[0] = m[5] = m[10] = 1.0f;
}

void SetLight(Vdac43dState& g, float x, float y, float z)
{
    float l = std::sqrt(x * x + y * y + z * z);
    if (l < 1e-6f)
    {
        x = 0.0f;
        y = 0.0f;
        z = -1.0f;
        l = 1.0f;
    }
    g.lx = x / l;
    g.ly = y / l;
    g.lz = z / l;
}

// m = m x n (both affine 3 x 4)
void MatMul(float* m, const float* n)
{
    float r[12];
    for (int i = 0; i < 3; i++)
    {
        const float* a = m + 4 * i;
        for (int j = 0; j < 4; j++)
            r[4 * i + j] = a[0] * n[j] + a[1] * n[4 + j] + a[2] * n[8 + j];
        r[4 * i + 3] += a[3];
    }
    std::memcpy(m, r, sizeof r);
}

void Rotate(float* m, uint32_t ax, uint32_t ay, uint32_t az)
{
    const float k = 6.28318530718f / 65536.0f;
    float n[12];
    float s = 0, c = 0;
    SinCos(k * static_cast<float>(az & 0xFFFFu), s, c);
    Identity(n);
    n[0] = c;
    n[1] = -s;
    n[4] = s;
    n[5] = c;
    MatMul(m, n);
    SinCos(k * static_cast<float>(ay & 0xFFFFu), s, c);
    Identity(n);
    n[0] = c;
    n[2] = s;
    n[8] = -s;
    n[10] = c;
    MatMul(m, n);
    SinCos(k * static_cast<float>(ax & 0xFFFFu), s, c);
    Identity(n);
    n[5] = c;
    n[6] = -s;
    n[9] = s;
    n[10] = c;
    MatMul(m, n);
}

uint32_t FormatBytes(uint32_t fmt)
{
    return (fmt == kFormatArgb1555 || fmt == kFormatRgb565) ? 2u : 1u;
}

bool FormatOk(uint32_t fmt)
{
    return fmt == kFormatArgb1555 || fmt == kFormatL8 || fmt == kFormatRgb332 || fmt == kFormatArgb2 ||
           fmt == kFormatRgb565;
}

// --- the frame's triangles -----------------------------------------------------------------------

struct CameraVertex
{
    float x, y, z, s; // camera space and light
};

Vdac43dTri* Tris(EveChip& chip)
{
    return reinterpret_cast<Vdac43dTri*>(chip.regions[RegionVdac43d].base);
}

uint32_t* CacheWords(EveChip& chip)
{
    return reinterpret_cast<uint32_t*>(chip.regions[RegionVdac43d].base + kVdac43dTrisBytes);
}

float PlaneDistance(const Vdac43dState& g, int k, const CameraVertex& v)
{
    switch (k)
    {
    case 0:
        return v.z - g.znear;
    case 1:
        return kGuard * v.z - g.focal * v.x;
    case 2:
        return kGuard * v.z + g.focal * v.x;
    case 3:
        return kGuard * v.z - g.focal * v.y;
    default:
        return kGuard * v.z + g.focal * v.y;
    }
}

int32_t ToFixed(float v)
{
    const float f = v * 16.0f + 0.5f;
    const int32_t n = static_cast<int32_t>(f);
    return n - (static_cast<float>(n) > f ? 1 : 0); // exact floor
}

void AddTri(EveChip& chip, const CameraVertex& a, const CameraVertex& b, const CameraVertex& c, uint16_t color)
{
    Vdac43dState& g = chip.vdac4State.threeD;
    const CameraVertex* p[3] = {&a, &b, &c};
    const float cx = 0.5f * static_cast<float>(g.w), cy = 0.5f * static_cast<float>(g.h);
    int32_t X[3], Y[3];
    float iz[3];
    uint8_t sh[3];
    for (int i = 0; i < 3; i++)
    {
        const float inv = 1.0f / p[i]->z;
        X[i] = ToFixed(cx + g.focal * p[i]->x * inv);
        Y[i] = ToFixed(cy - g.focal * p[i]->y * inv);
        iz[i] = g.znear * inv;
        if (iz[i] > 1.0f)
            iz[i] = 1.0f;
        const float s = p[i]->s;
        sh[i] = static_cast<uint8_t>(s <= 0.0f ? 0 : s >= 255.0f ? 255 : static_cast<int>(s + 0.5f));
    }
    const int64_t area = static_cast<int64_t>(X[1] - X[0]) * (Y[2] - Y[0]) -
                         static_cast<int64_t>(X[2] - X[0]) * (Y[1] - Y[0]);
    if (area == 0)
        return;
    if (area < 0) // clockwise on screen (y down)
    {
        std::swap(X[1], X[2]);
        std::swap(Y[1], Y[2]);
        std::swap(iz[1], iz[2]);
        std::swap(sh[1], sh[2]);
    }
    int32_t x0 = X[0], x1 = X[0], y0 = Y[0], y1 = Y[0];
    for (int i = 1; i < 3; i++)
    {
        x0 = X[i] < x0 ? X[i] : x0;
        x1 = X[i] > x1 ? X[i] : x1;
        y0 = Y[i] < y0 ? Y[i] : y0;
        y1 = Y[i] > y1 ? Y[i] : y1;
    }
    // rows and columns whose centres (16 n + 8) are inside the box
    constexpr int32_t kCentre = 8;
    int32_t r0 = (y0 - kCentre + 15) >> 4, r1 = (y1 - kCentre) >> 4;
    r0 = r0 < 0 ? 0 : r0;
    r1 = r1 > static_cast<int32_t>(g.h) - 1 ? static_cast<int32_t>(g.h) - 1 : r1;
    if (r0 > r1 || x1 < kCentre || x0 > 16 * static_cast<int32_t>(g.w) - kCentre)
        return;
    if (g.ntris >= kVdac43dMaxTris)
    {
        g.dropped++;
        return;
    }
    Vdac43dTri t{};
    for (int i = 0; i < 3; i++)
    {
        t.x[i] = X[i];
        t.y[i] = Y[i];
        t.iz[i] = iz[i];
        t.sh[i] = sh[i];
    }
    t.color = color;
    t.y0 = static_cast<int16_t>(r0);
    t.y1 = static_cast<int16_t>(r1);
    const uint32_t at = g.ntris++;
    Tris(chip)[at] = t;
    chip.regions[RegionVdac43d].MarkDirtyRange(at * static_cast<uint32_t>(sizeof(Vdac43dTri)),
                                                static_cast<uint32_t>(sizeof(Vdac43dTri)));
}

// A camera triangle: clipped by the near plane and the guard band, then into the frame.
void ClipTri(EveChip& chip, const CameraVertex& a, const CameraVertex& b, const CameraVertex& c, uint16_t color)
{
    const Vdac43dState& g = chip.vdac4State.threeD;
    uint32_t out = 0;
    const CameraVertex* p3[3] = {&a, &b, &c};
    for (int k = 0; k < 5; k++)
    {
        int behind = 0;
        for (int i = 0; i < 3; i++)
            behind += PlaneDistance(g, k, *p3[i]) < 0.0f ? 1 : 0;
        if (behind == 3)
            return;
        if (behind != 0)
            out |= 1u << k;
    }
    if (out == 0)
    {
        AddTri(chip, a, b, c, color);
        return;
    }
    CameraVertex buf[2][9];
    int n = 3, cur = 0;
    buf[0][0] = a;
    buf[0][1] = b;
    buf[0][2] = c;
    for (int k = 0; k < 5 && n != 0; k++)
    {
        if (!(out & (1u << k)))
            continue;
        const CameraVertex* in = buf[cur];
        CameraVertex* o = buf[cur ^ 1];
        int m = 0;
        for (int i = 0; i < n; i++)
        {
            const CameraVertex& p = in[i];
            const CameraVertex& q = in[(i + 1) % n];
            const float dp = PlaneDistance(g, k, p), dq = PlaneDistance(g, k, q);
            if (dp >= 0.0f)
                o[m++] = p;
            if ((dp >= 0.0f) != (dq >= 0.0f))
            {
                const float t = dp / (dp - dq);
                o[m].x = p.x + t * (q.x - p.x);
                o[m].y = p.y + t * (q.y - p.y);
                o[m].z = p.z + t * (q.z - p.z);
                o[m].s = p.s + t * (q.s - p.s);
                if (k == 0)
                    o[m].z = g.znear; // exactly on the plane: znear / z = 1
                m++;
            }
        }
        n = m;
        cur ^= 1;
    }
    for (int i = 1; i + 1 < n; i++)
        AddTri(chip, buf[cur][0], buf[cur][i], buf[cur][i + 1], color);
}

float Shade(const Vdac43dState& g, float nx, float ny, float nz)
{
    const float l = std::sqrt(nx * nx + ny * ny + nz * nz);
    const float d = l > 0.0f ? (nx * g.lx + ny * g.ly + nz * g.lz) / l : 0.0f;
    const float s = static_cast<float>(g.ambient) + (d > 0.0f ? d * static_cast<float>(g.diffuse) : 0.0f);
    return s > 255.0f ? 255.0f : s;
}

void ReadWords(const EveChip& chip, uint32_t address, uint32_t* out, uint32_t words)
{
    for (uint32_t i = 0; i < words; ++i)
        out[i] = ReadMemory32(chip, address + 4 * i);
}

// RAM_G words [addr, addr + 4 words): from the static-mesh cache (flag 8), else read into tmp;
// without the flag a cached copy is dropped (ft_3d.c mesh_words).
const uint32_t* MeshWords(EveChip& chip, uint32_t addr, uint32_t words, uint32_t* tmp, bool keep)
{
    Vdac43dState& g = chip.vdac4State.threeD;
    uint32_t* cache = CacheWords(chip);
    for (uint32_t i = 0; i < g.ncached; i++)
        if (g.cached[i].addr == addr && g.cached[i].words == words)
        {
            if (keep)
                return &cache[g.cached[i].at];
            g.cached[i].addr = 0xFFFFFFFFu;
        }
    if (keep && g.ncached < kVdac43dCacheItems && g.cacheUsed + words <= kVdac43dCacheWords)
    {
        Vdac43dCached& e = g.cached[g.ncached++];
        e.addr = addr;
        e.words = words;
        e.at = g.cacheUsed;
        g.cacheUsed += words;
        ReadWords(chip, addr, &cache[e.at], words);
        chip.regions[RegionVdac43d].MarkDirtyRange(kVdac43dTrisBytes + 4 * e.at, 4 * words);
        return &cache[e.at];
    }
    ReadWords(chip, addr, tmp, words);
    return tmp;
}

struct MeshVertex
{
    float x, y, z, nx, ny, nz;
};

bool Mesh(EveChip& chip)
{
    Vdac43dState& g = chip.vdac4State.threeD;
    const uint32_t* p = chip.state.copro.params;
    const uint32_t vaddr = p[0], faddr = p[1], nv = p[2] & 0xFFFFu, nf = p[2] >> 16, flags = p[3];
    if (nv > kVdac43dMaxVerts || nf > kVdac43dMaxFaces || (vaddr & 3u) || (faddr & 3u))
        return false;
    if (nv == 0 || nf == 0 || g.w == 0)
        return true;
    static thread_local MeshVertex v[kVdac43dMaxVerts];
    static thread_local uint8_t vsh[kVdac43dMaxVerts];
    static thread_local uint32_t tmpVerts[2 * kVdac43dMaxVerts], tmpFaces[2 * kVdac43dMaxFaces];
    const bool keep = (flags & kMeshStatic) != 0;
    const uint32_t* raw = MeshWords(chip, vaddr & kMeshAddressMask, 2 * nv, tmpVerts, keep);
    const float* m = g.m;
    for (uint32_t i = 0; i < nv; i++)
    {
        const float x = static_cast<float>(static_cast<int16_t>(raw[2 * i]));
        const float y = static_cast<float>(static_cast<int16_t>(raw[2 * i] >> 16));
        const float z = static_cast<float>(static_cast<int16_t>(raw[2 * i + 1]));
        MeshVertex& w = v[i];
        w.x = m[0] * x + m[1] * y + m[2] * z + m[3];
        w.y = m[4] * x + m[5] * y + m[6] * z + m[7];
        w.z = m[8] * x + m[9] * y + m[10] * z + m[11];
        w.nx = w.ny = w.nz = 0.0f;
    }
    const uint32_t* fw = MeshWords(chip, faddr & kMeshAddressMask, 2 * nf, tmpFaces, keep);
    const bool smooth = (flags & kMeshSmooth) != 0;
    if (smooth)
    {
        for (uint32_t f = 0; f < nf; f++)
        {
            const uint32_t ia = fw[2 * f] & 0xFFFFu, ib = fw[2 * f] >> 16, ic = fw[2 * f + 1] & 0xFFFFu;
            if (ia >= nv || ib >= nv || ic >= nv)
                continue;
            MeshVertex &a = v[ia], &b = v[ib], &cc = v[ic];
            const float ux = cc.x - a.x, uy = cc.y - a.y, uz = cc.z - a.z;
            const float wx = b.x - a.x, wy = b.y - a.y, wz = b.z - a.z;
            const float nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
            a.nx += nx;
            a.ny += ny;
            a.nz += nz;
            b.nx += nx;
            b.ny += ny;
            b.nz += nz;
            cc.nx += nx;
            cc.ny += ny;
            cc.nz += nz;
        }
        for (uint32_t i = 0; i < nv; i++)
            vsh[i] = static_cast<uint8_t>(Shade(g, v[i].nx, v[i].ny, v[i].nz) + 0.5f);
    }
    const bool faceNormal = (flags & kMeshCull) || (!smooth && (flags & kMeshLight));
    for (uint32_t f = 0; f < nf; f++)
    {
        const uint32_t ia = fw[2 * f] & 0xFFFFu, ib = fw[2 * f] >> 16, ic = fw[2 * f + 1] & 0xFFFFu;
        const uint16_t color = static_cast<uint16_t>(fw[2 * f + 1] >> 16);
        if (ia >= nv || ib >= nv || ic >= nv)
            continue;
        const MeshVertex &a = v[ia], &b = v[ib], &cc = v[ic];
        // outward normal (c - a) x (b - a): left-handed, z away from the viewer
        const float ux = cc.x - a.x, uy = cc.y - a.y, uz = cc.z - a.z;
        const float wx = b.x - a.x, wy = b.y - a.y, wz = b.z - a.z;
        float nx = 0, ny = 0, nz = 0;
        bool front = true;
        if (faceNormal)
        {
            nx = uy * wz - uz * wy;
            ny = uz * wx - ux * wz;
            nz = ux * wy - uy * wx;
            front = nx * a.x + ny * a.y + nz * a.z < 0.0f;
        }
        if ((flags & kMeshCull) && !front)
            continue;
        CameraVertex pv[3] = {{a.x, a.y, a.z, 255.0f}, {b.x, b.y, b.z, 255.0f}, {cc.x, cc.y, cc.z, 255.0f}};
        if (smooth)
        {
            pv[0].s = vsh[ia];
            pv[1].s = vsh[ib];
            pv[2].s = vsh[ic];
        }
        else if (flags & kMeshLight)
        {
            const float s = front ? Shade(g, nx, ny, nz) : Shade(g, -nx, -ny, -nz);
            pv[0].s = pv[1].s = pv[2].s = s;
        }
        ClipTri(chip, pv[0], pv[1], pv[2], color);
    }
    return true;
}

// New sources lie wholly inside RAM_G (ft_3d.c ram_range).
bool RamRange(uint32_t addr, uint32_t bytes)
{
    return !(addr & 3u) && addr < kTargetEnd && bytes <= kTargetEnd - addr;
}

// CMD_3D_TEXTURE: the parameters are checked as the firmware does; the texels are not kept (no
// textured rasteriser).
bool Texture(EveChip& chip)
{
    constexpr uint32_t kMaxSide = 4095, kTextureBytes = 16384;
    Vdac43dState& g = chip.vdac4State.threeD;
    const uint32_t* p = chip.state.copro.params;
    const uint32_t w = p[1] & 65535u, h = p[1] >> 16, fmt = p[2];
    const uint64_t bytes = static_cast<uint64_t>(w) * h * ((fmt == 1 || fmt == 2) ? 2u : 1u);
    if (g.ntris != 0 || w == 0 || h == 0 || w > kMaxSide || h > kMaxSide || fmt > 3 || bytes > kTextureBytes ||
        !RamRange(p[0], (static_cast<uint32_t>(bytes) + 3u) & ~3u))
        return false;
    if ((fmt == 0 || fmt == 3) && !RamRange(p[3], fmt == 3 ? 2048 : 1024))
        return false;
    g.texW = w;
    g.texH = h;
    g.texFormat = fmt;
    g.texValid = 1;
    return true;
}

// CMD_3D_TEXMESH: the firmware's checks; the textured triangles are not drawn.
bool Texmesh(EveChip& chip)
{
    constexpr uint32_t kVertexBytes = 16, kFaceBytes = 8;
    const Vdac43dState& g = chip.vdac4State.threeD;
    const uint32_t* p = chip.state.copro.params;
    const uint32_t nv = p[2] & 65535u, nf = p[2] >> 16, flags = p[3];
    if (!g.texValid || g.w == 0 || nv > kVdac43dMaxVerts || nf > kVdac43dMaxFaces || (flags & ~1u) ||
        !RamRange(p[0], nv * kVertexBytes) || !RamRange(p[1], nf * kFaceBytes))
        return false;
    for (uint32_t f = 0; f < nf; f++)
    {
        const uint32_t w0 = ReadMemory32(chip, p[1] + kFaceBytes * f), w1 = ReadMemory32(chip, p[1] + kFaceBytes * f + 4);
        if ((w1 >> 16) != 0 || (w0 & 65535u) >= nv || (w0 >> 16) >= nv || (w1 & 65535u) >= nv)
            return false;
    }
    return true;
}

// --- rasteriser ------------------------------------------------------------------------------------

constexpr uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

// v 0...255 to L + 1 levels with the threshold d 0...15
uint32_t Quantise(uint32_t v, uint32_t levels, uint32_t d)
{
    return (v * levels * 16u + d * 255u) / 4080u;
}

uint32_t PackPixel(uint32_t fmt, uint32_t r, uint32_t gg, uint32_t b, uint32_t d)
{
    switch (fmt)
    {
    case kFormatRgb565:
        return ((r >> 3) << 11) | ((gg >> 2) << 5) | (b >> 3);
    case kFormatArgb1555:
        return 0x8000u | ((r >> 3) << 10) | ((gg >> 3) << 5) | (b >> 3);
    case kFormatRgb332:
        return (Quantise(r, 7, d) << 5) | (Quantise(gg, 7, d) << 2) | Quantise(b, 3, d);
    case kFormatArgb2:
        return 0xC0u | (Quantise(r, 3, d) << 4) | (Quantise(gg, 3, d) << 2) | Quantise(b, 3, d);
    default:
        return (77u * r + 150u * gg + 29u * b + 128u) >> 8; // L8
    }
}

uint32_t PackClear(uint32_t fmt, uint32_t argb)
{
    const uint32_t a = argb >> 24, r = (argb >> 16) & 0xFF, gg = (argb >> 8) & 0xFF, b = argb & 0xFF;
    switch (fmt)
    {
    case kFormatArgb1555:
        return ((a >= 128) ? 0x8000u : 0u) | ((r >> 3) << 10) | ((gg >> 3) << 5) | (b >> 3);
    case kFormatRgb332:
        return (((r * 7 + 127) / 255) << 5) | (((gg * 7 + 127) / 255) << 2) | ((b * 3 + 127) / 255);
    case kFormatArgb2:
        return ((a >> 6) << 6) | (((r * 3 + 127) / 255) << 4) | (((gg * 3 + 127) / 255) << 2) | ((b * 3 + 127) / 255);
    default:
        return PackPixel(fmt, r, gg, b, 8);
    }
}

// float to an integer in [lo, hi]; NaN gives lo
int32_t ClampToInt(float v, int32_t lo, int32_t hi)
{
    if (!(v > static_cast<float>(lo)))
        return lo;
    if (v >= static_cast<float>(hi))
        return hi;
    return static_cast<int32_t>(v);
}

int64_t CeilDiv(int64_t a, int64_t b) // b > 0
{
    return a >= 0 ? (a + b - 1) / b : -((-a) / b);
}

int64_t FloorDiv(int64_t a, int64_t b) // b > 0
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

uint32_t Scale8(uint32_t c, uint32_t k)
{
    return (c * k * 257u + 32896u) >> 16;
}

// Triangle t into rows [ys, ys + rows) of the strip (ft_3d.c raster).
void Raster(const Vdac43dState& g, const Vdac43dTri& t, int ys, int rows, uint8_t* pix, uint32_t* zb)
{
    const int w = static_cast<int>(g.w);
    const uint32_t fmt = g.fmt, bytes = FormatBytes(fmt);
    int64_t sx[3], sy[3], c0[3];
    for (int e = 0; e < 3; e++)
    {
        const int a = e, b = (e + 1) % 3;
        const int64_t dx = static_cast<int64_t>(t.x[b]) - t.x[a], dy = static_cast<int64_t>(t.y[b]) - t.y[a];
        sx[e] = -16 * dy;
        sy[e] = 16 * dx;
        c0[e] = dx * (8 - static_cast<int64_t>(t.y[a])) - dy * (8 - static_cast<int64_t>(t.x[a]));
        if (!(dy < 0 || (dy == 0 && dx > 0)))
            c0[e] -= 1; // not a top or left edge: strictly inside
    }
    const float X10 = static_cast<float>(t.x[1] - t.x[0]), X20 = static_cast<float>(t.x[2] - t.x[0]);
    const float Y10 = static_cast<float>(t.y[1] - t.y[0]), Y20 = static_cast<float>(t.y[2] - t.y[0]);
    const int64_t area = static_cast<int64_t>(t.x[1] - t.x[0]) * (t.y[2] - t.y[0]) -
                         static_cast<int64_t>(t.x[2] - t.x[0]) * (t.y[1] - t.y[0]);
    const float inv =
        1.0f / (area <= 0x7fffffffLL ? static_cast<float>(static_cast<int32_t>(area)) : static_cast<float>(area));
    const float z1 = t.iz[1] - t.iz[0], z2 = t.iz[2] - t.iz[0];
    const float zgx = (z1 * Y20 - z2 * Y10) * inv, zgy = (z2 * X10 - z1 * X20) * inv;
    const bool flat = t.sh[0] == t.sh[1] && t.sh[1] == t.sh[2];
    const float s1 = static_cast<float>(t.sh[1]) - t.sh[0], s2 = static_cast<float>(t.sh[2]) - t.sh[0];
    const float sgx = (s1 * Y20 - s2 * Y10) * inv, sgy = (s2 * X10 - s1 * X20) * inv;
    const uint32_t c = t.color;
    uint32_t r8 = ((c >> 11) << 3) | (c >> 13), g8 = (((c >> 5) & 63) << 2) | ((c >> 9) & 3),
             b8 = ((c & 31) << 3) | ((c >> 2) & 7);
    const bool dither = fmt == kFormatRgb332 || fmt == kFormatArgb2;
    uint32_t solid = 0;
    if (flat)
    {
        const uint32_t s = t.sh[0];
        r8 = Scale8(r8, s);
        g8 = Scale8(g8, s);
        b8 = Scale8(b8, s);
        solid = PackPixel(fmt, r8, g8, b8, 8);
    }
    const int ya = t.y0 > ys ? t.y0 : ys, yb = t.y1 < ys + rows - 1 ? t.y1 : ys + rows - 1;
    for (int y = ya; y <= yb; y++)
    {
        int32_t lo = 0, hi = w - 1;
        bool empty = false;
        for (int e = 0; e < 3 && !empty; e++)
        {
            const int64_t ce = c0[e] + sy[e] * y;
            if (sx[e] > 0)
            {
                const int64_t v = CeilDiv(-ce, sx[e]);
                if (v > lo)
                    lo = v > w ? w : static_cast<int32_t>(v);
            }
            else if (sx[e] < 0)
            {
                const int64_t v = FloorDiv(ce, -sx[e]);
                if (v < hi)
                    hi = v < -1 ? -1 : static_cast<int32_t>(v);
            }
            else if (ce < 0)
                empty = true;
        }
        if (empty || lo > hi)
            continue;
        const float pl = static_cast<float>(16 * lo + 8 - t.x[0]), ph = static_cast<float>(16 * hi + 8 - t.x[0]);
        const float py = static_cast<float>(16 * y + 8 - t.y[0]);
        const int32_t span = hi - lo;
        const float zr = t.iz[0] + zgy * py;
        int32_t z = ClampToInt((zr + zgx * pl) * 16777216.0f, 1, 1 << 24);
        const int32_t ze = ClampToInt((zr + zgx * ph) * 16777216.0f, 1, 1 << 24);
        const int32_t dz = span != 0 ? (ze - z) / span : 0;
        int32_t s = 0, ds = 0;
        if (!flat)
        {
            const float sr = static_cast<float>(t.sh[0]) + sgy * py;
            s = ClampToInt((sr + sgx * pl) * 256.0f, 0, 255 * 256);
            const int32_t se = ClampToInt((sr + sgx * ph) * 256.0f, 0, 255 * 256);
            ds = span != 0 ? (se - s) / span : 0;
        }
        uint32_t* zp = zb + static_cast<size_t>(y - ys) * static_cast<size_t>(w);
        uint8_t* row = pix + static_cast<size_t>(y - ys) * static_cast<size_t>(w) * bytes;
        const uint8_t* bay = kBayer + 4 * (y & 3);
        for (int x = lo; x <= hi; x++, z += dz, s += ds)
        {
            const uint32_t zq = static_cast<uint32_t>(z);
            if (zq <= zp[x])
                continue;
            zp[x] = zq;
            uint32_t v = solid;
            if (!flat)
            {
                const int32_t sv = s >> 8;
                const uint32_t k = sv < 0 ? 0u : sv > 255 ? 255u : static_cast<uint32_t>(sv);
                v = PackPixel(fmt, Scale8(r8, k), Scale8(g8, k), Scale8(b8, k), bay[x & 3]);
            }
            else if (dither)
                v = PackPixel(fmt, r8, g8, b8, bay[x & 3]);
            if (bytes == 2)
            {
                row[2 * x] = static_cast<uint8_t>(v);
                row[2 * x + 1] = static_cast<uint8_t>(v >> 8);
            }
            else
                row[x] = static_cast<uint8_t>(v);
        }
    }
}

// CMD_3D_RENDER: the frame in strips, each cleared and written to the target in RAM_G.
bool Render(EveChip& chip)
{
    Vdac43dState& g = chip.vdac4State.threeD;
    if (g.w == 0)
        return false;
    const uint32_t w = g.w, h = g.h, bytes = FormatBytes(g.fmt), rowBytes = w * bytes;
    uint32_t lines = kVdac43dDepthWords / w;
    lines = lines > h ? h : lines;
    const uint32_t bg = PackClear(g.fmt, g.clear);
    static thread_local uint8_t pix[kStripBytes];
    uint32_t* zb = chip.vdac4Depth.get();
    const Vdac43dTri* tris = Tris(chip);
    for (uint32_t ys = 0; ys < h; ys += lines)
    {
        const uint32_t rows = h - ys < lines ? h - ys : lines;
        const uint32_t stripBytes = rows * rowBytes;
        for (uint32_t i = 0; i < stripBytes; i += bytes)
        {
            pix[i] = static_cast<uint8_t>(bg);
            if (bytes == 2)
                pix[i + 1] = static_cast<uint8_t>(bg >> 8);
        }
        std::memset(zb, 0, rows * w * sizeof(uint32_t));
        for (uint32_t i = 0; i < g.ntris; i++)
        {
            const Vdac43dTri& t = tris[i];
            if (t.y1 >= static_cast<int>(ys) && t.y0 < static_cast<int>(ys + rows))
                Raster(g, t, static_cast<int>(ys), static_cast<int>(rows), pix, zb);
        }
        const uint32_t base = g.addr + ys * rowBytes;
        for (uint32_t i = 0; i < stripBytes; ++i)
            BusWrite(chip, base + i, pix[i]);
    }
    g.lastTris = g.ntris;
    g.lastMs = 0;
    return true;
}

} // namespace

void Vdac43dReset(EveChip& chip)
{
    Vdac43dState& g = chip.vdac4State.threeD;
    Identity(g.m);
    g.sp = 0;
    g.addr = g.fmt = g.w = g.h = g.aa = 0;
    g.focal = 256.0f;
    g.znear = 1.0f;
    SetLight(g, -1.0f, 1.0f, -2.0f); // left, above, in front of the viewer
    g.ambient = 64;
    g.diffuse = 191;
    g.clear = 0;
    g.fogNear = g.fogFar = g.colorSteps = 0;
    g.material = 128u << 8;
    g.fogColor = 0;
    g.ntris = g.dropped = 0;
    g.lastTris = g.lastMs = 0;
    g.ncached = g.cacheUsed = 0;
    g.texValid = 0;
}

bool Vdac43dExecute(EveChip& chip)
{
    Vdac43dState& g = chip.vdac4State.threeD;
    const uint32_t* p = chip.state.copro.params;
    float n[12];
    switch (chip.state.copro.command)
    {
    case kCmd3dTarget:
    {
        const uint32_t w = p[2] & 0xFFFFu, h = p[2] >> 16, fmt = p[1] & 0xFFu;
        if ((p[1] & ~0x1FFu) || !FormatOk(fmt) || w < 4 || w > 1024 || (w & 3u) || h < 1 || h > 2048 || (p[0] & 3u) ||
            p[0] >= kTargetEnd || p[0] + w * h * FormatBytes(fmt) > kTargetEnd)
            return false;
        g.addr = p[0];
        g.fmt = fmt;
        g.aa = (p[1] >> 8) & 1u;
        g.w = w;
        g.h = h;
        g.ntris = g.dropped = 0;
        return true;
    }
    case kCmd3dProject:
        if (static_cast<int32_t>(p[0]) <= 0 || static_cast<int32_t>(p[1]) <= 0)
            return false;
        g.focal = Fixed(p[0]);
        g.znear = Fixed(p[1]);
        return true;
    case kCmd3dIdentity:
        Identity(g.m);
        return true;
    case kCmd3dTranslate:
        Identity(n);
        n[3] = Fixed(p[0]);
        n[7] = Fixed(p[1]);
        n[11] = Fixed(p[2]);
        MatMul(g.m, n);
        return true;
    case kCmd3dRotate:
        Rotate(g.m, p[0], p[1], p[2]);
        return true;
    case kCmd3dScale:
        Identity(n);
        n[0] = Fixed(p[0]);
        n[5] = Fixed(p[1]);
        n[10] = Fixed(p[2]);
        MatMul(g.m, n);
        return true;
    case kCmd3dPush:
        if (g.sp < kVdac43dStack)
            std::memcpy(g.stack[g.sp++], g.m, sizeof g.m);
        return true;
    case kCmd3dPop:
        if (g.sp > 0)
            std::memcpy(g.m, g.stack[--g.sp], sizeof g.m);
        return true;
    case kCmd3dLight:
        SetLight(g, static_cast<float>(static_cast<int16_t>(p[0])), static_cast<float>(static_cast<int16_t>(p[0] >> 16)),
                 static_cast<float>(static_cast<int16_t>(p[1])));
        g.ambient = (p[1] >> 16) & 0xFFu;
        g.diffuse = p[1] >> 24;
        return true;
    case kCmd3dClear:
        g.clear = p[0];
        g.ntris = g.dropped = 0;
        return true;
    case kCmd3dMesh:
        return Mesh(chip);
    case kCmd3dTexture:
        return Texture(chip);
    case kCmd3dTexmesh:
        return Texmesh(chip);
    case kCmd3dMaterial:
        if (g.ntris != 0 || (p[4] & ~0xff03u) || static_cast<int32_t>(p[3]) < 0 ||
            ((p[4] & 1u) && (static_cast<int32_t>(p[0]) < 0 || static_cast<int32_t>(p[1]) <= static_cast<int32_t>(p[0]))))
            return false;
        g.fogNear = Fixed(p[0]);
        g.fogFar = Fixed(p[1]);
        g.fogColor = p[2];
        g.colorSteps = Fixed(p[3]);
        g.material = p[4];
        return true;
    case kCmd3dRender:
        return Render(chip);
    case kCmd3dInfo:
        WriteRingResult(chip, 0, 0x3D000000u | (kVersion << 16) | kVdac43dMaxTris);
        WriteRingResult(chip, 1, (g.lastTris & 0xFFFFu) | (g.lastMs << 16));
        return true;
    default:
        return false;
    }
}

} // namespace EveLib
