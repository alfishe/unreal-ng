#pragma once
#include "pch.h"

class BreakpointManager;
class EmulatorContext;

class BreakpointManager_test : public ::testing::Test
{
protected:
    EmulatorContext* _context;
    BreakpointManager* _brkManager;

protected:
    void SetUp() override;
    void TearDown() override;
};


