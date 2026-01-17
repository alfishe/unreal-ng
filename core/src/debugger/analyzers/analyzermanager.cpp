#include "analyzermanager.h"

#include "emulator/emulatorcontext.h"
#include "debugger/debugmanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/platform.h"

AnalyzerManager::AnalyzerManager(EmulatorContext* context)
    : _context(context)
{
    // Don't access DebugManager members during construction
    // They will be initialized later when DebugManager construction completes
    _breakpointManager = nullptr;
    _featureManager = nullptr;
    (void)_context;  // May be used later for feature manager integration
}

AnalyzerManager::~AnalyzerManager()
{
    deactivateAll();
}

void AnalyzerManager::init(DebugManager* debugManager)
{
    // Initialize references after DebugManager construction is complete
    // We receive the DebugManager pointer directly because _context->pDebugManager
    // isn't set until after the DebugManager constructor completes
    if (debugManager) {
        _breakpointManager = debugManager->GetBreakpointsManager();
    }
    // FeatureManager integration will be added later when needed
}

// Analyzer lifecycle

void AnalyzerManager::registerAnalyzer(const std::string& id, std::unique_ptr<IAnalyzer> analyzer)
{
    if (_analyzers.count(id) > 0) {
        // Already registered - deactivate and replace
        if (isActive(id)) {
            deactivate(id);
        }
    }
    
    analyzer->_manager = this;
    _analyzers[id] = std::move(analyzer);
}

void AnalyzerManager::unregisterAnalyzer(const std::string& id)
{
    // Deactivate first if active (this will clean up subscriptions and breakpoints)
    if (isActive(id)) {
        deactivate(id);
    } else {
        // Even if not in active set, call onDeactivate() if analyzer exists
        // This ensures proper cleanup notification
        if (_analyzers.count(id) > 0) {
            _analyzers[id]->onDeactivate();
            // Clean up resources
            removeAllBreakpointsForAnalyzer(id);
            removeAllSubscriptionsForAnalyzer(id);
        }
    }
    
    _analyzers.erase(id);
}

IAnalyzer* AnalyzerManager::getAnalyzer(const std::string& id)
{
    auto it = _analyzers.find(id);
    if (it == _analyzers.end()) {
        return nullptr;
    }
    return it->second.get();
}

// Activation control

void AnalyzerManager::activate(const std::string& id)
{
    if (_analyzers.count(id) == 0) {
        return; // Analyzer not registered
    }
    
    if (isActive(id)) {
        return; // Already active
    }
    
    // Mark as active first
    _activeAnalyzers.insert(id);
    
    // Call analyzer's onActivate
    _analyzers[id]->onActivate(this);
}

void AnalyzerManager::deactivate(const std::string& id)
{
    if (!isActive(id)) {
        return; // Not active
    }
    
    // Call analyzer's onDeactivate
    if (_analyzers.count(id) > 0) {
        _analyzers[id]->onDeactivate();
    }
    
    // Remove from active set
    _activeAnalyzers.erase(id);
    
    // Clean up all resources owned by this analyzer
    removeAllBreakpointsForAnalyzer(id);
    removeAllSubscriptionsForAnalyzer(id);
}

void AnalyzerManager::activateAll()
{
    for (const auto& [id, analyzer] : _analyzers) {
        activate(id);
    }
}

void AnalyzerManager::deactivateAll()
{
    // Copy IDs to avoid iterator invalidation
    std::vector<std::string> activeIds;
    for (const auto& id : _activeAnalyzers) {
        activeIds.push_back(id);
    }
    
    for (const auto& id : activeIds) {
        deactivate(id);
    }
}

bool AnalyzerManager::isActive(const std::string& id) const
{
    return _activeAnalyzers.count(id) > 0;
}

// Query

std::vector<std::string> AnalyzerManager::getRegisteredAnalyzers() const
{
    std::vector<std::string> result;
    result.reserve(_analyzers.size());
    for (const auto& [id, _] : _analyzers) {
        result.push_back(id);
    }
    return result;
}

std::vector<std::string> AnalyzerManager::getActiveAnalyzers() const
{
    return std::vector<std::string>(_activeAnalyzers.begin(), _activeAnalyzers.end());
}

// Hot path subscriptions

CallbackId AnalyzerManager::subscribeCPUStep(
    void (*fn)(void* ctx, Z80* cpu, uint16_t pc),
    void* context,
    const std::string& analyzerId)
{
    CallbackId id = _nextCallbackId++;
    
    CPUStepCallback cb;
    cb.fn = fn;
    cb.context = context;
    cb.ownerId = analyzerId;
    
    _cpuStepCallbacks.push_back(cb);
    
    _subscriptionOwners[id] = analyzerId;
    _analyzerSubscriptions[analyzerId].push_back(id);
    
    return id;
}

CallbackId AnalyzerManager::subscribeMemoryRead(
    void (*fn)(void* ctx, uint16_t addr, uint8_t val),
    void* context,
    const std::string& analyzerId)
{
    CallbackId id = _nextCallbackId++;
    
    MemoryCallback cb;
    cb.fn = fn;
    cb.context = context;
    cb.ownerId = analyzerId;
    
    _memoryReadCallbacks.push_back(cb);
    
    _subscriptionOwners[id] = analyzerId;
    _analyzerSubscriptions[analyzerId].push_back(id);
    
    return id;
}

CallbackId AnalyzerManager::subscribeMemoryWrite(
    void (*fn)(void* ctx, uint16_t addr, uint8_t val),
    void* context,
    const std::string& analyzerId)
{
    CallbackId id = _nextCallbackId++;
    
    MemoryCallback cb;
    cb.fn = fn;
    cb.context = context;
    cb.ownerId = analyzerId;
    
    _memoryWriteCallbacks.push_back(cb);
    
    _subscriptionOwners[id] = analyzerId;
    _analyzerSubscriptions[analyzerId].push_back(id);
    
    return id;
}

// Warm path subscriptions

CallbackId AnalyzerManager::subscribeVideoLine(
    std::function<void(uint16_t line)> callback,
    const std::string& analyzerId)
{
    CallbackId id = _nextCallbackId++;
    
    _videoLineCallbacks.push_back({callback, analyzerId});
    
    _subscriptionOwners[id] = analyzerId;
    _analyzerSubscriptions[analyzerId].push_back(id);
    
    return id;
}

CallbackId AnalyzerManager::subscribeAudioSample(
    std::function<void(int16_t left, int16_t right)> callback,
    const std::string& analyzerId)
{
    CallbackId id = _nextCallbackId++;
    
    _audioCallbacks.push_back({callback, analyzerId});
    
    _subscriptionOwners[id] = analyzerId;
    _analyzerSubscriptions[analyzerId].push_back(id);
    
    return id;
}

// Breakpoint management

BreakpointId AnalyzerManager::requestExecutionBreakpoint(uint16_t address, const std::string& analyzerId)
{
    if (!_breakpointManager) return BRK_INVALID;
    
    // Add breakpoint using BreakpointManager helper method
    uint16_t bpId = _breakpointManager->AddExecutionBreakpoint(address);
    
    // Track ownership
    _breakpointOwners[bpId] = analyzerId;
    _analyzerBreakpoints[analyzerId].push_back(bpId);
    
    return bpId;
}

BreakpointId AnalyzerManager::requestMemoryBreakpoint(uint16_t address, bool onRead, bool onWrite, const std::string& analyzerId)
{
    if (!_breakpointManager) return BRK_INVALID;
    
    uint16_t bpId = BRK_INVALID;
    
    // Determine the combined memory type flags
    uint8_t memoryType = BRK_MEM_NONE;
    if (onRead) memoryType |= BRK_MEM_READ;
    if (onWrite) memoryType |= BRK_MEM_WRITE;
    
    if (memoryType != BRK_MEM_NONE) {
        bpId = _breakpointManager->AddCombinedMemoryBreakpoint(address, memoryType);
        
        // Track ownership
        _breakpointOwners[bpId] = analyzerId;
        _analyzerBreakpoints[analyzerId].push_back(bpId);
    }
    
    return bpId;
}

void AnalyzerManager::releaseBreakpoint(BreakpointId id)
{
    auto it = _breakpointOwners.find(id);
    if (it == _breakpointOwners.end()) {
        return; // Not found
    }
    
    // Remove using BreakpointManager method
    if (_breakpointManager) {
        _breakpointManager->RemoveBreakpointByID(id);
    }
    
    // Remove from ownership tracking
    std::string analyzerId = it->second;
    _breakpointOwners.erase(it);
    
    auto& bps = _analyzerBreakpoints[analyzerId];
    bps.erase(std::remove(bps.begin(), bps.end(), id), bps.end());
}

// Unsubscribe

void AnalyzerManager::unsubscribe(CallbackId id)
{
    auto it = _subscriptionOwners.find(id);
    if (it == _subscriptionOwners.end()) {
        return; // Not found
    }
    
    std::string analyzerId = it->second;
    
    // Remove from subscription lists (expensive - simplified for now)
    // Real impl should track callback type and position
    
    _subscriptionOwners.erase(it);
    
    auto& subs = _analyzerSubscriptions[analyzerId];
    subs.erase(std::remove(subs.begin(), subs.end(), id), subs.end());
}

void AnalyzerManager::unsubscribeAll(const std::string& analyzerId)
{
    removeAllSubscriptionsForAnalyzer(analyzerId);
}

// Dispatch methods

void AnalyzerManager::dispatchCPUStep(Z80* cpu, uint16_t pc)
{
    if (!_enabled) return;
    
    for (const auto& cb : _cpuStepCallbacks) {
        cb.fn(cb.context, cpu, pc);
    }
}

void AnalyzerManager::dispatchMemoryRead(uint16_t addr, uint8_t val)
{
    if (!_enabled) return;
    
    for (const auto& cb : _memoryReadCallbacks) {
        cb.fn(cb.context, addr, val);
    }
}

void AnalyzerManager::dispatchMemoryWrite(uint16_t addr, uint8_t val)
{
    if (!_enabled) return;
    
    for (const auto& cb : _memoryWriteCallbacks) {
        cb.fn(cb.context, addr, val);
    }
}

void AnalyzerManager::dispatchVideoLine(uint16_t line)
{
    if (!_enabled) return;
    
    for (const auto& [cb, _] : _videoLineCallbacks) {
        cb(line);
    }
}

void AnalyzerManager::dispatchAudioSample(int16_t left, int16_t right)
{
    if (!_enabled) return;
    
    for (const auto& [cb, _] : _audioCallbacks) {
        cb(left, right);
    }
}

void AnalyzerManager::dispatchFrameStart()
{
    if (!_enabled) return;
    
    for (const auto& id : _activeAnalyzers) {
        _analyzers.at(id)->onFrameStart();
    }
}

void AnalyzerManager::dispatchFrameEnd()
{
    if (!_enabled) return;
    
    for (const auto& id : _activeAnalyzers) {
        _analyzers.at(id)->onFrameEnd();
    }
}

void AnalyzerManager::dispatchBreakpointHit(uint16_t addr, BreakpointId bpId, Z80* cpu)
{
    if (!_enabled) return;
    (void)bpId;  // May be used in future for more precise lookup
    
    // Find all analyzers that own breakpoints at this address
    for (const auto& [id, analyzerId] : _breakpointOwners) {
        uint16_t bpAddr = static_cast<uint16_t>(id & 0xFFFF);
        if (bpAddr == addr && _activeAnalyzers.count(analyzerId) > 0) {
            _analyzers.at(analyzerId)->onBreakpointHit(addr, cpu);
        }
    }
}

// Feature toggle

void AnalyzerManager::setEnabled(bool enabled)
{
    _enabled = enabled;
}

bool AnalyzerManager::isEnabled() const
{
    return _enabled;
}

// Helper methods

void AnalyzerManager::removeAllBreakpointsForAnalyzer(const std::string& analyzerId)
{
    auto it = _analyzerBreakpoints.find(analyzerId);
    if (it == _analyzerBreakpoints.end()) {
        return;
    }
    
    for (BreakpointId bpId : it->second) {
        releaseBreakpoint(bpId);
    }
    
    _analyzerBreakpoints.erase(it);
}

void AnalyzerManager::removeAllSubscriptionsForAnalyzer(const std::string& analyzerId)
{
    auto it = _analyzerSubscriptions.find(analyzerId);
    if (it == _analyzerSubscriptions.end()) {
        return;
    }
    
    // Remove all callbacks owned by this analyzer
    _cpuStepCallbacks.erase(
        std::remove_if(_cpuStepCallbacks.begin(), _cpuStepCallbacks.end(),
            [&](const CPUStepCallback& cb) { return cb.ownerId == analyzerId; }),
        _cpuStepCallbacks.end()
    );
    
    _memoryReadCallbacks.erase(
        std::remove_if(_memoryReadCallbacks.begin(), _memoryReadCallbacks.end(),
            [&](const MemoryCallback& cb) { return cb.ownerId == analyzerId; }),
        _memoryReadCallbacks.end()
    );
    
    _memoryWriteCallbacks.erase(
        std::remove_if(_memoryWriteCallbacks.begin(), _memoryWriteCallbacks.end(),
            [&](const MemoryCallback& cb) { return cb.ownerId == analyzerId; }),
        _memoryWriteCallbacks.end()
    );
    
    _videoLineCallbacks.erase(
        std::remove_if(_videoLineCallbacks.begin(), _videoLineCallbacks.end(),
            [&](const auto& pair) { return pair.second == analyzerId; }),
        _videoLineCallbacks.end()
    );
    
    _audioCallbacks.erase(
        std::remove_if(_audioCallbacks.begin(), _audioCallbacks.end(),
            [&](const auto& pair) { return pair.second == analyzerId; }),
        _audioCallbacks.end()
    );
    
    // Remove from tracking maps
    for (CallbackId id : it->second) {
        _subscriptionOwners.erase(id);
    }
    
    _analyzerSubscriptions.erase(it);
}
