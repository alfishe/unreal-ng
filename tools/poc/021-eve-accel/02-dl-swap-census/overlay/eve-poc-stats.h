// eve-accel 01/02: counters and line timing collected by the census variant.
// One chip per process (the replay), so the counters are plain globals.
#pragma once

#include <cstdint>

namespace EveLib
{

enum PocTrigger : uint32_t
{
    kTrigRamGHost,     // host wrote RAM_G
    kTrigRamGCopro,    // coprocessor wrote RAM_G (inflate, memcpy, loadimage, ...)
    kTrigRegister,     // a register the drawing reads was written
    kTrigFrameEvent,   // end of the visible frame
    kTrigSetOutput,    // the host changed the output buffer
    kTrigCount
};

constexpr uint32_t kPocBatchBuckets = 8; // 1, 2-7, 8-31, 32-127, 128-511, 512-767, 768+, (unused)
constexpr uint32_t kPocMaxLines = 4096;
constexpr uint32_t kPocRegs = 128;

struct PocStats
{
    // Context of the current catch-up.
    uint32_t trigger = kTrigFrameEvent;
    bool inCopro = false;

    // Batches: catch-ups that drew at least one line.
    uint64_t batches[kTrigCount] = {};
    uint64_t batchLines[kTrigCount] = {};
    uint64_t batchHist[kTrigCount][kPocBatchBuckets] = {};
    uint64_t emptyCatchUps = 0;
    uint64_t batchLinesHist[kTrigCount][kPocBatchBuckets] = {}; // lines drawn per batch size bucket
    // RAM_G writes: would the write change memory the active display list reads?
    uint32_t triggerAddress = 0;
    uint64_t batchesNeeded[kTrigCount] = {};  // the write hit the list's read set
    uint64_t readSetChecks = 0, readSetRanges = 0, readSetBytes = 0;
    uint64_t neededThisFrame = 0;
    uint64_t framesOneBatchFiltered = 0;      // frames with no needed split (writes outside the read set deferred)

    // Frames.
    uint64_t frames = 0;               // frame events with drawing on
    uint64_t framesOneBatch = 0;       // all lines of the frame drawn by one catch-up
    uint64_t batchesThisFrame = 0;
    uint64_t batchesPerFrameHist[6] = {}; // 0, 1, 2, 3-4, 5-16, 17+

    // Swaps.
    uint64_t swapRequests[4] = {};     // by DLSWAP value written
    uint64_t swapsApplied[3] = {};     // 0 immediate (no scan), 1 line, 2 frame
    uint64_t lineSwapsMidFrame = 0;    // line swaps applied while lines of the frame were still due

    // Writes.
    uint64_t ramGWritesHost = 0, ramGWritesCopro = 0;
    uint64_t ramGWritesMidFrame = 0;   // a write that split a frame (lines drawn before, lines after)
    uint64_t ramDlWrites = 0;
    uint64_t drawingRegWrites[kPocRegs] = {};
    uint64_t drawingRegChanges[kPocRegs] = {};

    // Bitmap handle table across lines of one batch.
    uint64_t handleChangeFirstLine = 0; // the batch's first line changed the table
    uint64_t handleChangeLaterLine = 0; // a later line changed it (would break the fixed point)

    // Line timing (ns per drawn line).
    uint64_t lines = 0;
    double lineNsSum = 0, lineNsSq = 0;
    uint64_t lineNsHist[16] = {};      // log2 buckets of ns
    double lineIndexNs[kPocMaxLines] = {};
    uint64_t lineIndexCount[kPocMaxLines] = {};
    // Per frame: sum, max line and the frame's lines, for the imbalance figures.
    double frameNs = 0, frameMaxLineNs = 0;
    uint64_t frameLines = 0;
    double frameNsSum = 0, frameNsMax = 0;
    uint64_t framesTimed = 0;
    double imbalanceSum = 0;           // sum over frames of max line / mean line
    double chunkImbalance[4] = {};     // sum over frames of max chunk / mean chunk, 2/4/8/16 contiguous chunks
    double chunkImbalanceInterleaved[4] = {}; // the same with lines dealt round-robin (line % n)
    double frameLineNs[kPocMaxLines] = {};
};

PocStats& Poc();

} // namespace EveLib
