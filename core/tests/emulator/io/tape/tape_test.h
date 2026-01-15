#pragma once
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/tape/tape.h"
#include "pch.h"
#include "stdafx.h"

class Tape_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    TapeCUT* _tape = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};