// eve-accel 06: time the two antialiasing models on the GPU (Metal) and on one CPU core.
//
// Scenes of antialiased lines and points (a point is a segment of length 0) on 1024 x 768,
// each pixel blends every shape that reaches it. Model 0: eve-emu's distance table; model 1:
// 256 subsamples per pixel. Reports GPU time per frame, the CPU time of the same loops, and
// whether GPU and CPU agree pixel for pixel.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <vector>

namespace EveLib
{
uint32_t AntialiasAlpha(uint32_t radius, uint32_t distance); // eve-aa-table.cpp
}

namespace
{

struct Shape
{
    int32_t ax, ay, bx, by;
    uint32_t radius, color, alpha;
    int32_t x0, y0, x1, y1;
    uint32_t pad0;
};
struct Params
{
    uint32_t width, height, band, model;
};

constexpr uint32_t kW = 1024, kH = 768, kBand = 16;

double Now()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
double Median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : v[v.size() / 2];
}

uint32_t Mul(uint32_t a, uint32_t b)
{
    const uint32_t t = a * b + 127;
    return (t + 1 + (t >> 8)) >> 8;
}
int64_t FloorDivide(int64_t v, int64_t d)
{
    const int64_t q = v / d;
    return (v % d != 0 && v < 0) ? q - 1 : q;
}
uint32_t IntSqrt(uint64_t v)
{
    uint64_t r = static_cast<uint64_t>(std::sqrt(static_cast<double>(v)));
    while (r * r > v)
        --r;
    while ((r + 1) * (r + 1) <= v)
        ++r;
    return static_cast<uint32_t>(r);
}
uint32_t SegmentDistance(int64_t px, int64_t py, int64_t ax, int64_t ay, int64_t bx, int64_t by)
{
    const int64_t qx = px - ax, qy = py - ay, dx = bx - ax, dy = by - ay;
    const int64_t length2 = dx * dx + dy * dy;
    int64_t fx = 0, fy = 0;
    if (length2 > 0)
    {
        int64_t dot = qx * dx + qy * dy;
        dot = dot < 0 ? 0 : (dot > length2 ? length2 : dot);
        fx = FloorDivide(dot * dx, length2);
        fy = FloorDivide(dot * dy, length2);
    }
    const int64_t ex = qx - fx, ey = qy - fy;
    return IntSqrt(static_cast<uint64_t>(ex * ex + ey * ey));
}
float Distance2(float px, float py, float ax, float ay, float bx, float by)
{
    const float dx = bx - ax, dy = by - ay;
    const float length2 = dx * dx + dy * dy;
    float t = 0;
    if (length2 > 0)
        t = std::min(std::max(((px - ax) * dx + (py - ay) * dy) / length2, 0.0f), 1.0f);
    const float ex = px - (ax + t * dx), ey = py - (ay + t * dy);
    return ex * ex + ey * ey;
}

// The same per-pixel loop as the kernel, on one core
void RenderCpu(const std::vector<Shape>& shapes, const std::vector<uint32_t>& bandStart,
               const std::vector<uint32_t>& bandShapes, uint32_t model, std::vector<uint32_t>& out)
{
    out.assign(kW * kH, 0);
    for (uint32_t y = 0; y < kH; ++y)
        for (uint32_t x = 0; x < kW; ++x)
        {
            uint32_t c[3] = {0, 0, 0};
            const uint32_t band = y / kBand;
            for (uint32_t k = bandStart[band]; k < bandStart[band + 1]; ++k)
            {
                const Shape& s = shapes[bandShapes[k]];
                const int32_t xi = static_cast<int32_t>(x), yi = static_cast<int32_t>(y);
                if (xi < s.x0 || xi >= s.x1 || yi < s.y0 || yi >= s.y1)
                    continue;
                uint32_t coverage;
                if (model == 0)
                    coverage = EveLib::AntialiasAlpha(s.radius, SegmentDistance(xi * 16, yi * 16, s.ax, s.ay, s.bx, s.by));
                else
                {
                    const float r2 = static_cast<float>(s.radius) * static_cast<float>(s.radius);
                    uint32_t inside = 0;
                    // SIMD-CANDIDATE(256 subsample tests per pixel)
                    for (int j = 0; j < 16; ++j)
                        for (int i = 0; i < 16; ++i)
                            inside += Distance2(static_cast<float>(xi * 16 + i) - 7.5f, static_cast<float>(yi * 16 + j) - 7.5f,
                                                static_cast<float>(s.ax), static_cast<float>(s.ay), static_cast<float>(s.bx),
                                                static_cast<float>(s.by)) <= r2;
                    coverage = (inside * 255 + 128) / 256;
                }
                if (coverage == 0)
                    continue;
                const uint32_t a = Mul(s.alpha, coverage), inv = 255 - a;
                const uint32_t src[3] = {s.color >> 16, (s.color >> 8) & 255, s.color & 255};
                for (int i = 0; i < 3; ++i)
                    c[i] = std::min<uint32_t>(Mul(src[i], a) + Mul(c[i], inv), 255);
            }
            out[y * kW + x] = 0xFF000000u | (c[0] << 16) | (c[1] << 8) | c[2];
        }
}

std::vector<Shape> Scene(uint32_t lines, uint32_t points, uint32_t seed)
{
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> px(0, kW * 16 - 1), py(0, kH * 16 - 1), length(-200 * 16, 200 * 16);
    std::uniform_int_distribution<uint32_t> width(8, 64), pointSize(16, 128), color(0, 0xFFFFFF), alpha(64, 255);
    std::vector<Shape> shapes;
    for (uint32_t i = 0; i < lines + points; ++i)
    {
        Shape s{};
        s.ax = px(rng);
        s.ay = py(rng);
        if (i < lines)
        {
            s.bx = std::clamp(s.ax + length(rng), 0, static_cast<int>(kW * 16 - 1));
            s.by = std::clamp(s.ay + length(rng), 0, static_cast<int>(kH * 16 - 1));
            s.radius = width(rng);
        }
        else
        {
            s.bx = s.ax;
            s.by = s.ay;
            s.radius = pointSize(rng);
        }
        s.color = color(rng);
        s.alpha = alpha(rng);
        const int reach = static_cast<int>(s.radius) + 16;
        s.x0 = std::max(0, (std::min(s.ax, s.bx) - reach) / 16);
        s.x1 = std::min(static_cast<int>(kW), (std::max(s.ax, s.bx) + reach) / 16 + 1);
        s.y0 = std::max(0, (std::min(s.ay, s.by) - reach) / 16);
        s.y1 = std::min(static_cast<int>(kH), (std::max(s.ay, s.by) + reach) / 16 + 1);
        shapes.push_back(s);
    }
    return shapes;
}

} // namespace

int main()
{
    @autoreleasepool
    {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        std::ifstream file(EVE_METAL_AA_SOURCE);
        std::stringstream source;
        source << file.rdbuf();
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:@(source.str().c_str()) options:[MTLCompileOptions new] error:&error];
        if (!library)
        {
            std::fprintf(stderr, "compile: %s\n", error.localizedDescription.UTF8String);
            return 2;
        }
        id<MTLComputePipelineState> pipeline =
            [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"RenderLines"] error:&error];
        id<MTLCommandQueue> queue = [device newCommandQueue];
        // The table, dense: rows r = 1..61 (d = 0..r + 14), then the edge rows for r >= 62
        std::vector<uint8_t> table(61 * 80 + 200, 0);
        for (uint32_t r = 1; r < 62; ++r)
            for (uint32_t d = 0; d <= r + 14; ++d)
                table[(r - 1) * 80 + d] = static_cast<uint8_t>(EveLib::AntialiasAlpha(r, d));
        for (uint32_t i = 0; i <= 28; ++i) // kEdgeAlpha[i] = alpha at r + 14 - d = i, for r >= 62
            table[61 * 80 + i] = static_cast<uint8_t>(EveLib::AntialiasAlpha(100, 100 + 14 - i));
        id<MTLBuffer> tableBuffer = [device newBufferWithBytes:table.data() length:table.size() options:MTLResourceStorageModeShared];
        id<MTLBuffer> out = [device newBufferWithLength:kW * kH * 4 options:MTLResourceStorageModeShared];

        struct SceneSpec
        {
            const char* name;
            uint32_t lines, points;
        };
        const SceneSpec specs[] = {{"64 lines", 64, 0}, {"500 lines + 500 points", 500, 500}, {"2000 lines + 2000 points", 2000, 2000}};
        std::printf("%-26s %-6s %12s %14s %12s %10s\n", "scene", "model", "GPU ms", "CPU 1 core ms", "GPU = CPU", "pixel refs");
        for (const SceneSpec& spec : specs)
        {
            const std::vector<Shape> shapes = Scene(spec.lines, spec.points, 1234);
            std::vector<uint32_t> bandStart, bandShapes;
            uint64_t pixelRefs = 0;
            for (uint32_t band = 0; band < kH / kBand; ++band)
            {
                bandStart.push_back(static_cast<uint32_t>(bandShapes.size()));
                for (uint32_t i = 0; i < shapes.size(); ++i)
                    if (shapes[i].y0 < static_cast<int>((band + 1) * kBand) && shapes[i].y1 > static_cast<int>(band * kBand))
                        bandShapes.push_back(i);
            }
            bandStart.push_back(static_cast<uint32_t>(bandShapes.size()));
            for (const Shape& s : shapes)
                pixelRefs += static_cast<uint64_t>(s.x1 - s.x0) * static_cast<uint64_t>(s.y1 - s.y0);
            id<MTLBuffer> shapeBuffer = [device newBufferWithBytes:shapes.data() length:shapes.size() * sizeof(Shape) options:MTLResourceStorageModeShared];
            id<MTLBuffer> startBuffer = [device newBufferWithBytes:bandStart.data() length:bandStart.size() * 4 options:MTLResourceStorageModeShared];
            id<MTLBuffer> listBuffer = [device newBufferWithBytes:bandShapes.data() length:std::max<size_t>(4, bandShapes.size() * 4) options:MTLResourceStorageModeShared];
            for (uint32_t model = 0; model < 2; ++model)
            {
                const Params params{kW, kH, kBand, model};
                std::vector<double> gpu;
                for (int r = 0; r < 10; ++r)
                {
                    id<MTLCommandBuffer> cb = [queue commandBuffer];
                    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
                    [enc setComputePipelineState:pipeline];
                    [enc setBuffer:shapeBuffer offset:0 atIndex:0];
                    [enc setBuffer:startBuffer offset:0 atIndex:1];
                    [enc setBuffer:listBuffer offset:0 atIndex:2];
                    [enc setBuffer:tableBuffer offset:0 atIndex:3];
                    [enc setBuffer:out offset:0 atIndex:4];
                    [enc setBytes:&params length:sizeof(params) atIndex:5];
                    [enc dispatchThreads:MTLSizeMake(kW, kH, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
                    [enc endEncoding];
                    [cb commit];
                    [cb waitUntilCompleted];
                    gpu.push_back((cb.GPUEndTime - cb.GPUStartTime) * 1000.0);
                }
                std::vector<uint32_t> cpu;
                const double t0 = Now();
                RenderCpu(shapes, bandStart, bandShapes, model, cpu);
                const double cpuMs = Now() - t0;
                size_t differ = 0;
                const uint32_t* g = static_cast<const uint32_t*>(out.contents);
                for (size_t p = 0; p < cpu.size(); ++p)
                    differ += g[p] != cpu[p];
                char agree[32];
                std::snprintf(agree, sizeof agree, differ ? "%zu differ" : "yes", differ);
                std::printf("%-26s %-6s %12.3f %14.1f %12s %10llu\n", spec.name, model ? "256" : "table", Median(gpu), cpuMs,
                            agree, (unsigned long long)pixelRefs);
            }
        }
    }
    return 0;
}
