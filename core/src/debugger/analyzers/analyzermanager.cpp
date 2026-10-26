#include "analyzermanager.h"

#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"

AnalyzerManager::AnalyzerManager(EmulatorContext* context) : _context(context)
{
    // Don't access DebugManager members during construction
    // They will be initialized later when DebugManager construction completes
    _breakpointManager = nullptr;
    (void)_context;  // May be used later for feature manager integration
}

AnalyzerManager::~AnalyzerManager()
{
    // Before the breakpoint manager goes (DebugManager's destructor): the analyzers' taps and the sink with them
    deactivateAll();
    if (_breakpointManager != nullptr && _tapSink != BRK_NO_TAP)
        _breakpointManager->UnregisterTapSink(_tapSink);
}

void AnalyzerManager::init(DebugManager* debugManager)
{
    // Initialize references after DebugManager construction is complete
    // We receive the DebugManager pointer directly because _context->pDebugManager
    // isn't set until after the DebugManager constructor completes
    if (debugManager)
    {
        _breakpointManager = debugManager->GetBreakpointsManager();
        if (_breakpointManager != nullptr && _tapSink == BRK_NO_TAP)
            _tapSink = _breakpointManager->RegisterTapSink(this);
    }
}

// Analyzer lifecycle

void AnalyzerManager::registerAnalyzer(const std::string& id, std::unique_ptr<IAnalyzer> analyzer)
{
    if (_analyzers.count(id) > 0)
    {
        // Already registered - deactivate and replace
        if (isActive(id))
        {
            deactivate(id);
        }
    }

    analyzer->_manager = this;
    analyzer->_registrationId = id;
    _analyzers[id] = std::move(analyzer);
}

void AnalyzerManager::unregisterAnalyzer(const std::string& id)
{
    // Deactivate first if active (this will clean up subscriptions and breakpoints)
    if (isActive(id))
    {
        deactivate(id);
    }
    else
    {
        // Even if not in active set, call onDeactivate() if analyzer exists
        // This ensures proper cleanup notification
        if (_analyzers.count(id) > 0)
        {
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
    if (it == _analyzers.end())
    {
        return nullptr;
    }
    return it->second.get();
}

// Activation control

bool AnalyzerManager::activate(const std::string& id)
{
    if (_analyzers.count(id) == 0)
    {
        return false;  // Analyzer not registered
    }

    if (isActive(id))
    {
        return true;  // Already active
    }

    // Mark as active first
    _activeAnalyzers.insert(id);

    // Enable dispatching when first analyzer is activated
    if (_activeAnalyzers.size() == 1)
    {
        _enabled = true;
    }

    // Call analyzer's onActivate
    _analyzers[id]->onActivate(this);

    return true;
}

bool AnalyzerManager::deactivate(const std::string& id)
{
    if (!isActive(id))
    {
        return false;  // Not active
    }

    // Call analyzer's onDeactivate
    if (_analyzers.count(id) > 0)
    {
        _analyzers[id]->onDeactivate();
    }

    // Remove from active set
    _activeAnalyzers.erase(id);

    // Disable dispatching when last analyzer is deactivated
    if (_activeAnalyzers.empty())
    {
        _enabled = false;
    }

    // Clean up all resources owned by this analyzer
    removeAllBreakpointsForAnalyzer(id);
    removeAllSubscriptionsForAnalyzer(id);

    return true;
}

void AnalyzerManager::activateAll()
{
    for (const auto& [id, analyzer] : _analyzers)
    {
        activate(id);
    }
}

void AnalyzerManager::deactivateAll()
{
    // Copy IDs to avoid iterator invalidation
    std::vector<std::string> activeIds;
    for (const auto& id : _activeAnalyzers)
    {
        activeIds.push_back(id);
    }

    for (const auto& id : activeIds)
    {
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
    for (const auto& [id, _] : _analyzers)
    {
        result.push_back(id);
    }
    return result;
}

std::vector<std::string> AnalyzerManager::getActiveAnalyzers() const
{
    return std::vector<std::string>(_activeAnalyzers.begin(), _activeAnalyzers.end());
}

bool AnalyzerManager::hasAnalyzer(const std::string& id) const
{
    return _analyzers.count(id) > 0;
}

bool AnalyzerManager::ownsBreakpointAtAddress(uint16_t address) const
{
    if (!_enabled)
        return false;

    // O(1) lookup using address set
    return _ownedAddresses.count(address) > 0;
}

bool AnalyzerManager::ownsBreakpointAtAddress(uint16_t address, uint8_t page, MemoryBankModeEnum pageType) const
{
    if (!_enabled)
        return false;

    // O(1) lookup using composite key
    uint32_t key = makeBreakpointKey(address, page, pageType);
    return _ownedKeys.count(key) > 0;
}

// Hot path subscriptions

CallbackId AnalyzerManager::subscribeCPUStep(void (*fn)(void* ctx, Z80* cpu, uint16_t pc), void* context,
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

// Warm path subscriptions

CallbackId AnalyzerManager::subscribeVideoLine(std::function<void(uint16_t line)> callback,
                                               const std::string& analyzerId)
{
    CallbackId id = _nextCallbackId++;

    _videoLineCallbacks.push_back({callback, analyzerId});

    _subscriptionOwners[id] = analyzerId;
    _analyzerSubscriptions[analyzerId].push_back(id);

    return id;
}

CallbackId AnalyzerManager::subscribeAudioSample(std::function<void(int16_t left, int16_t right)> callback,
                                                 const std::string& analyzerId)
{
    CallbackId id = _nextCallbackId++;

    _audioCallbacks.push_back({callback, analyzerId});

    _subscriptionOwners[id] = analyzerId;
    _analyzerSubscriptions[analyzerId].push_back(id);

    return id;
}

// Breakpoint management

BreakpointId AnalyzerManager::addTap(BreakpointDescriptor* descriptor, const std::string& analyzerId, uint32_t key)
{
    if (_breakpointManager == nullptr || _tapSink == BRK_NO_TAP)
    {
        delete descriptor;
        return BRK_INVALID;
    }
    // A tap of this manager's sink: never stops, never adopts a user's breakpoint at the same address (nor is adopted)
    descriptor->owner = OWNER_ID;
    descriptor->tapSink = _tapSink;
    const uint16_t bpId = _breakpointManager->AddBreakpoint(descriptor);
    if (bpId == BRK_INVALID)
        return BRK_INVALID;

    // Track which analyzer requested this breakpoint for internal dispatch
    _breakpointOwners[bpId] = analyzerId;
    _breakpointKeys[bpId] = key;
    _ownedAddresses.insert(static_cast<uint16_t>(key & 0xFFFF));  // the queries (ownsBreakpointAtAddress)
    _ownedKeys.insert(key);
    _analyzerBreakpoints[analyzerId].push_back(bpId);
    return bpId;
}

BreakpointId AnalyzerManager::requestExecutionBreakpoint(uint16_t address, const std::string& analyzerId)
{
    auto* descriptor = new BreakpointDescriptor();
    descriptor->type = BRK_MEMORY;
    descriptor->memoryType = BRK_MEM_EXECUTE;
    descriptor->z80address = address;
    // Non-page-specific breakpoints use the BANK_INVALID marker in their key
    return addTap(descriptor, analyzerId, makeBreakpointKey(address));
}

BreakpointId AnalyzerManager::requestExecutionBreakpointInPage(uint16_t address, uint8_t page,
                                                               MemoryBankModeEnum pageType,
                                                               const std::string& analyzerId)
{
    auto* descriptor = new BreakpointDescriptor();
    descriptor->type = BRK_MEMORY;
    descriptor->memoryType = BRK_MEM_EXECUTE;
    descriptor->matchType = BRK_MATCH_BANK_ADDR;
    descriptor->z80address = address;
    descriptor->page = page;
    descriptor->pageType = pageType;
    return addTap(descriptor, analyzerId, makeBreakpointKey(address, page, pageType));
}

BreakpointId AnalyzerManager::requestMemoryBreakpoint(uint16_t address, bool onRead, bool onWrite,
                                                      const std::string& analyzerId)
{
    uint8_t memoryType = BRK_MEM_NONE;
    if (onRead)
        memoryType |= BRK_MEM_READ;
    if (onWrite)
        memoryType |= BRK_MEM_WRITE;
    if (memoryType == BRK_MEM_NONE)
        return BRK_INVALID;
    auto* descriptor = new BreakpointDescriptor();
    descriptor->type = BRK_MEMORY;
    descriptor->memoryType = memoryType;
    descriptor->z80address = address;
    return addTap(descriptor, analyzerId, makeBreakpointKey(address));
}

void AnalyzerManager::releaseBreakpoint(BreakpointId id)
{
    auto it = _breakpointOwners.find(id);
    if (it == _breakpointOwners.end())
    {
        return;  // Not found
    }

    // Remove using BreakpointManager method
    if (_breakpointManager)
    {
        _breakpointManager->RemoveBreakpointByID(id);
    }

    // Get the key before erasing (extract address from lower 16 bits)
    auto keyIt = _breakpointKeys.find(id);
    uint32_t removedKey = (keyIt != _breakpointKeys.end()) ? keyIt->second : 0xFFFFFFFF;
    uint16_t removedAddress = static_cast<uint16_t>(removedKey & 0xFFFF);
    
    // Remove from ownership tracking
    std::string analyzerId = it->second;
    _breakpointOwners.erase(it);
    _breakpointKeys.erase(id);

    // Check if any other breakpoint still uses this address (for _ownedAddresses)
    if (removedKey != 0xFFFFFFFF)
    {
        bool addressStillInUse = false;
        for (const auto& [bpId, key] : _breakpointKeys)
        {
            if ((key & 0xFFFF) == removedAddress)
            {
                addressStillInUse = true;
                break;
            }
        }
        if (!addressStillInUse)
        {
            _ownedAddresses.erase(removedAddress);
        }

        // Check if any other breakpoint still uses this exact key (for _ownedKeys)
        bool keyStillInUse = false;
        for (const auto& [bpId, key] : _breakpointKeys)
        {
            if (key == removedKey)
            {
                keyStillInUse = true;
                break;
            }
        }
        if (!keyStillInUse)
        {
            _ownedKeys.erase(removedKey);
        }
    }

    auto& bps = _analyzerBreakpoints[analyzerId];
    bps.erase(std::remove(bps.begin(), bps.end(), id), bps.end());
}

// Unsubscribe

void AnalyzerManager::unsubscribe(CallbackId id)
{
    auto it = _subscriptionOwners.find(id);
    if (it == _subscriptionOwners.end())
    {
        return;  // Not found
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
    if (!_enabled)
        return;

    for (const auto& cb : _cpuStepCallbacks)
    {
        cb.fn(cb.context, cpu, pc);
    }
}

void AnalyzerManager::dispatchVideoLine(uint16_t line)
{
    if (!_enabled)
        return;

    for (const auto& [cb, _] : _videoLineCallbacks)
    {
        cb(line);
    }
}

void AnalyzerManager::dispatchAudioSample(int16_t left, int16_t right)
{
    if (!_enabled)
        return;

    for (const auto& [cb, _] : _audioCallbacks)
    {
        cb(left, right);
    }
}

void AnalyzerManager::dispatchFrameStart()
{
    if (!_enabled)
        return;

    // TTD silent-replay suppression (parent TDD §8.2 + Appendix C).
    // Analyzers are observational — they must not run during replay.
    if (_context && _context->ttdReplayActive)
        return;

    for (const auto& id : _activeAnalyzers)
    {
        _analyzers.at(id)->onFrameStart();
    }
}

void AnalyzerManager::dispatchFrameEnd()
{
    if (!_enabled)
        return;

    // TTD silent-replay suppression (parent TDD §8.2 + Appendix C).
    if (_context && _context->ttdReplayActive)
        return;

    for (const auto& id : _activeAnalyzers)
    {
        _analyzers.at(id)->onFrameEnd();
    }
}

void AnalyzerManager::OnTap(const BreakpointTapHit& hit)
{
    Z80* cpu = _context != nullptr && _context->pCore != nullptr ? _context->pCore->GetZ80() : nullptr;
    dispatchBreakpointHit(hit.address, hit.breakpointID, cpu);
}

void AnalyzerManager::dispatchBreakpointHit(uint16_t addr, BreakpointId bpId, Z80* cpu)
{
    if (!_enabled)
        return;

    // Use the provided breakpoint ID for precise lookup
    // This correctly handles page-specific breakpoints where the same address
    // can have different breakpoints in different memory pages
    auto ownerIt = _breakpointOwners.find(bpId);
    if (ownerIt == _breakpointOwners.end())
    {
        // Not an analyzer-owned breakpoint
        return;
    }
    
    const std::string& analyzerId = ownerIt->second;
    
    // Only dispatch if the analyzer is active
    if (_activeAnalyzers.count(analyzerId) > 0)
    {
        _analyzers.at(analyzerId)->onBreakpointHit(addr, cpu);
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
    // Extract the node from the map first - this way releaseBreakpoint()'s
    // modification of _analyzerBreakpoints won't affect our iteration
    auto node = _analyzerBreakpoints.extract(analyzerId);
    if (node.empty())
    {
        return;
    }

    for (BreakpointId bpId : node.mapped())
    {
        releaseBreakpoint(bpId);
    }
}

void AnalyzerManager::removeAllSubscriptionsForAnalyzer(const std::string& analyzerId)
{
    auto it = _analyzerSubscriptions.find(analyzerId);
    if (it == _analyzerSubscriptions.end())
    {
        return;
    }

    // Remove all callbacks owned by this analyzer
    _cpuStepCallbacks.erase(std::remove_if(_cpuStepCallbacks.begin(), _cpuStepCallbacks.end(),
                                           [&](const CPUStepCallback& cb) { return cb.ownerId == analyzerId; }),
                            _cpuStepCallbacks.end());

    _videoLineCallbacks.erase(std::remove_if(_videoLineCallbacks.begin(), _videoLineCallbacks.end(),
                                             [&](const auto& pair) { return pair.second == analyzerId; }),
                              _videoLineCallbacks.end());

    _audioCallbacks.erase(std::remove_if(_audioCallbacks.begin(), _audioCallbacks.end(),
                                         [&](const auto& pair) { return pair.second == analyzerId; }),
                          _audioCallbacks.end());

    // Remove from tracking maps
    for (CallbackId id : it->second)
    {
        _subscriptionOwners.erase(id);
    }

    _analyzerSubscriptions.erase(it);
}
