#pragma once

#include <gtest/gtest.h>

#include <cstdint>

// Forward declarations
class EmulatorContext;
class PortTracker;
class FeatureManager;

/// \brief GoogleTest fixture for PortTracker unit tests
class PortTracker_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    PortTracker* _tracker = nullptr;
    FeatureManager* _featureManager = nullptr;

    void SetUp() override;
    void TearDown() override;
};
