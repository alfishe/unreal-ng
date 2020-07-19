#pragma once
#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/portdecoder.h"
#include "emulator/ports/models/portdecoder_pentagon128.h"

class PortDecoder_Pentagon128_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    PortDecoder_Pentagon128* _portDecoder = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};
