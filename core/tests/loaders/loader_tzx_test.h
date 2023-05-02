#pragma once

#ifndef LOADER_TZX_TEST_H
#define LOADER_TZX_TEST_H


#include "pch.h"
#include <gtest/gtest.h>
#include "emulator/cpu/core.h"
#include "loaders/tape//loader_tzx.h"

class LoaderTZX_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Core* _cpu = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};


#endif //UNREAL_LOADER_TZX_TEST_H
