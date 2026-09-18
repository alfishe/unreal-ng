#include "stdafx.h"
#include "pch.h"

#include "modelsregression_test.h"

#include <cstring>
#include <filesystem>

#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/rom.h"
#include "emulator/platform.h"
#include "emulator/ports/models/portdecoder_pentagon1024.h"
#include "emulator/ports/models/portdecoder_pentagon128.h"
#include "emulator/ports/models/portdecoder_pentagon512.h"
#include "emulator/ports/models/portdecoder_profi.h"
#include "emulator/ports/models/portdecoder_spectrum128.h"
#include "emulator/ports/models/portdecoder_spectrum3.h"
#include "emulator/ports/models/portdecoder_spectrum48.h"

/// region <Local helpers>

namespace
{
    using PortWrite = std::pair<uint16_t, uint8_t>;

    /// Logical ROM role names per hardware-reference 5.1 layout of each model's bundle
    /// (the synthetic pages carry their own tags; this only translates a page id to
    /// the role the loader assigned to it in rom.cpp)
    std::string RomRoleForPage(MEM_MODEL model, uint8_t page)
    {
        switch (model)
        {
            case MM_SPECTRUM48:
                return "R48";
            case MM_SPECTRUM128:
                return page == 0 ? "R128" : "R48";
            case MM_PLUS3:
                switch (page)
                {
                    case 0: return "R128";
                    case 1: return "SRV3";
                    case 2: return "DOS";
                    default: return "R48";
                }
            case MM_PENTAGON:
            case MM_PROFI:
                switch (page)
                {
                    case 0: return "SRV";
                    case 1: return "DOS";
                    case 2: return "R128";
                    default: return "R48";
                }
            default:
                return "ROM" + std::to_string(page);
        }
    }

    void CopyRomPath(char (&destination)[FILENAME_MAX], const std::string& path)
    {
        std::strncpy(destination, path.c_str(), sizeof(destination) - 1);
        destination[sizeof(destination) - 1] = '\0';
    }

    /// region <Scripted sequences (testing-plan 3.2)>

    std::vector<PortWrite> Sequence48K()
    {
        // No paging ports on the 48K - a single control "frame" captures the reset map
        return {};
    }

    std::vector<PortWrite> Sequence128K()
    {
        std::vector<PortWrite> writes;
        for (uint8_t bank = 0; bank <= 7; bank++)
            writes.push_back({0x7FFD, bank});
        writes.push_back({0x7FFD, 0x08});  // shadow screen
        writes.push_back({0x7FFD, 0x10});  // ROM1
        writes.push_back({0x7FFD, 0x30});  // ROM1 + paging lock
        writes.push_back({0x7FFD, 0x00});  // must be ignored after the lock
        return writes;
    }

    std::vector<PortWrite> SequencePlus3()
    {
        std::vector<PortWrite> writes;
        for (uint8_t bank = 0; bank <= 7; bank++)
        {
            writes.push_back({0x7FFD, bank});
            for (uint8_t value : {0x00, 0x01, 0x04, 0x05})
                writes.push_back({0x1FFD, value});
        }
        writes.push_back({0x7FFD, 0x08});
        writes.push_back({0x7FFD, 0x10});
        writes.push_back({0x7FFD, 0x30});
        writes.push_back({0x7FFD, 0x00});
        return writes;
    }

    std::vector<PortWrite> SequencePentagon128()
    {
        std::vector<PortWrite> writes;
        for (uint8_t bank = 0; bank <= 7; bank++)
            writes.push_back({0x7FFD, bank});
        writes.push_back({0x7FFD, 0x08});
        writes.push_back({0x7FFD, 0x10});
        // Bits 7:6 have no wiring on the 128K decoder - the writes pin that
        // they are ignored (ROM still follows bit 4 alone)
        writes.push_back({0x7FFD, 0x40});
        writes.push_back({0x7FFD, 0x80});
        writes.push_back({0x7FFD, 0xC0});
        writes.push_back({0x7FFD, 0x30});
        writes.push_back({0x7FFD, 0x00});
        return writes;
    }

    /// Same write list as SequencePentagon128: the Pentagon512 decoder reads
    /// #7FFD bits 7:6 as pb3/pb4, so identical writes diverge only in the goldens
    std::vector<PortWrite> SequencePentagon512()
    {
        std::vector<PortWrite> writes;
        for (uint8_t bank = 0; bank <= 7; bank++)
            writes.push_back({0x7FFD, bank});
        writes.push_back({0x7FFD, 0x08});
        writes.push_back({0x7FFD, 0x10});
        // Extension bits 7:6 select pb3/pb4 (RAM8/16/24) - must run before the
        // lock write to have any effect
        writes.push_back({0x7FFD, 0x40});
        writes.push_back({0x7FFD, 0x80});
        writes.push_back({0x7FFD, 0xC0});
        writes.push_back({0x7FFD, 0x30});
        writes.push_back({0x7FFD, 0x00});
        return writes;
    }

    std::vector<PortWrite> SequencePentagon1024()
    {
        std::vector<PortWrite> writes;
        for (uint8_t bank = 0; bank <= 7; bank++)
            writes.push_back({0x7FFD, bank});
        writes.push_back({0x7FFD, 0x08});
        writes.push_back({0x7FFD, 0x10});
        // Extension enabled after reset (#EFF7 bit 2 = 0): bit 5 is pb5, not
        // the paging lock - 0x20/0xE0 reach RAM32/RAM56, 0x30 does NOT lock
        writes.push_back({0x7FFD, 0x20});
        writes.push_back({0x7FFD, 0x40});
        writes.push_back({0x7FFD, 0x80});
        writes.push_back({0x7FFD, 0xE0});
        writes.push_back({0x7FFD, 0x30});
        // Disable the extension: banking falls back to 3-bit and the cached
        // #7FFD value is re-applied (RAM32 -> RAM0), bit 5 becomes the lock
        writes.push_back({0xEFF7, 0x04});
        writes.push_back({0x7FFD, 0x23});
        // Must be ignored - the lock latched with the extension off
        writes.push_back({0x7FFD, 0x00});
        return writes;
    }

    std::vector<PortWrite> SequenceProfi()
    {
        std::vector<PortWrite> writes;
        writes.push_back({0x7FFD, 0x00});
        for (uint8_t value = 0x00; value <= 0x0F; value++)
            writes.push_back({0xDFFD, value});
        writes.push_back({0x7FFD, 0x01});
        writes.push_back({0xDFFD, 0x08});
        writes.push_back({0x7FFD, 0x07});
        writes.push_back({0xDFFD, 0x0F});
        writes.push_back({0x7FFD, 0x08});
        writes.push_back({0x7FFD, 0x10});
        writes.push_back({0x7FFD, 0x30});
        writes.push_back({0x7FFD, 0x00});
        return writes;
    }

    /// endregion </Scripted sequences>
}

/// endregion </Local helpers>

/// region <SetUp / TearDown>

void ModelsRegression_Test::SetUp()
{
}

void ModelsRegression_Test::TearDown()
{
    if (_core != nullptr)
    {
        delete _core;
        _core = nullptr;
    }

    if (_context != nullptr)
    {
        delete _context;
        _context = nullptr;
    }
}

/// endregion </SetUp / TearDown>

/// region <Fixture methods>

bool ModelsRegression_Test::BuildMachine(MEM_MODEL model, uint32_t ramSizeKB, bool trdosPresent, const std::string& romLeafName)
{
    _context = new EmulatorContext(LoggerLevel::LogError);

    CONFIG& config = _context->config;
    config.mem_model = model;
    config.ramsize = ramSizeKB;
    config.trdos_present = trdosPresent;

    // Bundle sizes must match the loader validation exactly:
    // 48K requires 1 bank, 128K requires 2, the rest require 4
    const uint16_t romPages = (model == MM_SPECTRUM48) ? 1 : (model == MM_SPECTRUM128 ? 2 : 4);

    const std::string romPath = TestPathHelper::GetUniqueTestScratchPath(romLeafName);
    std::vector<uint8_t> image(static_cast<size_t>(romPages) * PAGE_SIZE);
    for (uint16_t page = 0; page < romPages; page++)
    {
        std::fill_n(image.begin() + static_cast<size_t>(page) * PAGE_SIZE,
                    PAGE_SIZE,
                    static_cast<uint8_t>(0xC0 | page));
    }
    if (!FileHelper::SaveBufferToFile(romPath, image.data(), image.size()))
        return false;

    switch (model)
    {
        case MM_SPECTRUM48:
            CopyRomPath(config.zx48_rom_path, romPath);
            break;
        case MM_SPECTRUM128:
            CopyRomPath(config.zx128_rom_path, romPath);
            break;
        case MM_PLUS3:
            CopyRomPath(config.plus3_rom_path, romPath);
            break;
        case MM_PENTAGON:
            CopyRomPath(config.pent_rom_path, romPath);
            break;
        case MM_PROFI:
            CopyRomPath(config.profi_rom_path, romPath);
            break;
        default:
            return false;
    }

    _core = new Core(_context);
    if (!_core->Init())
        return false;

    ROM rom(_context);
    if (!rom.LoadROM())
        return false;

    // Establish the model's power-on bank map
    _context->pPortDecoder->reset();

    // Self-identifying RAM pages (tags cover pages 0-63)
    Memory* memory = _context->pMemory;
    for (uint16_t page = 0; page < 64; page++)
    {
        std::memset(memory->RAMBase() + page * PAGE_SIZE, static_cast<uint8_t>(0x40 | page), PAGE_SIZE);
    }

    return true;
}

void ModelsRegression_Test::WritePort(uint16_t port, uint8_t value)
{
    _context->pPortDecoder->DecodePortOut(port, value, 0x0000);
}

std::string ModelsRegression_Test::Bank0Role(MEM_MODEL model)
{
    return BankTarget(model, 0x0000);
}

std::string ModelsRegression_Test::BankTarget(MEM_MODEL model, uint16_t window)
{
    const uint8_t tag = _context->pMemory->DirectReadFromZ80Memory(window);
    if ((tag & 0xC0) == 0x40)
        return "RAM" + std::to_string(tag & 0x3F);
    return RomRoleForPage(model, static_cast<uint8_t>(tag & 0x3F));
}

std::vector<ModelsRegressionRow> ModelsRegression_Test::RunSequence(const char* modelName,
                                                                    MEM_MODEL model,
                                                                    uint32_t ramSizeKB,
                                                                    bool trdosPresent,
                                                                    const std::vector<PortWrite>& writes)
{
    std::vector<ModelsRegressionRow> rows;
    const std::string leaf = std::string(modelName) + "-golden.rom";

    if (!BuildMachine(model, ramSizeKB, trdosPresent, leaf))
    {
        ADD_FAILURE() << "machine construction failed for model " << modelName;
        return rows;
    }

    auto capture = [&](uint16_t port, uint8_t value)
    {
        ModelsRegressionRow row;
        row.model = modelName;
        row.step = static_cast<int>(rows.size());
        row.port = port;
        row.value = value;
        row.bank0 = Bank0Role(model);
        row.bank1 = BankTarget(model, 0x4000);
        row.bank2 = BankTarget(model, 0x8000);
        row.bank3 = BankTarget(model, 0xC000);
        rows.push_back(row);
    };

    // Step 0 is the post-reset baseline before any scripted write
    capture(0x0000, 0x00);

    for (const PortWrite& write : writes)
    {
        WritePort(write.first, write.second);
        capture(write.first, write.second);
    }

    std::error_code ec;
    std::filesystem::remove(TestPathHelper::GetUniqueTestScratchPath(leaf), ec);

    return rows;
}

void ModelsRegression_Test::VerifyRowsAgainstGoldens(const char* decoderName,
                                                     const std::vector<ModelsRegressionRow>& observed,
                                                     const std::vector<ModelsRegressionRow>& goldens)
{
    ASSERT_EQ(observed.size(), goldens.size())
        << decoderName << " - the sequence and its golden table must be updated together";

    for (size_t i = 0; i < observed.size(); i++)
    {
        const ModelsRegressionRow& row = observed[i];
        const ModelsRegressionRow& golden = goldens[i];

        EXPECT_EQ(row.bank0, golden.bank0) << StringHelper::Format("model %s step %d bank0 (write %04X=%02X)",
                                                                   row.model.c_str(), row.step, row.port, row.value);
        EXPECT_EQ(row.bank1, golden.bank1) << StringHelper::Format("model %s step %d bank1 (write %04X=%02X)",
                                                                   row.model.c_str(), row.step, row.port, row.value);
        EXPECT_EQ(row.bank2, golden.bank2) << StringHelper::Format("model %s step %d bank2 (write %04X=%02X)",
                                                                   row.model.c_str(), row.step, row.port, row.value);
        EXPECT_EQ(row.bank3, golden.bank3) << StringHelper::Format("model %s step %d bank3 (write %04X=%02X)",
                                                                   row.model.c_str(), row.step, row.port, row.value);
    }
}

/// endregion </Fixture methods>

/// region <Golden bank maps>

// Goldens captured on a clean tree (2026-09-08) BEFORE any Scorpion production
// change. Editing these to "make a test pass" is forbidden: a mismatch means a
// real behavior change in that decoder (testing-plan 5, golden discipline).
//
// One table per port decoder: the factory maps (model, ramsize) to a decoder
// class and the class alone owns the #7FFD latch semantics. A single universal
// table cannot express the divergences pinned below, so each decoder keeps its
// own rows next to the test that asserts the factory really built it.
//
// Notable pinned behaviors (all pre-existing, none judged here):
//   - PortDecoder_Pentagon128 ignores #7FFD bits 7:6 (extension banks are a
//     Pentagon512+ decoder feature); the writes still flip ROM per bit 4
//   - PortDecoder_Pentagon512 honors #7FFD bits 7:6 as pb3/pb4 (RAM8/16/24)
//     while bit 5 remains the paging lock
//   - PortDecoder_Pentagon1024 honors bit 5 as pb5 while #EFF7 bit 2 = 0
//     (extension enabled); once #EFF7 bit 2 is set the decoder falls back to
//     3-bit banking and bit 5 becomes the lock again
//   - The +3 and Profi decoders apply #7FFD D4 with the ROM polarity opposite
//     to the 128K decoder (bit set -> BASIC 128)
//   - Port_1FFD is a no-op stub for both the +3 and the Scorpion decoders
static const std::vector<ModelsRegressionRow> kGoldenRows_Spectrum48 = {
    // Reset baseline only - the 48K has no paging ports
    {"48K", 0, 0x0000, 0x00, "R48", "RAM5", "RAM2", "RAM0"},
};

static const std::vector<ModelsRegressionRow> kGoldenRows_Spectrum128 = {
    // 128K
    {"128K", 0, 0x0000, 0x00, "R128", "RAM5", "RAM2", "RAM0"},
    {"128K", 1, 0x7FFD, 0x00, "R128", "RAM5", "RAM2", "RAM0"},
    {"128K", 2, 0x7FFD, 0x01, "R128", "RAM5", "RAM2", "RAM1"},
    {"128K", 3, 0x7FFD, 0x02, "R128", "RAM5", "RAM2", "RAM2"},
    {"128K", 4, 0x7FFD, 0x03, "R128", "RAM5", "RAM2", "RAM3"},
    {"128K", 5, 0x7FFD, 0x04, "R128", "RAM5", "RAM2", "RAM4"},
    {"128K", 6, 0x7FFD, 0x05, "R128", "RAM5", "RAM2", "RAM5"},
    {"128K", 7, 0x7FFD, 0x06, "R128", "RAM5", "RAM2", "RAM6"},
    {"128K", 8, 0x7FFD, 0x07, "R128", "RAM5", "RAM2", "RAM7"},
    {"128K", 9, 0x7FFD, 0x08, "R128", "RAM5", "RAM2", "RAM0"},
    {"128K", 10, 0x7FFD, 0x10, "R48", "RAM5", "RAM2", "RAM0"},
    {"128K", 11, 0x7FFD, 0x30, "R48", "RAM5", "RAM2", "RAM0"},
    {"128K", 12, 0x7FFD, 0x00, "R48", "RAM5", "RAM2", "RAM0"},
};

static const std::vector<ModelsRegressionRow> kGoldenRows_Spectrum3 = {
    // +3
    {"+3", 0, 0x0000, 0x00, "R48", "RAM5", "RAM2", "RAM0"},
    {"+3", 1, 0x7FFD, 0x00, "R48", "RAM5", "RAM2", "RAM0"},
    {"+3", 2, 0x1FFD, 0x00, "R48", "RAM5", "RAM2", "RAM0"},
    {"+3", 3, 0x1FFD, 0x01, "R48", "RAM5", "RAM2", "RAM0"},
    {"+3", 4, 0x1FFD, 0x04, "R48", "RAM5", "RAM2", "RAM0"},
    {"+3", 5, 0x1FFD, 0x05, "R48", "RAM5", "RAM2", "RAM0"},
    {"+3", 6, 0x7FFD, 0x01, "R48", "RAM5", "RAM2", "RAM1"},
    {"+3", 7, 0x1FFD, 0x00, "R48", "RAM5", "RAM2", "RAM1"},
    {"+3", 8, 0x1FFD, 0x01, "R48", "RAM5", "RAM2", "RAM1"},
    {"+3", 9, 0x1FFD, 0x04, "R48", "RAM5", "RAM2", "RAM1"},
    {"+3", 10, 0x1FFD, 0x05, "R48", "RAM5", "RAM2", "RAM1"},
    {"+3", 11, 0x7FFD, 0x02, "R48", "RAM5", "RAM2", "RAM2"},
    {"+3", 12, 0x1FFD, 0x00, "R48", "RAM5", "RAM2", "RAM2"},
    {"+3", 13, 0x1FFD, 0x01, "R48", "RAM5", "RAM2", "RAM2"},
    {"+3", 14, 0x1FFD, 0x04, "R48", "RAM5", "RAM2", "RAM2"},
    {"+3", 15, 0x1FFD, 0x05, "R48", "RAM5", "RAM2", "RAM2"},
    {"+3", 16, 0x7FFD, 0x03, "R48", "RAM5", "RAM2", "RAM3"},
    {"+3", 17, 0x1FFD, 0x00, "R48", "RAM5", "RAM2", "RAM3"},
    {"+3", 18, 0x1FFD, 0x01, "R48", "RAM5", "RAM2", "RAM3"},
    {"+3", 19, 0x1FFD, 0x04, "R48", "RAM5", "RAM2", "RAM3"},
    {"+3", 20, 0x1FFD, 0x05, "R48", "RAM5", "RAM2", "RAM3"},
    {"+3", 21, 0x7FFD, 0x04, "R48", "RAM5", "RAM2", "RAM4"},
    {"+3", 22, 0x1FFD, 0x00, "R48", "RAM5", "RAM2", "RAM4"},
    {"+3", 23, 0x1FFD, 0x01, "R48", "RAM5", "RAM2", "RAM4"},
    {"+3", 24, 0x1FFD, 0x04, "R48", "RAM5", "RAM2", "RAM4"},
    {"+3", 25, 0x1FFD, 0x05, "R48", "RAM5", "RAM2", "RAM4"},
    {"+3", 26, 0x7FFD, 0x05, "R48", "RAM5", "RAM2", "RAM5"},
    {"+3", 27, 0x1FFD, 0x00, "R48", "RAM5", "RAM2", "RAM5"},
    {"+3", 28, 0x1FFD, 0x01, "R48", "RAM5", "RAM2", "RAM5"},
    {"+3", 29, 0x1FFD, 0x04, "R48", "RAM5", "RAM2", "RAM5"},
    {"+3", 30, 0x1FFD, 0x05, "R48", "RAM5", "RAM2", "RAM5"},
    {"+3", 31, 0x7FFD, 0x06, "R48", "RAM5", "RAM2", "RAM6"},
    {"+3", 32, 0x1FFD, 0x00, "R48", "RAM5", "RAM2", "RAM6"},
    {"+3", 33, 0x1FFD, 0x01, "R48", "RAM5", "RAM2", "RAM6"},
    {"+3", 34, 0x1FFD, 0x04, "R48", "RAM5", "RAM2", "RAM6"},
    {"+3", 35, 0x1FFD, 0x05, "R48", "RAM5", "RAM2", "RAM6"},
    {"+3", 36, 0x7FFD, 0x07, "R48", "RAM5", "RAM2", "RAM7"},
    {"+3", 37, 0x1FFD, 0x00, "R48", "RAM5", "RAM2", "RAM7"},
    {"+3", 38, 0x1FFD, 0x01, "R48", "RAM5", "RAM2", "RAM7"},
    {"+3", 39, 0x1FFD, 0x04, "R48", "RAM5", "RAM2", "RAM7"},
    {"+3", 40, 0x1FFD, 0x05, "R48", "RAM5", "RAM2", "RAM7"},
    {"+3", 41, 0x7FFD, 0x08, "R48", "RAM5", "RAM2", "RAM0"},
    {"+3", 42, 0x7FFD, 0x10, "R128", "RAM5", "RAM2", "RAM0"},
    {"+3", 43, 0x7FFD, 0x30, "R128", "RAM5", "RAM2", "RAM0"},
    {"+3", 44, 0x7FFD, 0x00, "R128", "RAM5", "RAM2", "RAM0"},
};

static const std::vector<ModelsRegressionRow> kGoldenRows_Pentagon128 = {
    // Pentagon128 - #7FFD bits 7:6 unwired, bit 5 is the paging lock
    {"Pentagon128", 0, 0x0000, 0x00, "R128", "RAM5", "RAM2", "RAM0"},
    {"Pentagon128", 1, 0x7FFD, 0x00, "R128", "RAM5", "RAM2", "RAM0"},
    {"Pentagon128", 2, 0x7FFD, 0x01, "R128", "RAM5", "RAM2", "RAM1"},
    {"Pentagon128", 3, 0x7FFD, 0x02, "R128", "RAM5", "RAM2", "RAM2"},
    {"Pentagon128", 4, 0x7FFD, 0x03, "R128", "RAM5", "RAM2", "RAM3"},
    {"Pentagon128", 5, 0x7FFD, 0x04, "R128", "RAM5", "RAM2", "RAM4"},
    {"Pentagon128", 6, 0x7FFD, 0x05, "R128", "RAM5", "RAM2", "RAM5"},
    {"Pentagon128", 7, 0x7FFD, 0x06, "R128", "RAM5", "RAM2", "RAM6"},
    {"Pentagon128", 8, 0x7FFD, 0x07, "R128", "RAM5", "RAM2", "RAM7"},
    {"Pentagon128", 9, 0x7FFD, 0x08, "R128", "RAM5", "RAM2", "RAM0"},
    {"Pentagon128", 10, 0x7FFD, 0x10, "R48", "RAM5", "RAM2", "RAM0"},
    {"Pentagon128", 11, 0x7FFD, 0x40, "R128", "RAM5", "RAM2", "RAM0"},
    {"Pentagon128", 12, 0x7FFD, 0x80, "R128", "RAM5", "RAM2", "RAM0"},
    {"Pentagon128", 13, 0x7FFD, 0xC0, "R128", "RAM5", "RAM2", "RAM0"},
    {"Pentagon128", 14, 0x7FFD, 0x30, "R48", "RAM5", "RAM2", "RAM0"},
    {"Pentagon128", 15, 0x7FFD, 0x00, "R48", "RAM5", "RAM2", "RAM0"},
};

// Same write list as kGoldenRows_Pentagon128 - only bits 7:6 rows diverge
static const std::vector<ModelsRegressionRow> kGoldenRows_Pentagon512 = {
    // Pentagon512 - #7FFD bits 7:6 are pb3/pb4, bit 5 is still the paging lock
    {"Pentagon512", 0, 0x0000, 0x00, "R128", "RAM5", "RAM2", "RAM0"},
    {"Pentagon512", 1, 0x7FFD, 0x00, "R128", "RAM5", "RAM2", "RAM0"},
    {"Pentagon512", 2, 0x7FFD, 0x01, "R128", "RAM5", "RAM2", "RAM1"},
    {"Pentagon512", 3, 0x7FFD, 0x02, "R128", "RAM5", "RAM2", "RAM2"},
    {"Pentagon512", 4, 0x7FFD, 0x03, "R128", "RAM5", "RAM2", "RAM3"},
    {"Pentagon512", 5, 0x7FFD, 0x04, "R128", "RAM5", "RAM2", "RAM4"},
    {"Pentagon512", 6, 0x7FFD, 0x05, "R128", "RAM5", "RAM2", "RAM5"},
    {"Pentagon512", 7, 0x7FFD, 0x06, "R128", "RAM5", "RAM2", "RAM6"},
    {"Pentagon512", 8, 0x7FFD, 0x07, "R128", "RAM5", "RAM2", "RAM7"},
    {"Pentagon512", 9, 0x7FFD, 0x08, "R128", "RAM5", "RAM2", "RAM0"},
    {"Pentagon512", 10, 0x7FFD, 0x10, "R48", "RAM5", "RAM2", "RAM0"},
    {"Pentagon512", 11, 0x7FFD, 0x40, "R128", "RAM5", "RAM2", "RAM8"},
    {"Pentagon512", 12, 0x7FFD, 0x80, "R128", "RAM5", "RAM2", "RAM16"},
    {"Pentagon512", 13, 0x7FFD, 0xC0, "R128", "RAM5", "RAM2", "RAM24"},
    {"Pentagon512", 14, 0x7FFD, 0x30, "R48", "RAM5", "RAM2", "RAM0"},
    {"Pentagon512", 15, 0x7FFD, 0x00, "R48", "RAM5", "RAM2", "RAM0"},
};

static const std::vector<ModelsRegressionRow> kGoldenRows_Pentagon1024 = {
    // Pentagon1024 - extension enabled after reset (#EFF7 bit 2 = 0): #7FFD
    // bits [0:2] + bit 5 (pb5) + bits [6:7] (pb3/pb4) form the 6-bit page
    // index and bit 5 is NOT the lock; after #EFF7 bit 2 = 1 the decoder falls
    // back to 3-bit banking and bit 5 latches the lock again
    {"Pentagon1024", 0, 0x0000, 0x00, "R128", "RAM5", "RAM2", "RAM0"},
    {"Pentagon1024", 1, 0x7FFD, 0x00, "R128", "RAM5", "RAM2", "RAM0"},
    {"Pentagon1024", 2, 0x7FFD, 0x01, "R128", "RAM5", "RAM2", "RAM1"},
    {"Pentagon1024", 3, 0x7FFD, 0x02, "R128", "RAM5", "RAM2", "RAM2"},
    {"Pentagon1024", 4, 0x7FFD, 0x03, "R128", "RAM5", "RAM2", "RAM3"},
    {"Pentagon1024", 5, 0x7FFD, 0x04, "R128", "RAM5", "RAM2", "RAM4"},
    {"Pentagon1024", 6, 0x7FFD, 0x05, "R128", "RAM5", "RAM2", "RAM5"},
    {"Pentagon1024", 7, 0x7FFD, 0x06, "R128", "RAM5", "RAM2", "RAM6"},
    {"Pentagon1024", 8, 0x7FFD, 0x07, "R128", "RAM5", "RAM2", "RAM7"},
    {"Pentagon1024", 9, 0x7FFD, 0x08, "R128", "RAM5", "RAM2", "RAM0"},
    {"Pentagon1024", 10, 0x7FFD, 0x10, "R48", "RAM5", "RAM2", "RAM0"},
    {"Pentagon1024", 11, 0x7FFD, 0x20, "R128", "RAM5", "RAM2", "RAM32"},
    {"Pentagon1024", 12, 0x7FFD, 0x40, "R128", "RAM5", "RAM2", "RAM8"},
    {"Pentagon1024", 13, 0x7FFD, 0x80, "R128", "RAM5", "RAM2", "RAM16"},
    {"Pentagon1024", 14, 0x7FFD, 0xE0, "R128", "RAM5", "RAM2", "RAM56"},
    {"Pentagon1024", 15, 0x7FFD, 0x30, "R48", "RAM5", "RAM2", "RAM32"},
    {"Pentagon1024", 16, 0xEFF7, 0x04, "R48", "RAM5", "RAM2", "RAM0"},
    {"Pentagon1024", 17, 0x7FFD, 0x23, "R128", "RAM5", "RAM2", "RAM3"},
    {"Pentagon1024", 18, 0x7FFD, 0x00, "R128", "RAM5", "RAM2", "RAM3"},
};

static const std::vector<ModelsRegressionRow> kGoldenRows_Profi = {
    // Profi
    {"Profi", 0, 0x0000, 0x00, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 1, 0x7FFD, 0x00, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 2, 0xDFFD, 0x00, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 3, 0xDFFD, 0x01, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 4, 0xDFFD, 0x02, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 5, 0xDFFD, 0x03, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 6, 0xDFFD, 0x04, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 7, 0xDFFD, 0x05, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 8, 0xDFFD, 0x06, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 9, 0xDFFD, 0x07, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 10, 0xDFFD, 0x08, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 11, 0xDFFD, 0x09, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 12, 0xDFFD, 0x0A, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 13, 0xDFFD, 0x0B, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 14, 0xDFFD, 0x0C, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 15, 0xDFFD, 0x0D, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 16, 0xDFFD, 0x0E, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 17, 0xDFFD, 0x0F, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 18, 0x7FFD, 0x01, "R48", "RAM5", "RAM2", "RAM1"},
    {"Profi", 19, 0xDFFD, 0x08, "R48", "RAM5", "RAM2", "RAM1"},
    {"Profi", 20, 0x7FFD, 0x07, "R48", "RAM5", "RAM2", "RAM7"},
    {"Profi", 21, 0xDFFD, 0x0F, "R48", "RAM5", "RAM2", "RAM7"},
    {"Profi", 22, 0x7FFD, 0x08, "R48", "RAM5", "RAM2", "RAM0"},
    {"Profi", 23, 0x7FFD, 0x10, "R128", "RAM5", "RAM2", "RAM0"},
    {"Profi", 24, 0x7FFD, 0x30, "R128", "RAM5", "RAM2", "RAM0"},
    {"Profi", 25, 0x7FFD, 0x00, "R128", "RAM5", "RAM2", "RAM0"},
};

/// endregion </Golden bank maps>

/// region <Tests>

/// @brief Each port decoder owns its golden bank map. The factory maps
///        (model, ramsize) to a decoder class and the class alone defines the
///        #7FFD latch semantics, so every test below first asserts which
///        decoder the factory built and then pins its bank map write by write:
///        a mismatch means a real behavior change in that decoder (testing-plan
///        5, golden discipline). The Scorpion256 decoder keeps its own suite
///        (scorpionpaging_test.cpp).
TEST_F(ModelsRegression_Test, GoldenBankMap_Spectrum48)
{
    std::vector<ModelsRegressionRow> observed = RunSequence("48K", MM_SPECTRUM48, RAM_48, false, Sequence48K());

    ASSERT_NE(dynamic_cast<PortDecoder_Spectrum48*>(_context->pPortDecoder), nullptr)
        << "goldens are for PortDecoder_Spectrum48 but the factory built another decoder";

    VerifyRowsAgainstGoldens("PortDecoder_Spectrum48", observed, kGoldenRows_Spectrum48);
}

TEST_F(ModelsRegression_Test, GoldenBankMap_Spectrum128)
{
    std::vector<ModelsRegressionRow> observed = RunSequence("128K", MM_SPECTRUM128, RAM_128, false, Sequence128K());

    ASSERT_NE(dynamic_cast<PortDecoder_Spectrum128*>(_context->pPortDecoder), nullptr)
        << "goldens are for PortDecoder_Spectrum128 but the factory built another decoder";

    VerifyRowsAgainstGoldens("PortDecoder_Spectrum128", observed, kGoldenRows_Spectrum128);
}

TEST_F(ModelsRegression_Test, GoldenBankMap_Spectrum3)
{
    std::vector<ModelsRegressionRow> observed = RunSequence("+3", MM_PLUS3, RAM_128, true, SequencePlus3());

    ASSERT_NE(dynamic_cast<PortDecoder_Spectrum3*>(_context->pPortDecoder), nullptr)
        << "goldens are for PortDecoder_Spectrum3 but the factory built another decoder";

    VerifyRowsAgainstGoldens("PortDecoder_Spectrum3", observed, kGoldenRows_Spectrum3);
}

TEST_F(ModelsRegression_Test, GoldenBankMap_Pentagon128)
{
    std::vector<ModelsRegressionRow> observed = RunSequence("Pentagon128", MM_PENTAGON, RAM_128, true, SequencePentagon128());

    // Pentagon512/1024 decoders ARE-A Pentagon128 - reject the subclasses
    // whose bits 7:6 / bit 5 semantics differ from the pinned 128K rows
    PortDecoder_Pentagon128* decoder = dynamic_cast<PortDecoder_Pentagon128*>(_context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    ASSERT_EQ(dynamic_cast<PortDecoder_Pentagon512*>(decoder), nullptr)
        << "goldens are for PortDecoder_Pentagon128 but the factory built another decoder";

    VerifyRowsAgainstGoldens("PortDecoder_Pentagon128", observed, kGoldenRows_Pentagon128);
}

TEST_F(ModelsRegression_Test, GoldenBankMap_Pentagon512)
{
    std::vector<ModelsRegressionRow> observed = RunSequence("Pentagon512", MM_PENTAGON, RAM_512, true, SequencePentagon512());

    // A Pentagon512 decoder IS-A Pentagon128 - cast to the leaf class to also
    // reject the 1024 subclass, whose bit 5 semantics differ
    PortDecoder_Pentagon512* decoder = dynamic_cast<PortDecoder_Pentagon512*>(_context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    ASSERT_EQ(dynamic_cast<PortDecoder_Pentagon1024*>(decoder), nullptr)
        << "goldens are for PortDecoder_Pentagon512 but the factory built another decoder";

    VerifyRowsAgainstGoldens("PortDecoder_Pentagon512", observed, kGoldenRows_Pentagon512);
}

TEST_F(ModelsRegression_Test, GoldenBankMap_Pentagon1024)
{
    std::vector<ModelsRegressionRow> observed = RunSequence("Pentagon1024", MM_PENTAGON, RAM_1024, true, SequencePentagon1024());

    ASSERT_NE(dynamic_cast<PortDecoder_Pentagon1024*>(_context->pPortDecoder), nullptr)
        << "goldens are for PortDecoder_Pentagon1024 but the factory built another decoder";

    VerifyRowsAgainstGoldens("PortDecoder_Pentagon1024", observed, kGoldenRows_Pentagon1024);
}

TEST_F(ModelsRegression_Test, GoldenBankMap_Profi)
{
    std::vector<ModelsRegressionRow> observed = RunSequence("Profi", MM_PROFI, RAM_256, true, SequenceProfi());

    ASSERT_NE(dynamic_cast<PortDecoder_Profi*>(_context->pPortDecoder), nullptr)
        << "goldens are for PortDecoder_Profi but the factory built another decoder";

    VerifyRowsAgainstGoldens("PortDecoder_Profi", observed, kGoldenRows_Profi);
}

/// endregion </Tests>
