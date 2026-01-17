#pragma once

#include "stdafx.h"
#include "pch.h"

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

class BasicEncoder_Test : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;
    
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
};
