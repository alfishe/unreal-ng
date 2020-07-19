#pragma once
#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/portdecoder.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"

class PortDecoder_Scorpion256_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    PortDecoder_Scorpion256* _portDecoder = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};
