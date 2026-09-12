#pragma once
#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/models/scorpionfixture.h"

/// @brief Smoke tests for ScorpionMachineFixture itself - they pin the wiring
///        (Core::Init + synthetic ROM load + decoder reset + patterned RAM)
///        that every later Scorpion test builds on. Behavioral truth tables
///        start with the paging/ports tasks, not here.
class ScorpionMachine_Test : public ScorpionMachineFixture
{
};
