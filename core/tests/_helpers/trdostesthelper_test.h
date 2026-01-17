#pragma once

#include "stdafx.h"
#include "pch.h"

#include "emulator/emulator.h"

class TRDOSTestHelper_Test : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;
    
    Emulator* _emulator = nullptr;
};
