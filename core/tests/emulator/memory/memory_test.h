#pragma once
#include "stdafx.h"
#include "pch.h"

#include "emulator/memory/memory.h"

class Memory_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    MemoryCUT* _memory = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};