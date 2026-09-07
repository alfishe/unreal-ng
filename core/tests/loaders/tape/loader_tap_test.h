#pragma once

#ifndef LOADER_TAP_TEST_H
#define LOADER_TAP_TEST_H

#include "pch.h"
#include <gtest/gtest.h>
#include "emulator/cpu/core.h"
#include "loaders/tape//loader_tap.h"

class LoaderTAP_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Core* _cpu = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};


#endif // LOADER_TAP_TEST_H
