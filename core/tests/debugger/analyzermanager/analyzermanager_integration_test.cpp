/**
 * @file analyzermanager_integration_test.cpp
 * @brief Integration tests for AnalyzerManager breakpoint dispatch behavior
 * 
 * These tests verify the critical "silent dispatch" behavior:
 * - Analyzer-owned breakpoints trigger analyzer callbacks but NOT MessageCenter notifications
 * - Interactive breakpoints trigger MessageCenter notifications (pause UI debugger)
 */

#include "pch.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/ianalyzer.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "base/featuremanager.h"

#include "_helpers/messagecenterdrain.h"
#include "_helpers/testwaithelper.h"

// Mock Analyzer that tracks breakpoint hits
class TrackingAnalyzer : public IAnalyzer {
public:
    TrackingAnalyzer(const std::string& name, const std::string& uuid) 
        : _name(name), _uuid(uuid) {}
    
    // Tracking counters
    std::atomic<int> breakpointHitCount{0};
    std::atomic<uint16_t> lastBreakpointAddress{0};
    
    // IAnalyzer interface
    void onActivate(AnalyzerManager* mgr) override {
        _manager = mgr;
    }
    
    void onDeactivate() override {}
    void onFrameStart() override {}
    void onFrameEnd() override {}
    
    void onBreakpointHit(uint16_t address, Z80* cpu) override {
        breakpointHitCount++;
        lastBreakpointAddress = address;
        (void)cpu;
    }
    
    std::string getName() const override { return _name; }
    std::string getUUID() const override { return _uuid; }
    
    AnalyzerManager* getManager() const { return _manager; }
    
private:
    std::string _name;
    std::string _uuid;
    AnalyzerManager* _manager = nullptr;
};

/// @brief Test fixture for AnalyzerManager integration tests
class AnalyzerManagerIntegration_test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    AnalyzerManager* _manager = nullptr;
    BreakpointManager* _breakpointManager = nullptr;
    Memory* _memory = nullptr;
    Z80* _z80 = nullptr;
    
    void SetUp() override
    {
        // Create debug-enabled emulator with all required features
        _emulator = EmulatorTestHelper::CreateDebugEmulator({"debugmode", "breakpoints"});
        ASSERT_NE(_emulator, nullptr) << "Failed to create debug emulator";
        
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        ASSERT_NE(_context->pDebugManager, nullptr) << "DebugManager not initialized";
        
        _manager = _context->pDebugManager->GetAnalyzerManager();
        ASSERT_NE(_manager, nullptr) << "AnalyzerManager not initialized";
        
        _breakpointManager = _emulator->GetBreakpointManager();
        ASSERT_NE(_breakpointManager, nullptr) << "BreakpointManager not initialized";
        
        _memory = _emulator->GetMemory();
        ASSERT_NE(_memory, nullptr) << "Memory not initialized";
        
        _z80 = _context->pCore->GetZ80();
        ASSERT_NE(_z80, nullptr) << "Z80 not initialized";
    }
    
    void TearDown() override
    {
        _manager = nullptr;
        _breakpointManager = nullptr;
        _memory = nullptr;
        _z80 = nullptr;
        _context = nullptr;
        
        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }
    
    /// @brief Write test code to memory starting at address 0x0000
    void WriteTestCode(const uint8_t* code, size_t length)
    {
        for (size_t i = 0; i < length; i++)
        {
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(i), code[i]);
        }
    }
};

// =============================================================================
// Silent Dispatch Tests - Critical functional verification
// =============================================================================

/// @brief Verify analyzer breakpoint triggers callback but NOT MessageCenter
TEST_F(AnalyzerManagerIntegration_test, AnalyzerBreakpointIsSilent)
{
    // Test code: NOP at 0x0000, NOP at 0x0001, HALT at 0x0002
    uint8_t testCode[] = {
        0x00,       // $0000 NOP - analyzer breakpoint here
        0x00,       // $0001 NOP
        0x76        // $0002 HALT
    };
    WriteTestCode(testCode, sizeof(testCode));
    
    // Set up tracking analyzer
    auto analyzer = std::make_unique<TrackingAnalyzer>("silent-test", "silent-uuid");
    TrackingAnalyzer* mock = analyzer.get();
    
    _manager->registerAnalyzer("silent-test", std::move(analyzer));
    _manager->activate("silent-test");
    _manager->setEnabled(true);
    
    // Register ANALYZER-OWNED breakpoint at 0x0000
    BreakpointId analyzerBp = _manager->requestExecutionBreakpoint(0x0000, "silent-test");
    ASSERT_NE(analyzerBp, BRK_INVALID) << "Failed to register analyzer breakpoint";
    
    // Track MessageCenter notifications
    std::atomic<int> messageCenterNotifications{0};
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    
    auto handler = [&messageCenterNotifications](int id, Message* message) {
        messageCenterNotifications++;
        (void)id;
        (void)message;
    };
    uint64_t handlerId = messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, handler);
    
    // Execute a few CPU cycles (enough to hit 0x0000 and continue)
    _emulator->RunNCPUCycles(10, false);

    // Wait for the analyzer callback, then drain MessageCenter: everything posted before the drain has been delivered,
    // so "nothing arrived" is a fact and not "nothing arrived in the interval I guessed".
    EXPECT_TRUE(TestWait::ForAtLeast(mock->breakpointHitCount, 1))
        << "Analyzer onBreakpointHit was never called";
    ASSERT_TRUE(DrainMessageCenter(messageCenter));
    EXPECT_EQ(messageCenterNotifications.load(), 0) << "Silent breakpoint leaked a MessageCenter notification";
    
    // Remove observer
    messageCenter.RemoveObserverById(NC_EXECUTION_BREAKPOINT, handlerId);
    
    // CRITICAL ASSERTIONS:
    // 1. Analyzer callback SHOULD have been triggered
    EXPECT_GE(mock->breakpointHitCount.load(), 1) 
        << "Analyzer onBreakpointHit should have been called";
    EXPECT_EQ(mock->lastBreakpointAddress.load(), 0x0000)
        << "Breakpoint should have been at address 0x0000";
    
    // 2. MessageCenter SHOULD NOT have been notified (silent breakpoint)
    EXPECT_EQ(messageCenterNotifications.load(), 0) 
        << "MessageCenter should NOT be notified for analyzer-owned breakpoints";
}

/// @brief Verify interactive breakpoint DOES trigger MessageCenter
TEST_F(AnalyzerManagerIntegration_test, InteractiveBreakpointTriggersMessageCenter)
{
    // Test code: NOP at 0x0000, HALT at 0x0001
    uint8_t testCode[] = {
        0x00,       // $0000 NOP - interactive breakpoint here
        0x76        // $0001 HALT
    };
    WriteTestCode(testCode, sizeof(testCode));
    
    // Register INTERACTIVE breakpoint at 0x0000 (default owner)
    uint16_t interactiveBp = _breakpointManager->AddExecutionBreakpoint(0x0000);
    ASSERT_NE(interactiveBp, BRK_INVALID) << "Failed to register interactive breakpoint";
    
    // Track MessageCenter notifications
    std::atomic<int> messageCenterNotifications{0};
    std::atomic<bool> breakpointHit{false};
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    
    auto handler = [&messageCenterNotifications, &breakpointHit, this](int id, Message* message) {
        messageCenterNotifications++;
        breakpointHit = true;
        // Resume execution so we don't hang
        _emulator->Resume();  // Resume via Emulator (single source of truth)
        (void)id;
        (void)message;
    };
    uint64_t handlerId = messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, handler);
    
    // Execute CPU cycles
    _emulator->RunNCPUCycles(10, false);
    
    // Wait for async notification
    TestWait::For([&] { return breakpointHit.load(); });
    
    // Remove observer
    messageCenter.RemoveObserverById(NC_EXECUTION_BREAKPOINT, handlerId);
    
    // CRITICAL ASSERTION:
    // Interactive breakpoint SHOULD trigger MessageCenter notification
    EXPECT_GE(messageCenterNotifications.load(), 1) 
        << "MessageCenter SHOULD be notified for interactive breakpoints";
}

/// @brief Test both analyzer and interactive breakpoints coexist correctly
TEST_F(AnalyzerManagerIntegration_test, MixedBreakpointsBehavior)
{
    // Test code:
    // 0x0000: NOP - analyzer breakpoint (silent)
    // 0x0001: HALT - stops execution before we need to resume
    uint8_t testCode[] = {
        0x00,       // $0000 NOP - analyzer breakpoint
        0x76        // $0001 HALT
    };
    WriteTestCode(testCode, sizeof(testCode));
    
    // Set up tracking analyzer
    auto analyzer = std::make_unique<TrackingAnalyzer>("mixed-test", "mixed-uuid");
    TrackingAnalyzer* mock = analyzer.get();
    
    _manager->registerAnalyzer("mixed-test", std::move(analyzer));
    _manager->activate("mixed-test");
    _manager->setEnabled(true);
    
    // Register ANALYZER breakpoint at 0x0000 (silent)
    BreakpointId analyzerBp = _manager->requestExecutionBreakpoint(0x0000, "mixed-test");
    ASSERT_NE(analyzerBp, BRK_INVALID);
    
    // Track MessageCenter notifications
    std::atomic<int> messageCenterNotifications{0};
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    
    auto handler = [&messageCenterNotifications](int id, Message* message) {
        messageCenterNotifications++;
        (void)id;
        (void)message;
    };
    uint64_t handlerId = messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, handler);
    
    // Execute enough cycles to hit the analyzer breakpoint at 0x0000
    _emulator->RunNCPUCycles(10, false);

    // As above: wait for the callback, then drain MessageCenter before the negative assertion.
    EXPECT_TRUE(TestWait::ForAtLeast(mock->breakpointHitCount, 1))
        << "Analyzer onBreakpointHit was never called";
    ASSERT_TRUE(DrainMessageCenter(messageCenter));
    EXPECT_EQ(messageCenterNotifications.load(), 0) << "Silent breakpoint leaked a MessageCenter notification";
    
    // Remove observer
    messageCenter.RemoveObserverById(NC_EXECUTION_BREAKPOINT, handlerId);
    
    // ASSERTIONS:
    // 1. Analyzer callback should have fired for 0x0000
    EXPECT_GE(mock->breakpointHitCount.load(), 1) 
        << "Analyzer onBreakpointHit should have been called for silent breakpoint";
    
    // 2. MessageCenter should NOT have been notified (only analyzer breakpoint exists)
    EXPECT_EQ(messageCenterNotifications.load(), 0) 
        << "MessageCenter should NOT be notified for analyzer-owned breakpoints";
}

/// @brief Verify ownsBreakpointAtAddress correctly identifies analyzer vs interactive
TEST_F(AnalyzerManagerIntegration_test, OwnershipCheckDistinguishesBreakpoints)
{
    // Set up analyzer
    auto analyzer = std::make_unique<TrackingAnalyzer>("owner-test", "owner-uuid");
    _manager->registerAnalyzer("owner-test", std::move(analyzer));
    _manager->activate("owner-test");
    _manager->setEnabled(true);
    
    // Register analyzer breakpoint at 0x1000
    BreakpointId analyzerBp = _manager->requestExecutionBreakpoint(0x1000, "owner-test");
    ASSERT_NE(analyzerBp, BRK_INVALID);
    
    // Register interactive breakpoint at 0x2000
    uint16_t interactiveBp = _breakpointManager->AddExecutionBreakpoint(0x2000);
    ASSERT_NE(interactiveBp, BRK_INVALID);
    
    // Verify ownership
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x1000))
        << "AnalyzerManager should own breakpoint at 0x1000";
    
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x2000))
        << "AnalyzerManager should NOT own interactive breakpoint at 0x2000";
    
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x3000))
        << "AnalyzerManager should NOT own non-existent address";
}

/// @brief Analyzer breakpoints are taps (breakpoint-taps §8): they run without the debug mode. A request turns on the
/// instruction start's check (Z80::execTapHold) or the fast write's hook, never 'debugmode' / 'breakpoints'
TEST_F(AnalyzerManagerIntegration_test, AnalyzerTapsRunWithoutTheDebugMode)
{
    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = EmulatorTestHelper::CreateDebugEmulator({});
    ASSERT_NE(_emulator, nullptr);
    _context = _emulator->GetContext();
    _manager = _context->pDebugManager->GetAnalyzerManager();
    _breakpointManager = _emulator->GetBreakpointManager();
    _memory = _emulator->GetMemory();
    _z80 = _context->pCore->GetZ80();
    FeatureManager* fm = _emulator->GetFeatureManager();
    fm->setFeature("debugmode", false);
    fm->setFeature("breakpoints", false);
    ASSERT_FALSE(_z80->isDebugMode);

    const uint8_t testCode[] = {
        0x00,              // $0000 NOP          - exec tap
        0x32, 0x00, 0x90,  // $0001 LD (#9000),A - write tap
        0x76               // $0004 HALT
    };
    WriteTestCode(testCode, sizeof(testCode));
    auto analyzer = std::make_unique<TrackingAnalyzer>("taps-test", "taps-uuid");
    TrackingAnalyzer* mock = analyzer.get();
    _manager->registerAnalyzer("taps-test", std::move(analyzer));
    _manager->activate("taps-test");
    _manager->setEnabled(true);
    ASSERT_NE(_manager->requestExecutionBreakpoint(0x0000, "taps-test"), BRK_INVALID);
    ASSERT_NE(_manager->requestMemoryBreakpoint(0x9000, false, true, "taps-test"), BRK_INVALID);

    EXPECT_FALSE(fm->isEnabled("debugmode")) << "a tap needs no debug mode";
    EXPECT_FALSE(fm->isEnabled("breakpoints"));
    EXPECT_FALSE(_z80->isDebugMode);
    EXPECT_TRUE(_z80->execTapHold) << "the exec tap's instruction start check";
    EXPECT_TRUE(_z80->MemIf == _z80->FastMemIf || _z80->MemIf == _z80->FastContendedMemIf) << "the fast memory path";

    _emulator->RunNCPUCycles(30, false);
    EXPECT_EQ(mock->breakpointHitCount.load(), 2) << "the NOP and the write, each once";
    EXPECT_EQ(mock->lastBreakpointAddress.load(), 0x9000);
    EXPECT_EQ(_breakpointManager->GetLastTriggeredBreakpointID(), BRK_INVALID) << "a tap never stops";

    _manager->deactivate("taps-test");
    EXPECT_FALSE(_z80->execTapHold) << "released with the analyzer's taps";
}

/// @brief A user breakpoint and analyzer taps at one address: each analyzer is told, the user's still stops, and an
/// analyzer's release leaves the user's breakpoint (a plain request adopted the user's id before)
TEST_F(AnalyzerManagerIntegration_test, TapsAndUserBreakpointShareAnAddress)
{
    const uint8_t testCode[] = {0x00, 0x00, 0x76};  // $0000 NOP; NOP; HALT
    WriteTestCode(testCode, sizeof(testCode));
    auto first = std::make_unique<TrackingAnalyzer>("first", "first-uuid");
    auto second = std::make_unique<TrackingAnalyzer>("second", "second-uuid");
    TrackingAnalyzer* one = first.get();
    TrackingAnalyzer* two = second.get();
    _manager->registerAnalyzer("first", std::move(first));
    _manager->registerAnalyzer("second", std::move(second));
    _manager->activate("first");
    _manager->activate("second");
    _manager->setEnabled(true);

    const uint16_t user = _breakpointManager->AddExecutionBreakpoint(0x0001);
    const BreakpointId tapOne = _manager->requestExecutionBreakpoint(0x0001, "first");
    const BreakpointId tapTwo = _manager->requestExecutionBreakpoint(0x0001, "second");
    ASSERT_NE(user, BRK_INVALID);
    EXPECT_NE(tapOne, user) << "a tap is its own breakpoint";
    EXPECT_NE(tapTwo, tapOne);

    _emulator->RunNCPUCycles(10, false);
    EXPECT_GE(one->breakpointHitCount.load(), 1);
    EXPECT_GE(two->breakpointHitCount.load(), 1);
    EXPECT_EQ(_breakpointManager->GetLastTriggeredBreakpointID(), user) << "the user's breakpoint stopped";

    _manager->releaseBreakpoint(tapOne);
    EXPECT_NE(_breakpointManager->GetBreakpointById(user), nullptr) << "the user's breakpoint stays";
}

/// @brief Verify features remain enabled when last analyzer breakpoint is released
TEST_F(AnalyzerManagerIntegration_test, FeaturesRemainEnabledAfterBreakpointRelease)
{
    // Start with features enabled
    FeatureManager* fm = _emulator->GetFeatureManager();
    ASSERT_NE(fm, nullptr);
    
    EXPECT_TRUE(fm->isEnabled("debugmode"));
    EXPECT_TRUE(fm->isEnabled("breakpoints"));
    
    // Register analyzer and breakpoint
    auto analyzer = std::make_unique<TrackingAnalyzer>("release-test", "release-uuid");
    _manager->registerAnalyzer("release-test", std::move(analyzer));
    _manager->activate("release-test");
    
    BreakpointId bp = _manager->requestExecutionBreakpoint(0x2000, "release-test");
    ASSERT_NE(bp, BRK_INVALID);
    
    // Release the breakpoint
    _manager->releaseBreakpoint(bp);
    
    // Features should remain enabled (user may have other breakpoints or expect debug mode)
    // Automatic disable would be unexpected behavior
    EXPECT_TRUE(fm->isEnabled("debugmode")) 
        << "debugmode should remain enabled after breakpoint release";
    EXPECT_TRUE(fm->isEnabled("breakpoints")) 
        << "breakpoints should remain enabled after breakpoint release";
}

/// @brief Verify page-specific analyzer breakpoint is also silent
TEST_F(AnalyzerManagerIntegration_test, PageSpecificAnalyzerBreakpointIsSilent)
{
    // Set up tracking analyzer
    auto analyzer = std::make_unique<TrackingAnalyzer>("page-test", "page-uuid");
    TrackingAnalyzer* mock = analyzer.get();
    
    _manager->registerAnalyzer("page-test", std::move(analyzer));
    _manager->activate("page-test");
    _manager->setEnabled(true);
    
    // Register page-specific analyzer breakpoint in ROM page 0
    BreakpointId pageBp = _manager->requestExecutionBreakpointInPage(0x0000, 0, BANK_ROM, "page-test");
    ASSERT_NE(pageBp, BRK_INVALID);
    
    // Verify ownership with page-specific query
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x0000))
        << "AnalyzerManager should own page-specific breakpoint (address-only query)";
    
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x0000, 0, BANK_ROM))
        << "AnalyzerManager should own page-specific breakpoint (exact page query)";
    
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x0000, 1, BANK_ROM))
        << "AnalyzerManager should NOT own different ROM page";
    
    // Track MessageCenter - should NOT be notified
    std::atomic<int> messageCenterNotifications{0};
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    
    auto handler = [&messageCenterNotifications](int id, Message* message) {
        messageCenterNotifications++;
        (void)id;
        (void)message;
    };
    uint64_t handlerId = messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, handler);
    
    // Execute a few cycles at 0x0000 (ROM page 0)
    uint8_t testCode[] = { 0x00, 0x00, 0x76 };  // NOP, NOP, HALT
    WriteTestCode(testCode, sizeof(testCode));
    _emulator->RunNCPUCycles(5, false);
    
    ASSERT_TRUE(DrainMessageCenter(messageCenter));
    messageCenter.RemoveObserverById(NC_EXECUTION_BREAKPOINT, handlerId);
    
    // Page-specific analyzer breakpoint should be silent
    EXPECT_EQ(messageCenterNotifications.load(), 0) 
        << "Page-specific analyzer breakpoint should also be silent";
}
