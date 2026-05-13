#pragma once

#include <gtest/gtest.h>

class Emulator;
class EmulatorContext;
class AnalyzerManager;
class TRDOSAnalyzer;
class BreakpointManager;
class Z80;

/// @brief Test fixture for TRDOSAnalyzer unit tests
class TRDOSAnalyzer_test : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;
    
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    AnalyzerManager* _manager = nullptr;
    TRDOSAnalyzer* _analyzer = nullptr;
    BreakpointManager* _breakpointManager = nullptr;
    Z80* _z80 = nullptr;
};
