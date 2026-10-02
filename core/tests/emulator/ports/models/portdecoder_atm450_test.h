#pragma once
#include "stdafx.h"
#include "pch.h"

#include <memory>

#include "emulator/emulator.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/ports/models/portdecoder_atm450.h"

/// Runs on a real ATM450 machine (EmulatorManager + data/configs/atm450): the #FE arms need the
/// tape, beeper and screen, and the ROM checks need the real data/rom/atm1.rom page order
class PortDecoder_ATM450_Test : public ::testing::Test
{
protected:
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
    PortDecoder_ATM450* _portDecoder = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};
