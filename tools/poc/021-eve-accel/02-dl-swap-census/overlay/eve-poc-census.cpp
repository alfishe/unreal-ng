// eve-accel 01/02: the census variant's POC hooks - prints what eve-poc-stats.h collected.
#include "eve-poc-stats.h"
#include "eve-internal.h"
#include "eve-poc.h"

#include <algorithm>
#include <cmath>

namespace EveLib
{

PocStats& Poc()
{
    static PocStats* stats = new PocStats();
    return *stats;
}

} // namespace EveLib

using namespace EveLib;

extern "C" {

const char* EvePocVariant(void)
{
    return "census";
}

int EvePocSetThreads(EveChip*, int)
{
    return 1;
}

void EvePocSetSnapshot(EveChip*, const char*, uint64_t, uint64_t)
{
}

void EvePocPrintStats(EveChip* chip, FILE* out)
{
    const PocStats& p = Poc();
    static const char* const kTrig[kTrigCount] = {"RAM_G host write", "RAM_G coprocessor write",
                                                  "drawing register write", "frame end", "output change"};
    std::fprintf(out, "\n== batches (catch-ups that drew lines) ==\n");
    std::fprintf(out, "%-26s %10s %12s %8s | %8s %8s %8s %8s %8s %8s %8s\n", "trigger", "batches", "lines",
                 "avg", "1", "2-7", "8-31", "32-127", "128-511", "512-767", "768+");
    uint64_t allBatches = 0, allLines = 0;
    for (uint32_t t = 0; t < kTrigCount; ++t)
    {
        allBatches += p.batches[t];
        allLines += p.batchLines[t];
        std::fprintf(out, "%-26s %10llu %12llu %8.1f |", kTrig[t], (unsigned long long)p.batches[t],
                     (unsigned long long)p.batchLines[t],
                     p.batches[t] ? (double)p.batchLines[t] / (double)p.batches[t] : 0.0);
        for (uint32_t b = 0; b < 7; ++b)
            std::fprintf(out, " %8llu", (unsigned long long)p.batchHist[t][b]);
        std::fprintf(out, "\n");
    }
    std::fprintf(out, "%-26s %10llu %12llu %8.1f   (catch-ups with nothing to draw: %llu)\n", "all",
                 (unsigned long long)allBatches, (unsigned long long)allLines,
                 allBatches ? (double)allLines / (double)allBatches : 0.0, (unsigned long long)p.emptyCatchUps);
    // Lines drawn per batch size (the share a thread pool would see).
    std::fprintf(out, "share of lines, by the size of their batch:                         ");
    for (uint32_t b = 0; b < 7; ++b)
    {
        uint64_t s = 0;
        for (uint32_t t = 0; t < kTrigCount; ++t)
            s += p.batchLinesHist[t][b];
        std::fprintf(out, " %7.2f%%", allLines ? 100.0 * (double)s / (double)allLines : 0.0);
    }
    std::fprintf(out, "\nread set: %llu checks, %.1f ranges and %.0f bytes per check", (unsigned long long)p.readSetChecks, p.readSetChecks ? (double)p.readSetRanges / (double)p.readSetChecks : 0.0, p.readSetChecks ? (double)p.readSetBytes / (double)p.readSetChecks : 0.0);
    std::fprintf(out, "\nRAM_G-triggered batches whose write hit the active list's read set: host %llu of %llu, coprocessor %llu of %llu\n",
                 (unsigned long long)p.batchesNeeded[kTrigRamGHost], (unsigned long long)p.batches[kTrigRamGHost],
                 (unsigned long long)p.batchesNeeded[kTrigRamGCopro], (unsigned long long)p.batches[kTrigRamGCopro]);
    std::fprintf(out, "\n== frames ==\nframes drawn: %llu, drawn in one batch: %llu (%.2f %%)\n",
                 (unsigned long long)p.frames, (unsigned long long)p.framesOneBatch,
                 p.frames ? 100.0 * (double)p.framesOneBatch / (double)p.frames : 0.0);
    std::fprintf(out, "frames that would be one batch if RAM_G writes outside the read set did not split: %llu (%.2f %%)\n",
                 (unsigned long long)p.framesOneBatchFiltered,
                 p.frames ? 100.0 * (double)p.framesOneBatchFiltered / (double)p.frames : 0.0);
    std::fprintf(out, "batches per frame: 0: %llu, 1: %llu, 2: %llu, 3-4: %llu, 5-16: %llu, 17+: %llu\n",
                 (unsigned long long)p.batchesPerFrameHist[0], (unsigned long long)p.batchesPerFrameHist[1],
                 (unsigned long long)p.batchesPerFrameHist[2], (unsigned long long)p.batchesPerFrameHist[3],
                 (unsigned long long)p.batchesPerFrameHist[4], (unsigned long long)p.batchesPerFrameHist[5]);
    std::fprintf(out, "\n== display list swaps ==\nrequests by value: 0: %llu, 1 (line): %llu, 2 (frame): %llu, 3: %llu\n",
                 (unsigned long long)p.swapRequests[0], (unsigned long long)p.swapRequests[1],
                 (unsigned long long)p.swapRequests[2], (unsigned long long)p.swapRequests[3]);
    std::fprintf(out, "applied: immediately (no scan): %llu, at a line end: %llu (inside the visible lines: %llu), at the frame end: %llu\n",
                 (unsigned long long)p.swapsApplied[0], (unsigned long long)p.swapsApplied[1],
                 (unsigned long long)p.lineSwapsMidFrame, (unsigned long long)p.swapsApplied[2]);
    std::fprintf(out, "\n== writes ==\nRAM_G bytes: host %llu, coprocessor %llu; writes that found a frame partly drawn: %llu\n",
                 (unsigned long long)p.ramGWritesHost, (unsigned long long)p.ramGWritesCopro,
                 (unsigned long long)p.ramGWritesMidFrame);
    std::fprintf(out, "RAM_DL bytes (pending list, never read by drawing): %llu\n", (unsigned long long)p.ramDlWrites);
    std::fprintf(out, "drawing registers written (byte writes / bytes that changed):\n");
    for (uint32_t r = 0; r < kPocRegs; ++r)
        if (p.drawingRegWrites[r])
        {
            const RegInfo& info = RegisterInfo(*chip, static_cast<Reg>(r));
            std::fprintf(out, "  %-14s %10llu %10llu\n", info.name, (unsigned long long)p.drawingRegWrites[r],
                         (unsigned long long)p.drawingRegChanges[r]);
        }
    std::fprintf(out, "\n== bitmap handle table within a batch ==\nchanged by a batch's first line: %llu, by a later line: %llu\n",
                 (unsigned long long)p.handleChangeFirstLine, (unsigned long long)p.handleChangeLaterLine);

    std::fprintf(out, "\n== line cost (instrumented, ns per drawn line incl. output) ==\n");
    const double n = (double)p.lines;
    const double mean = n > 0 ? p.lineNsSum / n : 0;
    const double sd = n > 1 ? std::sqrt(std::max(0.0, p.lineNsSq / n - mean * mean)) : 0;
    std::fprintf(out, "lines %llu, mean %.0f ns, sd %.0f ns (cv %.2f)\n", (unsigned long long)p.lines, mean, sd,
                 mean > 0 ? sd / mean : 0.0);
    std::fprintf(out, "histogram (ns, log2 buckets):");
    for (uint32_t b = 0; b < 16; ++b)
        if (p.lineNsHist[b])
            std::fprintf(out, " [%u-%u): %llu", 1u << b, 2u << b, (unsigned long long)p.lineNsHist[b]);
    std::fprintf(out, "\n");
    if (p.framesTimed)
    {
        const double f = (double)p.framesTimed;
        std::fprintf(out, "full frames timed %llu: mean %.3f ms, max %.3f ms; max line / mean line %.2f\n",
                     (unsigned long long)p.framesTimed, p.frameNsSum / f / 1e6, p.frameNsMax / 1e6,
                     p.imbalanceSum / f);
        std::fprintf(out, "imbalance (slowest chunk / ideal), contiguous chunks: 2: %.3f 4: %.3f 8: %.3f 16: %.3f\n",
                     p.chunkImbalance[0] / f, p.chunkImbalance[1] / f, p.chunkImbalance[2] / f, p.chunkImbalance[3] / f);
        std::fprintf(out, "imbalance, lines dealt round-robin:           2: %.3f 4: %.3f 8: %.3f 16: %.3f\n",
                     p.chunkImbalanceInterleaved[0] / f, p.chunkImbalanceInterleaved[1] / f,
                     p.chunkImbalanceInterleaved[2] / f, p.chunkImbalanceInterleaved[3] / f);
    }
    // Mean cost per screen line in bands of 32 lines (where on the screen the cost is).
    std::fprintf(out, "mean ns by screen line (bands of 32):");
    for (uint32_t band = 0; band < kPocMaxLines / 32; ++band)
    {
        double s = 0;
        uint64_t c = 0;
        for (uint32_t l = band * 32; l < band * 32 + 32; ++l)
        {
            s += p.lineIndexNs[l];
            c += p.lineIndexCount[l];
        }
        if (c)
            std::fprintf(out, " %u:%.0f", band * 32, s / (double)c);
    }
    std::fprintf(out, "\n");
}
}
