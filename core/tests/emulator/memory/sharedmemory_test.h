#pragma once
#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/memory/memory.h"
#include "pch.h"
#include "stdafx.h"

/// @brief Integration tests for the sharedmemory feature toggle.
/// These tests verify that shared memory can be enabled/disabled at runtime
/// and that memory content is preserved during transitions.
class SharedMemory_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;

    /// @brief Helper to get the Memory instance from the emulator
    Memory* GetMemory();

    /// @brief Helper to get the FeatureManager from the emulator
    FeatureManager* GetFeatureManager();

    /// @brief Helper to check if shared memory is currently active
    bool IsSharedMemoryActive();

    /// @brief Helper to write a test pattern to memory
    void WriteTestPattern(uint8_t* base, size_t size, uint8_t seed);

    /// @brief Helper to verify a test pattern in memory
    bool VerifyTestPattern(const uint8_t* base, size_t size, uint8_t seed);
};
