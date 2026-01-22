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
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, handler);
    
    // Execute a few CPU cycles (enough to hit 0x0000 and continue)
    _emulator->RunNCPUCycles(10, false);
    
    // Wait briefly for any async notifications
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Remove observer
    messageCenter.RemoveObserver(NC_EXECUTION_BREAKPOINT, handler);
    
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
        _z80->Resume();
        (void)id;
        (void)message;
    };
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, handler);
    
    // Execute CPU cycles
    _emulator->RunNCPUCycles(10, false);
    
    // Wait for async notification
    auto start = std::chrono::steady_clock::now();
    while (!breakpointHit.load() && 
           std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Remove observer
    messageCenter.RemoveObserver(NC_EXECUTION_BREAKPOINT, handler);
    
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
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, handler);
    
    // Execute enough cycles to hit the analyzer breakpoint at 0x0000
    _emulator->RunNCPUCycles(10, false);
    
    // Wait briefly for any async notifications
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Remove observer
    messageCenter.RemoveObserver(NC_EXECUTION_BREAKPOINT, handler);
    
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
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, handler);
    
    // Execute a few cycles at 0x0000 (ROM page 0)
    uint8_t testCode[] = { 0x00, 0x00, 0x76 };  // NOP, NOP, HALT
    WriteTestCode(testCode, sizeof(testCode));
    _emulator->RunNCPUCycles(5, false);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    messageCenter.RemoveObserver(NC_EXECUTION_BREAKPOINT, handler);
    
    // Page-specific analyzer breakpoint should be silent
    EXPECT_EQ(messageCenterNotifications.load(), 0) 
        << "Page-specific analyzer breakpoint should also be silent";
}
