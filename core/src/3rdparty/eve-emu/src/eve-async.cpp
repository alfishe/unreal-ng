// eve-emu - drawing on a thread of its own (EveSetAsyncDrawing).
//
// The chip (the producer: whichever thread advances it) keeps its timing, coprocessor and registers as with
// drawing on, but draws nothing: at every point where it would draw (CatchUp, a frame start or end, a swap) it
// queues an operation with a copy of what changed among the drawing's inputs since the previous one - the RAM_G
// pages, the active display list, the registers, the output and the settings. The renderer thread applies the
// operations in order to a shadow chip, which draws. Every line sees exactly the inputs the producer had when it
// would have drawn it, so the picture, the line costs and the frame metrics are those of the synchronous drawing,
// whatever the renderer's pace.
//
// The drawing's own state (bitmap handles, the graphics context, line costs, frame metrics, REG_TAG, the kept
// lines and caches) lives in the shadow while the renderer runs. The readers of it (EveSaveState, the line costs,
// the metrics, EveProbePixel, a host read of REG_TAG) wait for the renderer first.
#include "eve-render.h"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

namespace EveLib
{

namespace
{

constexpr uint32_t kMaxFramesAhead = 2;   // frame ends queued before the producer waits
constexpr size_t kMaxQueuedOps = 1024;    // operations queued before the producer waits
// A sleeping renderer is woken once this many operations wait (or at a frame end, or when the producer waits for
// it), not for every one: a wake-up is a system call on the producer's thread, and a catch-up of a few lines is
// less work than waking for it. The operations and their order stay the same, only when they are drawn moves
constexpr size_t kWakeOps = 8;
constexpr uint32_t kBlockBytes = 1u << kChangeBlockShift;

enum class OpKind : uint8_t
{
    Lines,      // draw (or pass without drawing) the frame's lines up to `due`
    FrameStart, // a new frame: nothing drawn yet
    FrameEnd,   // the frame event: fold the metrics, present
    Swapped,    // a display list swap (`flag`: other words)
    Sync        // inputs only (enabling, the copy-back before disabling)
};

// What a sync takes from the producer besides the memory
enum SyncFlags : uint8_t
{
    SyncMemory = 1,    // every RAM_G page and the active list; the shadow's drawing state is invalidated
    SyncHandles = 2,   // the bitmap handles and the graphics context (reset, state load, enabling)
    SyncMetrics = 4,   // the frame metrics block (power-on, state load, enabling)
    SyncLineState = 8  // line costs, lines drawn, overflow count of the frame in flight (enabling)
};

struct Op
{
    OpKind kind = OpKind::Sync;
    uint8_t flag = 0;
    bool ahead = false; // lines drawn ahead (CatchUpAhead): REG_TAG as the host sees it stays as it was
    uint8_t sync = 0; // SyncFlags
    uint32_t due = 0;
    uint64_t frame = 0;
    uint64_t completed = 0; // EveCompletedFrames as of the operation: a frame end's own count

    // The inputs as of the operation
    uint8_t registers[kRamRegSize];
    uint8_t special[kSpecialSize];
    uint32_t* framebuffer = nullptr;
    uint32_t stridePixels = 0, widthCapacity = 0, heightCapacity = 0;
    int drawing = 0;
    uint64_t outputVersion = 0, drawRegChanges = 0;
    uint32_t ramGLimit = 0, romStart = 0;
    bool romBlank = false;
    bool bitmapFastPath = true, rasterSpanFill = true, linePlanEnabled = true, lineKeepEnabled = true;
    uint32_t lineBudgetMargin = 0;
    uint32_t drawThreads = 0, parallelMinLines = 0;
    std::vector<uint32_t> pages;   // RAM_G pages changed since the previous operation
    std::vector<uint8_t> pageBytes;
    bool hasList = false;          // the active display list follows
    uint8_t list[kRamDlSize];

    // A sync's drawing state
    BitmapHandle handles[kHandleCount];
    GraphicsContext context;
    FrameMetricsState metrics;
    std::vector<EveLineCost> lineCosts;
    uint32_t drawnLines = 0, overflowLines = 0;
};

} // namespace

struct AsyncDrawing
{
    EveChip* shadow = nullptr;
    EveFrameCallback onFrame = nullptr;
    void* user = nullptr;

    // Producer side
    uint64_t pageMark = 0;      // producer ramGWrites when the last operation was queued
    uint8_t syncPending = 0;    // SyncFlags for the next operation

    // The queue
    std::mutex lock;
    std::condition_variable work;  // the renderer waits for operations
    std::condition_variable done;  // the producer waits for room / for the renderer to be idle
    std::deque<std::unique_ptr<Op>> queue;
    std::vector<std::unique_ptr<Op>> free;
    uint32_t framesQueued = 0;
    // Frame ends queued and drawn since the renderer started, counted (under `lock`): EveFramesBarrierTo
    uint64_t frameEndsQueued = 0;
    uint64_t frameEndsDrawn = 0;
    // Operations queued and applied, counted (under `lock`); the frames barrier's REG_TAG is the shadow's after
    // operation `tagMark` (0: none asked), taken by the renderer when it gets there
    uint64_t opsQueued = 0;
    uint64_t opsApplied = 0;
    uint64_t tagMark = 0;
    uint32_t tagAtMark = 0;
    // REG_TAG as the host sees it: the shadow's after the last operation that is not drawn ahead
    uint32_t tagSeen = 0;
    bool busy = false;
    bool sleeping = false; // the renderer waits for `work` and nobody woke it since
    bool stop = false;
    std::thread thread;
    std::thread::id rendererId;

    ~AsyncDrawing()
    {
        {
            std::lock_guard<std::mutex> guard(lock);
            stop = true;
        }
        work.notify_all();
        if (thread.joinable())
            thread.join();
        EveDestroy(shadow);
    }
};

void AsyncDrawingDelete::operator()(AsyncDrawing* async) const
{
    delete async;
}

namespace
{

// --- Producer -------------------------------------------------------------------------------

std::unique_ptr<Op> TakeOp(AsyncDrawing& a)
{
    std::lock_guard<std::mutex> guard(a.lock);
    if (a.free.empty())
        return std::make_unique<Op>();
    std::unique_ptr<Op> op = std::move(a.free.back());
    a.free.pop_back();
    return op;
}

void CopyPage(Op& op, const EveChip& chip, uint32_t page)
{
    op.pages.push_back(page);
    const uint8_t* from = chip.regions[RegionRamG].base + (static_cast<size_t>(page) << kPageShift);
    op.pageBytes.insert(op.pageBytes.end(), from, from + kPageSize);
}

// The inputs of the drawing as they are now, what changed in RAM_G since the previous operation and a pending
// sync
void CaptureInputs(EveChip& chip, AsyncDrawing& a, Op& op)
{
    std::memcpy(op.registers, chip.regions[RegionReg].base, kRamRegSize);
    std::memcpy(op.special, chip.regions[RegionSpecial].base, kSpecialSize);
    op.framebuffer = chip.framebuffer;
    op.stridePixels = chip.stridePixels;
    op.widthCapacity = chip.widthCapacity;
    op.heightCapacity = chip.heightCapacity;
    op.drawing = chip.drawing;
    op.outputVersion = chip.outputVersion;
    op.drawRegChanges = chip.drawRegChanges;
    op.ramGLimit = chip.ramGLimit;
    op.romStart = chip.romStart;
    op.romBlank = chip.romBlank;
    op.bitmapFastPath = chip.bitmapFastPath;
    op.rasterSpanFill = chip.rasterSpanFill;
    op.linePlanEnabled = chip.linePlanEnabled;
    op.lineKeepEnabled = chip.lineKeepEnabled;
    op.lineBudgetMargin = chip.lineBudgetMargin;
    op.drawThreads = chip.drawThreads;
    op.parallelMinLines = chip.parallelMinLines;

    op.sync = a.syncPending;
    a.syncPending = 0;
    op.pages.clear();
    op.pageBytes.clear();
    op.hasList = false;
    const uint32_t pages = chip.ramGSize >> kPageShift;
    if (op.sync & SyncMemory)
    {
        op.pageBytes.reserve(chip.ramGSize);
        for (uint32_t page = 0; page < pages; ++page)
            CopyPage(op, chip, page);
        std::memcpy(op.list, ActiveDl(chip), kRamDlSize);
        op.hasList = true;
    }
    else
    {
        for (uint32_t page = 0; page < pages; ++page)
            if (chip.ramGPageChanges[page] > a.pageMark)
                CopyPage(op, chip, page);
    }
    a.pageMark = chip.ramGWrites;
    if (op.sync & SyncHandles)
    {
        std::memcpy(op.handles, chip.state.handles, sizeof(op.handles));
        op.context = chip.state.context;
    }
    if (op.sync & SyncMetrics)
        op.metrics = chip.state.metrics;
    if (op.sync & SyncLineState)
    {
        op.lineCosts.assign(chip.lineCosts.get(), chip.lineCosts.get() + kMaxLines);
        op.drawnLines = chip.drawnLines;
        op.overflowLines = chip.overflowLines;
    }
}

// Under `lock`, before the producer waits for the renderer: the operations queued get drawn
void WakeRenderer(AsyncDrawing& a)
{
    if (a.sleeping && !a.queue.empty())
    {
        a.sleeping = false;
        a.work.notify_one();
    }
}

void Queue(AsyncDrawing& a, std::unique_ptr<Op> op)
{
    const bool frameEnd = op->kind == OpKind::FrameEnd;
    std::unique_lock<std::mutex> guard(a.lock);
    // Back-pressure: the producer runs at most a couple of frames ahead of the picture
    const auto room = [&] {
        return a.stop || (a.queue.size() < kMaxQueuedOps && (!frameEnd || a.framesQueued < kMaxFramesAhead));
    };
    if (!room())
    {
        WakeRenderer(a);
        a.done.wait(guard, room);
    }
    if (frameEnd)
    {
        ++a.framesQueued;
        ++a.frameEndsQueued;
    }
    ++a.opsQueued;
    a.queue.push_back(std::move(op));
    if (frameEnd || a.queue.size() >= kWakeOps)
        WakeRenderer(a);
}

void QueueOp(EveChip& chip, OpKind kind, uint32_t due = 0, uint8_t flag = 0, bool ahead = false)
{
    AsyncDrawing& a = *chip.async;
    std::unique_ptr<Op> op = TakeOp(a);
    op->kind = kind;
    op->due = due;
    op->flag = flag;
    op->ahead = ahead;
    op->frame = chip.state.scan.frames;
    op->completed = chip.state.scan.completedFrames;
    CaptureInputs(chip, a, *op);
    if (kind == OpKind::Swapped && flag && !op->hasList)
    {
        std::memcpy(op->list, ActiveDl(chip), kRamDlSize);
        op->hasList = true;
    }
    Queue(a, std::move(op));
}

// --- Renderer -------------------------------------------------------------------------------

void ApplyPages(EveChip& s, const Op& op)
{
    uint8_t* ramG = s.regions[RegionRamG].base;
    for (size_t i = 0; i < op.pages.size(); ++i)
    {
        const uint32_t page = op.pages[i];
        const uint8_t* from = op.pageBytes.data() + i * kPageSize;
        uint8_t* to = ramG + (static_cast<size_t>(page) << kPageShift);
        // The shadow counts its changes as the producer's bus does: kept lines and decoded palettes hold
        // over equal bytes
        for (uint32_t block = 0; block < kPageSize; block += kBlockBytes)
        {
            if (std::memcmp(to + block, from + block, kBlockBytes) == 0)
                continue;
            std::memcpy(to + block, from + block, kBlockBytes);
            ++s.ramGWrites;
            s.ramGPageChanges[page] = s.ramGWrites;
            s.ramGBlockChanges[((page << kPageShift) + block) >> kChangeBlockShift] = s.ramGWrites;
        }
    }
}

void ApplyInputs(EveChip& s, const Op& op)
{
    // REG_TAG is the shadow's own (its drawing sets it), except when the chip's drawing state is the truth (a
    // reset, a state load, enabling)
    const uint32_t tag = RegGet(s, Reg::Tag);
    std::memcpy(s.regions[RegionReg].base, op.registers, kRamRegSize);
    std::memcpy(s.regions[RegionSpecial].base, op.special, kSpecialSize);
    if (!(op.sync & SyncHandles))
        RegSet(s, Reg::Tag, tag);
    s.framebuffer = op.framebuffer;
    s.stridePixels = op.stridePixels;
    s.widthCapacity = op.widthCapacity;
    s.heightCapacity = op.heightCapacity;
    s.drawing = op.drawing;
    s.outputVersion = op.outputVersion;
    s.drawRegChanges = op.drawRegChanges;
    s.ramGLimit = op.ramGLimit;
    s.romStart = op.romStart;
    s.romBlank = op.romBlank;
    s.bitmapFastPath = op.bitmapFastPath;
    s.rasterSpanFill = op.rasterSpanFill;
    s.linePlanEnabled = op.linePlanEnabled;
    s.lineKeepEnabled = op.lineKeepEnabled;
    s.lineBudgetMargin = op.lineBudgetMargin;
    s.drawThreads = op.drawThreads;
    s.parallelMinLines = op.parallelMinLines;
    ApplyPages(s, op);
    if (op.hasList)
        std::memcpy(ActiveDlRegion(s).base, op.list, kRamDlSize);
    if (op.sync & SyncMemory)
        DrawingInvalidate(s);
    if (op.sync & SyncHandles)
    {
        std::memcpy(s.state.handles, op.handles, sizeof(s.state.handles));
        s.state.context = op.context;
    }
    if (op.sync & SyncMetrics)
        s.state.metrics = op.metrics;
    if (op.sync & SyncLineState)
    {
        std::memcpy(s.lineCosts.get(), op.lineCosts.data(), kMaxLines * sizeof(EveLineCost));
        s.drawnLines = op.drawnLines;
        s.overflowLines = op.overflowLines;
    }
}

void Apply(AsyncDrawing& a, const Op& op)
{
    EveChip& s = *a.shadow;
    ApplyInputs(s, op);
    s.state.scan.frames = op.frame;
    switch (op.kind)
    {
    case OpKind::Lines:
        DrawLinesTo(s, op.due, op.flag != 0);
        break;
    case OpKind::FrameStart:
        FrameStart(s);
        break;
    case OpKind::FrameEnd:
        FoldFrameMetrics(s);
        if (a.onFrame)
            a.onFrame(a.user, op.completed, s.drawing);
        break;
    case OpKind::Swapped:
        DisplayListSwapped(s, op.flag != 0);
        break;
    case OpKind::Sync:
        break;
    }
}

void RendererLoop(AsyncDrawing* a)
{
    for (;;)
    {
        std::unique_ptr<Op> op;
        {
            std::unique_lock<std::mutex> guard(a->lock);
            while (!a->stop && a->queue.empty())
            {
                a->sleeping = true; // woken by WakeRenderer (a spurious wake-up finds the queue as it is)
                a->work.wait(guard);
            }
            a->sleeping = false;
            if (a->queue.empty())
                return; // stopped with nothing left
            op = std::move(a->queue.front());
            a->queue.pop_front();
            a->busy = true;
        }
        Apply(*a, *op);
        {
            std::lock_guard<std::mutex> guard(a->lock);
            a->busy = false;
            if (op->kind == OpKind::FrameEnd)
            {
                --a->framesQueued;
                ++a->frameEndsDrawn;
            }
            if (!op->ahead)
                a->tagSeen = RegGet(*a->shadow, Reg::Tag);
            if (++a->opsApplied == a->tagMark)
                a->tagAtMark = a->tagSeen;
            a->free.push_back(std::move(op));
        }
        a->done.notify_all();
    }
}

} // namespace

// --- Hooks of the drawing (eve-dl.cpp, eve-timing.cpp) ------------------------------------------

void AsyncLines(EveChip& chip, uint32_t due, bool draw, bool ahead)
{
    QueueOp(chip, OpKind::Lines, due, draw ? 1 : 0, ahead);
}

void AsyncCatchUpPoint(EveChip& chip)
{
    QueueOp(chip, OpKind::Sync);
}

void AsyncFrameStart(EveChip& chip)
{
    QueueOp(chip, OpKind::FrameStart);
}

void AsyncFrameEnd(EveChip& chip)
{
    QueueOp(chip, OpKind::FrameEnd);
}

void AsyncSwapped(EveChip& chip, bool changed)
{
    QueueOp(chip, OpKind::Swapped, 0, changed ? 1 : 0);
}

void AsyncInvalidate(EveChip& chip)
{
    if (chip.async)
        chip.async->syncPending |= SyncMemory;
}

void AsyncTakeDrawingState(EveChip& chip, bool metrics)
{
    AsyncDropPendingTag(chip);
    if (chip.async)
        chip.async->syncPending |= SyncHandles | (metrics ? SyncMetrics : 0);
}

void AsyncWait(const EveChip& chip)
{
    AsyncDrawing* a = chip.async.get();
    if (!a || !chip.asyncDrawing || std::this_thread::get_id() == a->rendererId)
        return;
    std::unique_lock<std::mutex> guard(a->lock);
    WakeRenderer(*a);
    a->done.wait(guard, [&] { return a->queue.empty() && !a->busy; });
}

bool AsyncDrawingStatePending(const EveChip& chip, bool metrics)
{
    return chip.async && (chip.async->syncPending & (metrics ? SyncMetrics : SyncHandles)) != 0;
}

const EveChip& DrawingChip(const EveChip& chip)
{
    if (!chip.asyncDrawing)
        return chip;
    AsyncWait(chip);
    return *chip.async->shadow;
}

void AsyncSyncTag(EveChip& chip)
{
    if (!chip.asyncDrawing)
        return;
    AsyncWait(chip);
    AsyncDropPendingTag(chip); // superseded: the tag of now
    std::lock_guard<std::mutex> guard(chip.async->lock);
    RegSet(chip, Reg::Tag, chip.async->tagSeen);
}

void AsyncFramesBarrier(EveChip& chip)
{
    AsyncFramesBarrierTo(chip, UINT64_MAX);
}

void AsyncFramesBarrierTo(EveChip& chip, uint64_t frameEnds)
{
    AsyncDrawing* a = chip.async.get();
    if (!a || !chip.asyncDrawing || std::this_thread::get_id() == a->rendererId)
        return;
    AsyncResolveTag(chip); // the last barrier's: the renderer is past it once it is past the frames after it
    std::unique_lock<std::mutex> guard(a->lock);
    WakeRenderer(*a);
    a->done.wait(guard, [&] { return a->framesQueued == 0 || a->frameEndsDrawn >= frameEnds; });
    if (a->opsApplied == a->opsQueued)
    {
        // The renderer drew everything: REG_TAG now, as the full barrier takes it
        RegSet(chip, Reg::Tag, a->tagSeen);
        return;
    }
    a->tagMark = a->opsQueued;
    chip.tagPending = true;
}

void AsyncStorePendingTag(const EveChip& chip)
{
    AsyncDrawing* a = chip.async.get();
    EveChip& writable = const_cast<EveChip&>(chip);
    writable.tagPending = false;
    if (!a)
        return;
    std::unique_lock<std::mutex> guard(a->lock);
    WakeRenderer(*a);
    a->done.wait(guard, [&] { return a->opsApplied >= a->tagMark; });
    a->tagMark = 0;
    RegSet(writable, Reg::Tag, a->tagAtMark);
}

void AsyncDropPendingTag(EveChip& chip)
{
    if (!chip.tagPending)
        return;
    chip.tagPending = false;
    std::lock_guard<std::mutex> guard(chip.async->lock);
    chip.async->tagMark = 0;
}

} // namespace EveLib

using namespace EveLib;

extern "C" {

int EveSetAsyncDrawing(EveChip* chip, int on, EveFrameCallback onFrame, void* user)
{
    SetLastError("");
    if (!on)
    {
        if (!chip->asyncDrawing)
            return 0;
        // The drawing's state comes back: the shadow's handles, context, metrics and line costs. A part the
        // chip took itself (a reset, a state load, an invalidation) that no operation handed on yet stays the chip's
        AsyncWait(*chip);
        const EveChip& s = *chip->async->shadow;
        const uint8_t pending = chip->async->syncPending;
        if (!(pending & SyncHandles))
        {
            std::memcpy(chip->state.handles, s.state.handles, sizeof(chip->state.handles));
            chip->state.context = s.state.context;
        }
        if (!(pending & SyncMetrics))
            chip->state.metrics = s.state.metrics;
        if (!(pending & (SyncMemory | SyncLineState)))
        {
            std::memcpy(chip->lineCosts.get(), s.lineCosts.get(), kMaxLines * sizeof(EveLineCost));
            chip->overflowLines = s.overflowLines;
        }
        AsyncDropPendingTag(*chip);
        chip->drawnAhead = false;
        if (!(pending & SyncHandles))
            RegSet(*chip, Reg::Tag, RegGet(s, Reg::Tag));
        // The frame buffer holds the shadow's lines: none of the chip's kept lines describes it
        for (uint32_t i = 0; i < kMaxLines; ++i)
            chip->lineKept[i].valid = false;
        for (LineSignature& line : chip->linePlan->lines)
            line.valid = false;
        chip->asyncDrawing = false;
        return 0;
    }
    if (chip->asyncDrawing)
    {
        AsyncWait(*chip);
        chip->async->onFrame = onFrame;
        chip->async->user = user;
        return 0;
    }
    if (!chip->async)
    {
        std::unique_ptr<AsyncDrawing, AsyncDrawingDelete> a(new (std::nothrow) AsyncDrawing());
        if (!a)
        {
            SetLastError("EveSetAsyncDrawing: out of memory");
            return 1;
        }
        a->shadow = EveCreate(&chip->config);
        if (!a->shadow)
            return 1;
        a->shadow->vdac4State = chip->vdac4State;
        a->shadow->hostStats = chip->hostStats; // the renderer measures for the owner
        try
        {
            a->thread = std::thread(RendererLoop, a.get());
        }
        catch (...)
        {
            SetLastError("EveSetAsyncDrawing: cannot start the renderer thread");
            return 1;
        }
        a->rendererId = a->thread.get_id();
        chip->async = std::move(a);
    }
    AsyncDrawing& a = *chip->async;
    a.onFrame = onFrame;
    a.user = user;
    // Everything, as the chip has it now; the next operation carries it
    a.syncPending = SyncMemory | SyncHandles | SyncMetrics | SyncLineState;
    chip->asyncDrawing = true;
    QueueOp(*chip, OpKind::Sync);
    return 0;
}

int EveAsyncDrawing(const EveChip* chip)
{
    return chip->asyncDrawing ? 1 : 0;
}

void EveDrawingBarrier(EveChip* chip)
{
    AsyncSyncTag(*chip);
}

void EveFramesBarrier(EveChip* chip)
{
    AsyncFramesBarrier(*chip);
}

void EveFramesBarrierTo(EveChip* chip, uint64_t frameEnds)
{
    AsyncFramesBarrierTo(*chip, frameEnds);
}

uint64_t EveQueuedFrameEnds(const EveChip* chip)
{
    const AsyncDrawing* a = chip->async.get();
    if (!a || !chip->asyncDrawing)
        return 0;
    std::lock_guard<std::mutex> guard(const_cast<std::mutex&>(a->lock));
    return a->frameEndsQueued;
}

} // extern "C"
