#pragma once
#include "stdafx.h"
#include "pch.h"

#include "emulator/emulatorcontext.h"
#include "emulator/io/tape/tape.h"


class Tape_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    TapeCUT* _tape = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};