// eve-emu - line executor, context, flow control, line cost, drawing in step with the
// beam (spec §5.1, §5.2, §6.1-6.3, arch §8.4).
#include "eve-render.h"

#include <new>

#include "eve-poc.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace EveLib
{

namespace
{

constexpr uint32_t kHighBitsShift = kBitmapSizeBits;  // _H bits sit above the 9 low bits
constexpr uint32_t kStrideLowBits = 10;
constexpr uint32_t kVertexFormatMax = 4;              // 1/16 pixel
constexpr uint32_t kDisplayListIndexMask = 0xFFFF;    // CALL / JUMP destination field
constexpr uint32_t kWhite = 0xFFFFFF;

uint32_t RomWord(const EveChip& chip, uint32_t address)
{
    return static_cast<uint32_t>(RomByte(chip, address)) | (static_cast<uint32_t>(RomByte(chip, address + 1)) << 8) |
           (static_cast<uint32_t>(RomByte(chip, address + 2)) << 16) |
           (static_cast<uint32_t>(RomByte(chip, address + 3)) << 24);
}

uint32_t Field(uint32_t word, uint32_t high, uint32_t low)
{
    return (word >> low) & ((1u << (high - low + 1)) - 1);
}

int32_t SignedField(uint32_t word, uint32_t high, uint32_t low)
{
    const uint32_t bits = high - low + 1;
    const uint32_t shift = 32 - bits;
    return static_cast<int32_t>(Field(word, high, low) << shift) >> shift;
}

// --- Drawing in step with the beam ------------------------------------------------------

uint32_t VisibleLines(const EveChip& chip)
{
    const uint32_t vsize = RegGet(chip, Reg::Vsize);
    return vsize < kMaxLines ? vsize : kMaxLines;
}

uint32_t LineWidth(const EveChip& chip)
{
    // In a portrait orientation a logical line is as long as the screen is high.
    const uint32_t hsize = RegGet(chip, Portrait(chip) ? Reg::Vsize : Reg::Hsize);
    return hsize < kMaxLineWidth ? hsize : kMaxLineWidth;
}

// Visible lines whose sampling time has come: line v is drawn during scan line
// VOFFSET + v - kLineLookahead, from its pixel clock 0 (spec §5.1, V8).
uint32_t LinesDue(const EveChip& chip)
{
    const int64_t scanLine = chip.state.scan.line;
    const int64_t due = scanLine - static_cast<int64_t>(RegGet(chip, Reg::Voffset)) + kLineLookahead + 1;
    const int64_t visible = VisibleLines(chip);
    return static_cast<uint32_t>(due < 0 ? 0 : (due > visible ? visible : due));
}

uint32_t LineBudget(const EveChip& chip)
{
    const uint32_t lineClocks = LineClocks(chip);
    uint32_t budget = lineClocks > kLineBudgetOverhead ? lineClocks - kLineBudgetOverhead : 0;
    if (lineClocks >= kLineBudgetFloor && budget < kLineBudgetFloor)
        budget = kLineBudgetFloor;
    return budget;
}

void PrepareRun(EveChip& chip, LineRun& run, uint32_t logicalLine, bool firstLine, bool probe = false,
                uint8_t* color = nullptr, uint8_t* stencil = nullptr, uint8_t* tag = nullptr,
                uint32_t* texels = nullptr)
{
    run.chip = &chip;
    run.handles = chip.state.handles;
    run.y = logicalLine;
    run.width = LineWidth(chip);
    run.color = probe ? chip.probeColor.get() : chip.lineColor.get();
    run.stencil = probe ? chip.probeStencil.get() : chip.lineStencil.get();
    run.tag = probe ? chip.probeTag.get() : chip.lineTag.get();
    run.texels = chip.lineTexels.get();
    if (color != nullptr)
    {
        // eve-accel 03: a worker's own buffers
        run.color = color;
        run.stencil = stencil;
        run.tag = tag;
        run.texels = texels;
    }
    run.savedCount = 0;
    run.palette.valid = false;
    run.primitive = kPrimNone;
    run.vertexCount = 0;
    run.previous = Vertex{};
    run.commands = 0;
    run.fillCost = 0;
    run.events = 0;
    run.probeX = -1;
    run.probe = nullptr;
    run.commandIndex = 0;
    run.commandWord = 0;
    const bool reset = kContextReset == ContextReset::PerLine ||
                       (kContextReset == ContextReset::PerFrame && firstLine);
    if (reset)
        ContextDefaults(run.ctx);
    else
        run.ctx = chip.state.context;
    // At the start of each line the buffers hold color 0 with alpha 0, stencil 0, tag 0.
    std::memset(run.color, 0, static_cast<size_t>(run.width) * kChannels);
    std::memset(run.stencil, 0, run.width);
    std::memset(run.tag, 0, run.width);
}

void FinishRun(EveChip& chip, const LineRun& run, uint32_t screenLine)
{
    if (kContextReset != ContextReset::PerLine)
        chip.state.context = run.ctx;
    EveLineCost& cost = chip.lineCosts[screenLine];
    cost.line = screenLine;
    cost.valid = 1;
    cost.commands = run.commands;
    cost.fillClocks = static_cast<uint32_t>((run.fillCost + kFillCostScale - 1) / kFillCostScale);
    cost.totalClocks = cost.commands + cost.fillClocks;
    cost.budget = LineBudget(chip);
    cost.overflow = cost.totalClocks > cost.budget ? 1 : 0;
    if (cost.overflow)
        ++chip.overflowLines;
}

// Portrait orientations (REG_ROTATE 2, 3, 6, 7): a screen line is a column of the logical
// picture, so the whole logical picture is drawn when the frame's first line is due and
// written into the frame buffer transposed (BT8XX, golden cases rotate-*).
void DrawPortraitFrame(EveChip& chip)
{
    const uint32_t logicalLines = RegGet(chip, Reg::Hsize) < kMaxLines ? RegGet(chip, Reg::Hsize) : kMaxLines;
    for (uint32_t line = 0; line < logicalLines; ++line)
    {
        LineRun run;
        PrepareRun(chip, run, line, line == 0);
        ExecuteLine<LineMode::Draw>(run);
        FinishRun(chip, run, line < kMaxLines ? line : 0);
        OutputPortraitLine(chip, line, run.color, run.width);
    }
}

void DrawVisibleLine(EveChip& chip, uint32_t screenLine)
{
    bool mirrorX = false;
    const uint32_t logicalLine = LogicalLine(chip, screenLine, mirrorX);
    LineRun run;
    PrepareRun(chip, run, logicalLine, screenLine == 0);
    ExecuteLine<LineMode::Draw>(run);
    FinishRun(chip, run, screenLine);
    // REG_TAG: the tag buffer at (REG_TAG_X, REG_TAG_Y) of the drawn frame (spec §3.3).
    if (logicalLine == RegGet(chip, Reg::TagY))
    {
        const uint32_t tagX = RegGet(chip, Reg::TagX);
        if (tagX < run.width)
            RegSet(chip, Reg::Tag, run.tag[tagX]);
    }
    OutputLine(chip, screenLine, run.color, run.width, mirrorX);
}

// --- eve-accel 03: parallel lines ----------------------------------------------------------
//
// A batch of due lines is drawn by a thread pool. Each line starts from the handle table
// the batch started with (a display list sets the same values on every line, so after its
// first line the table is a fixed point); a worker draws on a private copy and reports a
// line whose table differs at its end. Lines up to the first such line are exact; the rest
// of the batch is drawn again from that line's table. Line buffers, texels and the table
// copy are per worker; the frame buffer rows, line costs and REG_TAG are written per line.

struct ReadRange
{
    uint32_t from, to;
};

struct Worker
{
    std::unique_ptr<uint8_t[]> color, stencil, tag;
    std::unique_ptr<uint32_t[]> texels;
    BitmapHandle handles[kHandleCount];
    Worker()
        : color(new uint8_t[kMaxLineWidth * kChannels]()), stencil(new uint8_t[kMaxLineWidth]()),
          tag(new uint8_t[kMaxLineWidth]()), texels(new uint32_t[kMaxLineWidth]())
    {
    }
};

struct PocState;

struct Pool
{
    std::vector<std::thread> threads;
    std::mutex mutex;
    std::condition_variable start, done;
    uint64_t generation = 0;
    uint32_t running = 0;
    bool quit = false;
    PocState* job = nullptr;
};

struct PocState
{
    int threads = 1;
    uint32_t minParallel = 16;  // smaller batches are drawn serially
    bool deferWrites = false;   // EVE_POC_DEFER=1
    std::vector<std::unique_ptr<Worker>> workers;
    std::unique_ptr<Pool> pool;
    // The batch being drawn.
    EveChip* chip = nullptr;
    uint32_t from = 0, to = 0;
    BitmapHandle startHandles[kHandleCount];
    std::atomic<uint32_t> next{0};
    std::atomic<uint32_t> firstDiverged{UINT32_MAX};
    std::mutex divergedMutex;
    BitmapHandle divergedHandles[kHandleCount];
    int32_t tagValue = -1;
    // Read set of the active list (deferred writes).
    bool readSetValid = false;
    bool readSetOverflow = false;
    BitmapHandle readSetHandles[kHandleCount];
    std::vector<ReadRange> readSet;
    uint32_t deferredDue = 0;
    // The cheap profile (11).
    bool skipUnchanged = false; // EVE_POC_SKIP_UNCHANGED=1
    uint32_t lineStep = 1;      // EVE_POC_LINE_STEP=N: draw every Nth line, copy the others
    bool dirty = true, changedThisFrame = true;
    uint32_t* lastBuffer = nullptr;
    uint32_t lastStride = 0, lastTagX = 0, lastTagY = 0;
    uint64_t skippedLines = 0, frameIndex = 0;
    // Frame snapshots (04).
    const char* snapDir = nullptr;
    uint64_t snapFrom = 0, snapTo = 0, snapshots = 0, snapStep = 1; // EVE_POC_SNAP_STEP
    // GPU backend (04c).
    uint32_t gpuMinLines = 64; // EVE_POC_GPU_MIN: smaller batches stay on the CPU ("auto": by cost)
    bool gpuAuto = false;
    double cpuNsPerLine = 0, gw = 0, gn = 0, gt = 0, gnn = 0, gnt = 0, gpuA = 0, gpuB = 0;
    uint64_t cpuSamples = 0, gpuSamples = 0, decisions = 0;
    uint64_t gpuBatches = 0, gpuLines = 0, gpuFallbacks = 0;
    double gpuSeconds = 0;
    const char* gpuFallbackReason = nullptr;
    // Statistics.
    uint64_t batches = 0, parallelBatches = 0, serialLines = 0, parallelLines = 0, redrawnLines = 0,
             divergences = 0, deferredWrites = 0, readSetBuilds = 0;
    ~PocState()
    {
        if (pool)
        {
            {
                std::lock_guard<std::mutex> lock(pool->mutex);
                pool->quit = true;
            }
            pool->start.notify_all();
            for (std::thread& t : pool->threads)
                t.join();
        }
    }
};

std::map<const EveChip*, std::unique_ptr<PocState>>& PocStates()
{
    static auto* states = new std::map<const EveChip*, std::unique_ptr<PocState>>();
    return *states;
}

PocState& Poc(const EveChip& chip)
{
    std::unique_ptr<PocState>& s = PocStates()[&chip];
    if (!s)
    {
        s.reset(new PocState());
        const char* defer = std::getenv("EVE_POC_DEFER");
        s->deferWrites = defer != nullptr && defer[0] == '1';
        const char* skip = std::getenv("EVE_POC_SKIP_UNCHANGED");
        s->skipUnchanged = skip != nullptr && skip[0] == '1';
        const char* step = std::getenv("EVE_POC_LINE_STEP");
        if (step != nullptr && std::atoi(step) > 1)
            s->lineStep = static_cast<uint32_t>(std::atoi(step));
        const char* gpuMin = std::getenv("EVE_POC_GPU_MIN");
        if (gpuMin != nullptr && !std::strcmp(gpuMin, "auto"))
            s->gpuAuto = true;
        else if (gpuMin != nullptr)
            s->gpuMinLines = static_cast<uint32_t>(std::atoi(gpuMin));
        const char* minimum = std::getenv("EVE_POC_MIN_PARALLEL");
        if (minimum != nullptr)
            s->minParallel = static_cast<uint32_t>(std::atoi(minimum));
    }
    return *s;
}

// One line into a worker's buffers with the worker's handle table; true when the table at
// the line's end equals the one it started with.
void DrawLineOn(EveChip& chip, PocState& s, Worker& w, uint32_t screenLine)
{
    std::memcpy(w.handles, s.startHandles, sizeof(w.handles));
    bool mirrorX = false;
    const uint32_t logicalLine = LogicalLine(chip, screenLine, mirrorX);
    LineRun run;
    PrepareRun(chip, run, logicalLine, screenLine == 0, false, w.color.get(), w.stencil.get(), w.tag.get(),
               w.texels.get());
    run.handles = w.handles;
    ExecuteLine<LineMode::Draw>(run);
    // FinishRun without the shared overflow count (merged after the batch)
    EveLineCost& cost = chip.lineCosts[screenLine];
    cost.line = screenLine;
    cost.valid = 1;
    cost.commands = run.commands;
    cost.fillClocks = static_cast<uint32_t>((run.fillCost + kFillCostScale - 1) / kFillCostScale);
    cost.totalClocks = cost.commands + cost.fillClocks;
    cost.budget = LineBudget(chip);
    cost.overflow = cost.totalClocks > cost.budget ? 1 : 0;
    if (logicalLine == RegGet(chip, Reg::TagY))
    {
        const uint32_t tagX = RegGet(chip, Reg::TagX);
        if (tagX < run.width)
            s.tagValue = run.tag[tagX]; // one line per batch has this logical line
    }
    OutputLine(chip, screenLine, run.color, run.width, mirrorX);
    if (std::memcmp(w.handles, s.startHandles, sizeof(w.handles)) != 0)
    {
        std::lock_guard<std::mutex> lock(s.divergedMutex);
        if (screenLine < s.firstDiverged.load())
        {
            s.firstDiverged.store(screenLine);
            std::memcpy(s.divergedHandles, w.handles, sizeof(s.divergedHandles));
        }
    }
}

void WorkOn(PocState& s, Worker& w)
{
    for (;;)
    {
        const uint32_t line = s.next.fetch_add(1, std::memory_order_relaxed);
        if (line >= s.to)
            break;
        // A line after a diverged one is drawn again anyway
        if (line > s.firstDiverged.load(std::memory_order_relaxed))
            continue;
        DrawLineOn(*s.chip, s, w, line);
    }
}

void WorkerLoop(PocState* s, uint32_t index)
{
    Pool& pool = *s->pool;
    uint64_t seen = 0;
    for (;;)
    {
        {
            std::unique_lock<std::mutex> lock(pool.mutex);
            pool.start.wait(lock, [&] { return pool.quit || pool.generation != seen; });
            if (pool.quit)
                return;
            seen = pool.generation;
        }
        WorkOn(*s, *s->workers[index]);
        {
            std::lock_guard<std::mutex> lock(pool.mutex);
            if (--pool.running == 0)
                pool.done.notify_one();
        }
    }
}

void EnsurePool(PocState& s)
{
    while (s.workers.size() < static_cast<size_t>(s.threads))
        s.workers.emplace_back(new Worker());
    if (s.threads > 1 && !s.pool)
    {
        s.pool.reset(new Pool());
        for (int i = 1; i < s.threads; ++i)
            s.pool->threads.emplace_back(WorkerLoop, &s, static_cast<uint32_t>(i));
    }
}

void DrawVisibleLine(EveChip& chip, uint32_t screenLine);
uint32_t VisibleLines(const EveChip& chip);

// eve-accel 04: a frame drawn in one batch, written with what drawing it read
//   "EVESNAP1", u32 width, height, frame, cpu ns (serial drawing of the frame),
//   u32 regs[1024] (RAM_REG), u32 handles[32][12], u32 dl[2048], u8 ramG[1 MB],
//   u32 picture[width * height] (ARGB8888 as eve-emu wrote it)
void Snapshot(EveChip& chip, PocState& s, uint32_t due)
{
    const uint32_t width = RegGet(chip, Reg::Hsize);
    const uint32_t height = due;
    std::vector<uint32_t> handles(kHandleCount * 12);
    for (uint32_t i = 0; i < kHandleCount; ++i)
    {
        const BitmapHandle& h = chip.state.handles[i];
        uint32_t* o = &handles[i * 12];
        o[0] = h.source;
        o[1] = h.format;
        o[2] = h.filter;
        o[3] = h.wrapX;
        o[4] = h.wrapY;
        o[5] = HandleWidth(h);
        o[6] = HandleHeight(h);
        o[7] = HandleStride(h);
        o[8] = HandleLayoutHeight(h);
    }
    std::vector<uint8_t> ramG(RamG(chip), RamG(chip) + kRamGSize);
    std::vector<uint8_t> dl(ActiveDl(chip), ActiveDl(chip) + kRamDlSize);
    std::vector<uint8_t> regs(chip.regions[RegionReg].base, chip.regions[RegionReg].base + kRamRegSize);
    const auto t0 = std::chrono::steady_clock::now();
    while (chip.drawnLines < due)
        DrawVisibleLine(chip, chip.drawnLines++);
    const uint64_t ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
    s.serialLines += due;
    if (chip.framebuffer == nullptr || width > chip.widthCapacity || height > chip.heightCapacity)
        return;
    const uint32_t frame = static_cast<uint32_t>(chip.state.scan.completedFrames + 1);
    const std::string path = std::string(s.snapDir) + "/frame-" + std::to_string(frame) + ".snap";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
        return;
    const uint32_t header[5] = {width, height, frame, static_cast<uint32_t>(ns / 1000), 0};
    std::fwrite("EVESNAP1", 1, 8, f);
    std::fwrite(header, 4, 5, f);
    std::fwrite(regs.data(), 1, regs.size(), f);
    std::fwrite(handles.data(), 4, handles.size(), f);
    std::fwrite(dl.data(), 1, dl.size(), f);
    std::fwrite(ramG.data(), 1, ramG.size(), f);
    for (uint32_t y = 0; y < height; ++y)
        std::fwrite(chip.framebuffer + static_cast<size_t>(y) * chip.stridePixels, 4, width, f);
    std::fclose(f);
    ++s.snapshots;
}

// Draw lines [chip.drawnLines, due) - in parallel when the batch is large enough.
void CatchUpLines(EveChip& chip, uint32_t due)
{
    PocState& s = Poc(chip);
    if (chip.drawnLines >= due)
        return;
    // 11, cheap: skip lines whose picture cannot have changed (exact), or draw every Nth line
    if (s.skipUnchanged)
    {
        const uint32_t tagX = RegGet(chip, Reg::TagX), tagY = RegGet(chip, Reg::TagY);
        if (chip.framebuffer != s.lastBuffer || chip.stridePixels != s.lastStride || tagX != s.lastTagX ||
            tagY != s.lastTagY)
        {
            s.lastBuffer = chip.framebuffer;
            s.lastStride = chip.stridePixels;
            s.lastTagX = tagX;
            s.lastTagY = tagY;
            s.dirty = true;
            s.changedThisFrame = true;
        }
        if (!s.dirty)
        {
            s.skippedLines += due - chip.drawnLines;
            chip.drawnLines = due;
            return;
        }
    }
    if (s.lineStep > 1)
    {
        while (chip.drawnLines < due)
        {
            const uint32_t line = chip.drawnLines++;
            if (line % s.lineStep != 0 && chip.framebuffer != nullptr && line < chip.heightCapacity)
            {
                // copy the row above (the line that was drawn)
                std::memcpy(chip.framebuffer + static_cast<size_t>(line) * chip.stridePixels,
                            chip.framebuffer + static_cast<size_t>(line - 1) * chip.stridePixels,
                            static_cast<size_t>(std::min(RegGet(chip, Reg::Hsize), chip.widthCapacity)) * 4);
                ++s.skippedLines;
            }
            else
                DrawVisibleLine(chip, line);
        }
        return;
    }
    ++s.batches;
    if (s.snapDir != nullptr && chip.drawnLines == 0 && due == VisibleLines(chip) &&
        chip.state.scan.completedFrames + 1 >= s.snapFrom && chip.state.scan.completedFrames + 1 <= s.snapTo &&
        (chip.state.scan.completedFrames + 1 - s.snapFrom) % s.snapStep == 0)
    {
        Snapshot(chip, s, due);
        return;
    }
    // 04c: GPU or CPU for this batch. Fixed: EVE_POC_GPU_MIN lines and more go to the GPU.
    // EVE_POC_GPU_MIN=auto: by the measured cost - the GPU's time per batch fitted as
    // a + b x lines (least squares over recent GPU batches), against the CPU's measured time
    // per line; one batch in 64 takes the other path to keep both estimates current.
    const uint32_t batchLines = due - chip.drawnLines;
    bool useGpu = g_gpu.draw != nullptr && batchLines >= s.gpuMinLines;
    if (g_gpu.draw != nullptr && s.gpuAuto)
    {
        const bool ready = s.cpuSamples >= 8 && s.gpuSamples >= 8;
        if (!ready)
            useGpu = s.gpuSamples < 8 ? batchLines >= 64 : batchLines < 64 ? false : true;
        else
        {
            const double det = s.gw * s.gnn - s.gn * s.gn;
            const double b = det > 0 ? (s.gw * s.gnt - s.gn * s.gt) / det : 0;
            const double a = det > 0 ? (s.gt - b * s.gn) / s.gw : s.gt / s.gw;
            const double gpuNs = a + b * batchLines, cpuNs = s.cpuNsPerLine * batchLines;
            useGpu = gpuNs < cpuNs;
            s.gpuA = a;
            s.gpuB = b;
        }
        if ((++s.decisions & 63) == 0)
            useGpu = !useGpu && batchLines >= 2; // explore
    }
    const auto batchStart0 = std::chrono::steady_clock::now();
    struct CpuTimer
    {
        PocState& s;
        uint32_t lines;
        std::chrono::steady_clock::time_point t0;
        bool active = true;
        ~CpuTimer()
        {
            if (!active || lines < 4)
                return;
            const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
            const double perLine = ns / lines;
            s.cpuNsPerLine = s.cpuSamples == 0 ? perLine : 0.95 * s.cpuNsPerLine + 0.05 * perLine;
            ++s.cpuSamples;
        }
    };
    if (useGpu)
    {
        // 04c: the first line on the CPU (after it the handle table is a fixed point of the
        // list), the rest of the batch in one GPU dispatch
        ++s.serialLines;
        DrawVisibleLine(chip, chip.drawnLines++);
        const char* why = nullptr;
        const uint32_t from = chip.drawnLines;
        const auto t0 = std::chrono::steady_clock::now();
        const bool done = g_gpu.draw(chip, from, due, &why);
        s.gpuSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (done)
        {
            ++s.gpuBatches;
            s.gpuLines += due - from;
            {
                // the GPU cost model: time of the whole batch (first line on the CPU included)
                const double ns =
                    std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - batchStart0).count();
                const double n = batchLines, decay = 0.98;
                s.gw = decay * s.gw + 1;
                s.gn = decay * s.gn + n;
                s.gt = decay * s.gt + ns;
                s.gnn = decay * s.gnn + n * n;
                s.gnt = decay * s.gnt + n * ns;
                ++s.gpuSamples;
            }
            // REG_TAG: the tag buffer is not on the GPU; draw that one line on the CPU too
            const uint32_t tagY = RegGet(chip, Reg::TagY);
            if (tagY >= from && tagY < due)
            {
                EnsurePool(s);
                std::memcpy(s.startHandles, chip.state.handles, sizeof(s.startHandles));
                s.tagValue = -1;
                s.firstDiverged.store(UINT32_MAX);
                DrawLineOn(chip, s, *s.workers[0], tagY);
                if (s.tagValue >= 0)
                    RegSet(chip, Reg::Tag, static_cast<uint32_t>(s.tagValue));
            }
            chip.drawnLines = due;
            return;
        }
        ++s.gpuFallbacks;
        s.gpuFallbackReason = why;
        if (chip.drawnLines >= due)
            return;
    }
    CpuTimer cpuTimer{s, due - chip.drawnLines, std::chrono::steady_clock::now()};
    cpuTimer.active = !useGpu; // a GPU fallback is not a CPU sample
    if (s.threads <= 1 || due - chip.drawnLines < s.minParallel)
    {
        s.serialLines += due - chip.drawnLines;
        while (chip.drawnLines < due)
            DrawVisibleLine(chip, chip.drawnLines++);
        return;
    }
    static_assert(kContextReset == ContextReset::PerLine, "parallel lines need the context reset per line");
    EnsurePool(s);
    ++s.parallelBatches;
    // The first line serially: after it the handle table is a fixed point of the list
    ++s.serialLines;
    DrawVisibleLine(chip, chip.drawnLines++);
    const uint32_t batchStart = chip.drawnLines;
    s.chip = &chip;
    s.tagValue = -1;
    while (chip.drawnLines < due)
    {
        s.from = chip.drawnLines;
        s.to = due;
        std::memcpy(s.startHandles, chip.state.handles, sizeof(s.startHandles));
        s.firstDiverged.store(UINT32_MAX);
        s.next.store(s.from);
        {
            std::lock_guard<std::mutex> lock(s.pool->mutex);
            s.pool->running = static_cast<uint32_t>(s.threads - 1);
            ++s.pool->generation;
        }
        s.pool->start.notify_all();
        WorkOn(s, *s.workers[0]);
        {
            std::unique_lock<std::mutex> lock(s.pool->mutex);
            s.pool->done.wait(lock, [&] { return s.pool->running == 0; });
        }
        const uint32_t diverged = s.firstDiverged.load();
        if (diverged == UINT32_MAX)
        {
            s.parallelLines += due - s.from;
            chip.drawnLines = due;
        }
        else
        {
            // Lines up to the diverged one are exact; go on from its table
            ++s.divergences;
            s.parallelLines += diverged + 1 - s.from;
            s.redrawnLines += due - (diverged + 1);
            std::memcpy(chip.state.handles, s.divergedHandles, sizeof(s.divergedHandles));
            chip.drawnLines = diverged + 1;
        }
    }
    for (uint32_t l = batchStart; l < due; ++l)
        if (chip.lineCosts[l].overflow)
            ++chip.overflowLines;
    if (s.tagValue >= 0)
        RegSet(chip, Reg::Tag, static_cast<uint32_t>(s.tagValue));
}

void BuildReadSet(EveChip& chip, PocState& s);

// --- Execution ----------------------------------------------------------------------------

std::vector<ReadRange>* g_readSet = nullptr; // set while BuildReadSet runs the list

template <LineMode Mode>
void Vertex2(LineRun& run, const Vertex& v)
{
    if constexpr (Mode == LineMode::StateOnly)
    {
        if (g_readSet != nullptr && run.primitive == kPrimBitmaps)
        {
            const BitmapHandle& h = run.handles[v.handle];
            const uint32_t stride = HandleStride(h);
            const uint32_t rows = HandleLayoutHeight(h);
            const uint32_t base = h.source + static_cast<uint32_t>(v.cell) * stride * rows;
            g_readSet->push_back(ReadRange{base, base + stride * (h.format == kFormatBargraph ? 1 : rows)});
            if (h.format == kFormatPaletted565 || h.format == kFormatPaletted4444 || h.format == kFormatPaletted8)
                g_readSet->push_back(ReadRange{run.ctx.paletteSource,
                                               run.ctx.paletteSource + (h.format == kFormatPaletted8 ? 1024u : 512u)});
            if (h.format == kFormatText8x8 || h.format == kFormatTextVga)
                for (uint32_t f = 16; f < 20; ++f)
                    g_readSet->push_back(ReadRange{run.handles[f].source, run.handles[f].source + 128 * 16});
        }
        return;
    }
    else
    {
    switch (run.primitive)
    {
    case kPrimBitmaps:
        DrawBitmap<Mode>(run, v);
        break;
    case kPrimPoints:
        DrawPoint<Mode>(run, v);
        break;
    case kPrimLines:
        if (run.vertexCount & 1)
            DrawLine<Mode>(run, run.previous, v);
        break;
    case kPrimLineStrip:
        if (run.vertexCount > 0)
            DrawLine<Mode>(run, run.previous, v);
        break;
    case kPrimEdgeStripR:
    case kPrimEdgeStripL:
    case kPrimEdgeStripA:
    case kPrimEdgeStripB:
        if (run.vertexCount > 0)
            DrawEdge<Mode>(run, run.previous, v, run.primitive);
        break;
    case kPrimRects:
        if (run.vertexCount & 1)
            DrawRect<Mode>(run, run.previous, v);
        break;
    default:
        break;
    }
    }
}

void SetHandleLayout(BitmapHandle& h, uint32_t word)
{
    h.format = static_cast<uint8_t>(Field(word, 23, 19));
    h.strideLow = static_cast<uint16_t>(Field(word, 18, 9));
    h.layoutHeightLow = static_cast<uint16_t>(Field(word, 8, 0));
    if (!kSizeKeepsHighBits)
    {
        h.strideHigh = 0;
        h.layoutHeightHigh = 0;
    }
}

void SetHandleSize(BitmapHandle& h, uint32_t word)
{
    h.filter = static_cast<uint8_t>(Field(word, 20, 20));
    h.wrapX = static_cast<uint8_t>(Field(word, 19, 19));
    h.wrapY = static_cast<uint8_t>(Field(word, 18, 18));
    h.widthLow = static_cast<uint16_t>(Field(word, 17, 9));
    h.heightLow = static_cast<uint16_t>(Field(word, 8, 0));
    if (!kSizeKeepsHighBits)
    {
        h.widthHigh = 0;
        h.heightHigh = 0;
    }
}

enum class Flow { Next, Jump, Stop };

template <LineMode Mode>
Flow ExecuteWord(LineRun& run, uint32_t word, uint32_t& target, uint32_t* stack, uint32_t& depth, uint32_t index)
{
    GraphicsContext& ctx = run.ctx;
    EveChip& chip = *run.chip;
    const uint32_t kind = word >> kVertexKindShift;
    if (kind == kVertex2f || kind == kVertex2ii)
    {
        Vertex v{};
        v.index = index;
        v.word = word;
        if (kind == kVertex2f)
        {
            const uint32_t frac = ctx.vertexFormat < kVertexFormatMax ? ctx.vertexFormat : kVertexFormatMax;
            v.x = SignedField(word, 29, 15) * (1 << (kVertexFormatMax - frac));
            v.y = SignedField(word, 14, 0) * (1 << (kVertexFormatMax - frac));
            v.handle = ctx.handle;
            v.cell = ctx.cell;
        }
        else
        {
            v.x = static_cast<int32_t>(Field(word, 29, 21) * kSubpixel);
            v.y = static_cast<int32_t>(Field(word, 20, 12) * kSubpixel);
            v.handle = static_cast<uint8_t>(Field(word, 11, 7));
            v.cell = static_cast<uint8_t>(Field(word, 6, 0));
        }
        // VERTEX_TRANSLATE applies to both vertex forms (spec §6.4).
        v.x += ctx.translateX;
        v.y += ctx.translateY;
        Vertex2<Mode>(run, v);
        run.previous = v;
        ++run.vertexCount;
        return Flow::Next;
    }

    BitmapHandle& handle = run.handles[ctx.handle];
    switch (word >> kOpcodeShift)
    {
    case kOpDisplay:
        return Flow::Stop;
    case kOpBitmapSource:
        handle.source = Field(word, 21, 0);
        break;
    case kOpClearColorRgb:
        ctx.clearColorRgb = Field(word, 23, 0);
        break;
    case kOpTag:
        ctx.tag = static_cast<uint8_t>(Field(word, 7, 0));
        break;
    case kOpColorRgb:
        ctx.colorRgb = Field(word, 23, 0);
        break;
    case kOpBitmapHandle:
        ctx.handle = static_cast<uint8_t>(Field(word, 4, 0));
        break;
    case kOpCell:
        ctx.cell = static_cast<uint8_t>(Field(word, 6, 0));
        break;
    case kOpBitmapLayout:
        SetHandleLayout(handle, word);
        break;
    case kOpBitmapSize:
        SetHandleSize(handle, word);
        break;
    case kOpAlphaFunc:
        ctx.alphaFunc = static_cast<uint8_t>(Field(word, 10, 8));
        ctx.alphaRef = static_cast<uint8_t>(Field(word, 7, 0));
        break;
    case kOpStencilFunc:
        ctx.stencilFunc = static_cast<uint8_t>(Field(word, 19, 16));
        ctx.stencilRef = static_cast<uint8_t>(Field(word, 15, 8));
        ctx.stencilFuncMask = static_cast<uint8_t>(Field(word, 7, 0));
        break;
    case kOpBlendFunc:
        ctx.blendSrc = static_cast<uint8_t>(Field(word, 5, 3));
        ctx.blendDst = static_cast<uint8_t>(Field(word, 2, 0));
        break;
    case kOpStencilOp:
        ctx.stencilFail = static_cast<uint8_t>(Field(word, 5, 3));
        ctx.stencilPass = static_cast<uint8_t>(Field(word, 2, 0));
        break;
    case kOpPointSize:
        ctx.pointSize = static_cast<uint16_t>(Field(word, 12, 0));
        break;
    case kOpLineWidth:
        ctx.lineWidth = static_cast<uint16_t>(Field(word, 11, 0));
        break;
    case kOpClearColorA:
        ctx.clearColorA = static_cast<uint8_t>(Field(word, 7, 0));
        break;
    case kOpColorA:
        ctx.colorA = static_cast<uint8_t>(Field(word, 7, 0));
        break;
    case kOpClearStencil:
        ctx.clearStencil = static_cast<uint8_t>(Field(word, 7, 0));
        break;
    case kOpClearTag:
        ctx.clearTag = static_cast<uint8_t>(Field(word, 7, 0));
        break;
    case kOpStencilMask:
        ctx.stencilWriteMask = static_cast<uint8_t>(Field(word, 7, 0));
        break;
    case kOpTagMask:
        ctx.tagMask = static_cast<uint8_t>(Field(word, 0, 0));
        break;
    case kOpBitmapTransformA:
        ctx.transform[0] = SignedField(word, 16, 0);
        break;
    case kOpBitmapTransformB:
        ctx.transform[1] = SignedField(word, 16, 0);
        break;
    case kOpBitmapTransformC:
        ctx.transform[2] = SignedField(word, 23, 0);
        break;
    case kOpBitmapTransformD:
        ctx.transform[3] = SignedField(word, 16, 0);
        break;
    case kOpBitmapTransformE:
        ctx.transform[4] = SignedField(word, 16, 0);
        break;
    case kOpBitmapTransformF:
        ctx.transform[5] = SignedField(word, 23, 0);
        break;
    case kOpScissorXy:
        ctx.scissorX = static_cast<uint16_t>(Field(word, 21, 11));
        ctx.scissorY = static_cast<uint16_t>(Field(word, 10, 0));
        break;
    case kOpScissorSize:
        ctx.scissorWidth = static_cast<uint16_t>(Field(word, 23, 12));
        ctx.scissorHeight = static_cast<uint16_t>(Field(word, 11, 0));
        break;
    case kOpCall:
        if (depth == kCallStackDepth)
        {
            // "Any additional CALL/RETURN done will lead to unexpected behavior" [PG §4.19].
            ++run.events;
            return Flow::Stop;
        }
        stack[depth++] = index + 1;
        target = Field(word, 15, 0) & kDisplayListIndexMask;
        return Flow::Jump;
    case kOpJump:
        target = Field(word, 15, 0) & kDisplayListIndexMask;
        return Flow::Jump;
    case kOpBegin:
        run.primitive = static_cast<uint8_t>(Field(word, 3, 0));
        run.vertexCount = 0;
        break;
    case kOpColorMask:
        ctx.colorMask = static_cast<uint8_t>(Field(word, 3, 0));
        break;
    case kOpEnd:
        run.primitive = kPrimNone;
        run.vertexCount = 0;
        break;
    case kOpSaveContext:
        // A fifth push drops the oldest (spec §6.1).
        if (run.savedCount == kContextStackDepth)
        {
            for (uint32_t i = 1; i < kContextStackDepth; ++i)
                run.saved[i - 1] = run.saved[i];
            --run.savedCount;
        }
        run.saved[run.savedCount++] = ctx;
        break;
    case kOpRestoreContext:
        // An extra pop loads the defaults (spec §6.1).
        if (run.savedCount == 0)
            ContextDefaults(ctx);
        else
            ctx = run.saved[--run.savedCount];
        break;
    case kOpReturn:
        if (depth == 0)
        {
            ++run.events;
            return Flow::Stop;
        }
        target = stack[--depth];
        return Flow::Jump;
    case kOpMacro:
    {
        // The word in REG_MACRO_0 / 1 executes as if it were in the list.
        const uint32_t macro = RegGet(chip, Field(word, 0, 0) ? Reg::Macro1 : Reg::Macro0);
        if ((macro >> kOpcodeShift) == kOpMacro)
            break; // a macro that calls itself does nothing
        return ExecuteWord<Mode>(run, macro, target, stack, depth, index);
    }
    case kOpClear:
        if constexpr (Mode != LineMode::StateOnly)
            ClearLine<Mode>(run, Field(word, 2, 0));
        break;
    case kOpVertexFormat:
        ctx.vertexFormat = static_cast<uint8_t>(Field(word, 2, 0));
        break;
    case kOpBitmapLayoutH:
        handle.strideHigh = static_cast<uint8_t>(Field(word, 3, 2));
        handle.layoutHeightHigh = static_cast<uint8_t>(Field(word, 1, 0));
        break;
    case kOpBitmapSizeH:
        handle.widthHigh = static_cast<uint8_t>(Field(word, 3, 2));
        handle.heightHigh = static_cast<uint8_t>(Field(word, 1, 0));
        break;
    case kOpPaletteSource:
        ctx.paletteSource = Field(word, 21, 0);
        break;
    case kOpVertexTranslateX:
        ctx.translateX = SignedField(word, 16, 0);
        break;
    case kOpVertexTranslateY:
        ctx.translateY = SignedField(word, 16, 0);
        break;
    case kOpNop:
        break;
    default:
        ++run.events; // unknown opcodes are executed as NOP (spec §6.1, V15)
        break;
    }
    return Flow::Next;
}

// The RAM_G ranges the active list reads (bitmaps, palettes, text glyphs), from one run of
// the list without drawing on a copy of the handle table.
void BuildReadSet(EveChip& chip, PocState& s)
{
    ++s.readSetBuilds;
    std::memcpy(s.readSetHandles, chip.state.handles, sizeof(s.readSetHandles));
    BitmapHandle copy[kHandleCount];
    std::memcpy(copy, chip.state.handles, sizeof(copy));
    s.readSet.clear();
    g_readSet = &s.readSet;
    LineRun run;
    PrepareRun(chip, run, 0, true, true);
    run.handles = copy;
    ExecuteLine<LineMode::StateOnly>(run);
    g_readSet = nullptr;
    // A list whose table changes after its first line: be safe, no deferral
    s.readSetOverflow = s.readSet.size() > 4096 || std::memcmp(copy, chip.state.handles, sizeof(copy)) != 0;
    s.readSetValid = true;
}

// After a swap with drawing off, run the new list once without drawing: the per-handle
// bitmap parameters persist across lines, so their state after one line equals their
// state after any number of lines.
void StatePass(EveChip& chip)
{
    LineRun run;
    PrepareRun(chip, run, 0, true);
    ExecuteLine<LineMode::StateOnly>(run);
    if (kContextReset != ContextReset::PerLine)
        chip.state.context = run.ctx;
}

} // namespace

// --- Handles ------------------------------------------------------------------------------------

uint32_t HandleWidth(const BitmapHandle& h)
{
    const uint32_t width = h.widthLow | (static_cast<uint32_t>(h.widthHigh) << kHighBitsShift);
    return width == 0 ? kBitmapSizeFull : width;
}

uint32_t HandleHeight(const BitmapHandle& h)
{
    const uint32_t height = h.heightLow | (static_cast<uint32_t>(h.heightHigh) << kHighBitsShift);
    return height == 0 ? kBitmapSizeFull : height;
}

uint32_t HandleStride(const BitmapHandle& h)
{
    return h.strideLow | (static_cast<uint32_t>(h.strideHigh) << kStrideLowBits);
}

uint32_t HandleLayoutHeight(const BitmapHandle& h)
{
    return h.layoutHeightLow | (static_cast<uint32_t>(h.layoutHeightHigh) << kHighBitsShift);
}

void SetHandleFromMetrics(BitmapHandle& h, uint32_t format, uint32_t stride, uint32_t width, uint32_t height,
                          uint32_t source)
{
    constexpr uint32_t kLowMask9 = (1u << kBitmapSizeBits) - 1;
    constexpr uint32_t kLowMask10 = (1u << kStrideLowBits) - 1;
    h = BitmapHandle{};
    h.source = source;
    h.format = static_cast<uint8_t>(format);
    h.strideLow = static_cast<uint16_t>(stride & kLowMask10);
    h.strideHigh = static_cast<uint8_t>(stride >> kStrideLowBits);
    h.layoutHeightLow = static_cast<uint16_t>(height & kLowMask9);
    h.layoutHeightHigh = static_cast<uint8_t>(height >> kBitmapSizeBits);
    h.widthLow = static_cast<uint16_t>(width & kLowMask9);
    h.widthHigh = static_cast<uint8_t>(width >> kBitmapSizeBits);
    h.heightLow = static_cast<uint16_t>(height & kLowMask9);
    h.heightHigh = static_cast<uint8_t>(height >> kBitmapSizeBits);
}

// Bitmap parameters of a ROM font (spec §7.6): handle -> ROM font.
void LoadRomFontHandle(EveChip& chip, uint32_t handle, uint32_t font)
{
    const uint32_t block = RomWord(chip, kRomFontRootAddress) + kFontMetricSize * (font - kFirstRomFont);
    SetHandleFromMetrics(chip.state.handles[handle], RomWord(chip, block + kFontMetricFormat),
                         RomWord(chip, block + kFontMetricStride), RomWord(chip, block + kFontMetricWidth),
                         RomWord(chip, block + kFontMetricHeight), RomWord(chip, block + kFontMetricData));
}

// --- Context -------------------------------------------------------------------------------------

void ContextDefaults(GraphicsContext& ctx)
{
    // [PG §4.1 Table 5], corrected per spec §6.1.
    constexpr uint8_t kAllChannels = kMaskRed | kMaskGreen | kMaskBlue | kMaskAlpha;
    constexpr uint16_t kDefaultPointSize = 16;
    constexpr uint16_t kDefaultLineWidth = 16;
    constexpr uint8_t kDefaultTag = 255;
    constexpr uint8_t kDefaultVertexFormat = 4;
    ctx = GraphicsContext{};
    ctx.clearColorRgb = 0;
    ctx.colorRgb = kWhite;
    ctx.colorA = kChannelMax;
    ctx.clearColorA = 0;
    ctx.alphaFunc = kFuncAlways;
    ctx.alphaRef = 0;
    ctx.stencilFunc = kFuncAlways;
    ctx.stencilRef = 0;
    ctx.stencilFuncMask = kChannelMax;
    ctx.stencilWriteMask = kChannelMax;
    ctx.stencilFail = kStencilKeep;
    ctx.stencilPass = kStencilKeep;
    ctx.blendSrc = kBlendSrcAlpha;
    ctx.blendDst = kBlendOneMinusSrcAlpha;
    ctx.pointSize = kDefaultPointSize;
    ctx.lineWidth = kDefaultLineWidth;
    ctx.scissorX = 0;
    ctx.scissorY = 0;
    ctx.scissorWidth = kScissorInitialSize;
    ctx.scissorHeight = kScissorInitialSize;
    ctx.tag = kDefaultTag;
    ctx.tagMask = 1;
    ctx.colorMask = kAllChannels;
    ctx.transform[0] = kFixedOneTransform;
    ctx.transform[4] = kFixedOneTransform;
    ctx.vertexFormat = kDefaultVertexFormat;
}

template <LineMode Mode>
void ExecuteLine(LineRun& run)
{
    const uint8_t* list = ActiveDl(*run.chip);
    uint32_t stack[kCallStackDepth] = {};
    uint32_t depth = 0;
    uint32_t pc = 0;
    for (uint32_t executed = 0;; ++executed)
    {
        if (executed == kMaxCommandsPerLine)
        {
            ++run.events;
            break;
        }
        if (pc >= kDlWords)
            break; // ran off the end of RAM_DL (spec §6.2, V15)
        const uint32_t word = LoadLe32(list + kDlWordBytes * pc);
        ++run.commands;
        run.commandIndex = pc;
        run.commandWord = word;
        uint32_t target = pc + 1;
        const Flow flow = ExecuteWord<Mode>(run, word, target, stack, depth, pc);
        if (flow == Flow::Stop)
            break;
        pc = flow == Flow::Jump ? target : pc + 1;
    }
}

template void ExecuteLine<LineMode::Draw>(LineRun&);
template void ExecuteLine<LineMode::Probe>(LineRun&);
template void ExecuteLine<LineMode::StateOnly>(LineRun&);

// --- Entry points ---------------------------------------------------------------------------------

bool InitDrawing(EveChip& chip)
{
    InitBitmapTables();
    chip.bitmapFastPath = true;
    chip.lineColor.reset(new (std::nothrow) uint8_t[kMaxLineWidth * kChannels]());
    chip.lineStencil.reset(new (std::nothrow) uint8_t[kMaxLineWidth]());
    chip.lineTag.reset(new (std::nothrow) uint8_t[kMaxLineWidth]());
    chip.lineTexels.reset(new (std::nothrow) uint32_t[kMaxLineWidth]());
    chip.probeColor.reset(new (std::nothrow) uint8_t[kMaxLineWidth * kChannels]());
    chip.probeStencil.reset(new (std::nothrow) uint8_t[kMaxLineWidth]());
    chip.probeTag.reset(new (std::nothrow) uint8_t[kMaxLineWidth]());
    chip.lineCosts.reset(new (std::nothrow) EveLineCost[kMaxLines]());
    return chip.lineColor && chip.lineStencil && chip.lineTag && chip.lineTexels && chip.probeColor && chip.probeStencil &&
           chip.probeTag && chip.lineCosts;
}

void DrawingReset(EveChip& chip)
{
    for (BitmapHandle& h : chip.state.handles)
        h = BitmapHandle{};
    if (kRomFontHandlesAtReset && chip.romImage != nullptr)
        for (uint32_t handle = kFirstFontHandle; handle < kHandleCount; ++handle)
            LoadRomFontHandle(chip, handle, handle);
    ContextDefaults(chip.state.context);
    DrawingInvalidate(chip);
}

void CatchUp(EveChip& chip)
{
    const uint32_t due = LinesDue(chip);
    if (!chip.drawing || !ScanRunning(chip))
    {
        if (chip.drawnLines < due)
            chip.drawnLines = due;
        return;
    }
    if (Portrait(chip))
    {
        if (chip.drawnLines == 0 && due > 0)
            DrawPortraitFrame(chip);
        chip.drawnLines = due;
        return;
    }
    CatchUpLines(chip, due);
}

void PocCatchUpDeferred(EveChip& chip)
{
    PocState& s = Poc(chip);
    if (s.deferredDue > chip.drawnLines && chip.drawing && ScanRunning(chip) && !Portrait(chip))
        CatchUpLines(chip, std::min(s.deferredDue, LinesDue(chip)));
}

void PocInvalidateReadSet(EveChip& chip)
{
    PocState& s = Poc(chip);
    s.readSetValid = false;
    // a swap or a drawing register: the picture may change from here on (11)
    s.dirty = true;
    s.changedThisFrame = true;
}

void PocNoteRamGWrite(EveChip& chip, uint32_t address)
{
    PocState& s = Poc(chip);
    if (!s.skipUnchanged || (s.dirty && s.changedThisFrame))
        return;
    bool relevant = true;
    if (s.deferWrites && ScanRunning(chip) && !Portrait(chip))
    {
        if (!s.readSetValid || std::memcmp(s.readSetHandles, chip.state.handles, sizeof(s.readSetHandles)) != 0)
            BuildReadSet(chip, s);
        relevant = s.readSetOverflow;
        for (const ReadRange& r : s.readSet)
            relevant = relevant || (address >= r.from && address < r.to);
    }
    if (relevant)
    {
        s.dirty = true;
        s.changedThisFrame = true;
    }
}

void PocWriteDeferred(EveChip& chip)
{
    PocState& s = Poc(chip);
    const uint32_t due = LinesDue(chip);
    s.deferredDue = std::max(s.deferredDue, due);
    ++s.deferredWrites;
}

bool PocWriteNeedsCatchUp(EveChip& chip, uint32_t address)
{
    PocState& s = Poc(chip);
    if (!s.deferWrites)
        return true;
    if (chip.drawnLines >= LinesDue(chip))
        return false; // nothing due: CatchUp would draw nothing
    if (!ScanRunning(chip) || Portrait(chip))
        return true;
    if (!s.readSetValid || std::memcmp(s.readSetHandles, chip.state.handles, sizeof(s.readSetHandles)) != 0)
        BuildReadSet(chip, s);
    if (s.readSetOverflow)
        return true;
    for (const ReadRange& r : s.readSet)
        if (address >= r.from && address < r.to)
            return true;
    return false;
}

void FrameStart(EveChip& chip)
{
    Poc(chip).deferredDue = 0;
    {
        // 11: a frame may be skipped while nothing it reads has changed since the last frame
        // that was drawn completely after the last change
        PocState& s = Poc(chip);
        s.dirty = s.changedThisFrame;
        s.changedThisFrame = false;
        ++s.frameIndex;
    }
    chip.drawnLines = 0;
    chip.overflowLines = 0;
}

void DisplayListSwapped(EveChip& chip)
{
    if (!chip.drawing)
        StatePass(chip);
}

GpuHooks g_gpu = {nullptr, nullptr, nullptr};

void DrawingInvalidate(EveChip& chip)
{
    Poc(chip).dirty = true;
    Poc(chip).changedThisFrame = true;
    if (g_gpu.invalidate != nullptr)
        g_gpu.invalidate(chip);
    // The current frame is drawn again from line 0 by the next catch-up (arch §7.4).
    chip.drawnLines = 0;
    for (uint32_t i = 0; i < kMaxLines; ++i)
        chip.lineCosts[i] = EveLineCost{};
}

void GetLineCost(const EveChip& chip, uint32_t line, EveLineCost& out)
{
    if (line < kMaxLines)
        out = chip.lineCosts[line];
    out.line = line;
}

bool ProbePixel(const EveChip& chip, uint32_t x, uint32_t y, EvePixelSource& out)
{
    if (y >= VisibleLines(chip) || x >= LineWidth(chip))
        return false;
    // Re-run the line on the probe buffers; the chip itself is not changed.
    EveChip& scratch = const_cast<EveChip&>(chip);
    BitmapHandle handles[kHandleCount];
    std::memcpy(handles, chip.state.handles, sizeof(handles));
    bool mirrorX = false;
    const uint32_t logicalLine = LogicalLine(chip, y, mirrorX);
    LineRun run;
    PrepareRun(scratch, run, logicalLine, y == 0, true);
    run.probeX = static_cast<int32_t>(mirrorX ? run.width - 1 - x : x);
    run.probe = &out;
    ExecuteLine<LineMode::Probe>(run);
    // The run may change per-handle parameters as the live line would; put them back.
    std::memcpy(scratch.state.handles, handles, sizeof(handles));
    const uint8_t* pixel = run.color + kChannels * static_cast<uint32_t>(run.probeX);
    out.color = (static_cast<uint32_t>(kChannelMax) << 24) | (static_cast<uint32_t>(pixel[0]) << 16) |
                (static_cast<uint32_t>(pixel[1]) << 8) | pixel[2];
    out.tag = run.tag[run.probeX];
    out.stencil = run.stencil[run.probeX];
    return true;
}

} // namespace EveLib

// --- eve-accel POC hooks ----------------------------------------------------------------------

using namespace EveLib;

extern "C" {

const char* EvePocVariant(void)
{
    return g_gpu.draw != nullptr ? "gpu" : "threads";
}

int EvePocSetThreads(EveChip* chip, int threads)
{
    PocState& s = Poc(*chip);
    if (s.pool)
        return s.threads; // fixed once the pool runs
    s.threads = threads < 1 ? 1 : threads;
    return s.threads;
}

void EvePocSetSnapshot(EveChip* chip, const char* dir, uint64_t fromFrame, uint64_t toFrame)
{
    PocState& s = Poc(*chip);
    s.snapDir = dir;
    s.snapFrom = fromFrame;
    s.snapTo = toFrame;
    const char* step = std::getenv("EVE_POC_SNAP_STEP");
    s.snapStep = step != nullptr && std::atoi(step) > 0 ? static_cast<uint64_t>(std::atoi(step)) : 1;
}

void EvePocPrintStats(EveChip* chip, FILE* out)
{
    const PocState& s = Poc(*chip);
    const double lines = static_cast<double>(s.serialLines + s.parallelLines);
    std::fprintf(out, "threads %d, parallel from %u lines, deferred writes %s\n", s.threads, s.minParallel,
                 s.deferWrites ? "on" : "off");
    std::fprintf(out, "batches %llu (parallel %llu); lines serial %llu, parallel %llu (%.2f %% serial); "
                      "divergences %llu, lines drawn again %llu\n",
                 (unsigned long long)s.batches, (unsigned long long)s.parallelBatches,
                 (unsigned long long)s.serialLines, (unsigned long long)s.parallelLines,
                 lines > 0 ? 100.0 * static_cast<double>(s.serialLines) / lines : 0.0,
                 (unsigned long long)s.divergences, (unsigned long long)s.redrawnLines);
    if (s.skipUnchanged || s.lineStep > 1)
        std::fprintf(out, "cheap: %s%s, lines not drawn %llu\n", s.skipUnchanged ? "skip unchanged" : "",
                     s.lineStep > 1 ? (" line step " + std::to_string(s.lineStep)).c_str() : "",
                     (unsigned long long)s.skippedLines);
    std::fprintf(out, "RAM_G writes that skipped the catch-up: %llu; read set built %llu times; snapshots %llu\n",
                 (unsigned long long)s.deferredWrites, (unsigned long long)s.readSetBuilds,
                 (unsigned long long)s.snapshots);
    if (g_gpu.draw != nullptr)
        std::fprintf(out, "GPU: batches %llu, lines %llu (%.1f %% of all lines), from %u lines; CPU fallbacks %llu (last: %s); "
                          "%.3f s in the GPU path\n",
                     (unsigned long long)s.gpuBatches, (unsigned long long)s.gpuLines,
                     lines > 0 ? 100.0 * static_cast<double>(s.gpuLines) / (lines + static_cast<double>(s.gpuLines)) : 0.0,
                     s.gpuMinLines, (unsigned long long)s.gpuFallbacks,
                     s.gpuFallbackReason ? s.gpuFallbackReason : "-", s.gpuSeconds);
    if (g_gpu.draw != nullptr && s.gpuAuto)
        std::fprintf(out, "auto: GPU batch = %.1f us + %.3f us x lines, CPU %.3f us per line (%llu GPU / %llu CPU samples); "
                          "break-even at %.0f lines\n",
                     s.gpuA / 1000.0, s.gpuB / 1000.0, s.cpuNsPerLine / 1000.0, (unsigned long long)s.gpuSamples,
                     (unsigned long long)s.cpuSamples,
                     s.cpuNsPerLine > s.gpuB ? s.gpuA / (s.cpuNsPerLine - s.gpuB) : -1.0);
}
}

