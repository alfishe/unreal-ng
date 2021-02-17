#pragma once
#include "stdafx.h"
#include "pch.h"

class EmulatorContext;
class SoundChip_AY8910;

class SoundChip_AY8910_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    SoundChip_AY8910* _soundChip = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};
