#pragma once

#include <gtest/gtest.h>
#include "loaders/snapshot/loader_sna.h"

#include "emulator/cpu/core.h"

class LoaderSNA_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Core* _cpu = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};

