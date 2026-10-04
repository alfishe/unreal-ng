// eve-emu - line executor, context, flow control, line cost, drawing in step with the
// beam (spec §5.1, §5.2, §6.1-6.3, arch §8.4).
#include "eve-profile.h"
#include "eve-render.h"
#include <cstdio>

#include <new>

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

void PrepareRun(EveChip& chip, LineRun& run, uint32_t logicalLine, bool firstLine, bool probe = false)
{
    run.chip = &chip;
    run.y = logicalLine;
    run.width = LineWidth(chip);
    run.color = probe ? chip.probeColor.get() : chip.lineColor.get();
    run.stencil = probe ? chip.probeStencil.get() : chip.lineStencil.get();
    run.tag = probe ? chip.probeTag.get() : chip.lineTag.get();
    run.texels = chip.lineTexels.get();
    run.bilinear = chip.lineBilinear.get();
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

void FinishRun(EveChip& chip, const LineRun& run, uint32_t screenLine)
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
bool KeepLine(EveChip& chip, uint32_t screenLine, uint32_t logicalLine)
{
    if constexpr (!kLinePlanUsable)
        return false;
    if (!chip.lineKeepEnabled || !chip.linePlanEnabled || screenLine >= kMaxLines ||
        logicalLine == RegGet(chip, Reg::TagY) || !LineInputsUnchanged(chip, chip.lineKept[screenLine]))
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
void RememberLine(EveChip& chip, uint32_t screenLine)
{
    if (screenLine >= kMaxLines)
        return;
    LineKept& k = chip.lineKept[screenLine];
    k.valid = chip.lineFromPlan && chip.linePlan->valid;
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
    RememberLine(chip, screenLine);
}

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
        step.handle = run.chip->state.handles[v.handle];
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

void FinishRecording(LineRun& run)
{
    LinePlan& plan = *run.recording;
    std::memcpy(plan.endHandles, run.chip->state.handles, sizeof(plan.endHandles));
    plan.commands = run.commands;
    plan.events = run.events;
    ++plan.record;
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
    if (start >= kRamGSize || end <= start)
        return true; // ROM or nothing: constant
    if (end > kRamGSize)
        end = kRamGSize;
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
        return PagesUnchanged(chip, c.paletteSource, c.paletteSource + entryBytes * kPaletteEntries, mark);
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
    const int64_t lineSubpixel = static_cast<int64_t>(run.y) * kSubpixel + kPixelCenter;
    size_t k = 0;
    for (const LinePlanStep& s : plan.steps)
    {
        if (!StepReachesLine(s, lineSubpixel))
            continue;
        const GraphicsContext& c = plan.contexts[s.context];
        if (k == sig.steps.size() || !SameStep(s, c, sig.steps[k], sig.contexts[k]))
            return false;
        if (!sig.readsAnyMemory && !StepReadsUnchanged(chip, s, c, lineSubpixel, sig.ramGMark))
        {
            return false;
        }
        ++k;
    }
    return k == sig.steps.size();
}

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
    sig.steps.clear();
    sig.contexts.clear();
    sig.readsAnyMemory = false;
    const int64_t lineSubpixel = static_cast<int64_t>(run.y) * kSubpixel + kPixelCenter;
    for (const LinePlanStep& s : plan.steps)
    {
        if (!StepReachesLine(s, lineSubpixel))
            continue;
        const GraphicsContext& c = plan.contexts[s.context];
        if (!s.clear && s.primitive == kPrimBitmaps &&
            (s.handle.format == kFormatText8x8 || s.handle.format == kFormatTextVga || s.handle.format == kFormatBargraph))
            sig.readsAnyMemory = true;
        sig.steps.push_back(s);
        sig.contexts.push_back(c);
    }
    sig.ramGMark = chip.ramGWrites;
    sig.drawRegChanges = chip.drawRegChanges;
    sig.outputVersion = chip.outputVersion;
    sig.fillCost = run.fillCost;
    sig.valid = true;
}

void ReplaySteps(LineRun& run, const LinePlan& plan);

void ReplayPlan(LineRun& run, LinePlan& plan)
{
    if (KeepLineBySteps(run, plan))
    {
        std::memcpy(run.chip->state.handles, plan.endHandles, sizeof(plan.endHandles));
        run.commands = plan.commands;
        run.events = plan.events;
        run.fillCost = plan.lines[static_cast<size_t>(run.keepLine)].fillCost;
        run.pixelsKept = true;
#ifdef EVE_PROFILE
        Profile().linesKeptBySteps++;
#endif
        return;
    }
    ReplaySteps(run, plan);
    RememberSteps(run, plan);
}

void ReplaySteps(LineRun& run, const LinePlan& plan)
{
    EveChip& chip = *run.chip;
    const int64_t lineSubpixel = static_cast<int64_t>(run.y) * kSubpixel + kPixelCenter;
    uint32_t context = UINT32_MAX;
    for (const LinePlanStep& s : plan.steps)
    {
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
        chip.state.handles[s.v.handle] = s.handle;
        run.primitive = s.primitive;
        run.vertexCount = s.vertexCount;
        run.previous = s.previous;
        Vertex2<LineMode::Draw>(run, s.v);
    }
    std::memcpy(chip.state.handles, plan.endHandles, sizeof(plan.endHandles));
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

    BitmapHandle& handle = chip.state.handles[ctx.handle];
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
    if constexpr (Mode == LineMode::Draw)
        run.chip->lineFromPlan = false;
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
                chip.lineFromPlan = true;
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
            run.chip->lineFromPlan = true;
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
    chip.ramGPageChanges.reset(new (std::nothrow) uint64_t[kRamGSize >> kPageShift]());
    chip.ramGBlockChanges.reset(new (std::nothrow) uint64_t[kRamGSize >> kChangeBlockShift]());
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
}

void CatchUp(EveChip& chip)
{
    const uint32_t due = LinesDue(chip);
    if (!chip.drawing || !ScanRunning(chip))
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
    while (chip.drawnLines < due)
        DrawVisibleLine(chip, chip.drawnLines++);
}

void FrameStart(EveChip& chip)
{
    chip.drawnLines = 0;
    chip.overflowLines = 0;
}

void DisplayListSwapped(EveChip& chip, bool changed)
{
    if (changed)
        ++chip.dlVersion; // a swap to the same words changes nothing drawn
    if (!chip.drawing)
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
}

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
