#pragma once

#include <gtest/gtest.h>
#include "loaders/snapshot/loader_z80.h"

#include "emulator/cpu/core.h"

class LoaderZ80_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Core* _cpu = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};