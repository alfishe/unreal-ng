// eve-accel 04c: a Metal backend inside eve-emu (the "gpu" variant). eve-emu's lazy
// catch-up already cuts the frame into batches exactly at the points where something a line
// reads changes (RAM_G writes, drawing register writes, the frame end; with EVE_POC_DEFER=1
// only RAM_G writes the active list reads). Each batch large enough goes to the GPU as one
// dispatch of the op-list kernel (eve-ops.metal); the picture rows come back into the host's
// frame buffer. eve-replay's picture hashes then check the result over a whole capture.
//
// RAM_G reaches the GPU as dirty 4 KB pages (a write marks its page; the pages are copied
// before the next dispatch). The ROM is copied once.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "eve-render.h"
#include "eve-snap.h"

#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <vector>

namespace
{

struct GpuParams
{
    uint32_t width, height, band, opCount;
    uint32_t rowBase, rowCount, pad0, pad1;
};

constexpr uint32_t kPage = 4096;
constexpr uint32_t kPages = EveSnap::kRamGSize / kPage;

struct Backend
{
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLComputePipelineState> pipeline = nil;
    id<MTLBuffer> memory = nil, aa = nil, out = nil, ops = nil, bandStart = nil, bandOps = nil;
    size_t outBytes = 0, opsBytes = 0, startBytes = 0, listBytes = 0;
    std::vector<uint8_t> dirty = std::vector<uint8_t>(kPages, 1);
    bool allDirty = true;
    bool failed = false;
    uint64_t pagesUploaded = 0;
};

std::map<const EveChip*, std::unique_ptr<Backend>>& Backends()
{
    static auto* backends = new std::map<const EveChip*, std::unique_ptr<Backend>>();
    return *backends;
}

Backend* Get(EveChip& chip)
{
    std::unique_ptr<Backend>& b = Backends()[&chip];
    if (b)
        return b->failed ? nullptr : b.get();
    b.reset(new Backend());
    @autoreleasepool
    {
        b->device = MTLCreateSystemDefaultDevice();
        const std::string source = EveSnap::InlineCore(EVE_METAL_SOURCE, EVE_CORE_SOURCE);
        NSError* error = nil;
        id<MTLLibrary> library = [b->device newLibraryWithSource:@(source.c_str())
                                                         options:[MTLCompileOptions new]
                                                           error:&error];
        if (!library)
        {
            std::fprintf(stderr, "eve-gpu: Metal compile failed: %s\n", error.localizedDescription.UTF8String);
            b->failed = true;
            return nullptr;
        }
        b->pipeline = [b->device newComputePipelineStateWithFunction:[library newFunctionWithName:@"RenderFrame"]
                                                               error:&error];
        b->queue = [b->device newCommandQueue];
        b->memory = [b->device newBufferWithLength:EveSnap::kMemorySize options:MTLResourceStorageModeShared];
        std::memset(b->memory.contents, 0, EveSnap::kMemorySize);
        std::memcpy(static_cast<uint8_t*>(b->memory.contents) + EveSnap::kRomBase, EveSnap::Rom().data(),
                    EveSnap::Rom().size());
        b->aa = [b->device newBufferWithBytes:EveSnap::AaTable() length:EveSnap::kAaTableSize
                                      options:MTLResourceStorageModeShared];
    }
    return b.get();
}

void Grow(Backend& b, id<MTLBuffer> __strong& buffer, size_t& capacity, size_t bytes)
{
    if (bytes > capacity)
    {
        capacity = bytes * 2;
        buffer = [b.device newBufferWithLength:capacity options:MTLResourceStorageModeShared];
    }
}

void RamGWritten(EveChip& chip, uint32_t address)
{
    auto it = Backends().find(&chip);
    if (it != Backends().end())
        it->second->dirty[address / kPage] = 1;
}

void Invalidate(EveChip& chip)
{
    auto it = Backends().find(&chip);
    if (it != Backends().end())
        it->second->allDirty = true;
}

bool Draw(EveChip& chip, uint32_t from, uint32_t to, const char** why)
{
    using namespace EveLib;
    Backend* b = Get(chip);
    if (!b)
    {
        *why = "no Metal device";
        return false;
    }
    if (chip.framebuffer == nullptr)
    {
        *why = "no frame buffer";
        return false;
    }
    // The frame as the op list: registers, the handle table, the active list
    static EveSnap::Snapshot s; // regs, handles, dl only (no RAM_G copy)
    s.width = RegGet(chip, Reg::Hsize) < 4096 ? RegGet(chip, Reg::Hsize) : 4096;
    s.height = RegGet(chip, Reg::Vsize) < 4096 ? RegGet(chip, Reg::Vsize) : 4096;
    std::memcpy(s.regs, chip.regions[RegionReg].base, sizeof(s.regs));
    std::memcpy(s.dl, ActiveDl(chip), sizeof(s.dl));
    for (uint32_t i = 0; i < kHandleCount; ++i)
    {
        const BitmapHandle& h = chip.state.handles[i];
        s.handles[i] = EveSnap::Handle{h.source, h.format, h.filter, h.wrapX, h.wrapY, HandleWidth(h), HandleHeight(h),
                                       HandleStride(h), HandleLayoutHeight(h)};
    }
    static EveSnap::Flattened f;
    f = EveSnap::Flatten(s);
    if (!f.supported)
    {
        static std::string reason;
        reason = f.reason;
        *why = reason.c_str();
        return false;
    }
    if (s.width > chip.widthCapacity || to > chip.heightCapacity)
    {
        *why = "frame buffer smaller than the picture";
        return false;
    }
    @autoreleasepool
    {
        // RAM_G pages written since the last dispatch
        uint8_t* memory = static_cast<uint8_t*>(b->memory.contents);
        const uint8_t* ramG = RamG(chip);
        for (uint32_t p = 0; p < kPages; ++p)
            if (b->allDirty || b->dirty[p])
            {
                std::memcpy(memory + p * kPage, ramG + p * kPage, kPage);
                b->dirty[p] = 0;
                ++b->pagesUploaded;
            }
        b->allDirty = false;
        const size_t opsBytes = std::max<size_t>(1, f.ops.size()) * sizeof(EveSnap::Op);
        Grow(*b, b->ops, b->opsBytes, opsBytes);
        if (!f.ops.empty())
            std::memcpy(b->ops.contents, f.ops.data(), f.ops.size() * sizeof(EveSnap::Op));
        Grow(*b, b->bandStart, b->startBytes, f.bandStart.size() * 4);
        std::memcpy(b->bandStart.contents, f.bandStart.data(), f.bandStart.size() * 4);
        Grow(*b, b->bandOps, b->listBytes, std::max<size_t>(1, f.bandOps.size()) * 4);
        if (!f.bandOps.empty())
            std::memcpy(b->bandOps.contents, f.bandOps.data(), f.bandOps.size() * 4);
        Grow(*b, b->out, b->outBytes, static_cast<size_t>(s.width) * s.height * 4);

        const GpuParams params{s.width, s.height, EveSnap::Flattened::kBand, static_cast<uint32_t>(f.ops.size()),
                               from, to - from, 0, 0};
        id<MTLCommandBuffer> cb = [b->queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
        [enc setComputePipelineState:b->pipeline];
        [enc setBuffer:b->memory offset:0 atIndex:0];
        [enc setBuffer:b->ops offset:0 atIndex:1];
        [enc setBuffer:b->bandStart offset:0 atIndex:2];
        [enc setBuffer:b->bandOps offset:0 atIndex:3];
        [enc setBuffer:b->out offset:0 atIndex:4];
        [enc setBytes:&params length:sizeof(params) atIndex:5];
        [enc setBuffer:b->aa offset:0 atIndex:6];
        [enc dispatchThreads:MTLSizeMake(s.width, to - from, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        [enc endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        const uint32_t* rows = static_cast<const uint32_t*>(b->out.contents);
        for (uint32_t y = from; y < to; ++y)
            std::memcpy(chip.framebuffer + static_cast<size_t>(y) * chip.stridePixels, rows + static_cast<size_t>(y) * s.width,
                        s.width * 4);
    }
    return true;
}

// Register the backend before main (EVE_POC_GPU=0 turns it off)
struct Register
{
    Register()
    {
        const char* env = std::getenv("EVE_POC_GPU");
        if (env != nullptr && env[0] == '0')
            return;
        EveLib::g_gpu = EveLib::GpuHooks{Draw, RamGWritten, Invalidate};
    }
} g_register;

} // namespace
