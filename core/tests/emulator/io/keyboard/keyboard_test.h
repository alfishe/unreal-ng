#pragma once
#include "stdafx.h"
#include "pch.h"

#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"

class Keyboard_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    KeyboardCUT* _keyboard = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};