#pragma once

#include "pch.h"

// Forward declarations
class Emulator;
class EmulatorContext;
class AnalyzerManager;

/// \brief GoogleTest fixture for AnalyzerManager tests
class AnalyzerManager_test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    AnalyzerManager* _manager = nullptr;
    
    void SetUp() override;
    void TearDown() override;
};
