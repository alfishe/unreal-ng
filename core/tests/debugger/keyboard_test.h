#pragma once

#include <gtest/gtest.h>

// Forward declarations
class EmulatorContext;
class DebugKeyboardManager;

/// @brief Test fixture for DebugKeyboardManager unit tests
class DebugKeyboardManager_test : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;

protected:
    EmulatorContext* _context = nullptr;
    DebugKeyboardManager* _keyboardManager = nullptr;
};
