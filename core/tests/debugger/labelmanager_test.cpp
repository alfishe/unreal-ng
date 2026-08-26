#include "labelmanager_test.h"

#include <filesystem>
#include <fstream>

#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "debugger/labels/labelmanager.h"
#include "emulator/emulatorcontext.h"
#include "pch.h"

void LabelManager_test::SetUp()
{
    _context = new EmulatorContext(LoggerLevel::LogError);
    _labelManager = new LabelManager(_context);

    // Create test files in memory
    CreateTestMapFile();
    CreateTestSymFile();
}

void LabelManager_test::TearDown()
{
    if (_labelManager)
    {
        delete _labelManager;
        _labelManager = nullptr;
    }

    if (_context)
    {
        delete _context;
        _context = nullptr;
    }
}

void LabelManager_test::CreateTestMapFile()
{
    _testMapFile << "; Test map file\n";
    _testMapFile << "; Address   Label\n\n";
    _testMapFile << "0031       NODSK\n";
    _testMapFile << "6D91       ERRL\n";
    _testMapFile << "A250       RD_SEC\n";
    _testMapFile << "A255       READLP\n";
    _testMapFile << "A258       READLP1\n";
    _testMapFile << "A25B       RERDTR\n";
    _testMapFile << "A287       GOODRD\n";
    _testMapFile << "A294       NOSRDT\n";
    _testMapFile << "A29D       RD_ZF\n";
    _testMapFile << "A2A4       RZFD1\n";
    _testMapFile << "A2AA       RZFDTR\n";
    _testMapFile << "A2DD       GOODZF\n";
    _testMapFile << "A2EA       ZFSRDT\n";
    _testMapFile << "A2EE       WR_SEC\n";
    _testMapFile << "A2F3       WRITLP\n";
    _testMapFile << "A2F6       WRITLP1\n";
    _testMapFile << "A2F9       REWRTR\n";
    _testMapFile << "A32C       GOODWR\n";
    _testMapFile << "A339       NOSWRT\n";
    _testMapFile << "A33D       RES_VG\n";
    _testMapFile << "A34E       BRKCHK\n";
    _testMapFile << "A35E       GOODBR\n";
    _testMapFile << "A360       BADBR\n";
    _testMapFile << "A364       POSIT\n";
    _testMapFile << "A37C       LOGPOS\n";
    _testMapFile << "A389       STRMOT\n";
    _testMapFile << "A3B4       RELOG\n";
    _testMapFile << "A3BA       RELL\n";
    _testMapFile << "A3D3       TRKOK\n";
    _testMapFile << "A3E1       FREED\n";
    _testMapFile << "A3E6       LD_INF\n";
    _testMapFile << "A404       BADPR\n";
    _testMapFile << "A40A       RESET\n";
    _testMapFile << "A419       SORSET\n";
    _testMapFile << "A42B       WAITFF\n";
    _testMapFile << "A434       RD_DATA\n";
    _testMapFile << "A43C       WR_DATA\n";
    _testMapFile << "A444       CALCFF\n";
    _testMapFile << "A451       TOSYS\n";
    _testMapFile << "A45A       TOFF\n";
    _testMapFile << "A45E       TO7F\n";
    _testMapFile << "A462       TO3F\n";
    _testMapFile << "A466       TO5F\n";
    _testMapFile << "A46A       TO1F\n";
    _testMapFile << "A46C       TOPORT\n";
    _testMapFile << "A470       TODOS\n";
    _testMapFile << "A475       var_DRIVE\n";
    _testMapFile << "A476       var_LTRK\n";
    _testMapFile << "A477       FROM1F\n";
    _testMapFile << "A490       SYSRET\n";
    _testMapFile << "A491       LASTSP\n";
    _testMapFile << "A499       NODSK_0\n";
    _testMapFile << "A4A3       var_SCAN_B\n";
    _testMapFile << "A4A9       var_WRP\n";
    _testMapFile << "A4AA       var_LASTDR\n";
}

void LabelManager_test::CreateTestSymFile()
{
    _testSymFile << "; Test symbol file\n";
    _testSymFile << "; Format: ADDR NAME\n\n";
    _testSymFile << "1000  START\n";
    _testSymFile << "1003  MAIN_LOOP\n";
    _testSymFile << "1010  PROCESS_DATA\n";
    _testSymFile << "1020  DATA_BUFFER\n";
    _testSymFile << "1030  VAR_COUNTER\n";
    _testSymFile << "1032  VAR_STATUS\n";
    _testSymFile << "1033  VAR_FLAGS\n";
    _testSymFile << "2000  INIT_ROUTINE\n";
    _testSymFile << "2010  CLEAR_MEMORY\n";
    _testSymFile << "2020  COPY_DATA\n";
    _testSymFile << "2030  VERIFY_DATA\n";
    _testSymFile << "2040  EXIT_ROUTINE\n";
}

TEST_F(LabelManager_test, AddAndGetLabel)
{
    // Test adding a single label
    _labelManager->AddLabel("TEST_LABEL", 0x1234, 0x00, 0x5678, "code", "module1", "Test label");

    // Test getting the label by name
    auto label = _labelManager->GetLabelByName("TEST_LABEL");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->name, "TEST_LABEL");
    EXPECT_EQ(label->address, 0x1234);
    EXPECT_EQ(label->type, "code");
    EXPECT_EQ(label->module, "module1");
    EXPECT_EQ(label->comment, "Test label");

    // Test getting the label by Z80 address
    auto labelByAddr = _labelManager->GetLabelByZ80Address(0x1234);
    ASSERT_NE(labelByAddr, nullptr);
    EXPECT_EQ(labelByAddr->name, "TEST_LABEL");

    // Test getting the label by bank and bank offset
    // Note: We can't directly look up by physical address, so we test the bank and offset
    // that were stored in the label
    EXPECT_EQ(label->bank, 0x00);
    EXPECT_EQ(label->bankOffset, 0x5678);
}

TEST_F(LabelManager_test, RemoveLabel)
{
    // Add a test label
    _labelManager->AddLabel("TEST_LABEL", 0x1234, 0x00, 0x1234);

    // Verify it exists
    ASSERT_NE(_labelManager->GetLabelByName("TEST_LABEL"), nullptr);

    // Remove the label
    bool result = _labelManager->RemoveLabel("TEST_LABEL");
    EXPECT_TRUE(result);

    // Verify it's gone
    EXPECT_EQ(_labelManager->GetLabelByName("TEST_LABEL"), nullptr);
    EXPECT_EQ(_labelManager->GetLabelByZ80Address(0x1234), nullptr);

    // Test removing non-existent label
    result = _labelManager->RemoveLabel("NON_EXISTENT");
    EXPECT_FALSE(result);
}

TEST_F(LabelManager_test, ClearAllLabels)
{
    // Add some test labels
    _labelManager->AddLabel("LABEL1", 0x1000, 0x00, 0x1000);
    _labelManager->AddLabel("LABEL2", 0x2000, 0x00, 0x2000);
    _labelManager->AddLabel("LABEL3", 0x3000, 0x00, 0x3000);

    // Verify they exist
    EXPECT_EQ(_labelManager->GetLabelCount(), 3);

    // Clear all labels
    _labelManager->ClearAllLabels();

    // Verify all labels are gone
    EXPECT_EQ(_labelManager->GetLabelCount(), 0);
    EXPECT_EQ(_labelManager->GetLabelByName("LABEL1"), nullptr);
    EXPECT_EQ(_labelManager->GetLabelByZ80Address(0x1000), nullptr);
}

TEST_F(LabelManager_test, ParseMapFile)
{
    // Save test map file to disk
    std::string tempFilePath = (std::filesystem::temp_directory_path() / "test_map_file.map").string();
    {
        std::ofstream outFile(tempFilePath);
        outFile << _testMapFile.str();
    }

    // Load the map file
    bool result = _labelManager->LoadMapFile(tempFilePath);
    EXPECT_TRUE(result);

    // Test some known labels
    auto label = _labelManager->GetLabelByName("NODSK");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->address, 0x0031);

    label = _labelManager->GetLabelByName("RD_SEC");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->address, 0xA250);

    label = _labelManager->GetLabelByName("WR_SEC");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->address, 0xA2EE);

    // Clean up
    std::filesystem::remove(tempFilePath);
}

TEST_F(LabelManager_test, ParseSymFile)
{
    // Save test sym file to disk
    std::string tempFilePath = (std::filesystem::temp_directory_path() / "test_sym_file.sym").string();
    {
        std::ofstream outFile(tempFilePath);
        outFile << _testSymFile.str();
    }

    // Load the sym file
    bool result = _labelManager->LoadSymFile(tempFilePath);
    EXPECT_TRUE(result);

    // Test some known labels
    auto label = _labelManager->GetLabelByName("START");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->address, 0x1000);

    label = _labelManager->GetLabelByName("MAIN_LOOP");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->address, 0x1003);

    label = _labelManager->GetLabelByName("INIT_ROUTINE");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->address, 0x2000);

    // Clean up
    std::filesystem::remove(tempFilePath);
}

TEST_F(LabelManager_test, AutoDetectFileFormat)
{
    // Test with .map extension
    std::string mapFilePath = (std::filesystem::temp_directory_path() / "test_file.map").string();
    {
        std::ofstream outFile(mapFilePath);
        outFile << _testMapFile.str();
    }

    bool result = _labelManager->LoadLabels(mapFilePath);
    EXPECT_TRUE(result);
    EXPECT_NE(_labelManager->GetLabelByName("NODSK"), nullptr);

    // Clean up
    std::filesystem::remove(mapFilePath);
    _labelManager->ClearAllLabels();

    // Test with .sym extension
    std::string symFilePath = (std::filesystem::temp_directory_path() / "test_file.sym").string();
    {
        std::ofstream outFile(symFilePath);
        outFile << _testSymFile.str();
    }

    result = _labelManager->LoadLabels(symFilePath);
    EXPECT_TRUE(result);
    EXPECT_NE(_labelManager->GetLabelByName("START"), nullptr);

    // Clean up
    std::filesystem::remove(symFilePath);
}

TEST_F(LabelManager_test, SaveLabels)
{
    // Add some test labels
    _labelManager->AddLabel("LABEL1", 0x1000, 0x00, 0x1000, "code", "module1", "Test label 1");
    _labelManager->AddLabel("LABEL2", 0x2000, 0x00, 0x2000, "data", "module1", "Test label 2");
    _labelManager->AddLabel("LABEL3", 0x3000, 0x00, 0x3000, "bss", "module2", "Test label 3");

    // Save to a file
    std::string tempFilePath = (std::filesystem::temp_directory_path() / "saved_labels.sym").string();
    bool result = _labelManager->SaveLabels(tempFilePath);
    EXPECT_TRUE(result);

    // Clear current labels
    _labelManager->ClearAllLabels();
    EXPECT_EQ(_labelManager->GetLabelCount(), 0);

    // Load them back
    result = _labelManager->LoadLabels(tempFilePath);
    EXPECT_TRUE(result);

    // Verify the labels were loaded correctly
    EXPECT_EQ(_labelManager->GetLabelCount(), 3);

    auto label = _labelManager->GetLabelByName("LABEL1");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->address, 0x1000);
    EXPECT_EQ(label->type, "code");

    label = _labelManager->GetLabelByName("LABEL2");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->address, 0x2000);
    EXPECT_EQ(label->type, "data");

    // Clean up
    std::filesystem::remove(tempFilePath);
}

// ============================================================================
// Filtering API Tests
// ============================================================================

TEST_F(LabelManager_test, GetAllLabelsAtAddress)
{
    // Add multiple labels at the same address (different banks)
    _labelManager->AddLabel("bank0_routine", 0xC000, 0, 0x0000, "code", "BANKS");
    _labelManager->AddLabel("bank5_routine", 0xC000, 5, 0x0000, "code", "BANKS");
    _labelManager->AddLabel("other_label", 0x8000, UINT16_MAX, UINT16_MAX, "code", "MAIN");

    auto labels = _labelManager->GetAllLabelsAtAddress(0xC000);
    EXPECT_EQ(labels.size(), 2);

    labels = _labelManager->GetAllLabelsAtAddress(0x8000);
    EXPECT_EQ(labels.size(), 1);

    labels = _labelManager->GetAllLabelsAtAddress(0x9000);
    EXPECT_EQ(labels.size(), 0);
}

TEST_F(LabelManager_test, GetLabelsByModule)
{
    _labelManager->AddLabel("main", 0x8000, UINT16_MAX, UINT16_MAX, "code", "MAIN");
    _labelManager->AddLabel("init", 0x8050, UINT16_MAX, UINT16_MAX, "code", "MAIN");
    _labelManager->AddLabel("draw_sprite", 0x9000, UINT16_MAX, UINT16_MAX, "code", "GFX");
    _labelManager->AddLabel("sprite_data", 0x9100, UINT16_MAX, UINT16_MAX, "data", "GFX");
    _labelManager->AddLabel("play_sound", 0xA000, UINT16_MAX, UINT16_MAX, "code", "SOUND");

    auto mainLabels = _labelManager->GetLabelsByModule("MAIN");
    EXPECT_EQ(mainLabels.size(), 2);

    auto gfxLabels = _labelManager->GetLabelsByModule("GFX");
    EXPECT_EQ(gfxLabels.size(), 2);

    auto soundLabels = _labelManager->GetLabelsByModule("SOUND");
    EXPECT_EQ(soundLabels.size(), 1);

    auto emptyLabels = _labelManager->GetLabelsByModule("NONEXISTENT");
    EXPECT_EQ(emptyLabels.size(), 0);
}

TEST_F(LabelManager_test, GetLabelsByBank)
{
    _labelManager->AddLabel("bank0_code", 0xC000, 0, 0x0000, "code", "BANKS");
    _labelManager->AddLabel("bank0_data", 0xC100, 0, 0x0100, "data", "BANKS");
    _labelManager->AddLabel("bank5_code", 0xC000, 5, 0x0000, "code", "BANKS");
    _labelManager->AddLabel("rom_routine", 0x0D6B, 0, 0x0D6B, "code", "ROM");
    // Set ROM type for rom_routine
    auto romLabel = _labelManager->GetLabelByName("rom_routine");
    romLabel->setBankTypeROM();

    // Get all labels in bank 0 (both RAM and ROM)
    auto bank0Labels = _labelManager->GetLabelsByBank(0);
    EXPECT_EQ(bank0Labels.size(), 3);

    // Get only RAM labels in bank 0
    auto bank0RamLabels = _labelManager->GetLabelsByBank(0, BANK_RAM);
    EXPECT_EQ(bank0RamLabels.size(), 2);

    // Get only ROM labels in bank 0
    auto bank0RomLabels = _labelManager->GetLabelsByBank(0, BANK_ROM);
    EXPECT_EQ(bank0RomLabels.size(), 1);

    auto bank5Labels = _labelManager->GetLabelsByBank(5);
    EXPECT_EQ(bank5Labels.size(), 1);
}

TEST_F(LabelManager_test, GetLabelsByType)
{
    _labelManager->AddLabel("main", 0x8000, UINT16_MAX, UINT16_MAX, "code", "MAIN");
    _labelManager->AddLabel("loop", 0x8050, UINT16_MAX, UINT16_MAX, "code", "MAIN");
    _labelManager->AddLabel("score", 0x5C00, UINT16_MAX, UINT16_MAX, "data", "VARS");
    _labelManager->AddLabel("lives", 0x5C03, UINT16_MAX, UINT16_MAX, "data", "VARS");
    _labelManager->AddLabel("MAX_LIVES", 0x5C10, UINT16_MAX, UINT16_MAX, "const", "VARS");

    auto codeLabels = _labelManager->GetLabelsByType("code");
    EXPECT_EQ(codeLabels.size(), 2);

    auto dataLabels = _labelManager->GetLabelsByType("data");
    EXPECT_EQ(dataLabels.size(), 2);

    auto constLabels = _labelManager->GetLabelsByType("const");
    EXPECT_EQ(constLabels.size(), 1);
}

TEST_F(LabelManager_test, GetLabelsInRange)
{
    _labelManager->AddLabel("early", 0x4000, UINT16_MAX, UINT16_MAX, "code", "TEST");
    _labelManager->AddLabel("start", 0x8000, UINT16_MAX, UINT16_MAX, "code", "TEST");
    _labelManager->AddLabel("middle", 0x8500, UINT16_MAX, UINT16_MAX, "code", "TEST");
    _labelManager->AddLabel("end", 0x9000, UINT16_MAX, UINT16_MAX, "code", "TEST");
    _labelManager->AddLabel("late", 0xC000, UINT16_MAX, UINT16_MAX, "code", "TEST");

    auto rangeLabels = _labelManager->GetLabelsInRange(0x8000, 0x9000);
    EXPECT_EQ(rangeLabels.size(), 3);

    rangeLabels = _labelManager->GetLabelsInRange(0x8000, 0x8000);
    EXPECT_EQ(rangeLabels.size(), 1);

    rangeLabels = _labelManager->GetLabelsInRange(0xA000, 0xB000);
    EXPECT_EQ(rangeLabels.size(), 0);
}

TEST_F(LabelManager_test, GetLabelsWithCombinedFilter)
{
    _labelManager->AddLabel("main", 0x8000, UINT16_MAX, UINT16_MAX, "code", "MAIN");
    _labelManager->AddLabel("init", 0x8050, UINT16_MAX, UINT16_MAX, "code", "MAIN");
    _labelManager->AddLabel("draw", 0x9000, UINT16_MAX, UINT16_MAX, "code", "GFX");
    _labelManager->AddLabel("sprite_data", 0x9100, UINT16_MAX, UINT16_MAX, "data", "GFX");
    _labelManager->AddLabel("inactive_label", 0x9200, UINT16_MAX, UINT16_MAX, "code", "GFX");
    auto inactive = _labelManager->GetLabelByName("inactive_label");
    inactive->active = false;

    // Filter by module
    LabelManager::LabelFilter filter;
    filter.module = "GFX";
    auto results = _labelManager->GetLabels(filter);
    EXPECT_EQ(results.size(), 3);

    // Filter by module and type
    filter.type = "code";
    results = _labelManager->GetLabels(filter);
    EXPECT_EQ(results.size(), 2);

    // Filter by module, type, and active only
    filter.activeOnly = true;
    results = _labelManager->GetLabels(filter);
    EXPECT_EQ(results.size(), 1);
    EXPECT_EQ(results[0]->name, "draw");

    // Filter by address range
    LabelManager::LabelFilter rangeFilter;
    rangeFilter.addressFrom = 0x8000;
    rangeFilter.addressTo = 0x8100;
    results = _labelManager->GetLabels(rangeFilter);
    EXPECT_EQ(results.size(), 2);
}

TEST_F(LabelManager_test, GetLabelsFilterByBank)
{
    _labelManager->AddLabel("bank0", 0xC000, 0, 0x0000, "code", "BANKS");
    _labelManager->AddLabel("bank5", 0xC000, 5, 0x0000, "code", "BANKS");
    _labelManager->AddLabel("anybank", 0x8000, UINT16_MAX, UINT16_MAX, "code", "MAIN");

    LabelManager::LabelFilter filter;
    filter.bank = 0;
    auto results = _labelManager->GetLabels(filter);
    EXPECT_EQ(results.size(), 1);
    EXPECT_EQ(results[0]->name, "bank0");

    filter.bank = 5;
    results = _labelManager->GetLabels(filter);
    EXPECT_EQ(results.size(), 1);
    EXPECT_EQ(results[0]->name, "bank5");
}
