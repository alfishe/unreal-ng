#pragma once

#include <gtest/gtest.h>
#include "loaders/snapshot/loader_z80.h"

#include "emulator/cpu/core.h"

// LoaderZ80CUT is defined in loader_z80.h under _CODE_UNDER_TEST

class LoaderZ80_Fuzzing_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Core* _cpu = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;

    // Helper to create random data files for fuzzing
    void createRandomFile(const std::string& path, size_t size);
    void createCorruptedFile(const std::string& path, const std::string& sourceFile, size_t corruptionCount);
};
