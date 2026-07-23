#include "memoryregionanalyzer.h"

#include "common/uuid.h"
#include "debugger/analyzers/analyzermanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/io/porttracker.h"
#include "analysiscontext.h"
#include "codeflowanalyzer.h"

// Classifiers
#include "classifiers/generictagclassifier.h"
#include "classifiers/screenclassifier.h"
#include "classifiers/musicclassifier.h"
#include "classifiers/loaderclassifier.h"
#include "classifiers/decompressorclassifier.h"
#include "classifiers/smcclassifier.h"
#include "classifiers/interruptclassifier.h"

MemoryRegionAnalyzer::MemoryRegionAnalyzer(EmulatorContext* context)
    : _context(context)
{
    _uuid = unreal::UUID::Generate().toString();

    // Register classifiers in dependency order
    _pipeline.addClassifier(std::make_unique<GenericTagClassifier>());
    _pipeline.addClassifier(std::make_unique<ScreenClassifier>());
    _pipeline.addClassifier(std::make_unique<MusicClassifier>());
    _pipeline.addClassifier(std::make_unique<LoaderClassifier>());
    _pipeline.addClassifier(std::make_unique<InterruptClassifier>());
    _pipeline.addClassifier(std::make_unique<SMCClassifier>());
    _pipeline.addClassifier(std::make_unique<DecompressorClassifier>());
}

void MemoryRegionAnalyzer::onActivate(AnalyzerManager* mgr)
{
    _manager = mgr;
    _framesSinceRefresh = 0;

    // Initial classification
    refresh();
}

void MemoryRegionAnalyzer::onDeactivate()
{
    _manager = nullptr;
}

void MemoryRegionAnalyzer::onFrameEnd()
{
    _framesSinceRefresh++;

    if (_framesSinceRefresh >= _refreshInterval)
    {
        refresh();
        _framesSinceRefresh = 0;
    }
}

void MemoryRegionAnalyzer::refresh()
{
    runClassificationPipeline();

    // Use code flow info for smarter aggregation
    if (!_lastCodeFlowResult.isUnreachedCode.empty())
    {
        _regionMap.Build(_tagMap, _lastCodeFlowResult.isUnreachedCode);
    }
    else
    {
        _regionMap.Build(_tagMap);
    }
}

void MemoryRegionAnalyzer::runClassificationPipeline()
{
    // Step 1: Get primary types from R/W/X counters
    MemoryAccessTracker* tracker = nullptr;
    if (_context && _context->pMemory)
    {
        tracker = &_context->pMemory->GetAccessTracker();
    }
    _segmenter.Refresh(tracker);

    // Copy types to tag map
    const auto& types = _segmenter.GetTypes();
    for (uint32_t addr = 0; addr < 65536; addr++)
    {
        _tagMap.setType(static_cast<uint16_t>(addr), types[addr]);
    }

    // Step 2: Apply hardcoded regions (VRAM, system vars)
    applyHardcodedRegions();

    // Step 3: Build analysis context
    AnalysisContext ctx;
    ctx.emulatorContext = _context;
    ctx.tracker = tracker;
    ctx.portTracker = _context ? _context->pPortTracker : nullptr;
    ctx.currentFrame = _context ? _context->emulatorState.frame_counter : 0;

    // Step 4: Run classifier pipeline
    _pipeline.runAll(ctx, _tagMap);

    // Step 5: Code flow analysis - identify unreached CODE (does NOT modify tagMap)
    if (_context && _context->pMemory)
    {
        CodeFlowAnalyzer codeFlow(_context->pMemory);
        _lastCodeFlowResult = codeFlow.Analyze(_tagMap);
    }
}

void MemoryRegionAnalyzer::applyHardcodedRegions()
{
    // Screen buffer: 0x4000–0x5AFF
    for (uint16_t addr = 0x4000; addr <= 0x5AFF; addr++)
    {
        _tagMap.addTag(addr, MemoryTag::ScreenBuffer);
    }

    // System variables: 0x5B00–0x5CBF
    for (uint16_t addr = 0x5B00; addr <= 0x5CBF; addr++)
    {
        _tagMap.addTag(addr, MemoryTag::SystemVariables);
    }
}
