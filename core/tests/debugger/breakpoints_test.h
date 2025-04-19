#pragma once
#include "pch.h"

#include "debugger/breakpoints/breakpointmanager.h"

class EmulatorContext;

class BreakpointManager_test : public ::testing::Test
{
protected:
    EmulatorContext* _context;
    BreakpointManagerCUT* _brkManager;
protected:
    void SetUp() override;
    void TearDown() override;
};


