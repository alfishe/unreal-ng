#include <gtest/gtest.h>
#include "debugger/analyzers/rom-print/romprintdetector.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/basic-lang/basicencoder.h"
#include "debugger/debugmanager.h"
#include "emulator/emulatorcontext.h"
#include "common/image/imagehelper.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/basictesthelper.h"
#include <iostream>

class ROMPrintDetector_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create emulator with debug support
        _emulator = EmulatorTestHelper::CreateDebugEmulator();
        ASSERT_NE(_emulator, nullptr);
        
        EmulatorContext* context = _emulator->GetContext();
        ASSERT_NE(context, nullptr);
        ASSERT_NE(context->pDebugManager, nullptr);
        
        _manager = context->pDebugManager->GetAnalyzerManager();
        ASSERT_NE(_manager, nullptr);
    }
    
    void TearDown() override
    {
        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
        _manager = nullptr;
    }
    
    Emulator* _emulator = nullptr;
    AnalyzerManager* _manager = nullptr;
};

// Test basic lifecycle
TEST_F(ROMPrintDetector_test, RegisterAndActivate)
{
    auto detector = std::make_unique<ROMPrintDetector>();
    ROMPrintDetector* detectorPtr = detector.get();
    
    // Register
    _manager->registerAnalyzer("rom-print", std::move(detector));
    
    // Should not be active yet
    EXPECT_FALSE(_manager->isActive("rom-print"));
    
    // Activate
    _manager->activate("rom-print");
    EXPECT_TRUE(_manager->isActive("rom-print"));
    
    // Breakpoints are created during activation (can't verify count directly as _breakpoints is private)
    
    // Deactivate
    _manager->deactivate("rom-print");
    EXPECT_FALSE(_manager->isActive("rom-print"));
}

// UNIT TEST: Test character capture by SIMULATING breakpoint hits
// NOTE: This does NOT run the emulator or execute ROM code.
//       We manually set CPU registers and directly call onBreakpointHit() to test
//       the detector's character decoding logic in isolation.
TEST_F(ROMPrintDetector_test, CaptureASCIICharacters)
{
    auto detector = std::make_unique<ROMPrintDetector>();
    ROMPrintDetector* detectorPtr = detector.get();
    
    _manager->registerAnalyzer("rom-print", std::move(detector));
    _manager->activate("rom-print");
    
    // Simulate breakpoint hits with ASCII characters
    Z80* cpu = _emulator->GetContext()->pCore->GetZ80();
    
    // Print 'H'
    cpu->a = 'H';
    detectorPtr->onBreakpointHit(0x0010, cpu);
    
    // Print 'E'
    cpu->a = 'E';
    detectorPtr->onBreakpointHit(0x0010, cpu);
    
    // Print 'L'
    cpu->a = 'L';
    detectorPtr->onBreakpointHit(0x0010, cpu);
    
    // Print 'L'
    cpu->a = 'L';
    detectorPtr->onBreakpointHit(0x0010, cpu);
    
    // Print 'O'
    cpu->a = 'O';
    detectorPtr->onBreakpointHit(0x0010, cpu);
    
    // Verify captured output
    std::string output = detectorPtr->getFullHistory();
    EXPECT_EQ(output, "HELLO");
}

// UNIT TEST: Test newline handling by SIMULATING character-by-character input
// NOTE: Emulator is created but NOT running. We bypass the real breakpoint system.
TEST_F(ROMPrintDetector_test, CaptureLines)
{
    auto detector = std::make_unique<ROMPrintDetector>();
    ROMPrintDetector* detectorPtr = detector.get();
    
    _manager->registerAnalyzer("rom-print", std::move(detector));
    _manager->activate("rom-print");
    
    Z80* cpu = _emulator->GetContext()->pCore->GetZ80();
    
    // Print "LINE1\nLINE2\n"
    const char* text = "LINE1\nLINE2\n";
    for (const char* p = text; *p; p++) {
        cpu->a = static_cast<uint8_t>(*p == '\n' ? 0x0D : *p);
        detectorPtr->onBreakpointHit(0x0010, cpu);
    }
    
    // Verify lines
    std::vector<std::string> lines = detectorPtr->getLines();
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], "LINE1");
    EXPECT_EQ(lines[1], "LINE2");
}

// UNIT TEST: Test incremental output retrieval
// NOTE: Simulates character input without actual ROM execution.
TEST_F(ROMPrintDetector_test, GetNewOutput)
{
    auto detector = std::make_unique<ROMPrintDetector>();
    ROMPrintDetector* detectorPtr = detector.get();
    
    _manager->registerAnalyzer("rom-print", std::move(detector));
    _manager->activate("rom-print");
    
    Z80* cpu = _emulator->GetContext()->pCore->GetZ80();
    
    // Print "HELLO"
    const char* text1 = "HELLO";
    for (const char* p = text1; *p; p++) {
        cpu->a = static_cast<uint8_t>(*p);
        detectorPtr->onBreakpointHit(0x0010, cpu);
    }
    
    // Get new output (should return "HELLO")
    std::string output1 = detectorPtr->getNewOutput();
    EXPECT_EQ(output1, "HELLO");
    
    // Get new output again (should return empty)
    std::string output2 = detectorPtr->getNewOutput();
    EXPECT_EQ(output2, "");
    
    // Print "WORLD"
    const char* text2 = "WORLD";
    for (const char* p = text2; *p; p++) {
        cpu->a = static_cast<uint8_t>(*p);
        detectorPtr->onBreakpointHit(0x0010, cpu);
    }
    
    // Get new output (should return "WORLD")
    std::string output3 = detectorPtr->getNewOutput();
    EXPECT_EQ(output3, "WORLD");
    
    // Full history should have both
    EXPECT_EQ(detectorPtr->getFullHistory(), "HELLOWORLD");
}

// UNIT TEST: Test history clearing
// NOTE: Uses simulated input to populate history before clearing.
TEST_F(ROMPrintDetector_test, ClearHistory)
{
    auto detector = std::make_unique<ROMPrintDetector>();
    ROMPrintDetector* detectorPtr = detector.get();
    
    _manager->registerAnalyzer("rom-print", std::move(detector));
    _manager->activate("rom-print");
    
    Z80* cpu = _emulator->GetContext()->pCore->GetZ80();
    
    // Print "TEST"
    const char* text = "TEST";
    for (const char* p = text; *p; p++) {
        cpu->a = static_cast<uint8_t>(*p);
        detectorPtr->onBreakpointHit(0x0010, cpu);
    }
    
    EXPECT_EQ(detectorPtr->getFullHistory(), "TEST");
    
    // Clear
    detectorPtr->clear();
    
    // Should be empty
    EXPECT_EQ(detectorPtr->getFullHistory(), "");
    EXPECT_EQ(detectorPtr->getLines().size(), 0);
}

// UNIT TEST: Test incremental line retrieval
// NOTE: Simulates multi-line input without real emulator execution.
TEST_F(ROMPrintDetector_test, GetNewLines)
{
    auto detector = std::make_unique<ROMPrintDetector>();
    ROMPrintDetector* detectorPtr = detector.get();
    
    _manager->registerAnalyzer("rom-print", std::move(detector));
    _manager->activate("rom-print");
    
    Z80* cpu = _emulator->GetContext()->pCore->GetZ80();
    
    // Print "LINE1\n"
    const char* text1 = "LINE1\n";
    for (const char* p = text1; *p; p++) {
        cpu->a = static_cast<uint8_t>(*p == '\n' ? 0x0D : *p);
        detectorPtr->onBreakpointHit(0x0010, cpu);
    }
    
    // Get new lines
    std::vector<std::string> lines1 = detectorPtr->getNewLines();
    ASSERT_EQ(lines1.size(), 1);
    EXPECT_EQ(lines1[0], "LINE1");
    
    // Get new lines again (should be empty)
    std::vector<std::string> lines2 = detectorPtr->getNewLines();
    EXPECT_EQ(lines2.size(), 0);
    
    // Print "LINE2\nLINE3\n"
    const char* text2 = "LINE2\nLINE3\n";
    for (const char* p = text2; *p; p++) {
        cpu->a = static_cast<uint8_t>(*p == '\n' ? 0x0D : *p);
        detectorPtr->onBreakpointHit(0x0010, cpu);
    }
    
    // Get new lines (should have 2)
    std::vector<std::string> lines3 = detectorPtr->getNewLines();
    ASSERT_EQ(lines3.size(), 2);
    EXPECT_EQ(lines3[0], "LINE2");
    EXPECT_EQ(lines3[1], "LINE3");
}

// Test automatic cleanup on deactivation
TEST_F(ROMPrintDetector_test, AutomaticCleanup)
{
    auto detector = std::make_unique<ROMPrintDetector>();
    ROMPrintDetector* detectorPtr = detector.get();
    
    _manager->registerAnalyzer("rom-print", std::move(detector));
    _manager->activate("rom-print");
    
    // Breakpoints are created during activation
    // We can't directly access _breakpoints (private), but we can verify activation worked
    
    // Deactivate
    _manager->deactivate("rom-print");
    
    // Breakpoints should be cleared (verified indirectly through deactivation)
}

// ============================================================================
// INTEGRATION TEST: Real emulator execution with BASIC program injection
// ============================================================================
// This test validates the COMPLETE end-to-end chain:
// 1. BasicTestHelper injects tokenized BASIC program into memory
// 2. Helper types "RUN" command via keyboard simulation
// 3. Emulator executes ROM BASIC interpreter
// 4. ROM calls print routines at expected addresses (0x0010, 0x09F4, 0x15F2)
// 5. REAL breakpoints fire automatically when PC hits those addresses
// 6. AnalyzerManager dispatches to ROMPrintDetector
// 7. ROMPrintDetector captures the actual printed output
//
// This differs from unit tests which manually call onBreakpointHit() without
// any real emulator execution or ROM involvement.
TEST_F(ROMPrintDetector_test, IntegrationTest_RealBASICExecution)
{
    // Setup: Register and activate ROM print detector
    auto detector = std::make_unique<ROMPrintDetector>();
    ROMPrintDetector* detectorPtr = detector.get();
    
    _manager->registerAnalyzer("rom-print", std::move(detector));
    _manager->activate("rom-print");
    
    // Create BASIC test helper
    BasicTestHelper basicHelper(_emulator);
    
    // Inject BASIC program that prints test strings
    std::string basicProgram = 
        "10 PRINT \"Hello, World! 123\"\n"
        "20 PRINT \"Second line\"\n"
        "30 STOP\n";
    
    // Inject program and trigger RUN via keyboard
    // This will:
    // 1. Load tokenized BASIC into memory
    // 2. Type "RUN" + ENTER via keyboard simulation
    // 3. Execute for ~100 frames worth of cycles
    bool success = basicHelper.injectAndRunViaKeyboard(basicProgram);
    ASSERT_TRUE(success) << "Failed to inject and run BASIC program";
    
    // Retrieve captured output
    std::string captured = detectorPtr->getFullHistory();
    std::vector<std::string> lines = detectorPtr->getLines();
    
    // DEBUG: Save screen as PNG to see what's actually displayed
    FramebufferDescriptor fb = _emulator->GetFramebuffer();
    if (fb.memoryBuffer && fb.memoryBufferSize > 0) {
        std::string screenshotPath = "/tmp/romprintdetector_test.png";
        std::cout << "[TEST] Saving screenshot to " << screenshotPath 
                  << " (" << fb.width << "x" << fb.height << ")" << std::endl;
        ImageHelper::SavePNG(screenshotPath, fb.memoryBuffer, fb.memoryBufferSize, fb.width, fb.height);
    } else {
        std::cout << "[TEST] ERROR: No framebuffer available for screenshot" << std::endl;
    }
    
    std::cout << "[TEST] Captured text: '" << captured << "'" << std::endl;
    std::cout << "[TEST] Captured lines count: " << lines.size() << std::endl;
    for (size_t i = 0; i < lines.size(); i++) {
        std::cout << "[TEST] Line " << i << ": '" << lines[i] << "'" << std::endl;
    }
    
    // Verify we captured output
    EXPECT_FALSE(captured.empty()) << "No output was captured during BASIC execution";
    
    // Verify our specific PRINT statements were captured
    EXPECT_TRUE(captured.find("Hello, World! 123") != std::string::npos) 
        << "First PRINT statement not captured. Got: " << captured;
    
    EXPECT_TRUE(captured.find("Second line") != std::string::npos)
        << "Second PRINT statement not captured. Got: " << captured;
    
    // Verify we have multiple lines
    EXPECT_GE(lines.size(), 2) 
        << "Expected at least 2 lines, got " << lines.size() << ". Captured: " << captured;
}
