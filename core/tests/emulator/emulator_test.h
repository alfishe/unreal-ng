#pragma once
#include "stdafx.h"
#include "pch.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"

class Emulator_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Core* _cpu = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;

    void DestroyEmulator();
};
