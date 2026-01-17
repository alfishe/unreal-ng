#include "memory_test.h"

#include "common/dumphelper.h"
#include "common/stringhelper.h"
#include <cstdlib>  // For abs(int)

/// region <SetUp / TearDown>

void Memory_Test::SetUp()
{
    _context = new EmulatorContext(LoggerLevel::LogError);
    _memory = new MemoryCUT(_context);
}

void Memory_Test::TearDown()
{
    if (_memory != nullptr)
    {
        delete _memory;
        _memory = nullptr;
    }
}

/// endregion </Setup / TearDown>

TEST_F(Memory_Test, RAMBase)
{
    uint8_t* referenceRAMBase = _memory->_memory;

    uint8_t* value = _memory->RAMBase();
    EXPECT_EQ(value, referenceRAMBase);
}

TEST_F(Memory_Test, CacheBase)
{
    uint8_t* referenceCacheBase = _memory->_memory + MAX_RAM_PAGES * PAGE_SIZE;

    uint8_t* value = _memory->CacheBase();
    EXPECT_EQ(value, referenceCacheBase);
}

TEST_F(Memory_Test, MiscBase)
{
    uint8_t* referenceMiscBase = _memory->_memory + (MAX_RAM_PAGES + MAX_CACHE_PAGES) * PAGE_SIZE;

    uint8_t* value = _memory->MiscBase();
    EXPECT_EQ(value, referenceMiscBase);
}

TEST_F(Memory_Test, ROMBase)
{
    uint8_t* referenceROMBase = _memory->_memory + (MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES) * PAGE_SIZE;

    uint8_t* value = _memory->ROMBase();
    EXPECT_EQ(value, referenceROMBase);
}


TEST_F(Memory_Test, RAMPageAddress)
{
    const uint8_t* memoryBase = _memory->_memory;
    const uint8_t* ramBase = memoryBase + RAM_OFFSET;

    // Check for valid pages
    for (int i = 0; i < MAX_RAM_PAGES; i++)
    {
        uint8_t* pageAddress = _memory->RAMPageAddress(i);

        EXPECT_EQ(pageAddress, ramBase + PAGE_SIZE * i);
    }

    // There shouldn't be any ROM pages with such index
    for (int i = MAX_RAM_PAGES; i < 0xFFFF; i++)
    {
        uint8_t* pageAddress = _memory->RAMPageAddress(i);

        EXPECT_EQ(pageAddress, nullptr);
    }
}

TEST_F(Memory_Test, ROMPageAddress)
{
    const uint8_t* memoryBase = _memory->_memory;
    const uint8_t* romBase = memoryBase + ROM_OFFSET;

    // Check for valid pages
    for (int i = 0; i < MAX_ROM_PAGES; i++)
    {
        uint8_t* pageAddress = _memory->ROMPageHostAddress(i);

        EXPECT_EQ(pageAddress, romBase + PAGE_SIZE * i);
    }

    // There shouldn't be any ROM pages with such index
    for (int i = MAX_ROM_PAGES; i < 0xFF; i++)
    {
        uint8_t* pageAddress = _memory->ROMPageHostAddress(i);

        EXPECT_EQ(pageAddress, nullptr);
    }
}

TEST_F(Memory_Test, GetRAMPageFromAddress)
{
    // Positive case with all valid addresses belong to RAM pages
    for (uint16_t ramPage = 0; ramPage < MAX_RAM_PAGES; ramPage++)
    {
        uint8_t* pagePhysicalBase = _memory->_memory + ramPage * PAGE_SIZE;

        for (uint16_t addr = 0; addr < 0x3FFF; addr++)
        {
            uint16_t detectedPage =  _memory->GetRAMPageFromAddress(pagePhysicalBase + addr);
            EXPECT_EQ(detectedPage, ramPage);
        }
    }

    // Negative cases with addresses not belong to RAM pages
    for (int offset = 1; offset > 10000; offset++)
    {
        uint8_t* addressBeforeRAM = _memory->_memory - offset;

        uint16_t detectedPage =  _memory->GetRAMPageFromAddress(addressBeforeRAM);
        EXPECT_EQ(detectedPage, MEMORY_UNMAPPABLE);
    }

    for (int offset = 0; offset > 10000; offset+= 1)
    {
        uint8_t* addressAfterRAM = _memory->RAMBase() + PAGE_SIZE * MAX_RAM_PAGES;

        uint16_t detectedPage =  _memory->GetRAMPageFromAddress(addressAfterRAM);
        EXPECT_EQ(detectedPage, MEMORY_UNMAPPABLE);
    }
}

TEST_F(Memory_Test, GetROMPageFromAddress)
{
    // Positive case with all valid addresses belong to RAM pages
    for (uint16_t romPage = 0; romPage < MAX_ROM_PAGES; romPage++)
    {
        uint8_t* pagePhysicalBase = _memory->ROMBase() + romPage * PAGE_SIZE;

        for (uint16_t addr = 0; addr < 0x3FFF; addr++)
        {
            uint16_t detectedPage =  _memory->GetROMPageFromAddress(pagePhysicalBase + addr);
            EXPECT_EQ(detectedPage, romPage);
        }
    }

    // Negative cases with addresses not belong to RAM pages
    for (int offset = 1; offset > 10000; offset++)
    {
        uint8_t* addressBeforeROM = _memory->ROMBase() - offset;

        uint16_t detectedPage =  _memory->GetROMPageFromAddress(addressBeforeROM);
        EXPECT_EQ(detectedPage, MEMORY_UNMAPPABLE);
    }

    for (int offset = 0; offset > 10000; offset+= 1)
    {
        uint8_t* addressAfterROM = _memory->ROMBase() + PAGE_SIZE * MAX_ROM_PAGES;

        uint16_t detectedPage =  _memory->GetRAMPageFromAddress(addressAfterROM);
        EXPECT_EQ(detectedPage, MEMORY_UNMAPPABLE);
    }
}

TEST_F(Memory_Test, GetPhysicalOffsetForZ80Address)
{
    // Use default 48k memory bank layout (ROM, RAM5, RAM2, RAM0)
    _memory->DefaultBanksFor48k();
    const uint8_t* rom0Page = _memory->ROMBase() + PAGE_SIZE * 0;
    const uint8_t* ram0Page = _memory->RAMBase() + PAGE_SIZE * 0;
    const uint8_t* ram2Page = _memory->RAMBase() + PAGE_SIZE * 2;
    const uint8_t* ram5Page = _memory->RAMBase() + PAGE_SIZE * 5;

    // Positive cases
    for (uint32_t i = 0; i <= 0xFFFF; i++)
    {
        uint16_t addr = static_cast<uint16_t>(i);
        uint16_t addressInPage = addr & 0b0011'1111'1111'1111;
        size_t offsetReference;

        if (i >= 0 && i < 0x4000)
        {
            uint16_t romPage = _memory->GetROMPage();
            offsetReference = (_memory->ROMBase() - _memory->RAMBase()) + PAGE_SIZE * romPage + addressInPage;
        }
        else if (i >= 0x4000 && i < 0x8000)
        {

            uint16_t ramPage = _memory->GetRAMPageForBank1();
            offsetReference = PAGE_SIZE * ramPage + addressInPage;
        }
        else if (i >= 0x8000 && i < 0xC000)
        {
            uint16_t ramPage = _memory->GetRAMPageForBank2();
            offsetReference = PAGE_SIZE * ramPage + addressInPage;
        }
        else
        {
            uint16_t ramPage = _memory->GetRAMPageForBank3();
            offsetReference = PAGE_SIZE * ramPage + addressInPage;
        }

        size_t offsetValue = _memory->GetPhysicalOffsetForZ80Address(addr);

        if (offsetValue != offsetReference)
        {
            size_t diff = abs(static_cast<long>(offsetValue - offsetReference));

            std::string message = StringHelper::Format("[#%04X]Expected offset:%X, found:%X, diff:%X", addr, offsetValue, offsetReference, diff);
            FAIL() << message;
        }
    }
}

TEST_F(Memory_Test, GetPhysicalOffsetForZ80Bank)
{

}

/// region <ROM Switching Tests>

// Test SetROMDOS correctly updates all internal state
TEST_F(Memory_Test, SetROMDOS_UpdatesAllState)
{
    // First, set up with a different ROM (48k) to ensure we're actually switching
    _memory->SetROM48k(false);
    ASSERT_TRUE(_memory->_isPage0ROM48k) << "Pre-condition: should start with 48k ROM";
    ASSERT_FALSE(_memory->_isPage0ROMDOS) << "Pre-condition: DOS flag should be false";
    
    // Write a marker byte to DOS ROM so we can verify it's actually mapped
    // The first byte of DOS ROM (TR-DOS) is typically different from SOS ROM
    uint8_t expectedDOSByte = _memory->base_dos_rom[0];
    uint8_t expectedSOSByte = _memory->base_sos_rom[0];
    
    // Act: Switch to DOS ROM
    _memory->SetROMDOS(false);  // updatePorts=false for unit test
    
    // Assert: Bank 0 read pointer points to DOS ROM
    EXPECT_EQ(_memory->_bank_read[0], _memory->base_dos_rom) 
        << "_bank_read[0] should point to base_dos_rom";
    
    // Assert: Bank mode is ROM
    EXPECT_EQ(_memory->_bank_mode[0], BANK_ROM) 
        << "_bank_mode[0] should be BANK_ROM";
    
    // Assert: isCurrentROMDOS() returns true
    EXPECT_TRUE(_memory->isCurrentROMDOS()) 
        << "isCurrentROMDOS() should return true";
    
    // Assert: Other ROM flags are false
    EXPECT_FALSE(_memory->_isPage0ROM48k) 
        << "_isPage0ROM48k should be false after SetROMDOS";
    EXPECT_FALSE(_memory->_isPage0ROM128k) 
        << "_isPage0ROM128k should be false after SetROMDOS";
    EXPECT_FALSE(_memory->_isPge0ROMService) 
        << "_isPge0ROMService should be false after SetROMDOS";
    
    // Assert: DirectReadFromZ80Memory(0x0000) returns DOS ROM byte
    uint8_t readByte = _memory->DirectReadFromZ80Memory(0x0000);
    EXPECT_EQ(readByte, expectedDOSByte) 
        << "DirectReadFromZ80Memory(0x0000) should return DOS ROM first byte";
    
    // Verify it's actually different from SOS ROM (if they differ)
    if (expectedDOSByte != expectedSOSByte)
    {
        EXPECT_NE(readByte, expectedSOSByte) 
            << "Should NOT be reading SOS ROM byte";
    }
}

// Test SetROM48k correctly updates all internal state
TEST_F(Memory_Test, SetROM48k_UpdatesAllState)
{
    // First, set up with a different ROM (DOS) to ensure we're actually switching
    _memory->SetROMDOS(false);
    ASSERT_TRUE(_memory->_isPage0ROMDOS) << "Pre-condition: should start with DOS ROM";
    ASSERT_FALSE(_memory->_isPage0ROM48k) << "Pre-condition: 48k flag should be false";
    
    // Get expected byte from 48k (SOS) ROM
    uint8_t expectedSOSByte = _memory->base_sos_rom[0];
    
    // Act: Switch to 48k ROM
    _memory->SetROM48k(false);  // updatePorts=false for unit test
    
    // Assert: Bank 0 read pointer points to SOS ROM
    EXPECT_EQ(_memory->_bank_read[0], _memory->base_sos_rom) 
        << "_bank_read[0] should point to base_sos_rom";
    
    // Assert: Bank mode is ROM
    EXPECT_EQ(_memory->_bank_mode[0], BANK_ROM) 
        << "_bank_mode[0] should be BANK_ROM";
    
    // Assert: IsCurrentROM48k() returns true
    EXPECT_TRUE(_memory->IsCurrentROM48k()) 
        << "IsCurrentROM48k() should return true";
    
    // Assert: Other ROM flags are false
    EXPECT_FALSE(_memory->_isPage0ROMDOS) 
        << "_isPage0ROMDOS should be false after SetROM48k";
    EXPECT_FALSE(_memory->_isPage0ROM128k) 
        << "_isPage0ROM128k should be false after SetROM48k";
    EXPECT_FALSE(_memory->_isPge0ROMService) 
        << "_isPge0ROMService should be false after SetROM48k";
    
    // Assert: DirectReadFromZ80Memory(0x0000) returns SOS ROM byte
    uint8_t readByte = _memory->DirectReadFromZ80Memory(0x0000);
    EXPECT_EQ(readByte, expectedSOSByte) 
        << "DirectReadFromZ80Memory(0x0000) should return 48k ROM first byte";
}

// Test SetROM128k correctly updates all internal state
TEST_F(Memory_Test, SetROM128k_UpdatesAllState)
{
    // First, set up with a different ROM (48k) to ensure we're actually switching
    _memory->SetROM48k(false);
    
    // Get expected byte from 128k ROM
    uint8_t expected128kByte = _memory->base_128_rom[0];
    
    // Act: Switch to 128k ROM
    _memory->SetROM128k(false);  // updatePorts=false for unit test
    
    // Assert: Bank 0 read pointer points to 128k ROM
    EXPECT_EQ(_memory->_bank_read[0], _memory->base_128_rom) 
        << "_bank_read[0] should point to base_128_rom";
    
    // Assert: Bank mode is ROM
    EXPECT_EQ(_memory->_bank_mode[0], BANK_ROM) 
        << "_bank_mode[0] should be BANK_ROM";
    
    // Assert: isCurrentROM128k() returns true
    EXPECT_TRUE(_memory->isCurrentROM128k()) 
        << "isCurrentROM128k() should return true";
    
    // Assert: Other ROM flags are false
    EXPECT_FALSE(_memory->_isPage0ROM48k) 
        << "_isPage0ROM48k should be false after SetROM128k";
    EXPECT_FALSE(_memory->_isPage0ROMDOS) 
        << "_isPage0ROMDOS should be false after SetROM128k";
    EXPECT_FALSE(_memory->_isPge0ROMService) 
        << "_isPge0ROMService should be false after SetROM128k";
    
    // Assert: DirectReadFromZ80Memory(0x0000) returns 128k ROM byte
    uint8_t readByte = _memory->DirectReadFromZ80Memory(0x0000);
    EXPECT_EQ(readByte, expected128kByte) 
        << "DirectReadFromZ80Memory(0x0000) should return 128k ROM first byte";
}

// Test SetROMSystem correctly updates all internal state
TEST_F(Memory_Test, SetROMSystem_UpdatesAllState)
{
    // First, set up with a different ROM (48k) to ensure we're actually switching
    _memory->SetROM48k(false);
    
    // Get expected byte from System ROM
    uint8_t expectedSysByte = _memory->base_sys_rom[0];
    
    // Act: Switch to System ROM
    _memory->SetROMSystem(false);  // updatePorts=false for unit test
    
    // Assert: Bank 0 read pointer points to System ROM
    EXPECT_EQ(_memory->_bank_read[0], _memory->base_sys_rom) 
        << "_bank_read[0] should point to base_sys_rom";
    
    // Assert: Bank mode is ROM
    EXPECT_EQ(_memory->_bank_mode[0], BANK_ROM) 
        << "_bank_mode[0] should be BANK_ROM";
    
    // Assert: Other ROM flags are false  
    EXPECT_FALSE(_memory->_isPage0ROM48k) 
        << "_isPage0ROM48k should be false after SetROMSystem";
    EXPECT_FALSE(_memory->_isPage0ROMDOS) 
        << "_isPage0ROMDOS should be false after SetROMSystem";
    EXPECT_FALSE(_memory->_isPage0ROM128k) 
        << "_isPage0ROM128k should be false after SetROMSystem";
    
    // Assert: DirectReadFromZ80Memory(0x0000) returns System ROM byte
    uint8_t readByte = _memory->DirectReadFromZ80Memory(0x0000);
    EXPECT_EQ(readByte, expectedSysByte) 
        << "DirectReadFromZ80Memory(0x0000) should return System ROM first byte";
}

// Test ROM switching round-trip: switch through all ROMs and back
TEST_F(Memory_Test, ROMSwitching_RoundTrip)
{
    // Start with 48k
    _memory->SetROM48k(false);
    EXPECT_TRUE(_memory->IsCurrentROM48k());
    
    // Switch to DOS
    _memory->SetROMDOS(false);
    EXPECT_TRUE(_memory->isCurrentROMDOS());
    EXPECT_FALSE(_memory->IsCurrentROM48k());
    
    // Switch to 128k
    _memory->SetROM128k(false);
    EXPECT_TRUE(_memory->isCurrentROM128k());
    EXPECT_FALSE(_memory->isCurrentROMDOS());
    
    // Switch to System
    _memory->SetROMSystem(false);
    EXPECT_FALSE(_memory->isCurrentROM128k());
    // Note: isCurrentROMService() may not exist, check via flag
    EXPECT_TRUE(_memory->_isPge0ROMService);
    
    // Switch back to 48k
    _memory->SetROM48k(false);
    EXPECT_TRUE(_memory->IsCurrentROM48k());
    EXPECT_FALSE(_memory->_isPge0ROMService);
}

/// endregion </ROM Switching Tests>