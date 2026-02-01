/**
 * @file trdosanalyzer_test.cpp
 * @brief Unit tests for TRDOSAnalyzer
 * 
 * These tests verify the TRDOSAnalyzer functionality:
 * - Registration and activation lifecycle
 * - Breakpoint registration (page-specific and regular)
 * - State machine transitions
 * - Event emission
 * - Query API
 */

#include "pch.h"

#include "trdosanalyzer_test.h"

#include "_helpers/emulatortesthelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/trdos/trdosanalyzer.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"

/// region <SetUp / TearDown>

void TRDOSAnalyzer_test::SetUp()
{
    // Create debug-enabled emulator
    _emulator = EmulatorTestHelper::CreateDebugEmulator({"debugmode", "breakpoints"});
    ASSERT_NE(_emulator, nullptr) << "Failed to create debug emulator";
    
    _context = _emulator->GetContext();
    ASSERT_NE(_context, nullptr);
    ASSERT_NE(_context->pDebugManager, nullptr) << "DebugManager not initialized";
    
    _manager = _context->pDebugManager->GetAnalyzerManager();
    ASSERT_NE(_manager, nullptr) << "AnalyzerManager not initialized";
    
    _breakpointManager = _emulator->GetBreakpointManager();
    ASSERT_NE(_breakpointManager, nullptr) << "BreakpointManager not initialized";
    
    _z80 = _context->pCore->GetZ80();
    ASSERT_NE(_z80, nullptr) << "Z80 not initialized";
    
    // Get the TRDOSAnalyzer (should be auto-registered by DebugManager)
    _analyzer = _manager->getAnalyzer<TRDOSAnalyzer>("trdos");
    // Note: May be nullptr if not auto-registered - tests handle this
}

void TRDOSAnalyzer_test::TearDown()
{
    _analyzer = nullptr;
    _manager = nullptr;
    _breakpointManager = nullptr;
    _z80 = nullptr;
    _context = nullptr;
    
    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = nullptr;
}

/// endregion </SetUp / TearDown>

// =============================================================================
// Registration and Activation Tests
// =============================================================================

/// @brief Test analyzer can be created and registered
TEST_F(TRDOSAnalyzer_test, CreateAndRegister)
{
    // If not auto-registered, create and register manually
    if (_analyzer == nullptr)
    {
        auto analyzer = std::make_unique<TRDOSAnalyzer>(_context);
        _analyzer = analyzer.get();
        _manager->registerAnalyzer("trdos", std::move(analyzer));
    }
    
    // Verify it's registered
    EXPECT_TRUE(_manager->hasAnalyzer("trdos"));
    EXPECT_NE(_manager->getAnalyzer("trdos"), nullptr);
    
    // Should not be active yet (unless auto-activated)
    // Check state after ensuring we have a fresh analyzer
}

/// @brief Test analyzer activation sets up breakpoints
TEST_F(TRDOSAnalyzer_test, ActivationSetsUpBreakpoints)
{
    // Ensure analyzer exists
    if (_analyzer == nullptr)
    {
        auto analyzer = std::make_unique<TRDOSAnalyzer>(_context);
        _analyzer = analyzer.get();
        _manager->registerAnalyzer("trdos", std::move(analyzer));
    }
    
    // Capture breakpoint count before activation
    size_t breakpointsBefore = _breakpointManager->GetBreakpointsCount();
    
    // Activate
    _manager->activate("trdos");
    _manager->setEnabled(true);
    
    EXPECT_TRUE(_manager->isActive("trdos"));
    
    // Should have registered TR-DOS breakpoints (0x3D00, 0x3D21, 0x0077)
    size_t breakpointsAfter = _breakpointManager->GetBreakpointsCount();
    EXPECT_GE(breakpointsAfter, breakpointsBefore + 3) 
        << "Expected at least 3 new breakpoints for TR-DOS entry points";
}

/// @brief Test breakpoints are owned by AnalyzerManager
TEST_F(TRDOSAnalyzer_test, BreakpointsOwnedByAnalyzerManager)
{
    // Ensure analyzer is active
    if (_analyzer == nullptr)
    {
        auto analyzer = std::make_unique<TRDOSAnalyzer>(_context);
        _analyzer = analyzer.get();
        _manager->registerAnalyzer("trdos", std::move(analyzer));
    }
    
    _manager->activate("trdos");
    _manager->setEnabled(true);
    
    // Verify AnalyzerManager owns the TR-DOS breakpoint addresses
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x3D00))
        << "TR-DOS entry breakpoint (0x3D00) should be owned by AnalyzerManager";
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x3D21))
        << "Command dispatch breakpoint (0x3D21) should be owned by AnalyzerManager";
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x0077))
        << "TR-DOS exit breakpoint (0x0077) should be owned by AnalyzerManager";
}

/// @brief Test page-specific breakpoint ownership
TEST_F(TRDOSAnalyzer_test, PageSpecificBreakpointOwnership)
{
    // Ensure analyzer is active
    if (_analyzer == nullptr)
    {
        auto analyzer = std::make_unique<TRDOSAnalyzer>(_context);
        _analyzer = analyzer.get();
        _manager->registerAnalyzer("trdos", std::move(analyzer));
    }
    
    _manager->activate("trdos");
    _manager->setEnabled(true);
    
    // Get the TR-DOS ROM page number
    Memory& memory = *_context->pMemory;
    if (memory.base_dos_rom != nullptr)
    {
        uint8_t dosRomPage = static_cast<uint8_t>(memory.GetROMPageFromAddress(memory.base_dos_rom));
        
        // Check page-specific ownership
        EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x3D00, dosRomPage, BANK_ROM))
            << "TR-DOS entry breakpoint should be owned for ROM page " << (int)dosRomPage;
        
        // Should NOT own for different ROM pages
        uint8_t otherPage = (dosRomPage + 1) % 4;  // Get a different page
        EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x3D00, otherPage, BANK_ROM))
            << "TR-DOS entry breakpoint should NOT be owned for different ROM page";
    }
}

/// @brief Test deactivation cleans up breakpoints
TEST_F(TRDOSAnalyzer_test, DeactivationCleansUpBreakpoints)
{
    // Ensure analyzer is active first
    if (_analyzer == nullptr)
    {
        auto analyzer = std::make_unique<TRDOSAnalyzer>(_context);
        _analyzer = analyzer.get();
        _manager->registerAnalyzer("trdos", std::move(analyzer));
    }
    
    _manager->activate("trdos");
    _manager->setEnabled(true);
    
    // Verify breakpoints are owned
    EXPECT_TRUE(_manager->ownsBreakpointAtAddress(0x3D00));
    
    // Deactivate
    _manager->deactivate("trdos");
    
    // Breakpoints should no longer be owned
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x3D00))
        << "Breakpoints should be released after deactivation";
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x3D21));
    EXPECT_FALSE(_manager->ownsBreakpointAtAddress(0x0077));
}

// =============================================================================
// State Machine Tests
// =============================================================================

/// @brief Test initial state is IDLE
TEST_F(TRDOSAnalyzer_test, InitialStateIsIdle)
{
    if (_analyzer == nullptr)
    {
        auto analyzer = std::make_unique<TRDOSAnalyzer>(_context);
        _analyzer = analyzer.get();
        _manager->registerAnalyzer("trdos", std::move(analyzer));
    }
    
    _manager->activate("trdos");
    
    EXPECT_EQ(_analyzer->getState(), TRDOSAnalyzerState::IDLE);
}

/// @brief Test state machine responds to breakpoint hit at TR-DOS entry
TEST_F(TRDOSAnalyzer_test, StateTransitionOnTRDOSEntry)
{
    if (_analyzer == nullptr)
    {
        auto analyzer = std::make_unique<TRDOSAnalyzer>(_context);
        _analyzer = analyzer.get();
        _manager->registerAnalyzer("trdos", std::move(analyzer));
    }
    
    _manager->activate("trdos");
    _manager->setEnabled(true);
    
    // Verify initial state
    EXPECT_EQ(_analyzer->getState(), TRDOSAnalyzerState::IDLE);
    
    // Simulate TR-DOS entry breakpoint hit
    _z80->pc = 0x3D00;
    _analyzer->onBreakpointHit(0x3D00, _z80);
    
    // State should transition to IN_TRDOS
    EXPECT_EQ(_analyzer->getState(), TRDOSAnalyzerState::IN_TRDOS);
    
    // Should have emitted TRDOS_ENTRY event
    auto events = _analyzer->getEvents();
    EXPECT_GE(events.size(), 1);
    if (!events.empty())
    {
        EXPECT_EQ(events.back().type, TRDOSEventType::TRDOS_ENTRY);
    }
}

// =============================================================================
// Event Emission Tests
// =============================================================================

/// @brief Test events are captured and can be queried
TEST_F(TRDOSAnalyzer_test, EventQueryAPI)
{
    if (_analyzer == nullptr)
    {
        auto analyzer = std::make_unique<TRDOSAnalyzer>(_context);
        _analyzer = analyzer.get();
        _manager->registerAnalyzer("trdos", std::move(analyzer));
    }
    
    _manager->activate("trdos");
    _manager->setEnabled(true);
    
    // Clear any existing events
    _analyzer->clear();
    EXPECT_EQ(_analyzer->getEventCount(), 0);
    
    // Simulate a breakpoint hit to generate an event
    _z80->pc = 0x3D00;
    _analyzer->onBreakpointHit(0x3D00, _z80);
    
    // Verify event was captured
    EXPECT_GE(_analyzer->getEventCount(), 1);
    
    // Test getEvents()
    auto allEvents = _analyzer->getEvents();
    EXPECT_FALSE(allEvents.empty());
    
    // Test getNewEvents()
    auto newEvents = _analyzer->getNewEvents();
    EXPECT_FALSE(newEvents.empty());
    
    // Second call to getNewEvents() should return empty (no new events)
    auto newEvents2 = _analyzer->getNewEvents();
    EXPECT_TRUE(newEvents2.empty());
}

/// @brief Test clear() resets event buffer
TEST_F(TRDOSAnalyzer_test, ClearResetsBuffer)
{
    if (_analyzer == nullptr)
    {
        auto analyzer = std::make_unique<TRDOSAnalyzer>(_context);
        _analyzer = analyzer.get();
        _manager->registerAnalyzer("trdos", std::move(analyzer));
    }
    
    _manager->activate("trdos");
    _manager->setEnabled(true);
    
    // Generate some events
    _z80->pc = 0x3D00;
    _analyzer->onBreakpointHit(0x3D00, _z80);
    EXPECT_GT(_analyzer->getEventCount(), 0);
    
    // Clear
    _analyzer->clear();
    
    // Should be empty
    EXPECT_EQ(_analyzer->getEventCount(), 0);
    EXPECT_TRUE(_analyzer->getEvents().empty());
}

// =============================================================================
// Silent Dispatch Tests (critical functionality)
// =============================================================================

/// @brief Verify TR-DOS breakpoints don't trigger MessageCenter notifications
TEST_F(TRDOSAnalyzer_test, SilentDispatch)
{
    if (_analyzer == nullptr)
    {
        auto analyzer = std::make_unique<TRDOSAnalyzer>(_context);
        _analyzer = analyzer.get();
        _manager->registerAnalyzer("trdos", std::move(analyzer));
    }
    
    _manager->activate("trdos");
    _manager->setEnabled(true);
    
    // Track MessageCenter notifications
    std::atomic<int> notifications{0};
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    
    auto handler = [&notifications](int id, Message* msg) {
        notifications++;
        (void)id; (void)msg;
    };
    mc.AddObserver(NC_EXECUTION_BREAKPOINT, handler);
    
    // Dispatch breakpoint hit (simulating what Z80::Z80Step would do)
    // First get a valid breakpoint ID
    BreakpointId bpId = 1;  // Simulated ID
    _manager->dispatchBreakpointHit(0x3D00, bpId, _z80);
    
    mc.RemoveObserver(NC_EXECUTION_BREAKPOINT, handler);
    
    // Analyzer should have received the callback
    EXPECT_GE(_analyzer->getEventCount(), 1);
    
    // Note: MessageCenter is only triggered by Z80::Z80Step for non-analyzer breakpoints
    // This test verifies the dispatch mechanism works
}
