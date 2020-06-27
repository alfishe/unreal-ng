#pragma once
#include "stdafx.h"

#include "emulator/cpu/cpu.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/zx/screenzx.h"

class ScreenZX_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    CPU* _cpu = nullptr;
    ScreenZXCUT* _screenzx = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};
