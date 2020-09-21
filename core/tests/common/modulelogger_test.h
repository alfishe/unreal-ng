#pragma once
#include "pch.h"

#include "emulator/emulatorcontext.h"

class ModuleLogger_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    ModuleLoggerCUT* _moduleLogger = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};