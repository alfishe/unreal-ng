// eve-accel 04/07: evaluate snapshot op lists with Metal and compare with eve-emu's picture.
//
//   metal-render [--repeat N] [--per-op] [--overhead] <snapshot>...
//
//   default     one dispatch per frame (RenderFrame): every pixel runs the ops of its band
//   --per-op    one dispatch per op (RenderOp) over its rectangle: the "kernel per
//               primitive" model, pixel state in device memory between dispatches
//   --overhead  time an empty dispatch (commit + wait) and N small dispatches in one buffer
//   --bands N   04b: one dispatch per band of N lines (N = 1: a dispatch per line), all in
//               one command buffer (the GPU runs them back to back)
//   --sync      04b: with --bands, one command buffer per band, committed and waited for
//               before the next (what a CPU that changes memory between bands must do)
//   --sub       04b: with --bands, 256 threads per pixel (RenderLineSub, sub-pixel AA)
//   --line-threads 08: one GPU thread per line walking the whole line (RenderLineSeq)
//   --metallib F 09: load the kernels from a precompiled library instead of compiling the
//               source (start-up cost of the GPU path)
//   --concurrent 04b: with --bands, a concurrent encoder (no barrier between the band
//               dispatches; they write different rows)
//
// Times per frame: flatten (CPU), upload (RAM_G 1 MB + op list into shared buffers),
// GPU (command buffer GPUStartTime..GPUEndTime), submit-to-done wall time, readback (copy of
// the picture out of the shared buffer). Apple GPUs share memory with the CPU: on a discrete
// GPU the upload and readback are PCIe transfers.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "eve-snap.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>

namespace
{

struct Params
{
    uint32_t width, height, band, opCount;
    uint32_t rowBase, rowCount, pad0, pad1;
};
struct OpParams
{
    uint32_t width, index, x0, y0;
};

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

} // namespace

int main(int argc, char** argv)
{
    @autoreleasepool
    {
        int repeat = 10;
        bool perOp = false, overhead = false, sync = false, sub = false, concurrent = false, lineThreads = false;
        uint32_t bandRows = 0; // 0: the whole frame in one dispatch
        const char* metallib = nullptr;
        std::vector<std::string> files;
        for (int i = 1; i < argc; ++i)
        {
            if (!std::strcmp(argv[i], "--repeat") && i + 1 < argc)
                repeat = std::atoi(argv[++i]);
            else if (!std::strcmp(argv[i], "--per-op"))
                perOp = true;
            else if (!std::strcmp(argv[i], "--overhead"))
                overhead = true;
            else if (!std::strcmp(argv[i], "--bands") && i + 1 < argc)
                bandRows = static_cast<uint32_t>(std::atoi(argv[++i]));
            else if (!std::strcmp(argv[i], "--sync"))
                sync = true;
            else if (!std::strcmp(argv[i], "--sub"))
                sub = true;
            else if (!std::strcmp(argv[i], "--concurrent"))
                concurrent = true;
            else if (!std::strcmp(argv[i], "--line-threads"))
                lineThreads = true;
            else if (!std::strcmp(argv[i], "--metallib") && i + 1 < argc)
                metallib = argv[++i];
            else
                files.push_back(argv[i]);
        }
        const double deviceStart = Now();
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        std::printf("start-up: device %.1f ms\n", Now() - deviceStart);
        if (!device)
        {
            std::fprintf(stderr, "no Metal device\n");
            return 2;
        }
        const std::string sourceText = EveSnap::InlineCore(EVE_METAL_SOURCE, EVE_CORE_SOURCE);
        NSError* error = nil;
        MTLCompileOptions* options = [MTLCompileOptions new];
        const double compileStart = Now();
        id<MTLLibrary> library =
            metallib ? [device newLibraryWithURL:[NSURL fileURLWithPath:@(metallib)] error:&error]
                     : [device newLibraryWithSource:[NSString stringWithUTF8String:sourceText.c_str()]
                                            options:options
                                              error:&error];
        std::printf("start-up: library %s in %.1f ms\n", metallib ? "loaded" : "compiled from source", Now() - compileStart);
        if (!library)
        {
            std::fprintf(stderr, "Metal compile failed: %s\n", error.localizedDescription.UTF8String);
            return 2;
        }
        auto pipeline = [&](const char* name) {
            NSError* e = nil;
            id<MTLComputePipelineState> p =
                [device newComputePipelineStateWithFunction:[library newFunctionWithName:@(name)] error:&e];
            if (!p)
                std::fprintf(stderr, "pipeline %s: %s\n", name, e.localizedDescription.UTF8String);
            return p;
        };
        id<MTLComputePipelineState> renderFrame = pipeline("RenderFrame");
        id<MTLComputePipelineState> renderOp = pipeline("RenderOp");
        id<MTLComputePipelineState> clearState = pipeline("ClearState");
        id<MTLComputePipelineState> resolve = pipeline("Resolve");
        id<MTLComputePipelineState> empty = pipeline("Empty");
        id<MTLComputePipelineState> renderLineSub = pipeline("RenderLineSub");
        id<MTLComputePipelineState> renderLineSeq = pipeline("RenderLineSeq");
        std::printf("device: %s; kernels compiled in %.1f ms; RenderFrame: %lu threads per group max, SIMD width %lu\n",
                    device.name.UTF8String, Now() - compileStart,
                    (unsigned long)renderFrame.maxTotalThreadsPerThreadgroup,
                    (unsigned long)renderFrame.threadExecutionWidth);
        id<MTLCommandQueue> queue = [device newCommandQueue];

        if (overhead)
        {
            id<MTLBuffer> dummy = [device newBufferWithLength:4 options:MTLResourceStorageModeShared];
            std::vector<double> wall, gpu;
            for (int r = 0; r < 200; ++r)
            {
                const double t0 = Now();
                id<MTLCommandBuffer> cb = [queue commandBuffer];
                id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
                [enc setComputePipelineState:empty];
                [enc setBuffer:dummy offset:0 atIndex:0];
                [enc dispatchThreads:MTLSizeMake(64, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                [enc endEncoding];
                [cb commit];
                [cb waitUntilCompleted];
                wall.push_back(Now() - t0);
                gpu.push_back((cb.GPUEndTime - cb.GPUStartTime) * 1000.0);
            }
            std::printf("empty dispatch, commit + wait: median %.3f ms wall, %.3f ms GPU\n", Median(wall), Median(gpu));
            for (int n : {10, 100, 1000})
            {
                std::vector<double> w2, g2;
                for (int r = 0; r < 20; ++r)
                {
                    const double t0 = Now();
                    id<MTLCommandBuffer> cb = [queue commandBuffer];
                    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
                    [enc setComputePipelineState:empty];
                    [enc setBuffer:dummy offset:0 atIndex:0];
                    for (int k = 0; k < n; ++k)
                        [enc dispatchThreads:MTLSizeMake(64, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                    [enc endEncoding];
                    [cb commit];
                    [cb waitUntilCompleted];
                    w2.push_back(Now() - t0);
                    g2.push_back((cb.GPUEndTime - cb.GPUStartTime) * 1000.0);
                }
                std::printf("%5d serial empty dispatches in one encoder: median %.3f ms wall, %.3f ms GPU (%.2f us each)\n",
                            n, Median(w2), Median(g2), Median(g2) * 1000.0 / n);
            }
        }

        // RAM_G + ROM in one buffer; the ROM part is written once, RAM_G every frame
        id<MTLBuffer> ram = [device newBufferWithLength:EveSnap::kMemorySize options:MTLResourceStorageModeShared];
        std::memset(ram.contents, 0, EveSnap::kMemorySize);
        std::memcpy(static_cast<uint8_t*>(ram.contents) + EveSnap::kRomBase, EveSnap::Rom().data(), EveSnap::Rom().size());
        id<MTLBuffer> out = nil, stateRgba = nil, stateStencil = nil;
        id<MTLBuffer> aa = [device newBufferWithBytes:EveSnap::AaTable() length:EveSnap::kAaTableSize
                                              options:MTLResourceStorageModeShared];
        uint32_t outPixels = 0;
        int total = 0, supported = 0, exact = 0;
        std::vector<double> flattenMs, uploadMs, gpuMs, wallMs, readMs, cpuMs, opsCount;
        std::vector<uint32_t> host;
        for (const std::string& file : files)
        {
            EveSnap::Snapshot s;
            if (!EveSnap::Load(file, s))
                continue;
            ++total;
            double t0 = Now();
            const EveSnap::Flattened f = EveSnap::Flatten(s);
            const double flatten = Now() - t0;
            if (!f.supported)
            {
                std::printf("%s: unsupported (%s)\n", file.c_str(), f.reason.c_str());
                continue;
            }
            ++supported;
            const uint32_t pixels = s.width * s.height;
            if (pixels > outPixels)
            {
                outPixels = pixels;
                out = [device newBufferWithLength:pixels * 4 options:MTLResourceStorageModeShared];
                stateRgba = [device newBufferWithLength:pixels * 4 options:MTLResourceStorageModePrivate];
                stateStencil = [device newBufferWithLength:pixels options:MTLResourceStorageModePrivate];
            }
            // Upload: RAM_G and the op list (every frame, as an emulator would)
            t0 = Now();
            std::memcpy(ram.contents, s.ramG.data(), EveSnap::kRamGSize);
            std::vector<EveSnap::Op> opsCopy = f.ops;
            if (opsCopy.empty())
                opsCopy.push_back(EveSnap::Op{});
            id<MTLBuffer> ops = [device newBufferWithBytes:opsCopy.data()
                                                    length:opsCopy.size() * sizeof(EveSnap::Op)
                                                   options:MTLResourceStorageModeShared];
            id<MTLBuffer> bandStart = [device newBufferWithBytes:f.bandStart.data()
                                                          length:f.bandStart.size() * 4
                                                         options:MTLResourceStorageModeShared];
            std::vector<uint32_t> bandOps = f.bandOps;
            if (bandOps.empty())
                bandOps.push_back(0);
            id<MTLBuffer> bandList = [device newBufferWithBytes:bandOps.data()
                                                         length:bandOps.size() * 4
                                                        options:MTLResourceStorageModeShared];
            const double upload = Now() - t0;
            const Params params{s.width, s.height, EveSnap::Flattened::kBand, static_cast<uint32_t>(f.ops.size()),
                                0, s.height, 0, 0};
            std::vector<double> gpu, wall;
            if (bandRows > 0 && !perOp)
            {
                // 04b: bands of bandRows lines, each its own dispatch
                for (int r = 0; r < repeat; ++r)
                {
                    t0 = Now();
                    double gpuSum = 0;
                    id<MTLCommandBuffer> cb = nil;
                    id<MTLComputeCommandEncoder> enc = nil;
                    for (uint32_t row = 0; row < s.height; row += bandRows)
                    {
                        if (!cb)
                        {
                            cb = [queue commandBuffer];
                            enc = concurrent ? [cb computeCommandEncoderWithDispatchType:MTLDispatchTypeConcurrent]
                                             : [cb computeCommandEncoder];
                            [enc setComputePipelineState:sub ? renderLineSub : renderFrame];
                            [enc setBuffer:ram offset:0 atIndex:0];
                            [enc setBuffer:ops offset:0 atIndex:1];
                            [enc setBuffer:bandStart offset:0 atIndex:2];
                            [enc setBuffer:bandList offset:0 atIndex:3];
                            [enc setBuffer:out offset:0 atIndex:4];
                            [enc setBuffer:aa offset:0 atIndex:6];
                        }
                        Params band = params;
                        band.rowBase = row;
                        band.rowCount = std::min(bandRows, s.height - row);
                        [enc setBytes:&band length:sizeof(band) atIndex:5];
                        if (sub)
                            [enc dispatchThreadgroups:MTLSizeMake(s.width, band.rowCount, 1)
                                threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
                        else
                            [enc dispatchThreads:MTLSizeMake(s.width, band.rowCount, 1)
                                threadsPerThreadgroup:MTLSizeMake(16, std::min<uint32_t>(16, band.rowCount), 1)];
                        if (sync || row + bandRows >= s.height)
                        {
                            [enc endEncoding];
                            [cb commit];
                            [cb waitUntilCompleted];
                            gpuSum += (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
                            cb = nil;
                        }
                    }
                    wall.push_back(Now() - t0);
                    gpu.push_back(gpuSum);
                }
            }
            for (int r = 0; r < (bandRows > 0 && !perOp ? 0 : repeat); ++r)
            {
                t0 = Now();
                id<MTLCommandBuffer> cb = [queue commandBuffer];
                id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
                if (!perOp)
                {
                    [enc setComputePipelineState:lineThreads ? renderLineSeq : renderFrame];
                    [enc setBuffer:ram offset:0 atIndex:0];
                    [enc setBuffer:ops offset:0 atIndex:1];
                    [enc setBuffer:bandStart offset:0 atIndex:2];
                    [enc setBuffer:bandList offset:0 atIndex:3];
                    [enc setBuffer:out offset:0 atIndex:4];
                    [enc setBytes:&params length:sizeof(params) atIndex:5];
                    [enc setBuffer:aa offset:0 atIndex:6];
                    if (lineThreads)
                        [enc dispatchThreads:MTLSizeMake(s.height, 1, 1) threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
                    else
                        [enc dispatchThreads:MTLSizeMake(s.width, s.height, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
                }
                else
                {
                    [enc setComputePipelineState:clearState];
                    [enc setBuffer:stateRgba offset:0 atIndex:0];
                    [enc setBuffer:stateStencil offset:0 atIndex:1];
                    [enc setBytes:&params length:sizeof(params) atIndex:2];
                    [enc dispatchThreads:MTLSizeMake(s.width, s.height, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
                    [enc setComputePipelineState:renderOp];
                    [enc setBuffer:ram offset:0 atIndex:0];
                    [enc setBuffer:ops offset:0 atIndex:1];
                    [enc setBuffer:stateRgba offset:0 atIndex:2];
                    [enc setBuffer:stateStencil offset:0 atIndex:3];
                    [enc setBuffer:aa offset:0 atIndex:5];
                    for (uint32_t k = 0; k < f.ops.size(); ++k)
                    {
                        const EveSnap::Op& op = f.ops[k];
                        const OpParams p{s.width, k, static_cast<uint32_t>(op.x0), static_cast<uint32_t>(op.y0)};
                        [enc setBytes:&p length:sizeof(p) atIndex:4];
                        [enc dispatchThreads:MTLSizeMake(static_cast<NSUInteger>(op.x1 - op.x0),
                                                         static_cast<NSUInteger>(op.y1 - op.y0), 1)
                            threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
                    }
                    [enc setComputePipelineState:resolve];
                    [enc setBuffer:stateRgba offset:0 atIndex:0];
                    [enc setBuffer:out offset:0 atIndex:1];
                    [enc setBytes:&params length:sizeof(params) atIndex:2];
                    [enc dispatchThreads:MTLSizeMake(s.width, s.height, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
                }
                [enc endEncoding];
                [cb commit];
                [cb waitUntilCompleted];
                wall.push_back(Now() - t0);
                gpu.push_back((cb.GPUEndTime - cb.GPUStartTime) * 1000.0);
            }
            t0 = Now();
            host.resize(pixels);
            std::memcpy(host.data(), out.contents, pixels * 4);
            const double read = Now() - t0;
            size_t diff = 0, first = SIZE_MAX;
            for (size_t p = 0; p < pixels; ++p)
                if (host[p] != s.picture[p])
                {
                    if (first == SIZE_MAX)
                        first = p;
                    ++diff;
                }
            if (diff == 0)
                ++exact;
            const double g = Median(gpu), w = Median(wall);
            std::printf("%s: ops %zu, %s; flatten %.3f ms, upload %.3f ms, GPU %.3f ms, submit-to-done %.3f ms, "
                        "readback %.3f ms; eve-emu (1 core) %.2f ms\n",
                        file.c_str(), f.ops.size(), diff ? "DIFFERS" : "bit-exact", flatten, upload, g, w, read,
                        s.cpuUs / 1000.0);
            if (diff)
                std::printf("   %zu pixels differ, first at (%zu, %zu): GPU %08X eve-emu %08X\n", diff, first % s.width,
                            first / s.width, host[first], s.picture[first]);
            flattenMs.push_back(flatten);
            uploadMs.push_back(upload);
            gpuMs.push_back(g);
            wallMs.push_back(w);
            readMs.push_back(read);
            cpuMs.push_back(s.cpuUs / 1000.0);
            opsCount.push_back(static_cast<double>(f.ops.size()));
        }
        char mode[128];
        if (perOp)
            std::snprintf(mode, sizeof mode, "one dispatch per op");
        else if (bandRows > 0)
            std::snprintf(mode, sizeof mode, "one dispatch per %u line(s)%s%s%s", bandRows,
                          sync ? ", commit + wait per band" : ", one command buffer", sub ? ", 256 threads per pixel" : "",
                          concurrent ? ", concurrent encoder" : "");
        else if (lineThreads)
            std::snprintf(mode, sizeof mode, "one dispatch per frame, one thread per line");
        else
            std::snprintf(mode, sizeof mode, "one dispatch per frame");
        std::printf("\nmode %s: frames %d, expressible %d, bit-exact %d\n", mode, total, supported, exact);
        if (supported)
            std::printf("median per frame: ops %.0f, flatten %.3f ms, upload %.3f ms, GPU %.3f ms, submit-to-done %.3f ms, "
                        "readback %.3f ms, end to end %.3f ms; eve-emu on one core %.2f ms\n",
                        Median(opsCount), Median(flattenMs), Median(uploadMs), Median(gpuMs), Median(wallMs),
                        Median(readMs), Median(flattenMs) + Median(uploadMs) + Median(wallMs) + Median(readMs),
                        Median(cpuMs));
        return exact == supported ? 0 : 1;
    }
}
