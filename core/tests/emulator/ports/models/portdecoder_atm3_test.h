#pragma once
#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/portdecoder.h"
#include "emulator/ports/models/portdecoder_atm3.h"

class PortDecoder_ATM3_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
    PortDecoder_ATM3* _portDecoder = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};
