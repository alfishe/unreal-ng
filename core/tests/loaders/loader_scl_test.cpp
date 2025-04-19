#include <gtest/gtest.h>

#include "emulator/cpu/core.h"
#include "loaders/disk/loader_trd.h"
#include <common/dumphelper.h>
#include <common/filehelper.h>
#include <common/stringhelper.h>
#include <loaders/disk/loader_scl.h>

/// region <Types>

class LoaderSCL_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;

protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);

        // Set-up module logger only for FDC messages
        _context->pModuleLogger->TurnOffLoggingForAll();
        _context->pModuleLogger->TurnOnLoggingForModule(PlatformModulesEnum::MODULE_DISK, PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC);

        // Mock Core and Z80 to make timings work
        _core = new CoreCUT(_context);
        _z80 = new Z80(_context);
        _core->_z80 = _z80;
        _context->pCore = _core;
    }

    void TearDown() override
    {
        if (_context)
        {
            if (_context->pCore)
            {
                if (_core->_z80)
                {
                    _core->_z80 = nullptr;

                    delete _z80;
                }

                _context->pCore = nullptr;

                delete _core;
            }

            delete _context;
        }
    }
};

/// endregion </Types>

/// region <Tests>

TEST_F(LoaderSCL_Test, load)
{
    // Loading test image from /bin/testdata folder copied by CMake
    std::string filepath = "testdata/loaders/scl/eyeache2.scl";
    filepath = FileHelper::AbsolutePath(filepath, true);
    EXPECT_EQ(FileHelper::FileExists(filepath), true) << "File " << filepath << " does not exist";

    LoaderSCLCUT loaderSCL(_context, filepath);
    bool result = loaderSCL.loadImage();

    EXPECT_EQ(result, true) << "Unable to load SCL file";
}

/// endregion </Tests>

TEST_F(LoaderSCL_Test, addFile)
{
    // Create a test disk image with 80 tracks and 2 sides
    DiskImage diskImage = DiskImage(80, 2);
    LoaderTRD loaderTrd(_context, "");
    bool result = loaderTrd.format(&diskImage);
    EXPECT_EQ(result, true) << "Empty image low level format unsuccessful";

    // Initialize LoaderSCL using CUT wrapper (no load from disk)
    LoaderSCLCUT loader(_context, "test.scl");
    loader._diskImage = &diskImage;

    /// region <Make system sector snapshots>
    DiskImage::Track* track = diskImage.getTrack(0);
    uint8_t* sector0Data = track->getDataForSector(0);
    uint8_t* sector8Data = track->getDataForSector(8);
    // Save sector 0 snapshot
    std::vector<uint8_t> sector0Snapshot;
    sector0Snapshot.assign(sector0Data, sector0Data + SECTORS_SIZE_BYTES);

    std::vector<uint8_t> sector8Snapshot;
    sector8Snapshot.assign(sector8Data, sector8Data + SECTORS_SIZE_BYTES);
    /// endregion </Make system sector snapshots>

    // Create test file data (1 sector)
    uint8_t testData[256];
    memset(testData, 0xAA, sizeof(testData));

    // Create test file descriptor
    TRDOSDirectoryEntryBase fileDescriptor
    {
        {'T', 'E', 'S', 'T', 'F', 'I', 'L', 'E'},   // Name
        0x00,                                       // Type
        0x0000,                                     // Start (will be set by addFile)
        0x0100,                                     // Length (256 bytes)
        1                                           // SizeInSectors
    };

    // Add file to disk
    EXPECT_TRUE(loader.addFile(&fileDescriptor, testData)) << "Unable to add file to disk image";

    // Check that sector 0 and 8 were modified
    std::string message = DumpHelper::DumpBufferDifferences(sector0Data, sector0Snapshot.data(), SECTORS_SIZE_BYTES);
    EXPECT_NE(sector0Snapshot, std::vector<uint8_t>(sector0Data, sector0Data + SECTORS_SIZE_BYTES)) << message;
    EXPECT_NE(sector8Snapshot, std::vector<uint8_t>(sector8Data, sector8Data + SECTORS_SIZE_BYTES));

    // Verify catalog was updated
    TRDVolumeInfo* volumeInfo = (TRDVolumeInfo*)track->getSector(TRDOS_VOLUME_SECTOR)->data;
    EXPECT_EQ(1, volumeInfo->fileCount);
    EXPECT_EQ(FREE_SECTORS_ON_EMPTY_DISK - 1, volumeInfo->freeSectorCount);
    EXPECT_EQ(1, volumeInfo->firstFreeTrack);
    EXPECT_EQ(1, volumeInfo->firstFreeSector);

    // Verify file descriptor in catalog
    TRDOSDirectoryEntry* catalogEntry = (TRDOSDirectoryEntry*)track->getRawSector(1)->data;
    EXPECT_STREQ("TESTFILE", catalogEntry->Name);
    EXPECT_EQ(0, catalogEntry->Type);
    EXPECT_EQ(1, catalogEntry->StartTrack);
    EXPECT_EQ(0, catalogEntry->StartSector);
    EXPECT_EQ(256, catalogEntry->Length);
    EXPECT_EQ(1, catalogEntry->SizeInSectors);

    // Verify file data was written
    DiskImage::Track* fileDataTrack = diskImage.getTrack(catalogEntry->StartTrack);
    DiskImage::RawSectorBytes* fileDataSector = fileDataTrack->getSector(catalogEntry->StartSector);
    for (int i = 0; i < 256; i++)
    {
        EXPECT_EQ(0xAA, fileDataSector->data[i]) << "Track: " << catalogEntry->StartTrack << " Sector: " << catalogEntry->StartSector << "Offset: " << i;
    }
}
