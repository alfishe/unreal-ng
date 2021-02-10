#include "memory_test.h"

#include "common/dumphelper.h"
#include "common/stringhelper.h"

/// region <SetUp / TearDown>

void Memory_Test::SetUp()
{
    _context = new EmulatorContext();
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
        uint8_t* pageAddress = _memory->ROMPageAddress(i);

        EXPECT_EQ(pageAddress, romBase + PAGE_SIZE * i);
    }

    // There shouldn't be any ROM pages with such index
    for (int i = MAX_ROM_PAGES; i < 0xFF; i++)
    {
        uint8_t* pageAddress = _memory->ROMPageAddress(i);

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