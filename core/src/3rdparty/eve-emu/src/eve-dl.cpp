// eve-emu - line executor, context, flow control, line cost, drawing in step with the
// beam (spec §5.1, §5.2, §6.1-6.3, arch §8.4).
#include "eve-profile.h"
#include "eve-render.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#if !defined(_WIN32)
#include <time.h>
#endif

#include <new>

namespace EveLib
{

// CPU time of the calling thread (ns): what drawing cost with the time the thread was not running left out (a loaded
// host preempts it for tens of ms). Wall time where the system has no per-thread clock (Windows). Read once per frame:
// a read costs microseconds (macOS: a system call)
uint64_t ThreadCpuNs()
{
#if !defined(_WIN32)
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
        return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
#endif
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct HostDrawStats
{
    // The frame being drawn (the drawing thread only)
    uint32_t work[kMaxLines] = {};
    uint64_t cpuMark = 0;  // the publishing thread's CPU time at the previous frame end
    std::thread::id cpuThread; // ... and that thread (the renderer started or stopped: a new mark)
    // Published at each frame end (under `lock`)
    std::mutex lock;
    EveHostDrawStats last{};
    uint32_t lineNs[kMaxLines] = {};
    EveHostDrawStats summary{}; // since EveResetHostDrawStats: frames, sum, max
    uint32_t maxLineNs[kMaxLines] = {}; // the lines of summary's dearest frame
};


namespace
{

constexpr uint32_t kStrideLowBits = kHandleStrideLowBits;
constexpr uint32_t kVertexFormatMax = 4;              // 1/16 pixel
constexpr uint32_t kDisplayListIndexMask = 0xFFFF;    // CALL / JUMP destination field
constexpr uint32_t kWhite = 0xFFFFFF;

// A word of the ROM image (VDAC4: also before the font copy, as the core's preset handles)
uint32_t RomWord(const EveChip& chip, uint32_t address)
{
    return static_cast<uint32_t>(RomImageByte(chip, address)) |
           (static_cast<uint32_t>(RomImageByte(chip, address + 1)) << 8) |
           (static_cast<uint32_t>(RomImageByte(chip, address + 2)) << 16) |
           (static_cast<uint32_t>(RomImageByte(chip, address + 3)) << 24);
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

// A line worker's own buffers, decoded palettes and handles (DrawLinesParallel)
struct LineScratch
{
    std::unique_ptr<uint8_t[]> color{new (std::nothrow) uint8_t[kMaxLineWidth * kChannels]()};
    std::unique_ptr<uint8_t[]> stencil{new (std::nothrow) uint8_t[kMaxLineWidth]()};
    std::unique_ptr<uint8_t[]> tag{new (std::nothrow) uint8_t[kMaxLineWidth]()};
    std::unique_ptr<uint32_t[]> texels{new (std::nothrow) uint32_t[kMaxLineWidth]()};
    std::unique_ptr<uint32_t[]> bilinear{new (std::nothrow) uint32_t[kBilinearScratch]()};
    std::unique_ptr<PaletteCache[]> palettes{new (std::nothrow) PaletteCache[kPaletteCacheEntries]()};
    uint32_t paletteNext = 0;
    BitmapHandle handles[kHandleCount] = {};
    LineRepeat repeat;

    bool Ok() const { return color && stencil && tag && texels && bilinear && palettes && repeat.color; }
};

void PrepareRun(EveChip& chip, LineRun& run, uint32_t logicalLine, bool firstLine, bool probe = false,
                LineScratch* scratch = nullptr)
{
    run.chip = &chip;
    run.y = logicalLine;
    run.width = LineWidth(chip);
    if (scratch)
    {
        run.color = scratch->color.get();
        run.stencil = scratch->stencil.get();
        run.tag = scratch->tag.get();
        run.texels = scratch->texels.get();
        run.bilinear = scratch->bilinear.get();
        run.handles = scratch->handles;
        run.palettes = scratch->palettes.get();
        run.paletteNext = &scratch->paletteNext;
        run.repeat = &scratch->repeat;
    }
    else
    {
        run.color = probe ? chip.probeColor.get() : chip.lineColor.get();
        run.stencil = probe ? chip.probeStencil.get() : chip.lineStencil.get();
        run.tag = probe ? chip.probeTag.get() : chip.lineTag.get();
        run.texels = chip.lineTexels.get();
        run.bilinear = chip.lineBilinear.get();
        run.handles = chip.state.handles;
        run.palettes = chip.palettes.get();
        run.paletteNext = &chip.paletteNext;
        run.repeat = probe || !chip.linePlan->repeat.color ? nullptr : &chip.linePlan->repeat;
    }
    run.paletteHit = 0;
    run.fromPlan = false;
    run.savedCount = 0;
    run.recording = nullptr;
    run.tagLive = probe || logicalLine == RegGet(chip, Reg::TagY);
    run.keepLine = -1;
    run.pixelsKept = false;
    run.remembered = false;
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
    if (run.tagLive)
        std::memset(run.tag, 0, run.width);
}

// `overflowLines`: where an overflowing line is counted (a line worker's count; else the chip's)
void FinishRun(EveChip& chip, const LineRun& run, uint32_t screenLine, uint32_t* overflowLines = nullptr)
{
    if constexpr (kContextReset != ContextReset::PerLine)
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
        ++(overflowLines ? *overflowLines : chip.overflowLines);
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

constexpr bool kLinePlanUsable = kContextReset == ContextReset::PerLine;
bool PlanMatches(const EveChip& chip, const LinePlan& plan);

// --- Kept lines (EveChip::lineKept) ------------------------------------------------------------

bool LineInputsUnchanged(const EveChip& chip, const LineKept& k)
{
    return k.valid && k.ramGChanges == chip.ramGWrites && k.drawRegChanges == chip.drawRegChanges &&
           k.dlVersion == chip.dlVersion && k.outputVersion == chip.outputVersion &&
           k.planRecord == chip.linePlan->record && PlanMatches(chip, *chip.linePlan);
}

// A line whose inputs did not change since it was drawn into the frame buffer is still
// there: only what drawing it does besides the pixels happens - the handles become what
// the walk leaves (the recorded walk's), the overflow count counts it. REG_TAG's line is
// always drawn (REG_TAG_X / Y are not inputs of the other lines).
bool LineKeepable(const EveChip& chip, uint32_t screenLine, uint32_t logicalLine)
{
    if constexpr (!kLinePlanUsable)
        return false;
    return chip.lineKeepEnabled && chip.linePlanEnabled && screenLine < kMaxLines &&
           logicalLine != RegGet(chip, Reg::TagY) && LineInputsUnchanged(chip, chip.lineKept[screenLine]);
}

bool KeepLine(EveChip& chip, uint32_t screenLine, uint32_t logicalLine)
{
    if (!LineKeepable(chip, screenLine, logicalLine))
        return false;
    std::memcpy(chip.state.handles, chip.linePlan->endHandles, sizeof(chip.state.handles));
    if (chip.lineCosts[screenLine].overflow)
        ++chip.overflowLines;
#ifdef EVE_PROFILE
    Profile().linesKept++;
#endif
    return true;
}

// After drawing a line from the recorded walk: the inputs it was drawn with
void RememberLine(EveChip& chip, uint32_t screenLine, bool fromPlan)
{
    if (screenLine >= kMaxLines)
        return;
    LineKept& k = chip.lineKept[screenLine];
    k.valid = fromPlan && chip.linePlan->valid;
    k.ramGChanges = chip.ramGWrites;
    k.drawRegChanges = chip.drawRegChanges;
    k.dlVersion = chip.dlVersion;
    k.outputVersion = chip.outputVersion;
    k.planRecord = chip.linePlan->record;
}

void DrawVisibleLine(EveChip& chip, uint32_t screenLine)
{
    bool mirrorX = false;
    const uint32_t logicalLine = LogicalLine(chip, screenLine, mirrorX);
    if (KeepLine(chip, screenLine, logicalLine))
        return;
    LineRun run;
    PrepareRun(chip, run, logicalLine, screenLine == 0);
    if (screenLine < kMaxLines && chip.lineKeepEnabled && !run.tagLive)
        run.keepLine = static_cast<int32_t>(screenLine);
#ifdef EVE_PROFILE
    const uint64_t lineStart = ProfileNow();
#endif
    ExecuteLine<LineMode::Draw>(run);
#ifdef EVE_PROFILE
    Profile().lines.calls++;
    Profile().lines.pixels += run.commands;
    Profile().lines.nanos += ProfileNow() - lineStart;
#endif
    FinishRun(chip, run, screenLine);
    // REG_TAG: the tag buffer at (REG_TAG_X, REG_TAG_Y) of the drawn frame (spec §3.3).
    if (logicalLine == RegGet(chip, Reg::TagY))
    {
        const uint32_t tagX = RegGet(chip, Reg::TagX);
        if (tagX < run.width)
            RegSet(chip, Reg::Tag, run.tag[tagX]);
    }
    if (!run.pixelsKept)
    {
        OutputLine(chip, screenLine, run.color, run.width, mirrorX);
        // Drawn without a signature (REG_TAG's line, no recorded walk): an older one no
        // longer describes the frame buffer
        if (!run.remembered && screenLine < chip.linePlan->lines.size())
            chip.linePlan->lines[screenLine].valid = false;
    }
    RememberLine(chip, screenLine, run.fromPlan);
}


} // namespace

// --- Lines on worker threads (EveSetDrawThreads) ------------------------------------------------
//
// While every line replays the recorded walk and the walk leaves the handles as it found them (start handles equal
// to end handles), a line's drawing reads the chip and writes only what is its own: its pixels in the frame buffer,
// its cost, its kept-line record, its signature in the plan. Such lines are drawn side by side, each worker with its
// own line buffers, decoded palettes and handle copy; REG_TAG's line (it sets REG_TAG) and lines past kMaxLines stay
// on the calling thread. The calling thread draws too: workers that wake late find no lines left, so a batch never
// waits for a thread the host did not run.

struct LineWorkers
{
    std::vector<std::thread> threads;
    std::vector<std::unique_ptr<LineScratch>> scratch; // [0] the calling thread's, [1 + i] worker i's
    std::mutex lock;
    std::condition_variable start;
    std::condition_variable done;
    bool stop = false;

    // The batch: published under `lock`; lines are claimed through `cursor` (batch number << 32 | next line), so a
    // worker that wakes after its batch ended claims nothing of the next one
    uint64_t batch = 0;
    EveChip* chip = nullptr;
    uint32_t last = 0;
    uint32_t tagLine = UINT32_MAX; // REG_TAG's line: left to the calling thread
    std::atomic<uint64_t> cursor{0};
    std::atomic<uint32_t> linesLeft{0};
    std::atomic<uint32_t> overflow{0};
};

void LineWorkersDelete::operator()(LineWorkers* workers) const
{
    if (!workers)
        return;
    {
        std::lock_guard<std::mutex> guard(workers->lock);
        workers->stop = true;
    }
    workers->start.notify_all();
    for (std::thread& t : workers->threads)
        t.join();
    delete workers;
}

namespace
{

void DrawWorkerLine(EveChip& chip, LineScratch& scratch, uint32_t screenLine, uint32_t& overflow)
{
    bool mirrorX = false;
    const uint32_t logicalLine = LogicalLine(chip, screenLine, mirrorX);
    if (LineKeepable(chip, screenLine, logicalLine))
    {
        if (chip.lineCosts[screenLine].overflow)
            ++overflow;
        return;
    }
    LinePlan& plan = *chip.linePlan;
    std::memcpy(scratch.handles, plan.startHandles, sizeof(scratch.handles));
    LineRun run;
    PrepareRun(chip, run, logicalLine, screenLine == 0, false, &scratch);
    if (chip.lineKeepEnabled && !run.tagLive)
        run.keepLine = static_cast<int32_t>(screenLine);
    ExecuteLine<LineMode::Draw>(run);
    FinishRun(chip, run, screenLine, &overflow);
    if (!run.pixelsKept)
    {
        OutputLine(chip, screenLine, run.color, run.width, mirrorX);
        if (!run.remembered && screenLine < plan.lines.size())
            plan.lines[screenLine].valid = false;
    }
    RememberLine(chip, screenLine, run.fromPlan);
}

// Claims and draws lines of batch `batch` until none is left; true if it drew any
// Lines are claimed a few at a time: neighbours drawn by one thread repeat each other's texel rows (LineRepeat)
constexpr uint32_t kClaimLines = 8;

// Claims and draws lines of batch `batch` until none is left
void DrawClaimedLines(LineWorkers& w, LineScratch& scratch, uint64_t batch, EveChip& chip, uint32_t last,
                      uint32_t tagLine)
{
    HostDrawStats* stats = chip.hostStats.get();
    for (;;)
    {
        uint64_t c = w.cursor.load(std::memory_order_relaxed);
        uint32_t line, count;
        do
        {
            line = static_cast<uint32_t>(c);
            if ((c >> 32) != (batch & 0xFFFFFFFFu) || line >= last)
                return;
            count = std::min(kClaimLines, last - line);
        } while (!w.cursor.compare_exchange_weak(c, c + count, std::memory_order_acq_rel, std::memory_order_relaxed));
        uint32_t overflow = 0;
        for (uint32_t l = line; l < line + count; ++l)
        {
            if (l == tagLine)
                continue;
            const auto before = stats ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            DrawWorkerLine(chip, scratch, l, overflow);
            if (stats)
                stats->work[l] = static_cast<uint32_t>(std::min<int64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - before).count(),
                    UINT32_MAX));
        }
        // The overflow count is in before the lines are counted done: the calling thread reads it once none is left
        if (overflow)
            w.overflow.fetch_add(overflow, std::memory_order_relaxed);
        if (w.linesLeft.fetch_sub(count, std::memory_order_acq_rel) == count)
        {
            std::lock_guard<std::mutex> guard(w.lock);
            w.done.notify_all();
        }
    }
}

void WorkerLoop(LineWorkers* w, size_t index)
{
    uint64_t seen = 0;
    for (;;)
    {
        uint64_t batch;
        EveChip* chip;
        uint32_t last, tagLine;
        {
            std::unique_lock<std::mutex> guard(w->lock);
            w->start.wait(guard, [&] { return w->stop || w->batch != seen; });
            if (w->stop)
                return;
            seen = batch = w->batch;
            chip = w->chip;
            last = w->last;
            tagLine = w->tagLine;
        }
        if (!chip)
            continue;
        DrawClaimedLines(*w, *w->scratch[index], batch, *chip, last, tagLine);
    }
}

// The chip's workers as many as it asks for (none: false)
bool EnsureLineWorkers(EveChip& chip)
{
    const uint32_t wanted = chip.drawThreads > 1 ? chip.drawThreads - 1 : 0;
    if (chip.lineWorkers && chip.lineWorkers->threads.size() == wanted)
        return wanted > 0;
    chip.lineWorkers.reset();
    if (wanted == 0)
        return false;
    std::unique_ptr<LineWorkers, LineWorkersDelete> w(new (std::nothrow) LineWorkers());
    if (!w)
        return false;
    for (uint32_t i = 0; i <= wanted; ++i)
    {
        w->scratch.emplace_back(new (std::nothrow) LineScratch());
        if (!w->scratch.back() || !w->scratch.back()->Ok())
            return false;
    }
    for (uint32_t i = 0; i < wanted; ++i)
        w->threads.emplace_back(WorkerLoop, w.get(), static_cast<size_t>(i + 1));
    chip.lineWorkers = std::move(w);
    return true;
}

// Every line from here replays the plan and leaves the handles as it found them
bool LinesIndependent(const EveChip& chip)
{
#ifdef EVE_PROFILE
    return false; // the counters are not per thread
#else
    if constexpr (!kLinePlanUsable)
        return false;
    const LinePlan* plan = chip.linePlan.get();
    return chip.linePlanEnabled && plan && PlanMatches(chip, *plan) &&
           std::memcmp(plan->startHandles, plan->endHandles, sizeof(plan->startHandles)) == 0;
#endif
}

// Lines drawnLines..last-1 (all below kMaxLines) on the workers and the calling thread
void DrawLinesParallel(EveChip& chip, uint32_t last)
{
    LineWorkers& w = *chip.lineWorkers;
    LinePlan& plan = *chip.linePlan;
    if (plan.lines.size() < kMaxLines)
        plan.lines.resize(kMaxLines);
    const uint32_t first = chip.drawnLines;
    // REG_TAG's line: drawn here afterwards (it sets REG_TAG)
    uint32_t tagLine = UINT32_MAX;
    for (uint32_t line = first; line < last; ++line)
    {
        bool mirrorX = false;
        if (LogicalLine(chip, line, mirrorX) == RegGet(chip, Reg::TagY))
        {
            tagLine = line;
            break;
        }
    }
    uint64_t batch;
    {
        std::lock_guard<std::mutex> guard(w.lock);
        batch = ++w.batch;
        w.chip = &chip;
        w.last = last;
        w.tagLine = tagLine;
        w.overflow.store(0, std::memory_order_relaxed);
        w.linesLeft.store(last - first, std::memory_order_relaxed);
        w.cursor.store(((batch & 0xFFFFFFFFu) << 32) | first, std::memory_order_release);
    }
    w.start.notify_all();
    DrawClaimedLines(w, *w.scratch[0], batch, chip, last, tagLine);
    {
        std::unique_lock<std::mutex> guard(w.lock);
        w.done.wait(guard, [&] { return w.linesLeft.load(std::memory_order_acquire) == 0; });
        w.chip = nullptr;
    }
    chip.overflowLines += w.overflow.load(std::memory_order_relaxed);
    chip.parallelLines += last - first;
    chip.drawnLines = last;
    if (tagLine != UINT32_MAX)
    {
        HostDrawStats* stats = chip.hostStats.get();
        const auto before = std::chrono::steady_clock::now();
        DrawVisibleLine(chip, tagLine);
        if (stats)
            stats->work[tagLine] = static_cast<uint32_t>(std::min<int64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - before).count(),
                UINT32_MAX));
    }
}

} // namespace

namespace
{

// --- Execution ----------------------------------------------------------------------------

template <LineMode Mode>
void Vertex2(LineRun& run, const Vertex& v)
{
    if constexpr (Mode == LineMode::StateOnly)
    {
        (void)run;
        (void)v;
        return;
    }
    else
    {
#ifdef EVE_PROFILE
    const uint64_t primitiveStart = run.primitive != kPrimBitmaps && Mode == LineMode::Draw ? ProfileNow() : 0;
#endif
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
#ifdef EVE_PROFILE
    if (primitiveStart != 0)
    {
        ProfileCell& cell = Profile().primitives[run.primitive];
        ++cell.calls;
        cell.nanos += ProfileNow() - primitiveStart;
    }
#endif
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

// --- Line plan (eve-render.h LinePlan) ------------------------------------------------------


void RecordStep(LineRun& run, const Vertex& v, bool clear, uint32_t clearMask)
{
    LinePlan& plan = *run.recording;
    if (plan.contexts.empty() || std::memcmp(&plan.contexts.back(), &run.ctx, sizeof(GraphicsContext)) != 0)
        plan.contexts.push_back(run.ctx);
    LinePlanStep step{};
    step.clear = clear;
    step.primitive = run.primitive;
    step.context = static_cast<uint32_t>(plan.contexts.size() - 1);
    step.vertexCount = run.vertexCount;
    step.clearMask = clearMask;
    step.v = v;
    step.previous = run.previous;
    step.commandIndex = run.commandIndex;
    step.commandWord = run.commandWord;
    if (!clear)
    {
        step.handle = run.handles[v.handle];
        step.rowTest = run.primitive == kPrimBitmaps;
        step.rowsSubpixel = static_cast<int64_t>(HandleHeight(step.handle)) * kSubpixel;
        // Points, lines and rectangles draw only near their vertices (DrawPoint / DrawLine /
        // DrawRect return early beyond the radius); edge strips reach to the screen edge
        const bool pair = run.primitive == kPrimLines || run.primitive == kPrimLineStrip || run.primitive == kPrimRects;
        if (run.primitive == kPrimPoints || pair)
        {
            const int64_t reach = ShapeReach(run.primitive == kPrimPoints ? run.ctx.pointSize : run.ctx.lineWidth);
            const int64_t y0 = pair && run.vertexCount > 0 && run.previous.y < v.y ? run.previous.y : v.y;
            const int64_t y1 = pair && run.vertexCount > 0 && run.previous.y > v.y ? run.previous.y : v.y;
            step.extentTest = true;
            step.yFirst = y0 - reach;
            step.yLast = y1 + reach;
        }
    }
    plan.steps.push_back(step);
}

bool PlanMatches(const EveChip& chip, const LinePlan& plan)
{
    return plan.valid && plan.dlVersion == chip.dlVersion &&
           plan.macro0 == RegGet(chip, Reg::Macro0) && plan.macro1 == RegGet(chip, Reg::Macro1) &&
           std::memcmp(plan.startHandles, chip.state.handles, sizeof(plan.startHandles)) == 0;
}

void StartRecording(LineRun& run, LinePlan& plan)
{
    const EveChip& chip = *run.chip;
    plan.valid = false;
    plan.dlVersion = chip.dlVersion;
    plan.macro0 = RegGet(chip, Reg::Macro0);
    plan.macro1 = RegGet(chip, Reg::Macro1);
    std::memcpy(plan.startHandles, chip.state.handles, sizeof(plan.startHandles));
    plan.contexts.clear();
    plan.steps.clear();
    run.recording = &plan;
}

void RememberSteps(LineRun& run, LinePlan& plan);
void IndexSteps(LinePlan& plan);

void FinishRecording(LineRun& run)
{
    LinePlan& plan = *run.recording;
    std::memcpy(plan.endHandles, run.handles, sizeof(plan.endHandles));
    plan.commands = run.commands;
    plan.events = run.events;
    ++plan.record;
    IndexSteps(plan);
    LinePlan::Recording& kept = plan.history[plan.record % LinePlan::kHistory];
    kept.record = plan.record;
    kept.steps = plan.steps;
    kept.contexts = plan.contexts;
    plan.valid = true;
    run.recording = nullptr;
    RememberSteps(run, plan);
}

// --- Lines kept by their steps (eve-render.h LineSignature) -----------------------------------

bool StepReachesLine(const LinePlanStep& s, int64_t lineSubpixel)
{
    if (s.extentTest)
        return lineSubpixel >= s.yFirst && lineSubpixel <= s.yLast;
    if (!s.rowTest)
        return true;
    const int64_t rely = lineSubpixel - s.v.y;
    return rely >= 0 && rely < s.rowsSubpixel;
}

constexpr uint32_t kPlanBandShift = 4; // 16 lines per band
constexpr uint32_t kPlanBands = kMaxLines >> kPlanBandShift;

int64_t FloorDiv(int64_t a, int64_t b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// The logical lines a step may reach, [first, last] (StepReachesLine's tests solved for the line); false: none
bool StepLines(const LinePlanStep& s, int64_t& first, int64_t& last)
{
    // A line y reaches subpixel row y * kSubpixel + kPixelCenter
    int64_t lo = 0, hi = kMaxLines - 1;
    if (s.extentTest)
    {
        lo = -FloorDiv(-(s.yFirst - kPixelCenter), kSubpixel);  // ceil
        hi = FloorDiv(s.yLast - kPixelCenter, kSubpixel);
    }
    else if (s.rowTest)
    {
        lo = -FloorDiv(-(s.v.y - kPixelCenter), kSubpixel);
        hi = FloorDiv(s.v.y + s.rowsSubpixel - 1 - kPixelCenter, kSubpixel);
    }
    first = std::max<int64_t>(lo, 0);
    last = std::min<int64_t>(hi, kMaxLines - 1);
    return first <= last;
}

// LinePlan::bandFirst / bandSteps for the recorded steps
void IndexSteps(LinePlan& plan)
{
    plan.bandFirst.assign(kPlanBands + 1, 0);
    for (const LinePlanStep& s : plan.steps)
    {
        int64_t first, last;
        if (StepLines(s, first, last))
            for (int64_t b = first >> kPlanBandShift; b <= (last >> kPlanBandShift); ++b)
                ++plan.bandFirst[static_cast<size_t>(b) + 1];
    }
    for (uint32_t b = 0; b < kPlanBands; ++b)
        plan.bandFirst[b + 1] += plan.bandFirst[b];
    plan.bandSteps.resize(plan.bandFirst[kPlanBands]);
    std::vector<uint32_t> fill(plan.bandFirst.begin(), plan.bandFirst.end() - 1);
    for (uint32_t i = 0; i < plan.steps.size(); ++i)
    {
        int64_t first, last;
        if (StepLines(plan.steps[i], first, last))
            for (int64_t b = first >> kPlanBandShift; b <= (last >> kPlanBandShift); ++b)
                plan.bandSteps[fill[static_cast<size_t>(b)]++] = i;
    }
}

// The steps that may reach logical line y, in order
struct BandSteps
{
    const uint32_t* first;
    const uint32_t* last;
    const uint32_t* begin() const { return first; }
    const uint32_t* end() const { return last; }
};

BandSteps StepsNear(const LinePlan& plan, uint32_t y)
{
    const uint32_t band = (y < kMaxLines ? y : kMaxLines - 1) >> kPlanBandShift;
    const uint32_t* base = plan.bandSteps.data();
    return {base + plan.bandFirst[band], base + plan.bandFirst[band + 1]};
}

bool SameVertex(const Vertex& a, const Vertex& b)
{
    return a.x == b.x && a.y == b.y && a.handle == b.handle && a.cell == b.cell;
}

// The context as drawing reads it: the vertex already carries the handle, the cell, the
// vertex format and the translation
GraphicsContext DrawingContext(const GraphicsContext& c)
{
    GraphicsContext d = c;
    d.handle = 0;
    d.cell = 0;
    d.vertexFormat = 0;
    d.translateX = 0;
    d.translateY = 0;
    return d;
}

// Equal drawing: everything the drawing functions read. The display list position and word
// only name the command for a probe; the vertex count and the previous vertex matter only
// to the primitives drawn between two vertices (Vertex2)
bool SameStep(const LinePlanStep& a, const GraphicsContext& ca, const LinePlanStep& b, const GraphicsContext& cb)
{
    if (a.clear != b.clear || a.rowTest != b.rowTest || a.primitive != b.primitive || a.clearMask != b.clearMask ||
        !SameVertex(a.v, b.v))
        return false;
    const bool pairs = !a.clear && a.primitive != kPrimBitmaps && a.primitive != kPrimPoints;
    if (pairs && (a.vertexCount != b.vertexCount || !SameVertex(a.previous, b.previous)))
        return false;
    if (!a.clear && a.primitive == kPrimBitmaps && std::memcmp(&a.handle, &b.handle, sizeof(BitmapHandle)) != 0)
        return false;
    const GraphicsContext da = DrawingContext(ca), db = DrawingContext(cb);
    return std::memcmp(&da, &db, sizeof(GraphicsContext)) == 0;
}

bool PagesUnchanged(const EveChip& chip, uint64_t start, uint64_t end, uint64_t mark)
{
    if (start >= chip.ramGLimit || end <= start)
        return true; // ROM or nothing: constant (a map change invalidates every line)
    if (end > chip.ramGLimit)
        end = chip.ramGLimit;
    for (uint64_t page = start >> kPageShift; page <= (end - 1) >> kPageShift; ++page)
    {
        if (chip.ramGPageChanges[page] <= mark)
            continue;
        // The page changed: which of its blocks within [start, end)
        const uint64_t lo = start > (page << kPageShift) ? start : (page << kPageShift);
        const uint64_t hi = end < ((page + 1) << kPageShift) ? end : ((page + 1) << kPageShift);
        for (uint64_t block = lo >> kChangeBlockShift; block <= (hi - 1) >> kChangeBlockShift; ++block)
            if (chip.ramGBlockChanges[block] > mark)
                return false;
    }
    return true;
}

// RAM_G a bitmap step can read on this line, a superset: the rows its sample positions
// fall on (the whole layout under a rotation or REPEAT in y) and the palette. Returns false
// for reads that are not bounded this way (text, bargraph)
bool StepReadsUnchanged(const EveChip& chip, const LinePlanStep& s, const GraphicsContext& c, int64_t lineSubpixel,
                        uint64_t mark)
{
    if (s.clear || s.primitive != kPrimBitmaps)
        return true;
    const BitmapHandle& h = s.handle;
    if (h.format == kFormatText8x8 || h.format == kFormatTextVga || h.format == kFormatBargraph)
        return false;
    const uint64_t stride = HandleStride(h);
    const uint64_t rows = HandleLayoutHeight(h);
    const uint64_t base = static_cast<uint64_t>(h.source) + static_cast<uint64_t>(s.v.cell) * stride * rows;
    uint64_t start = base, end = base + stride * rows;
    if (c.transform[3] == 0 && !h.wrapY && rows > 0)
    {
        // Rows do not change along the line: y' = E y + F (+ the next row for BILINEAR)
        const int64_t rely = lineSubpixel - s.v.y;
        const int64_t sy = ((static_cast<int64_t>(c.transform[4]) * rely) >> kSubpixelShift) + c.transform[5] -
                           (h.filter ? kBilinearOffset : 0);
        int64_t first = sy >> kFixedShift;
        int64_t last = first + (h.filter ? 1 : 0);
        if (first < 0)
            first = 0;
        if (last >= static_cast<int64_t>(rows))
            last = static_cast<int64_t>(rows) - 1;
        if (first > last)
            start = end = 0; // BORDER: no row read
        else
        {
            start = base + static_cast<uint64_t>(first) * stride;
            end = base + static_cast<uint64_t>(last + 1) * stride;
        }
    }
    if (!PagesUnchanged(chip, start, end, mark))
        return false;
    // The palette: 2 bytes per entry, 4 for PALETTED8
    if (h.format == kFormatPaletted4444 || h.format == kFormatPaletted565 || h.format == kFormatPaletted8)
    {
        const uint64_t entryBytes = h.format == kFormatPaletted8 ? 4 : 2;
        // VDAC4 PALETTED8 reads from PALETTE_SOURCE & ~3
        const uint64_t palette =
            chip.vdac4 && h.format == kFormatPaletted8 ? (c.paletteSource & ~3u) : c.paletteSource;
        return PagesUnchanged(chip, palette, palette + entryBytes * kPaletteEntries, mark);
    }
    return true;
}

// The line's pixels are in the frame buffer already: its reaching steps are those it was
// drawn from and nothing they read changed since
bool KeepLineBySteps(LineRun& run, const LinePlan& plan)
{
    if (run.keepLine < 0 || static_cast<size_t>(run.keepLine) >= plan.lines.size())
        return false;
    const EveChip& chip = *run.chip;
    const LineSignature& sig = plan.lines[static_cast<size_t>(run.keepLine)];
    if (!sig.valid || sig.drawRegChanges != chip.drawRegChanges || sig.outputVersion != chip.outputVersion ||
        (sig.readsAnyMemory && sig.ramGMark != chip.ramGWrites))
        return false;
    const LinePlan::Recording& then = plan.history[sig.record % LinePlan::kHistory];
    if (then.record != sig.record)
        return false;  // drawn from a recording no longer kept
    const int64_t lineSubpixel = static_cast<int64_t>(run.y) * kSubpixel + kPixelCenter;
    size_t k = 0;
    for (const uint32_t index : StepsNear(plan, run.y))
    {
        const LinePlanStep& s = plan.steps[index];
        if (!StepReachesLine(s, lineSubpixel))
            continue;
        const GraphicsContext& c = plan.contexts[s.context];
        if (k == sig.steps.size())
            return false;
        const LinePlanStep& old = then.steps[sig.steps[k]];
        if (!SameStep(s, c, old, then.contexts[old.context]))
            return false;
        if (!sig.readsAnyMemory && !StepReadsUnchanged(chip, s, c, lineSubpixel, sig.ramGMark))
        {
            return false;
        }
        ++k;
    }
    return k == sig.steps.size();
}

void TakeSteps(LineSignature& sig, const LineRun& run, const LinePlan& plan);

// After drawing a line from the plan: what it was drawn from
void RememberSteps(LineRun& run, LinePlan& plan)
{
    if (run.keepLine < 0)
        return;
    run.remembered = true;
    const EveChip& chip = *run.chip;
    if (plan.lines.size() < kMaxLines)
        plan.lines.resize(kMaxLines);
    LineSignature& sig = plan.lines[static_cast<size_t>(run.keepLine)];
    // The same recording reaches this line with the same steps (only a recording changes them): only the marks move
    if (!sig.valid || sig.record != plan.record || sig.y != run.y)
        TakeSteps(sig, run, plan);
    sig.ramGMark = chip.ramGWrites;
    sig.drawRegChanges = chip.drawRegChanges;
    sig.outputVersion = chip.outputVersion;
    sig.fillCost = run.fillCost;
    sig.valid = true;
}

void TakeSteps(LineSignature& sig, const LineRun& run, const LinePlan& plan)
{
    sig.record = plan.record;
    sig.y = run.y;
    sig.steps.clear();
    sig.readsAnyMemory = false;
    const int64_t lineSubpixel = static_cast<int64_t>(run.y) * kSubpixel + kPixelCenter;
    for (const uint32_t index : StepsNear(plan, run.y))
    {
        const LinePlanStep& s = plan.steps[index];
        if (!StepReachesLine(s, lineSubpixel))
            continue;
        if (!s.clear && s.primitive == kPrimBitmaps &&
            (s.handle.format == kFormatText8x8 || s.handle.format == kFormatTextVga || s.handle.format == kFormatBargraph))
            sig.readsAnyMemory = true;
        sig.steps.push_back(index);
    }
}

void ReplaySteps(LineRun& run, const LinePlan& plan);

// LineRepeat::keys of the line: false when a reaching step depends on the line otherwise (shapes, text, rotated or
// sheared matrices)
bool LineKeys(const LineRun& run, const LinePlan& plan, std::vector<int64_t>& keys)
{
    keys.clear();
    const int64_t lineSubpixel = static_cast<int64_t>(run.y) * kSubpixel + kPixelCenter;
    for (const uint32_t index : StepsNear(plan, run.y))
    {
        const LinePlanStep& s = plan.steps[index];
        if (!StepReachesLine(s, lineSubpixel))
            continue;
        const GraphicsContext& c = plan.contexts[s.context];
        // ScissorSpan's row test
        const bool rows = !(run.y < c.scissorY || run.y >= static_cast<uint32_t>(c.scissorY) + c.scissorHeight);
        int64_t row = 0;
        if (!s.clear)
        {
            const BitmapHandle& h = s.handle;
            if (s.primitive != kPrimBitmaps || h.format == kFormatText8x8 || h.format == kFormatTextVga ||
                h.format == kFormatBargraph || c.transform[1] != 0 || c.transform[3] != 0)
                return false;
            // DrawBitmap's y' with B = D = 0: the whole span samples this row (NEAREST) or these rows and weights
            const int64_t rely = lineSubpixel - s.v.y;
            const int64_t sy = ((static_cast<int64_t>(c.transform[4]) * rely) >> kSubpixelShift) + c.transform[5];
            row = h.filter ? sy : (sy >> kFixedShift);
        }
        keys.push_back(index);
        keys.push_back(rows ? 1 : 0);
        keys.push_back(row);
    }
    return true;
}

bool SameRepeatInputs(const LineRepeat& r, const LineRun& run, const LinePlan& plan)
{
    const EveChip& chip = *run.chip;
    return r.valid && r.record == plan.record && r.width == run.width && r.ramGWrites == chip.ramGWrites &&
           r.drawRegChanges == chip.drawRegChanges && r.outputVersion == chip.outputVersion &&
           r.dlVersion == chip.dlVersion;
}

void ReplayPlan(LineRun& run, LinePlan& plan)
{
    if (KeepLineBySteps(run, plan))
    {
        std::memcpy(run.handles, plan.endHandles, sizeof(plan.endHandles));
        run.commands = plan.commands;
        run.events = plan.events;
        run.fillCost = plan.lines[static_cast<size_t>(run.keepLine)].fillCost;
        run.pixelsKept = true;
#ifdef EVE_PROFILE
        Profile().linesKeptBySteps++;
#endif
        return;
    }
    LineRepeat* repeat = run.repeat;
    const bool keyed = repeat && !run.tagLive && run.chip->lineRepeatEnabled && LineKeys(run, plan, repeat->next);
    if (keyed && SameRepeatInputs(*repeat, run, plan) && repeat->keys == repeat->next)
    {
        std::memcpy(run.color, repeat->color.get(), static_cast<size_t>(run.width) * kChannels);
        std::memcpy(run.handles, plan.endHandles, sizeof(plan.endHandles));
        run.commands = plan.commands;
        run.events = plan.events;
        run.fillCost = repeat->fillCost;
        ++repeat->hits;
        RememberSteps(run, plan);
        return;
    }
    ReplaySteps(run, plan);
    RememberSteps(run, plan);
    if (keyed)
    {
        const EveChip& chip = *run.chip;
        std::swap(repeat->keys, repeat->next);
        std::memcpy(repeat->color.get(), run.color, static_cast<size_t>(run.width) * kChannels);
        repeat->valid = true;
        repeat->record = plan.record;
        repeat->width = run.width;
        repeat->ramGWrites = chip.ramGWrites;
        repeat->drawRegChanges = chip.drawRegChanges;
        repeat->outputVersion = chip.outputVersion;
        repeat->dlVersion = chip.dlVersion;
        repeat->fillCost = run.fillCost;
    }
}

void ReplaySteps(LineRun& run, const LinePlan& plan)
{
    const int64_t lineSubpixel = static_cast<int64_t>(run.y) * kSubpixel + kPixelCenter;
    uint32_t context = UINT32_MAX;
    for (const uint32_t index : StepsNear(plan, run.y))
    {
        const LinePlanStep& s = plan.steps[index];
        if (!StepReachesLine(s, lineSubpixel))
            continue; // the drawing's own first test: nothing of it on this line
        if (s.context != context)
        {
            context = s.context;
            run.ctx = plan.contexts[context];
        }
        run.commandIndex = s.commandIndex;
        run.commandWord = s.commandWord;
        if (s.clear)
        {
            ClearLine<LineMode::Draw>(run, s.clearMask);
            continue;
        }
        run.handles[s.v.handle] = s.handle;
        run.primitive = s.primitive;
        run.vertexCount = s.vertexCount;
        run.previous = s.previous;
        Vertex2<LineMode::Draw>(run, s.v);
    }
    std::memcpy(run.handles, plan.endHandles, sizeof(plan.endHandles));
    run.commands = plan.commands;
    run.events = plan.events;
}

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
        if constexpr (Mode == LineMode::Draw)
            if (run.recording != nullptr)
                RecordStep(run, v, false, 0);
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
        if constexpr (Mode == LineMode::Draw)
            if (run.recording != nullptr)
                RecordStep(run, Vertex{}, true, Field(word, 2, 0));
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

// After a swap with drawing off, run the new list once without drawing: the per-handle
// bitmap parameters persist across lines, so their state after one line equals their
// state after any number of lines.
void StatePass(EveChip& chip)
{
    LineRun run;
    PrepareRun(chip, run, 0, true);
    ExecuteLine<LineMode::StateOnly>(run);
    if constexpr (kContextReset != ContextReset::PerLine)
        chip.state.context = run.ctx;
}

} // namespace

// --- Handles ------------------------------------------------------------------------------------

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
    if constexpr (Mode == LineMode::Draw)
        run.fromPlan = false;
    if constexpr (Mode == LineMode::Draw && kLinePlanUsable)
    {
        EveChip& chip = *run.chip;
        if (chip.linePlanEnabled && chip.linePlan)
        {
            if (PlanMatches(chip, *chip.linePlan))
            {
#ifdef EVE_PROFILE
                Profile().planReplays++;
#endif
                ReplayPlan(run, *chip.linePlan);
                run.fromPlan = true;
                return;
            }
            StartRecording(run, *chip.linePlan);
        }
    }
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
#ifdef EVE_PROFILE
        if constexpr (Mode == LineMode::Draw)
            Profile().opcodes[word >> 24]++;
#endif
        run.commandIndex = pc;
        run.commandWord = word;
        uint32_t target = pc + 1;
        const Flow flow = ExecuteWord<Mode>(run, word, target, stack, depth, pc);
        if (flow == Flow::Stop)
            break;
        pc = flow == Flow::Jump ? target : pc + 1;
    }
    if constexpr (Mode == LineMode::Draw)
        if (run.recording != nullptr)
        {
            FinishRecording(run);
            run.fromPlan = true;
        }
}

template void ExecuteLine<LineMode::Draw>(LineRun&);
template void ExecuteLine<LineMode::Probe>(LineRun&);
template void ExecuteLine<LineMode::StateOnly>(LineRun&);

void LinePlanDelete::operator()(LinePlan* plan) const
{
    delete plan;
}

// --- Entry points ---------------------------------------------------------------------------------

bool InitDrawing(EveChip& chip)
{
    InitBitmapTables();
    chip.bitmapFastPath = true;
    chip.lineBudgetMargin = kLineBudgetMarginPercent;
    chip.lineColor.reset(new (std::nothrow) uint8_t[kMaxLineWidth * kChannels]());
    chip.lineStencil.reset(new (std::nothrow) uint8_t[kMaxLineWidth]());
    chip.lineTag.reset(new (std::nothrow) uint8_t[kMaxLineWidth]());
    chip.lineTexels.reset(new (std::nothrow) uint32_t[kMaxLineWidth]());
    chip.lineBilinear.reset(new (std::nothrow) uint32_t[kBilinearScratch]());
    chip.palettes.reset(new (std::nothrow) PaletteCache[kPaletteCacheEntries]());
    chip.linePlan.reset(new (std::nothrow) LinePlan());
    chip.lineKept.reset(new (std::nothrow) LineKept[kMaxLines]());
    chip.ramGPageChanges.reset(new (std::nothrow) uint64_t[chip.ramGSize >> kPageShift]());
    chip.ramGBlockChanges.reset(new (std::nothrow) uint64_t[chip.ramGSize >> kChangeBlockShift]());
    chip.probeColor.reset(new (std::nothrow) uint8_t[kMaxLineWidth * kChannels]());
    chip.probeStencil.reset(new (std::nothrow) uint8_t[kMaxLineWidth]());
    chip.probeTag.reset(new (std::nothrow) uint8_t[kMaxLineWidth]());
    chip.lineCosts.reset(new (std::nothrow) EveLineCost[kMaxLines]());
    return chip.lineColor && chip.lineStencil && chip.lineTag && chip.lineTexels && chip.lineBilinear && chip.palettes && chip.linePlan && chip.lineKept && chip.ramGPageChanges && chip.ramGBlockChanges && chip.probeColor && chip.probeStencil &&
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
    AsyncTakeDrawingState(chip, false);
}

void CatchUp(EveChip& chip)
{
    const uint32_t due = LinesDue(chip);
    const bool draw = chip.drawing && ScanRunning(chip);
    if (chip.asyncDrawing)
    {
        // The renderer draws them; the lines passed move on here as they would with the drawing. Nothing new
        // passed: nothing to queue (a portrait frame's repeated catch-up only forgets kept lines again)
        const uint32_t next = draw && Portrait(chip) ? due : (chip.drawnLines > due ? chip.drawnLines : due);
        if (next == chip.drawnLines)
        {
            // Lines drawn ahead count from here (REG_TAG as the drawing on this thread has it now)
            if (chip.drawnAhead)
                AsyncCatchUpPoint(chip);
            chip.drawnAhead = false;
            return;
        }
        AsyncLines(chip, due, draw);
        chip.drawnLines = next;
        chip.drawnAhead = false;
        return;
    }
    DrawLinesTo(chip, due, draw);
}

void CatchUpAhead(EveChip& chip)
{
    // The renderer draws along the scan instead of a whole frame at its event. What the host sees of the drawing,
    // REG_TAG, is the renderer's as of the last catch-up (before a change, at the frame event), as the drawing on
    // this thread leaves it: lines drawn ahead do not move it (AsyncLines `ahead`). A portrait frame is drawn at its
    // catch-ups only
    if (!chip.asyncDrawing || !chip.drawing || !ScanRunning(chip) || Portrait(chip))
        return;
    const uint32_t due = LinesDue(chip);
    if (due <= chip.drawnLines)
        return;
    AsyncLines(chip, due, true, true);
    chip.drawnLines = due;
    chip.drawnAhead = true;
}

void DrawLinesTo(EveChip& chip, uint32_t due, bool draw)
{
    if (!draw)
    {
        // Lines passed without drawing are not measured: their costs from an earlier
        // frame must not count for this one (the frame's metrics block, the in-flight view)
        for (uint32_t line = chip.drawnLines; line < due && line < kMaxLines; ++line)
        {
            chip.lineCosts[line].valid = 0;
            chip.lineKept[line].valid = false; // the frame buffer keeps an older picture there
            if (line < chip.linePlan->lines.size())
                chip.linePlan->lines[line].valid = false;
        }
        if (chip.drawnLines < due)
            chip.drawnLines = due;
        return;
    }
    if (Portrait(chip))
    {
        // Portrait frames are drawn whole and transposed: no screen line is kept
        for (uint32_t line = 0; line < kMaxLines; ++line)
            chip.lineKept[line].valid = false;
        for (LineSignature& s : chip.linePlan->lines)
            s.valid = false;
        if (chip.drawnLines == 0 && due > 0)
            DrawPortraitFrame(chip);
        chip.drawnLines = due;
        return;
    }
    if (chip.drawnLines >= due)
        return; // a catch-up with nothing due: most of them (every bus access catches up)
    HostDrawStats* stats = chip.hostStats.get();
    if (!stats)
    {
        const uint32_t parallelEnd = due < kMaxLines ? due : kMaxLines;
        const bool parallel = (chip.drawThreads > 1 || chip.lineWorkers) && EnsureLineWorkers(chip);
        while (chip.drawnLines < due)
        {
            if (parallel && chip.drawnLines + chip.parallelMinLines <= parallelEnd && LinesIndependent(chip))
                DrawLinesParallel(chip, parallelEnd);
            else
                DrawVisibleLine(chip, chip.drawnLines++);
        }
        return;
    }
    // One clock read per line (wall, about 20 ns against the microseconds a line takes)
    const uint32_t parallelEnd = due < kMaxLines ? due : kMaxLines;
    const bool parallel = (chip.drawThreads > 1 || chip.lineWorkers) && EnsureLineWorkers(chip);
    auto before = std::chrono::steady_clock::now();
    while (chip.drawnLines < due)
    {
        // The rest of the lines side by side once each replays the plan (the first line after a change records it)
        if (parallel && chip.drawnLines + chip.parallelMinLines <= parallelEnd && LinesIndependent(chip))
        {
            DrawLinesParallel(chip, parallelEnd);
            before = std::chrono::steady_clock::now();
            continue;
        }
        const uint32_t line = chip.drawnLines++;
        DrawVisibleLine(chip, line);
        const auto after = std::chrono::steady_clock::now();
        if (line < kMaxLines)
            stats->work[line] = static_cast<uint32_t>(
                std::min<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count(), UINT32_MAX));
        before = after;
    }
}

void FrameStart(EveChip& chip)
{
    chip.drawnLines = 0;
    chip.overflowLines = 0;
    if (chip.asyncDrawing)
        AsyncFrameStart(chip);
}

void DisplayListSwapped(EveChip& chip, bool changed)
{
    if (changed)
        ++chip.dlVersion; // a swap to the same words changes nothing drawn
    if (chip.asyncDrawing)
        AsyncSwapped(chip, changed); // the handles are the renderer's: it runs the state pass when not drawing
    else if (!chip.drawing)
        StatePass(chip);
}

void DrawingInvalidate(EveChip& chip)
{
    ++chip.ramGWrites;  // memory restored or reset: decoded palettes no longer hold
    ++chip.dlVersion;   // and the recorded walk
    // The current frame is drawn again from line 0 by the next catch-up (arch §7.4).
    chip.drawnLines = 0;
    for (uint32_t i = 0; i < kMaxLines; ++i)
    {
        chip.lineCosts[i] = EveLineCost{};
        chip.lineKept[i].valid = false;
    }
    for (LineSignature& s : chip.linePlan->lines)
        s.valid = false;
    AsyncInvalidate(chip);
}


void HostDrawStatsDelete::operator()(HostDrawStats* stats) const
{
    delete stats;
}

std::shared_ptr<HostDrawStats> MakeHostDrawStats()
{
    return std::shared_ptr<HostDrawStats>(new (std::nothrow) HostDrawStats(), HostDrawStatsDelete());
}

namespace
{
void PublishHostDrawStats(EveChip& chip, uint32_t lines, bool valid)
{
    HostDrawStats* stats = chip.hostStats.get();
    if (!stats)
        return;
    lines = std::min(lines, kMaxLines);
    uint64_t total = 0;
    uint64_t worst = 0;
    uint32_t worstLine = 0;
    for (uint32_t i = 0; i < lines; ++i)
    {
        total += stats->work[i];
        if (stats->work[i] > worst)
        {
            worst = stats->work[i];
            worstLine = i;
        }
    }
    std::lock_guard<std::mutex> guard(stats->lock);
    EveHostDrawStats& last = stats->last;
    last.frame = chip.state.scan.frames;
    last.valid = valid ? 1 : 0;
    last.lines = lines;
    last.totalNs = total;
    last.worstLine = worstLine;
    last.worstLineNs = worst;
    // The drawing thread's CPU time since the previous frame end: the renderer's drawing (it does nothing else), or
    // everything the emulation thread ran when it draws itself
    const uint64_t cpuNow = ThreadCpuNs();
    const bool sameThread = stats->cpuThread == std::this_thread::get_id();
    last.cpuNs = sameThread && stats->cpuMark && cpuNow > stats->cpuMark ? cpuNow - stats->cpuMark : 0;
    stats->cpuMark = cpuNow;
    stats->cpuThread = std::this_thread::get_id();
    std::memcpy(stats->lineNs, stats->work, lines * sizeof(stats->work[0]));
    std::memset(stats->work, 0, lines * sizeof(stats->work[0]));
    if (!valid)
        return;
    EveHostDrawStats& sum = stats->summary;
    ++sum.frames;
    sum.sumNs += total;
    sum.cpuNs += last.cpuNs; // summary: CPU time of all the frames
    if (last.cpuNs > sum.maxCpuNs)
    {
        sum.maxCpuNs = last.cpuNs;
        sum.maxCpuFrame = last.frame;
    }
    if (total > sum.maxFrameNs)
    {
        sum.maxFrameNs = total;
        sum.maxFrame = last.frame;
        sum.lines = lines;
        sum.worstLine = worstLine;
        sum.worstLineNs = worst;
        std::memcpy(stats->maxLineNs, stats->lineNs, lines * sizeof(stats->lineNs[0]));
    }
}
} // namespace

void FoldFrameMetrics(EveChip& chip)
{
    FrameMetricsState& m = chip.state.metrics;
    const uint32_t lines = VisibleLines(chip);
    const uint32_t hard = LineBudget(chip);
    const uint32_t soft = static_cast<uint32_t>((static_cast<uint64_t>(hard) * (100 - chip.lineBudgetMargin)) / 100);
    m.frame = chip.state.scan.frames;
    m.lines = lines;
    m.hardBudget = hard;
    m.softBudget = soft;
    m.worstLine = 0;
    m.worstClocks = 0;
    m.totalClocks = 0;
    m.linesOverSoft = 0;
    m.linesOverHard = 0;
    bool valid = chip.drawing != 0 && lines > 0;
    for (uint32_t i = 0; i < lines; ++i)
    {
        const EveLineCost& cost = chip.lineCosts[i];
        if (!cost.valid)
            valid = false;
        const uint32_t clocks = cost.valid ? cost.totalClocks : 0;
        m.lineClocks[i] = static_cast<uint16_t>(clocks > 0xFFFF ? 0xFFFF : clocks);
        m.totalClocks += clocks;
        if (clocks > m.worstClocks)
        {
            m.worstClocks = clocks;
            m.worstLine = i;
        }
        if (clocks > hard)
            ++m.linesOverHard;
        else if (clocks > soft)
            ++m.linesOverSoft;
    }
    if (lines < kMetricsLines)
        std::memset(&m.lineClocks[lines], 0, (kMetricsLines - lines) * sizeof(m.lineClocks[0]));
    m.valid = valid ? 1 : 0;
    PublishHostDrawStats(chip, lines, valid);
}

uint32_t FrameLinesDue(const EveChip& chip)
{
    return chip.drawnLines;
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

using namespace EveLib;

extern "C" {

size_t EveGetHostDrawStats(const EveChip* chip, EveHostDrawStats* last, EveHostDrawStats* summary, uint32_t* lineNs,
                           size_t maxLines)
{
    HostDrawStats* stats = chip ? chip->hostStats.get() : nullptr;
    if (!stats)
    {
        if (last)
            *last = EveHostDrawStats{};
        if (summary)
            *summary = EveHostDrawStats{};
        return 0;
    }
    std::lock_guard<std::mutex> guard(stats->lock);
    if (last)
        *last = stats->last;
    if (summary)
        *summary = stats->summary;
    if (!lineNs)
        return 0;
    const size_t n = std::min<size_t>(maxLines, stats->last.lines);
    std::memcpy(lineNs, stats->lineNs, n * sizeof(lineNs[0]));
    return n;
}

size_t EveGetHostDrawMaxLines(const EveChip* chip, uint32_t* lineNs, size_t maxLines)
{
    HostDrawStats* stats = chip ? chip->hostStats.get() : nullptr;
    if (!stats || !lineNs)
        return 0;
    std::lock_guard<std::mutex> guard(stats->lock);
    const size_t n = std::min<size_t>(maxLines, stats->summary.lines);
    std::memcpy(lineNs, stats->maxLineNs, n * sizeof(lineNs[0]));
    return n;
}

uint32_t EveSetDrawThreads(EveChip* chip, uint32_t threads)
{
    if (!chip)
        return 0;
    constexpr uint32_t kMaxDrawThreads = 64;
    chip->drawThreads = threads > kMaxDrawThreads ? kMaxDrawThreads : threads;
    if (chip->drawThreads <= 1 && !chip->asyncDrawing)
        chip->lineWorkers.reset(); // the renderer's shadow drops its own with the next operation
    return chip->drawThreads > 1 ? chip->drawThreads : 1;
}

void EveResetHostDrawStats(EveChip* chip)
{
    HostDrawStats* stats = chip ? chip->hostStats.get() : nullptr;
    if (!stats)
        return;
    std::lock_guard<std::mutex> guard(stats->lock);
    stats->summary = EveHostDrawStats{};
}

} // extern "C"
