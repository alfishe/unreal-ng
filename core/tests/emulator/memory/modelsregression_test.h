#pragma once
#include "stdafx.h"
#include "pch.h"

#include <string>
#include <vector>

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"

/// @brief One observation of the four Z80-visible bank windows.
///
/// bank0 carries the ROM role (R128 = BASIC 128, R48 = 48K BASIC,
/// SRV = service ROM, SRV3 = the +3 special/service ROM, DOS = TR-DOS) or
/// "RAMn" when RAM bank 0 is paged at #0000. bank1..3 always name the RAM page.
struct ModelsRegressionRow
{
    std::string model;
    int step = 0;
    uint16_t port = 0;
    uint8_t value = 0;
    std::string bank0;
    std::string bank1;
    std::string bank2;
    std::string bank3;
};

/// @brief Golden bank-map regression harness for the pre-existing models
///        (48K, 128K, +3, Pentagon128, Profi).
///
/// Runs a scripted sequence of #7FFD / #1FFD / #DFFD writes through each
/// model's port decoder and dumps the resulting bank map after every write.
/// The goldens are committed in the .cpp file and were captured on a clean
/// tree before any Scorpion production change: a mismatch means a real
/// behavior change in an existing model and blocks the task that caused it
/// (testing-plan 2026-09-07-scorpion-zs256-clone 3.2 / 5).
class ModelsRegression_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Core* _core = nullptr;

    void SetUp() override;
    void TearDown() override;

    bool BuildMachine(MEM_MODEL model, uint32_t ramSizeKB, bool trdosPresent, const std::string& romLeafName);

    void WritePort(uint16_t port, uint8_t value);

    /// ROM role (or "RAMn") for the page currently visible at #0000
    std::string Bank0Role(MEM_MODEL model);
    /// "RAMn" (or a ROM role, should a model ever page ROM high) for a window base
    std::string BankTarget(MEM_MODEL model, uint16_t window);

    /// Runs the scripted writes and collects one row per write (plus the
    /// initial post-reset state as step 0)
    std::vector<ModelsRegressionRow> RunSequence(const char* modelName,
                                                 MEM_MODEL model,
                                                 uint32_t ramSizeKB,
                                                 bool trdosPresent,
                                                 const std::vector<std::pair<uint16_t, uint8_t>>& writes);
};
