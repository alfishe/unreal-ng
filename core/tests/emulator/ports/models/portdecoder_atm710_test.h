#pragma once
#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/portdecoder.h"
#include "emulator/ports/models/portdecoder_atm710.h"

class PortDecoder_ATM710_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
    PortDecoder_ATM710* _portDecoder = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};
