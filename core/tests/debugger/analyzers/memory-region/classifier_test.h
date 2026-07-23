#pragma once

#include <gtest/gtest.h>

// Forward declarations
class Emulator;
class EmulatorContext;
class AnalyzerManager;

/// @brief GoogleTest fixture for memory-region classifier tests
class Classifier_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    AnalyzerManager* _manager = nullptr;

    void SetUp() override;
    void TearDown() override;
};
