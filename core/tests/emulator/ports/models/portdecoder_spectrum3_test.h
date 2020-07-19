#pragma once
#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/portdecoder.h"
#include "emulator/ports/models/portdecoder_spectrum3.h"

class PortDecoder_Spectrum3_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    PortDecoder_Spectrum3* _portDecoder = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};
