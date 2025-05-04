#include <gtest/gtest.h>

#include <common/filehelper.h>
#include <common/stringhelper.h>
#include "emulator/cpu/core.h"
#include "loaders/disk/loader_trd.h"

/// region <Types>

class LoaderTRD_Test : public ::testing::Test
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

TEST_F(LoaderTRD_Test, DiskImage_getTrackForCylinderAndSide)
{
    constexpr const uint8_t MAX_SIDES = 2;

    DiskImage* diskImage = new DiskImage(MAX_CYLINDERS, MAX_SIDES);
    if (diskImage)
    {
        std::set<DiskImage::Track*> uniqueTrackPointers;

        for (size_t cylinder = 0; cylinder < MAX_CYLINDERS; cylinder++)
        {
            for (size_t side = 0; side < MAX_SIDES; side++)
            {
                DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);

                auto it = uniqueTrackPointers.find(track);
                if (it == uniqueTrackPointers.end())
                {
                    // No such track pointer registered, we can add
                    uniqueTrackPointers.insert(track);
                }
                else
                {
                    FAIL() << StringHelper::Format("Cylinder %d side %d pointer is not unique ", cylinder, side);
                }
            }
        }


        delete diskImage;
    }
    else
    {
        FAIL() << "Unable to create DiskImage";
    }
}

/// Test that image load basically works
TEST_F(LoaderTRD_Test, Load)
{
    // Loading test image from /bin/testdata folder copied by CMake
    std::string filepath = "testdata/loaders/trd/EyeAche.trd";
    filepath = FileHelper::AbsolutePath(filepath);
    LoaderTRDCUT loaderTrd(_context, filepath);
    bool result = loaderTrd.loadImage();

    EXPECT_EQ(result, true) << StringHelper::Format("File '%s' was not loaded", filepath.c_str()) << std::endl;
    EXPECT_EQ(loaderTrd._diskImage->getLoaded(), true);
}

/// Test that TR-DOS sector 9 (volume information) is parsed correctly
TEST_F(LoaderTRD_Test, Sector9)
{
    /// region <Load test image>

    // Loading test image from /bin/testdata folder copied by CMake
    std::string filepath = "testdata/loaders/trd/EyeAche.trd";
    filepath = FileHelper::AbsolutePath(filepath);
    LoaderTRDCUT loaderTrd(_context, filepath);
    bool result = loaderTrd.loadImage();

    EXPECT_EQ(result, true) << StringHelper::Format("File '%s' was not loaded", filepath.c_str()) << std::endl;
    EXPECT_EQ(loaderTrd._diskImage->getLoaded(), true);
    /// endregion </Load test image>

    DiskImage* diskImage = loaderTrd.getImage();
    EXPECT_NE(diskImage, nullptr);

    DiskImage::Track* track00 = diskImage->getTrackForCylinderAndSide(0, 0);
    EXPECT_NE(track00, nullptr);

    uint8_t* sector09 = track00->getDataForSector(8);
    TRDVolumeInfo* volumeInfo = (TRDVolumeInfo*)sector09;

    EXPECT_EQ(volumeInfo->trDOSSignature, TRD_SIGNATURE);
    EXPECT_EQ(volumeInfo->deletedFileCount, 0);
}



// Test validateTRDOSImage method
TEST_F(LoaderTRD_Test, validateTRDOSImage)
{
    // Loading test image from /bin/testdata folder copied by CMake
    std::string filepath = "testdata/loaders/trd/EyeAche.trd";
    filepath = FileHelper::AbsolutePath(filepath);

    /// region <Prepare test>

    // Create loader
    LoaderTRDCUT loaderTrd(_context, filepath);
    
    // Load the image
    bool loadResult = loaderTrd.loadImage();
    ASSERT_TRUE(loadResult) << "Failed to load test TRD image";
    
    // Get the disk image
    DiskImage* diskImage = loaderTrd.getImage();
    ASSERT_NE(diskImage, nullptr) << "No disk image loaded";
    
    // Validate the TRDOS image
    bool validationResult = loaderTrd.validateTRDOSImage(diskImage);
    EXPECT_TRUE(validationResult) << "Valid TRDOS image failed validation";
    
    // Additional checks on the image structure
    DiskImage::Track* track0 = diskImage->getTrackForCylinderAndSide(0, 0);
    ASSERT_NE(track0, nullptr) << "Track 0 not found";
    
    uint8_t* volumeSector = track0->getDataForSector(8); // Volume sector is sector 9 (index 8)
    ASSERT_NE(volumeSector, nullptr) << "Volume sector not found";
    
    /// endregion </Prepare test>

}

// Test validateEmptyTRDOSImage method
TEST_F(LoaderTRD_Test, validateEmptyTRDOSImage)
{
    // Test all supported TR-DOS disk formats
    struct DiskFormat
    {
        uint8_t tracks;
        uint8_t sides;
        TRDDiskType diskType;
        uint16_t expectedFreeSectors;
    };

    // Define all supported disk formats and their expected free sectors
    const std::vector<DiskFormat> formats =
    {
        { 80, 2, DS_80, TRD_FREE_SECTORS_ON_DS_80_EMPTY_DISK},          // 80 tracks, double-sided (DS)
        { 40, 2, DS_40, TRD_FREE_SECTORS_ON_DS_40_EMPTY_DISK },         // 40 tracks, double-sided (DS)
        { 80, 1, SS_80, TRD_FREE_SECTORS_ON_SS_80_EMPTY_DISK },         // 80 tracks, single-sided (SS)
        { 40, 1, SS_40, TRD_FREE_SECTORS_ON_SS_40_EMPTY_DISK }          // 40 tracks, single-sided (SS)
    };

    for (const auto& format : formats)
    {
        SCOPED_TRACE(StringHelper::Format("Testing format: %d tracks, %d sides", format.tracks, format.sides));

        // Create disk image with specified format
        DiskImage diskImage(format.tracks, format.sides);
        
        // Create a loader and format the disk
        LoaderTRDCUT loaderTrd(_context, "test.trd");
        bool formatResult = loaderTrd.format(&diskImage);
        ASSERT_TRUE(formatResult) << "Failed to format empty disk image";
        
        // Validate the empty TRDOS image
        bool validationResult = loaderTrd.validateTRDOSImage(&diskImage);
        EXPECT_TRUE(validationResult) << "Empty TRDOS image failed validation";
        
        // Additional checks on the image structure
        DiskImage::Track* track0 = diskImage.getTrackForCylinderAndSide(0, 0);
        ASSERT_NE(track0, nullptr) << "Track 0 not found";
        
        uint8_t* volumeSectorData = track0->getDataForSector(8); // Volume sector is sector 9 (index 8)
        ASSERT_NE(volumeSectorData, nullptr) << "Volume sector not found";
        TRDVolumeInfo* volumeSector = (TRDVolumeInfo*)volumeSectorData;
        
        // Check disk type in volume sector
        uint8_t diskType = volumeSector->diskType;
        EXPECT_EQ(diskType, format.diskType) << "Incorrect disk type 0x" << std::hex << (int)diskType << " (" << getTRDDiskTypeName(diskType) << "). Expected: 0x" << std::hex << (int)format.diskType << " (" << getTRDDiskTypeName(format.diskType) << ")";;

        // Check free sectors count in volume sector
        uint16_t freeSectors = volumeSector->freeSectorCount;
        EXPECT_EQ(freeSectors, format.expectedFreeSectors) << "Incorrect free sectors count in empty disk for " << getTRDDiskTypeName(format.diskType) << " disk type";

        // Check file count should be 0
        uint8_t fileCount = volumeSector->fileCount;
        EXPECT_EQ(fileCount, 0) << "Empty disk should have 0 files";

        // Check the deleted file count
        uint8_t deletedFileCount = volumeSector->deletedFileCount;
        EXPECT_EQ(deletedFileCount, 0) << "Empty disk should have 0 deleted files";
        
        // Check the first free track and sector
        uint8_t firstFreeTrack = volumeSector->firstFreeTrack;
        uint8_t firstFreeSector = volumeSector->firstFreeSector;
        EXPECT_EQ(firstFreeTrack, 1) << "Incorrect first free track";
        EXPECT_EQ(firstFreeSector, 0) << "Incorrect first free sector";
    }
}

/// Test format method for all supported TR-DOS disk types
TEST_F(LoaderTRD_Test, Format)
{
    // Test all supported TR-DOS disk formats
    struct DiskFormat
    {
        uint8_t tracks;
        uint8_t sides;
        TRDDiskType diskType;
        uint16_t expectedFreeSectors;
    };

    // Define all supported disk formats and their expected free sectors
    const std::vector<DiskFormat> formats =
    {
        { 80, 2, DS_80, TRD_FREE_SECTORS_ON_DS_80_EMPTY_DISK },  // 80 tracks, double-sided (DS)
        { 40, 2, DS_40, TRD_FREE_SECTORS_ON_DS_40_EMPTY_DISK },  // 40 tracks, double-sided (DS)
        { 80, 1, SS_80, TRD_FREE_SECTORS_ON_SS_80_EMPTY_DISK },  // 80 tracks, single-sided (SS)
        { 40, 1, SS_40, TRD_FREE_SECTORS_ON_SS_40_EMPTY_DISK }   // 40 tracks, single-sided (SS)
    };

    for (const auto& format : formats)
    {
        SCOPED_TRACE(StringHelper::Format("Testing format: %d tracks, %d sides", format.tracks, format.sides));

        // Create disk image with specified format
        DiskImage* diskImage = new DiskImage(format.tracks, format.sides);
        ASSERT_NE(diskImage, nullptr) << "Failed to create disk image";

        // Create loader
        LoaderTRDCUT loaderTrd(_context, "");

        // Format the disk
        bool formatResult = loaderTrd.format(diskImage);
        EXPECT_TRUE(formatResult) << "Format failed for disk type " << getTRDDiskTypeName(format.diskType);

        // Verify volume information
        DiskImage::Track* track0 = diskImage->getTrackForCylinderAndSide(0, 0);
        ASSERT_NE(track0, nullptr) << "Track 0 not found";

        uint8_t* volumeSector = track0->getDataForSector(8); // Volume sector is sector 9 (index 8)
        ASSERT_NE(volumeSector, nullptr) << "Volume sector not found";

        TRDVolumeInfo* volumeInfo = (TRDVolumeInfo*)volumeSector;
        ASSERT_NE(volumeInfo, nullptr) << "Volume info not found";

        // Verify volume information fields
        EXPECT_EQ(volumeInfo->trDOSSignature, TRD_SIGNATURE) << "Invalid TR-DOS signature";
        EXPECT_EQ(volumeInfo->diskType, format.diskType) << "Invalid disk type";
        EXPECT_EQ(volumeInfo->freeSectorCount, format.expectedFreeSectors) << "Invalid free sectors count";
        EXPECT_EQ(volumeInfo->firstFreeTrack, 1) << "Invalid first free track";
        EXPECT_EQ(volumeInfo->firstFreeSector, 0) << "Invalid first free sector";
        EXPECT_EQ(volumeInfo->fileCount, 0) << "Invalid file count";
        EXPECT_EQ(volumeInfo->deletedFileCount, 0) << "Invalid deleted file count";

        // Verify all sectors are formatted
        for (uint8_t cylinder = 0; cylinder < format.tracks; cylinder++)
        {
            for (uint8_t side = 0; side < format.sides; side++)
            {
                DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
                ASSERT_NE(track, nullptr) << "Track not found for cylinder " << (int)cylinder << " side " << (int)side;

                for (uint8_t sector = 0; sector < TRD_SECTORS_PER_TRACK; sector++)
                {
                    DiskImage::AddressMarkRecord* markRecord = track->getIDForSector(sector);
                    ASSERT_NE(markRecord, nullptr) << "ID record not found for sector " << (int)sector;

                    EXPECT_EQ(markRecord->cylinder, cylinder) << "Invalid cylinder number";
                    EXPECT_EQ(markRecord->head, side) << "Invalid head number";
                    EXPECT_EQ(markRecord->sector, sector + 1) << "Invalid sector number";
                    EXPECT_EQ(markRecord->sector_size, 0x01) << "Invalid sector size";
                    EXPECT_TRUE(markRecord->isCRCValid()) << "Invalid CRC for sector ID mark";
                }
            }
        }

        delete diskImage;
    }
}

/// endregion </Tests>
