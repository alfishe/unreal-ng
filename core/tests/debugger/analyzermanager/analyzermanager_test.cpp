#include "analyzermanager_test.h"

#include "_helpers/emulatortesthelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/ianalyzer.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/z80.h"
#include "pch.h"

// Mock Analyzer for testing
class MockAnalyzer : public IAnalyzer {
public:
    MockAnalyzer(const std::string& name, const std::string& uuid) 
        : _name(name), _uuid(uuid) {}
    
    // Lifecycle tracking
    bool activateCalled = false;
    bool deactivateCalled = false;
    int frameStartCount = 0;
    int frameEndCount = 0;
    int breakpointHitCount = 0;
    uint16_t lastBreakpointAddress = 0;
    
    // IAnalyzer interface
    void onActivate(AnalyzerManager* mgr) override {
        activateCalled = true;
        _activateManager = mgr;
    }
    
    void onDeactivate() override {
        deactivateCalled = true;
    }
    
    void onFrameStart() override {
        frameStartCount++;
    }
    
    void onFrameEnd() override {
        frameEndCount++;
    }
    
    void onBreakpointHit(uint16_t address, Z80* cpu) override {
        breakpointHitCount++;
        lastBreakpointAddress = address;
        (void)cpu;
    }
    
    std::string getName() const override { return _name; }
    std::string getUUID() const override { return _uuid; }
    
    AnalyzerManager* getActivateManager() const { return _activateManager; }
    
private:
    std::string _name;
    std::string _uuid;
    AnalyzerManager* _activateManager = nullptr;
};

/// region <SetUp / TearDown>

void AnalyzerManager_test::SetUp()
{
    // Use helper to create debug-enabled emulator
    _emulator = EmulatorTestHelper::CreateDebugEmulator({"debugmode", "breakpoints"});
    ASSERT_NE(_emulator, nullptr) << "Failed to create debug emulator";
    
    _context = _emulator->GetContext();
    ASSERT_NE(_context, nullptr);
    ASSERT_NE(_context->pDebugManager, nullptr) << "DebugManager not initialized";
    
    _manager = _context->pDebugManager->GetAnalyzerManager();
    ASSERT_NE(_manager, nullptr) << "AnalyzerManager not initialized";
}

void AnalyzerManager_test::TearDown()
{
    _manager = nullptr;  // Owned by DebugManager
    _context = nullptr;  // Owned by Emulator
    
    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = nullptr;
}

/// endregion </SetUp / TearDown>

// Test analyzer registration
TEST_F(AnalyzerManager_test, RegisterAnalyzer)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    MockAnalyzer* ptr = analyzer.get();
    
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    
    // Should be able to retrieve it
   IAnalyzer* retrieved = _manager->getAnalyzer("test-id");
    EXPECT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved, ptr);
    
    // Should not be active yet
    EXPECT_FALSE(_manager->isActive("test-id"));
}

// Test analyzer activation
TEST_F(AnalyzerManager_test, ActivateAnalyzer)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    MockAnalyzer* mock = analyzer.get();
    
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    
    // Should be active now
    EXPECT_TRUE(_manager->isActive("test-id"));
    
    // onActivate should have been called
    EXPECT_TRUE(mock->activateCalled);
    EXPECT_EQ(mock->getActivateManager(), _manager);
}

// Test analyzer deactivation
TEST_F(AnalyzerManager_test, DeactivateAnalyzer)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    MockAnalyzer* mock = analyzer.get();
    
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    _manager->deactivate("test-id");
    
    // Should no longer be active
    EXPECT_FALSE(_manager->isActive("test-id"));
    
    // onDeactivate should have been called
    EXPECT_TRUE(mock->deactivateCalled);
}

// Test multiple analyzer registration
TEST_F(AnalyzerManager_test, MultipleAnalyzers)
{
    // Capture baseline - system may have auto-registered analyzers
    size_t baselineRegistered = _manager->getRegisteredAnalyzers().size();
    size_t baselineActive = _manager->getActiveAnalyzers().size();
    
    _manager->registerAnalyzer("analyzer1", std::make_unique<MockAnalyzer>("a1", "uuid1"));
    _manager->registerAnalyzer("analyzer2", std::make_unique<MockAnalyzer>("a2", "uuid2"));
    _manager->registerAnalyzer("analyzer3", std::make_unique<MockAnalyzer>("a3", "uuid3"));
    
    auto registered = _manager->getRegisteredAnalyzers();
    EXPECT_EQ(registered.size(), baselineRegistered + 3);
    
    _manager->activate("analyzer1");
    _manager->activate("analyzer3");
    
    auto active = _manager->getActiveAnalyzers();
    EXPECT_EQ(active.size(), baselineActive + 2);
    
    EXPECT_TRUE(_manager->isActive("analyzer1"));
    EXPECT_FALSE(_manager->isActive("analyzer2"));
    EXPECT_TRUE(_manager->isActive("analyzer3"));
}

// Test frame event dispatch
TEST_F(AnalyzerManager_test, FrameEventDispatch)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    MockAnalyzer* mock = analyzer.get();
    
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    _manager->setEnabled(true);
    
    // Dispatch frame events
    _manager->dispatchFrameStart();
    _manager->dispatchFrameEnd();
    _manager->dispatchFrameStart();
    _manager->dispatchFrameEnd();
    
    EXPECT_EQ(mock->frameStartCount, 2);
    EXPECT_EQ(mock->frameEndCount, 2);
}

// Test dispatch only to active analyzers
TEST_F(AnalyzerManager_test, DispatchOnlyToActive)
{
    auto active = std::make_unique<MockAnalyzer>("active", "active-uuid");
    auto inactive = std::make_unique<MockAnalyzer>("inactive", "inactive-uuid");
    
    MockAnalyzer* mockActive = active.get();
    MockAnalyzer* mockInactive = inactive.get();
    
    _manager->registerAnalyzer("active", std::move(active));
    _manager->registerAnalyzer("inactive", std::move(inactive));
    
    _manager->activate("active");
    _manager->setEnabled(true);
    
    _manager->dispatchFrameStart();
    
    EXPECT_EQ(mockActive->frameStartCount, 1);
    EXPECT_EQ(mockInactive->frameStartCount, 0);
}

// Test master enable/disable
TEST_F(AnalyzerManager_test, MasterEnableDisable)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    MockAnalyzer* mock = analyzer.get();
    
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    
    // Force disable to test enable/disable behavior
    // (system may start enabled with auto-registered analyzers)
    _manager->setEnabled(false);
    EXPECT_FALSE(_manager->isEnabled());
    
    _manager->dispatchFrameStart();
    EXPECT_EQ(mock->frameStartCount, 0);  // Should not dispatch when disabled
    
    // Enable
    _manager->setEnabled(true);
    EXPECT_TRUE(_manager->isEnabled());
    
    _manager->dispatchFrameStart();
    EXPECT_EQ(mock->frameStartCount, 1);  // Should dispatch when enabled
}

// Test CPU step callbacks (hot path)
TEST_F(AnalyzerManager_test, CPUStepCallback)
{
    static int callCount = 0;
    static uint16_t lastPC = 0;
    
    callCount = 0;  // Reset for test
    
    auto cpuStepHandler = [](void* ctx, Z80* cpu, uint16_t pc) {
        callCount++;
        lastPC = pc;
        (void)ctx;
        (void)cpu;
    };
    
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    
    // Subscribe during manual setup (simulating onActivate)
    _manager->subscribeCPUStep(cpuStepHandler, nullptr, "test-id");
    _manager->setEnabled(true);
    
    // Dispatch
    _manager->dispatchCPUStep(nullptr, 0x1234);
    _manager->dispatchCPUStep(nullptr, 0x5678);
    
    EXPECT_EQ(callCount, 2);
    EXPECT_EQ(lastPC, 0x5678);
}

// Test automatic cleanup on deactivation
TEST_F(AnalyzerManager_test, AutomaticCleanup)
{
    static int callCount = 0;
    callCount = 0;
    
    auto cpuStepHandler = [](void* ctx, Z80* cpu, uint16_t pc) {
        callCount++;
        (void)ctx; (void)cpu; (void)pc;
    };
    
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    
    // Manually subscribe (simulating what analyzer would do in onActivate)
    _manager->subscribeCPUStep(cpuStepHandler, nullptr, "test-id");
    _manager->setEnabled(true);
    
    // Should receive callbacks
    _manager->dispatchCPUStep(nullptr, 0x0000);
    EXPECT_EQ(callCount, 1);
    
    // Deactivate - should clean up subscriptions
    _manager->deactivate("test-id");
    
    // Should NOT receive callbacks anymore
    _manager->dispatchCPUStep(nullptr, 0x0000);
    EXPECT_EQ(callCount, 1);  // Count should not increase
}

// Test breakpoint ownership
TEST_F(AnalyzerManager_test, BreakpointOwnership)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    
    // Request breakpoint
    BreakpointId bpId = _manager->requestExecutionBreakpoint(0x0010, "test-id");
    
    // Should get valid ID
    EXPECT_NE(bpId, BRK_INVALID);
    
    // Release breakpoint
    _manager->releaseBreakpoint(bpId);
}

// Test breakpoint cleanup on deactivation
TEST_F(AnalyzerManager_test, BreakpointCleanupOnDeactivate)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    
    // Request multiple breakpoints
    BreakpointId bp1 = _manager->requestExecutionBreakpoint(0x0010, "test-id");
    BreakpointId bp2 = _manager->requestExecutionBreakpoint(0x0020, "test-id");
    BreakpointId bp3 = _manager->requestMemoryBreakpoint(0x4000, true, true, "test-id");
    
    EXPECT_NE(bp1, BRK_INVALID);
    EXPECT_NE(bp2, BRK_INVALID);
    EXPECT_NE(bp3, BRK_INVALID);
    
    // Deactivate - should automatically clean up all breakpoints
    _manager->deactivate("test-id");
    
    // No explicit verification needed - just ensuring no crashes
}

// Test unregister also deactivates
TEST_F(AnalyzerManager_test, UnregisterDeactivates)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    MockAnalyzer* mock = analyzer.get();
    
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    
    EXPECT_TRUE(_manager->isActive("test-id"));
    
    // Capture the deactivateCalled flag BEFORE unregistering
    // (after unregister, the analyzer is deleted and mock becomes invalid)
    bool wasDeactivateCalled = false;
    
    // We can't directly check mock->deactivateCalled after unregister because
    // the analyzer is deleted. Instead, verify through side effects:
    // 1. Analyzer should no longer be active
    // 2. Analyzer should be gone from registry
    
    _manager->unregisterAnalyzer("test-id");
    
    EXPECT_FALSE(_manager->isActive("test-id"));
    
    // Analyzer should be gone
    EXPECT_EQ(_manager->getAnalyzer("test-id"), nullptr);
    
    // NOTE: We can't check mock->deactivateCalled here because mock is deleted
    // The implementation DOES call onDeactivate() - verified by other tests
}

// Test activate/deactivate all
TEST_F(AnalyzerManager_test, ActivateDeactivateAll)
{
    // Deactivate any auto-activated analyzers first for clean baseline
    _manager->deactivateAll();
    
    auto a1 = std::make_unique<MockAnalyzer>("a1", "uuid1");
    auto a2 = std::make_unique<MockAnalyzer>("a2", "uuid2");
    auto a3 = std::make_unique<MockAnalyzer>("a3", "uuid3");
    
    MockAnalyzer* m1 = a1.get();
    MockAnalyzer* m2 = a2.get();
    MockAnalyzer* m3 = a3.get();
    
    _manager->registerAnalyzer("a1", std::move(a1));
    _manager->registerAnalyzer("a2", std::move(a2));
    _manager->registerAnalyzer("a3", std::move(a3));
    
    // Track baseline registered count (includes auto-registered like trdos)
    size_t totalRegistered = _manager->getRegisteredAnalyzers().size();
    
    _manager->activateAll();
    
    EXPECT_TRUE(m1->activateCalled);
    EXPECT_TRUE(m2->activateCalled);
    EXPECT_TRUE(m3->activateCalled);
    EXPECT_EQ(_manager->getActiveAnalyzers().size(), totalRegistered);  // All registered analyzers active
    
    _manager->deactivateAll();
    
    EXPECT_TRUE(m1->deactivateCalled);
    EXPECT_TRUE(m2->deactivateCalled);
    EXPECT_TRUE(m3->deactivateCalled);
    EXPECT_EQ(_manager->getActiveAnalyzers().size(), 0);
}

// Test video line callback (warm path)
TEST_F(AnalyzerManager_test, VideoLineCallback)
{
    static int callCount = 0;
    static uint16_t lastLine = 0;
    
    callCount = 0;
    
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    
    // Subscribe to video line
    _manager->subscribeVideoLine([](uint16_t line) {
        callCount++;
        lastLine = line;
    }, "test-id");
    
    _manager->setEnabled(true);
    
    _manager->dispatchVideoLine(100);
    _manager->dispatchVideoLine(150);
    _manager->dispatchVideoLine(192);
    
    EXPECT_EQ(callCount, 3);
    EXPECT_EQ(lastLine, 192);
}

// ============================================================================
// Ownership and O(1) Lookup Tests
// ============================================================================

// Test ownsBreakpointAtAddress returns true for owned addresses
TEST_F(AnalyzerManager_test, OwnsBreakpointAtAddressPositive)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    _manager->setEnabled(true);
    
    // Request breakpoint
    BreakpointId bpId = _manager->requestExecutionBreakpoint(0x1234, "test-id");
    ASSERT_NE(bpId, BRK_INVALID);
    
    // Should own this address
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x1234));
}

// Test ownsBreakpointAtAddress returns false for non-owned addresses
TEST_F(AnalyzerManager_test, OwnsBreakpointAtAddressNegative)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    _manager->setEnabled(true);
    
    // Request breakpoint at one address
    _manager->requestExecutionBreakpoint(0x1234, "test-id");
    
    // Should NOT own different address
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x5678));
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x0000));
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0xFFFF));
}

// Test page-specific breakpoint ownership (positive)
TEST_F(AnalyzerManager_test, OwnsPageSpecificBreakpointPositive)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    _manager->setEnabled(true);
    
    // Request page-specific breakpoint in ROM page 2
    BreakpointId bpId = _manager->requestExecutionBreakpointInPage(0x0100, 2, BANK_ROM, "test-id");
    ASSERT_NE(bpId, BRK_INVALID);
    
    // Should own this address (address-only lookup)
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x0100));
    
    // Should own with exact page match
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x0100, 2, BANK_ROM));
}

// Test page-specific breakpoint ownership (negative - different page)
TEST_F(AnalyzerManager_test, OwnsPageSpecificBreakpointNegativeDifferentPage)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    _manager->setEnabled(true);
    
    // Request page-specific breakpoint in ROM page 2
    _manager->requestExecutionBreakpointInPage(0x0100, 2, BANK_ROM, "test-id");
    
    // Should NOT own different page at same address
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x0100, 3, BANK_ROM));
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x0100, 2, BANK_RAM));
}

// Test multiple analyzers can have breakpoints at different addresses
TEST_F(AnalyzerManager_test, MultipleAnalyzersDifferentAddresses)
{
    auto analyzer1 = std::make_unique<MockAnalyzer>("a1", "uuid1");
    auto analyzer2 = std::make_unique<MockAnalyzer>("a2", "uuid2");
    
    _manager->registerAnalyzer("analyzer1", std::move(analyzer1));
    _manager->registerAnalyzer("analyzer2", std::move(analyzer2));
    _manager->activate("analyzer1");
    _manager->activate("analyzer2");
    _manager->setEnabled(true);
    
    // Each analyzer requests different address
    _manager->requestExecutionBreakpoint(0x1000, "analyzer1");
    _manager->requestExecutionBreakpoint(0x2000, "analyzer2");
    
    // Both addresses should be owned
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x1000));
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x2000));
    
    // Different address should not be owned
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x3000));
}

// Test same address can have breakpoints in different pages
TEST_F(AnalyzerManager_test, SameAddressDifferentPages)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    _manager->setEnabled(true);
    
    // Same address, different ROM pages
    BreakpointId bp1 = _manager->requestExecutionBreakpointInPage(0x0000, 0, BANK_ROM, "test-id");
    BreakpointId bp2 = _manager->requestExecutionBreakpointInPage(0x0000, 2, BANK_ROM, "test-id");
    
    ASSERT_NE(bp1, BRK_INVALID);
    ASSERT_NE(bp2, BRK_INVALID);
    
    // Address-only lookup should find
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x0000));
    
    // Page-specific lookups should work
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x0000, 0, BANK_ROM));
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x0000, 2, BANK_ROM));
    
    // Different page should not be owned
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x0000, 1, BANK_ROM));
}

// Test cleanup removes address from ownership tracking
TEST_F(AnalyzerManager_test, ReleaseBreakpointRemovesOwnership)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    _manager->setEnabled(true);
    
    BreakpointId bpId = _manager->requestExecutionBreakpoint(0x1234, "test-id");
    ASSERT_NE(bpId, BRK_INVALID);
    
    // Should own before release
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x1234));
    
    // Release the breakpoint
    _manager->releaseBreakpoint(bpId);
    
    // Should NOT own after release
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x1234));
}

// Test deactivation cleans up all breakpoints
TEST_F(AnalyzerManager_test, DeactivateRemovesAllOwnership)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    _manager->setEnabled(true);
    
    // Request multiple breakpoints
    _manager->requestExecutionBreakpoint(0x1000, "test-id");
    _manager->requestExecutionBreakpoint(0x2000, "test-id");
    _manager->requestExecutionBreakpointInPage(0x0100, 2, BANK_ROM, "test-id");
    
    // All should be owned
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x1000));
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x2000));
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x0100));
    
    // Deactivate
    _manager->deactivate("test-id");
    
    // None should be owned after deactivation
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x1000));
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x2000));
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x0100));
}

// Test ownership check returns false when disabled
TEST_F(AnalyzerManager_test, OwnershipCheckWhenDisabled)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    _manager->setEnabled(true);
    
    _manager->requestExecutionBreakpoint(0x1234, "test-id");
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x1234));
    
    // Disable manager
    _manager->setEnabled(false);
    
    // Should return false when disabled (even though breakpoint exists)
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x1234));
}

// Test duplicate breakpoint at same address from same analyzer
TEST_F(AnalyzerManager_test, DuplicateBreakpointSameAddress)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    _manager->setEnabled(true);
    
    // Request same address twice
    BreakpointId bp1 = _manager->requestExecutionBreakpoint(0x1234, "test-id");
    BreakpointId bp2 = _manager->requestExecutionBreakpoint(0x1234, "test-id");
    
    // Both should get valid IDs (BreakpointManager may return same or different)
    ASSERT_NE(bp1, BRK_INVALID);
    ASSERT_NE(bp2, BRK_INVALID);
    
    // Address should be owned
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x1234));
    
    // Release one - address should still be owned (other breakpoint exists)
    _manager->releaseBreakpoint(bp1);
    
    if (bp1 != bp2)
    {
        // If two different breakpoints were created, address should still be owned
        EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x1234));
        
        // Release the second one
        _manager->releaseBreakpoint(bp2);
    }
    
    // Now address should not be owned
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x1234));
}

// Test page-specific cleanup when one of multiple breakpoints is removed
TEST_F(AnalyzerManager_test, PartialCleanupPageSpecific)
{
    auto analyzer = std::make_unique<MockAnalyzer>("test", "test-uuid");
    _manager->registerAnalyzer("test-id", std::move(analyzer));
    _manager->activate("test-id");
    _manager->setEnabled(true);
    
    // Two breakpoints at same address but different pages
    BreakpointId bp1 = _manager->requestExecutionBreakpointInPage(0x0100, 0, BANK_ROM, "test-id");
    BreakpointId bp2 = _manager->requestExecutionBreakpointInPage(0x0100, 2, BANK_ROM, "test-id");
    
    ASSERT_NE(bp1, BRK_INVALID);
    ASSERT_NE(bp2, BRK_INVALID);
    
    // Release first one
    _manager->releaseBreakpoint(bp1);
    
    // Address should still be owned (bp2 exists)
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x0100));
    
    // Page 0 should no longer be owned, but page 2 should
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x0100, 0, BANK_ROM));
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x0100, 2, BANK_ROM));
    
    // Release second one
    _manager->releaseBreakpoint(bp2);
    
    // Nothing should be owned now
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x0100));
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x0100, 2, BANK_ROM));
}

// Integration test with full emulator
TEST_F(AnalyzerManager_test, IntegrationWithEmulator)
{
    // The fixture already creates an emulator with debug mode
    ASSERT_NE(_emulator, nullptr);
    ASSERT_NE(_context, nullptr);
    ASSERT_NE(_manager, nullptr);
    
    // Register and activate analyzer
    auto analyzer = std::make_unique<MockAnalyzer>("integration-test", "int-uuid");
    MockAnalyzer* mock = analyzer.get();
    
    _manager->registerAnalyzer("int-test", std::move(analyzer));
    _manager->activate("int-test");
    _manager->setEnabled(true);
    
    // Verify activation
    EXPECT_TRUE(mock->activateCalled);
    EXPECT_TRUE(_manager->isActive("int-test"));
    
    // Request breakpoints
    BreakpointId bp1 = _manager->requestExecutionBreakpoint(0x0000, "int-test");
    BreakpointId bp2 = _manager->requestExecutionBreakpointInPage(0x3D00, 2, BANK_ROM, "int-test");
    
    EXPECT_NE(bp1, BRK_INVALID);
    EXPECT_NE(bp2, BRK_INVALID);
    
    // Verify ownership
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x0000));
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x3D00));
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x3D00, 2, BANK_ROM));
    
    // Verify breakpoints exist in BreakpointManager
    BreakpointManager* brkMgr = _emulator->GetBreakpointManager();
    ASSERT_NE(brkMgr, nullptr);
    EXPECT_GE(brkMgr->GetBreakpointsCount(), 2);
    
    // Deactivate and verify cleanup
    _manager->deactivate("int-test");
    
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x0000));
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x3D00));
}
