#pragma once
#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/portdecoder.h"
#include "emulator/ports/models/portdecoder_spectrum128.h"

class PortDecoder_Spectrum128_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    PortDecoder_Spectrum128* _portDecoder = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};
