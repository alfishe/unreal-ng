#pragma once
#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/portdecoder.h"
#include "emulator/ports/models/portdecoder_profi.h"

class PortDecoder_Profi_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    PortDecoder_Profi* _portDecoder = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};
