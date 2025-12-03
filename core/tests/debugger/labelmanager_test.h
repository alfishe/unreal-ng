#pragma once

#include <gtest/gtest.h>
#include <string>
#include <sstream>
#include "debugger/labels/labelmanager.h"

class LabelManager_test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    LabelManager* _labelManager = nullptr;
    std::stringstream _testMapFile;
    std::stringstream _testSymFile;

protected:
    void SetUp() override;
    void TearDown() override;
    
    // Helper methods
    void CreateTestMapFile();
    void CreateTestSymFile();
};
