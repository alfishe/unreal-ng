// eve-accel 05: the op list evaluated by an OpenGL 4.1 fragment shader (macOS has no GL
// compute shaders: 4.1 is its last version). One full-screen triangle per frame; each
// fragment runs the ops of its row band, exactly as the Metal kernel and EveSnap::Pixel do:
// integer arithmetic only, no fixed-function blending (that is 8-bit unorm and cannot give
// eve-emu's rounding). RAM_G + ROM, the op list and the band lists are texture buffers; the
// picture goes to an RGBA8UI render target and is read back with glReadPixels.
//
//   gl-render [--repeat N] <snapshot>...
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#include "eve-snap.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>

namespace
{

const char* kVertex = R"(#version 410
void main()
{
    // A triangle that covers the viewport
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// A port of EveSnap::Pixel (common/eve-snap.h). Op fields are read from a texture buffer
// of uint (44 words per op, in the order of struct Op).
const char* kFragment = R"(#version 410
uniform usamplerBuffer memory;   // R8UI: RAM_G, zeros, ROM
uniform usamplerBuffer ops;      // R32UI: 44 words per op
uniform usamplerBuffer bands;    // R32UI: bandStart[bands + 1], then the op indices
uniform int bandCount;
uniform int bandRows;
uniform int height;
out uvec4 fragColor;

uint W(int op, int i) { return texelFetch(ops, op * 44 + i).r; }
int I(int op, int i) { return int(texelFetch(ops, op * 44 + i).r); }
uint Byte(uint a) { return texelFetch(memory, int(a)).r; }
uint Read16(uint a) { return Byte(a) | (Byte(a + 1u) << 8); }

uint Mul(uint a, uint b)
{
    uint t = a * b + 127u;
    return (t + 1u + (t >> 8)) >> 8;
}
uint Expand(uint v, uint bits)
{
    int shift = 8 - int(bits);
    uint o = 0u;
    for (int pos = shift; pos > -int(bits); pos -= int(bits))
        o |= pos >= 0 ? v << uint(pos) : v >> uint(-pos);
    return o & 255u;
}
uint Pack(uint r, uint g, uint b, uint a) { return (r << 24) | (g << 16) | (b << 8) | a; }
uint Direct(uint p, uint rs, uint rb, uint gs, uint gb, uint bs, uint bb, uint as_, uint ab)
{
    uint r = Expand((p >> rs) & ((1u << rb) - 1u), rb);
    uint g = Expand((p >> gs) & ((1u << gb) - 1u), gb);
    uint b = Expand((p >> bs) & ((1u << bb) - 1u), bb);
    uint a = ab != 0u ? Expand((p >> as_) & ((1u << ab) - 1u), ab) : 255u;
    return Pack(r, g, b, a);
}

// op word indices (struct Op)
const int kKind = 0, kX0 = 1, kX1 = 2, kY0 = 3, kY1 = 4, kColorMask = 5, kBlendSrc = 6, kBlendDst = 7,
          kAlphaFunc = 8, kAlphaRef = 9, kStencilFunc = 10, kStencilRef = 11, kStencilFuncMask = 12,
          kStencilWriteMask = 13, kStencilFail = 14, kStencilPass = 15, kColorRgb = 16, kColorA = 17,
          kClearMask = 18, kClearRgba = 19, kClearStencil = 20, kVy = 21, kHeight16 = 22, kKx = 23, kKy = 24,
          kA = 25, kB = 26, kD = 27, kE = 28, kBase = 29, kFormat = 30, kFilter = 31, kWrapX = 32, kWrapY = 33,
          kStride = 34, kLayoutWidth = 35, kLayoutHeight = 36, kPalette = 37;

uint Texel(int op, int tx, int ty)
{
    uint x = uint(tx), y = uint(ty);
    uint row = W(op, kBase) + y * W(op, kStride);
    uint f = W(op, kFormat);
    uint pal = W(op, kPalette);
    if (f == 0u) return Direct(Read16(row + 2u * x), 10u, 5u, 5u, 5u, 0u, 5u, 15u, 1u);
    if (f == 1u) return Pack(255u, 255u, 255u, Expand((Byte(row + x / 8u) >> (7u - x % 8u)) & 1u, 1u));
    if (f == 17u) return Pack(255u, 255u, 255u, Expand((Byte(row + x / 4u) >> (6u - 2u * (x % 4u))) & 3u, 2u));
    if (f == 2u) return Pack(255u, 255u, 255u, Expand((Byte(row + x / 2u) >> (4u - 4u * (x % 2u))) & 15u, 4u));
    if (f == 3u) return Pack(255u, 255u, 255u, Byte(row + x));
    if (f == 4u) return Direct(Byte(row + x), 5u, 3u, 2u, 3u, 0u, 2u, 0u, 0u);
    if (f == 5u) return Direct(Byte(row + x), 4u, 2u, 2u, 2u, 0u, 2u, 6u, 2u);
    if (f == 6u) return Direct(Read16(row + 2u * x), 8u, 4u, 4u, 4u, 0u, 4u, 12u, 4u);
    if (f == 7u) return Direct(Read16(row + 2u * x), 11u, 5u, 5u, 6u, 0u, 5u, 0u, 0u);
    if (f == 14u) return Direct(Read16(pal + 2u * Byte(row + x)), 11u, 5u, 5u, 6u, 0u, 5u, 0u, 0u);
    if (f == 15u) return Direct(Read16(pal + 2u * Byte(row + x)), 8u, 4u, 4u, 4u, 0u, 4u, 12u, 4u);
    if (f == 16u) { uint v = Byte(pal + 4u * Byte(row + x)); return Pack(v, v, v, v); }
    return 0u;
}
int Wrap(int t, int n) { return t >= 0 ? t % n : n - 1 - ((-t - 1) % n); }
uint Wrapped(int op, int tx, int ty)
{
    int w = I(op, kLayoutWidth), h = I(op, kLayoutHeight);
    if (w == 0 || h == 0) return 0u;
    if (W(op, kWrapX) != 0u) tx = Wrap(tx, w); else if (tx < 0 || tx >= w) return 0u;
    if (W(op, kWrapY) != 0u) ty = Wrap(ty, h); else if (ty < 0 || ty >= h) return 0u;
    return Texel(op, tx, ty);
}
bool Compare(uint func, uint v, uint ref)
{
    if (func == 0u) return false;
    if (func == 1u) return v < ref;
    if (func == 2u) return v <= ref;
    if (func == 3u) return v > ref;
    if (func == 4u) return v >= ref;
    if (func == 5u) return v == ref;
    if (func == 6u) return v != ref;
    return true;
}
uint StencilOp(uint o, uint v, uint ref)
{
    if (o == 0u) return 0u;
    if (o == 2u) return ref;
    if (o == 3u) return v == 255u ? v : v + 1u;
    if (o == 4u) return v == 0u ? v : v - 1u;
    if (o == 5u) return ~v & 255u;
    return v;
}
uint Factor(uint f, uint sa, uint da)
{
    if (f == 0u) return 0u;
    if (f == 1u) return 255u;
    if (f == 2u) return sa;
    if (f == 3u) return da;
    if (f == 4u) return 255u - sa;
    if (f == 5u) return 255u - da;
    return 0u;
}

void main()
{
    int x = int(gl_FragCoord.x);
    int y = height - 1 - int(gl_FragCoord.y); // the render target is stored bottom-up
    int band = y / bandRows;
    uvec4 c = uvec4(0u);
    uint stencil = 0u;
    int from = int(texelFetch(bands, band).r), to = int(texelFetch(bands, band + 1).r);
    for (int k = from; k < to; ++k)
    {
        int op = int(texelFetch(bands, bandCount + 1 + k).r);
        if (y < I(op, kY0) || y >= I(op, kY1) || x < I(op, kX0) || x >= I(op, kX1)) continue;
        uint colorMask = W(op, kColorMask);
        if (W(op, kKind) == 0u)
        {
            if ((W(op, kClearMask) & 4u) != 0u)
            {
                uint v = W(op, kClearRgba);
                if ((colorMask & 8u) != 0u) c.x = v >> 24;
                if ((colorMask & 4u) != 0u) c.y = (v >> 16) & 255u;
                if ((colorMask & 2u) != 0u) c.z = (v >> 8) & 255u;
                if ((colorMask & 1u) != 0u) c.w = v & 255u;
            }
            if ((W(op, kClearMask) & 2u) != 0u)
            {
                uint wm = W(op, kStencilWriteMask);
                stencil = (stencil & ~wm & 255u) | (W(op, kClearStencil) & wm);
            }
            continue;
        }
        int rely = y * 16 - I(op, kVy);
        if (rely < 0 || rely >= I(op, kHeight16)) continue;
        int dx = x - I(op, kX0);
        int sx = I(op, kKx) + I(op, kA) * dx + I(op, kB) * y;
        int sy = I(op, kKy) + I(op, kD) * dx + I(op, kE) * y;
        uint t;
        if (W(op, kFilter) == 0u)
            t = Wrapped(op, sx >> 8, sy >> 8);
        else
        {
            int tx = sx >> 8, ty = sy >> 8;
            uint fx = uint(sx - tx * 256), fy = uint(sy - ty * 256);
            uint t00 = Wrapped(op, tx, ty), t10 = Wrapped(op, tx + 1, ty);
            uint t01 = Wrapped(op, tx, ty + 1), t11 = Wrapped(op, tx + 1, ty + 1);
            uint w11 = (fx * fy) >> 8;
            uint w00 = 256u - fx - fy + w11, w10 = fx - w11, w01 = fy - w11;
            t = 0u;
            for (int sh = 24; sh >= 0; sh -= 8)
            {
                uint v = ((((t00 >> sh) & 255u) * w00) >> 8) + ((((t10 >> sh) & 255u) * w10) >> 8) +
                         ((((t01 >> sh) & 255u) * w01) >> 8) + ((((t11 >> sh) & 255u) * w11) >> 8);
                t |= v << sh;
            }
        }
        uint rgb = W(op, kColorRgb);
        uvec4 s = uvec4(Mul(t >> 24, rgb >> 16), Mul((t >> 16) & 255u, (rgb >> 8) & 255u), Mul((t >> 8) & 255u, rgb & 255u),
                        Mul(t & 255u, W(op, kColorA)));
        if (!Compare(W(op, kAlphaFunc), s.w, W(op, kAlphaRef))) continue;
        uint fm = W(op, kStencilFuncMask), ref = W(op, kStencilRef), wm = W(op, kStencilWriteMask);
        bool pass = Compare(W(op, kStencilFunc), stencil & fm, ref & fm);
        uint updated = StencilOp(pass ? W(op, kStencilPass) : W(op, kStencilFail), stencil, ref);
        stencil = (stencil & ~wm & 255u) | (updated & wm);
        if (!pass) continue;
        uint sf = Factor(W(op, kBlendSrc), s.w, c.w), df = Factor(W(op, kBlendDst), s.w, c.w);
        uvec4 o = min(uvec4(Mul(s.x, sf) + Mul(c.x, df), Mul(s.y, sf) + Mul(c.y, df), Mul(s.z, sf) + Mul(c.z, df),
                            Mul(s.w, sf) + Mul(c.w, df)), uvec4(255u));
        if ((colorMask & 8u) != 0u) c.x = o.x;
        if ((colorMask & 4u) != 0u) c.y = o.y;
        if ((colorMask & 2u) != 0u) c.z = o.z;
        if ((colorMask & 1u) != 0u) c.w = o.w;
    }
    fragColor = uvec4(c.z, c.y, c.x, 255u); // B, G, R, A bytes = 0xFFRRGGBB little-endian
}
)";

double Now()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double Median(std::vector<double> v)
{
    if (v.empty())
        return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

GLuint Compile(GLenum kind, const char* text)
{
    const GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &text, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "shader: %s\n", log);
    }
    return s;
}

struct TextureBuffer
{
    GLuint buffer = 0, texture = 0;
    void Create(GLenum format)
    {
        glGenBuffers(1, &buffer);
        glGenTextures(1, &texture);
        glBindBuffer(GL_TEXTURE_BUFFER, buffer);
        glBindTexture(GL_TEXTURE_BUFFER, texture);
        glTexBuffer(GL_TEXTURE_BUFFER, format, buffer);
    }
    void Upload(const void* data, size_t bytes)
    {
        glBindBuffer(GL_TEXTURE_BUFFER, buffer);
        glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(bytes), data, GL_STREAM_DRAW);
    }
};

} // namespace

int main(int argc, char** argv)
{
    int repeat = 10;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--repeat") && i + 1 < argc)
            repeat = std::atoi(argv[++i]);
        else
            files.push_back(argv[i]);
    }
    // A headless 4.1 core context
    CGLPixelFormatAttribute attributes[] = {kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
                                            kCGLPFAAccelerated, kCGLPFAAllowOfflineRenderers,
                                            (CGLPixelFormatAttribute)0};
    CGLPixelFormatObj pixelFormat = nullptr;
    GLint formats = 0;
    if (CGLChoosePixelFormat(attributes, &pixelFormat, &formats) != kCGLNoError || !pixelFormat)
    {
        std::fprintf(stderr, "no OpenGL 4.1 pixel format\n");
        return 2;
    }
    CGLContextObj context = nullptr;
    CGLCreateContext(pixelFormat, nullptr, &context);
    CGLSetCurrentContext(context);
    GLint maxTexels = 0;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxTexels);
    std::printf("OpenGL %s, %s; texture buffer up to %d texels\n", glGetString(GL_VERSION), glGetString(GL_RENDERER),
                maxTexels);

    const double c0 = Now();
    const GLuint program = glCreateProgram();
    glAttachShader(program, Compile(GL_VERTEX_SHADER, kVertex));
    glAttachShader(program, Compile(GL_FRAGMENT_SHADER, kFragment));
    glBindFragDataLocation(program, 0, "fragColor");
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[4096];
        glGetProgramInfoLog(program, sizeof log, nullptr, log);
        std::fprintf(stderr, "link: %s\n", log);
        return 2;
    }
    glUseProgram(program);
    std::printf("shaders compiled and linked in %.1f ms\n", Now() - c0);
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    TextureBuffer memory, ops, bands;
    memory.Create(GL_R8UI);
    ops.Create(GL_R32UI);
    bands.Create(GL_R32UI);
    // The memory image: RAM_G every frame, the ROM once (glBufferSubData)
    {
        std::vector<uint8_t> image(EveSnap::kMemorySize, 0);
        std::memcpy(image.data() + EveSnap::kRomBase, EveSnap::Rom().data(), EveSnap::Rom().size());
        memory.Upload(image.data(), image.size());
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, memory.texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, ops.texture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, bands.texture);
    glUniform1i(glGetUniformLocation(program, "memory"), 0);
    glUniform1i(glGetUniformLocation(program, "ops"), 1);
    glUniform1i(glGetUniformLocation(program, "bands"), 2);
    glUniform1i(glGetUniformLocation(program, "bandRows"), static_cast<GLint>(EveSnap::Flattened::kBand));

    GLuint target = 0, fbo = 0, query = 0;
    uint32_t targetW = 0, targetH = 0;
    glGenQueries(1, &query);
    int total = 0, supported = 0, exact = 0;
    std::vector<double> uploadMs, gpuMs, wallMs, readMs, cpuMs;
    std::vector<uint32_t> host;
    for (const std::string& file : files)
    {
        EveSnap::Snapshot s;
        if (!EveSnap::Load(file, s))
            continue;
        ++total;
        EveSnap::Flattened f = EveSnap::Flatten(s);
        // GLSL 4.1 has no 64-bit integers: eve-emu's line distance (64-bit products and a
        // 64-bit division) is not ported; points and rectangles fit in 32 bits but are
        // left out too - this harness covers the bitmap and clear ops
        if (f.supported && f.primitiveOps > 0)
        {
            f.supported = false;
            f.reason = "antialiased primitives (not ported to GLSL 4.1)";
        }
        if (!f.supported)
        {
            std::printf("%s: unsupported (%s)\n", file.c_str(), f.reason.c_str());
            continue;
        }
        ++supported;
        if (s.width != targetW || s.height != targetH)
        {
            targetW = s.width;
            targetH = s.height;
            if (!target)
            {
                glGenTextures(1, &target);
                glGenFramebuffers(1, &fbo);
            }
            glBindTexture(GL_TEXTURE_2D, target);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI, static_cast<GLsizei>(s.width), static_cast<GLsizei>(s.height), 0,
                         GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, nullptr);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                std::fprintf(stderr, "framebuffer incomplete\n");
                return 2;
            }
            glViewport(0, 0, static_cast<GLsizei>(s.width), static_cast<GLsizei>(s.height));
        }
        double t0 = Now();
        glBindBuffer(GL_TEXTURE_BUFFER, memory.buffer);
        glBufferSubData(GL_TEXTURE_BUFFER, 0, EveSnap::kRamGSize, s.ramG.data());
        std::vector<EveSnap::Op> opsCopy = f.ops;
        if (opsCopy.empty())
            opsCopy.push_back(EveSnap::Op{});
        ops.Upload(opsCopy.data(), opsCopy.size() * sizeof(EveSnap::Op));
        std::vector<uint32_t> bandData = f.bandStart;
        bandData.insert(bandData.end(), f.bandOps.begin(), f.bandOps.end());
        bands.Upload(bandData.data(), bandData.size() * 4);
        glUniform1i(glGetUniformLocation(program, "bandCount"), static_cast<GLint>(f.bandStart.size() - 1));
        glUniform1i(glGetUniformLocation(program, "height"), static_cast<GLint>(s.height));
        glFinish();
        const double upload = Now() - t0;
        std::vector<double> gpu, wall;
        for (int r = 0; r < repeat; ++r)
        {
            t0 = Now();
            glBeginQuery(GL_TIME_ELAPSED, query);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glEndQuery(GL_TIME_ELAPSED);
            glFinish();
            wall.push_back(Now() - t0);
            GLuint64 ns = 0;
            glGetQueryObjectui64v(query, GL_QUERY_RESULT, &ns);
            gpu.push_back(static_cast<double>(ns) / 1e6);
        }
        t0 = Now();
        host.resize(static_cast<size_t>(s.width) * s.height);
        // rows bottom-up in GL: read the whole image, then flip
        std::vector<uint32_t> raw(host.size());
        glReadPixels(0, 0, static_cast<GLsizei>(s.width), static_cast<GLsizei>(s.height), GL_RGBA_INTEGER,
                     GL_UNSIGNED_BYTE, raw.data());
        for (uint32_t y = 0; y < s.height; ++y)
            std::memcpy(&host[static_cast<size_t>(y) * s.width], &raw[static_cast<size_t>(s.height - 1 - y) * s.width],
                        s.width * 4);
        const double read = Now() - t0;
        size_t diff = 0, first = SIZE_MAX;
        for (size_t p = 0; p < host.size(); ++p)
            if (host[p] != s.picture[p])
            {
                if (first == SIZE_MAX)
                    first = p;
                ++diff;
            }
        if (!diff)
            ++exact;
        std::printf("%s: ops %zu, %s; upload %.3f ms, GPU %.3f ms, draw-to-finish %.3f ms, readback %.3f ms; eve-emu %.2f ms\n",
                    file.c_str(), f.ops.size(), diff ? "DIFFERS" : "bit-exact", upload, Median(gpu), Median(wall), read,
                    s.cpuUs / 1000.0);
        if (diff)
            std::printf("   %zu pixels differ, first at (%zu, %zu): GL %08X eve-emu %08X\n", diff, first % s.width,
                        first / s.width, host[first], s.picture[first]);
        uploadMs.push_back(upload);
        gpuMs.push_back(Median(gpu));
        wallMs.push_back(Median(wall));
        readMs.push_back(read);
        cpuMs.push_back(s.cpuUs / 1000.0);
    }
    std::printf("\nframes %d, expressible %d, bit-exact %d\n", total, supported, exact);
    if (supported)
        std::printf("median per frame: upload %.3f ms, GPU %.3f ms, draw-to-finish %.3f ms, readback %.3f ms; eve-emu %.2f ms\n",
                    Median(uploadMs), Median(gpuMs), Median(wallMs), Median(readMs), Median(cpuMs));
    CGLSetCurrentContext(nullptr);
    CGLDestroyContext(context);
    CGLDestroyPixelFormat(pixelFormat);
    return exact == supported ? 0 : 1;
}
