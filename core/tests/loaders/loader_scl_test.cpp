#include "stdafx.h"

#include "gtest/gtest.h"

#include "common/dumphelper.h"
#include "common/filehelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/trdos.h"
#include "loaders/disk/loader_scl.h"
#include "3rdparty/digestpp/digestpp.hpp"

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
    /// region <Reference data>
    const TRDFile referenceFiles[] =
    {
        { { 'E', 'Y', 'E', 'A', 'C', 'H', 'E', '2' }, 0x42, 0x0045,   69, 255,  0,  1 },
        { { 'e', 'y', 'e', 'a', 'c', 'h', 'e', '2' }, 0x31, 0x0000,    0, 255, 15, 16 },
        { { 'e', 'y', 'e', 'a', 'c', 'h', 'e', '2' }, 0x32, 0x0000,    0, 255, 14, 32 },
        { { 'e', 'y', 'e', 'a', 'c', 'h', 'e', '2' }, 0x33, 0x0000,    0, 255, 13, 48 },
        { { 'e', 'y', 'e', 'a', 'c', 'h', 'e', '2' }, 0x34, 0x0000,    0, 138, 12, 64 },
        { { 'b', 'o', 'o', 't', ' ', ' ', ' ', ' ' }, 0x42, 0x0a08, 2568,  11,  6, 73 }
    };

    const std::vector<string> referenceFileChecksums =
    {
        "bf0df0228d47e0713a3d30b3b1b6202ef42bfd8d2818d0fe4693cfd5926b17c6",
        "81da3d3e3387944cde415eb88fc25c6d2aaced39963bae2a0df77e756e983612",
        "abbc74a320f6a9a9d394e960acbc6a603abc06bfc95d6978f4a0022976121d74",
        "d0a16eb507c14876725a12e8134136a0bd0b1d06c9c01084b0a3cd6cd4f45e38",
        "f32f7b2c3e6cab2e4f3bc37fa3610646834d7e2f3964a75c0030986d199a866a",
        "738a4f7811553a23fd28f012750816280ec36c3f7e92b2d415f92732d4ed5aae"
    };

    /// endregion </Reference data>

    // Loading test image from /bin/testdata folder copied by CMake
    std::string filepath = "testdata/loaders/scl/eyeache2.scl";
    filepath = FileHelper::AbsolutePath(filepath, true);
    EXPECT_EQ(FileHelper::FileExists(filepath), true) << "File " << filepath << " does not exist";

    LoaderSCLCUT loaderSCL(_context, filepath);
    bool result = loaderSCL.loadImage();

    EXPECT_EQ(result, true) << "Unable to load SCL file";

    // Get the loaded disk image
    DiskImage* diskImage = loaderSCL.getImage();
    ASSERT_NE(diskImage, nullptr) << "No disk image loaded";

    // Check disk geometry
    EXPECT_EQ(diskImage->getCylinders(), 80) << "Unexpected number of cylinders";
    EXPECT_EQ(diskImage->getSides(), 2) << "Unexpected number of sides";

    // Verify catalog entries
    TRDCatalog* catalog = (TRDCatalog*)diskImage->getTrack(0)->getRawSector(0)->data;
    ASSERT_NE(catalog, nullptr) << "Catalog not found in sector 0";

    // Verify volume info
    TRDVolumeInfo* volumeInfo = (TRDVolumeInfo*)diskImage->getTrack(0)->getRawSector(TRD_VOLUME_SECTOR)->data;
    ASSERT_NE(volumeInfo, nullptr) << "Volume info not found in sector " << TRD_VOLUME_SECTOR;
    EXPECT_EQ(volumeInfo->trDOSSignature, TRD_SIGNATURE) << "Invalid TR-DOS signature";
    EXPECT_EQ(volumeInfo->diskType, DS_80) << "Unexpected disk type";

    // Verify file entries
    const size_t fileCount = volumeInfo->fileCount;
    for (size_t i = 0; i < fileCount; i++)
    {
        const TRDFile& file = catalog->files[i];
        const TRDFile& refFile = referenceFiles[i];

        if (std::equal(refFile.name, refFile.name + 8, file.name))
        {
            // Verify all properties match reference
            EXPECT_EQ(file.type, refFile.type) << "File " << std::string(file.name, file.name + 8) << " has incorrect type";
            EXPECT_EQ(file.params, refFile.params) << "File " << std::string(file.name, file.name + 8) << " has incorrect params";
            EXPECT_EQ(file.lengthInBytes, refFile.lengthInBytes) << "File " << std::string(file.name, file.name + 8) << " has incorrect length";
            EXPECT_EQ(file.sizeInSectors, refFile.sizeInSectors) << "File " << std::string(file.name, file.name + 8) << " has incorrect sector count";
            EXPECT_EQ(file.startTrack, refFile.startTrack) << "File " << std::string(file.name, file.name + 8) << " has incorrect start track";
            EXPECT_EQ(file.startSector, refFile.startSector) << "File " << std::string(file.name, file.name + 8) << " has incorrect start sector";
        }
    }

    // Verify files content
    for (size_t i = 0; i < fileCount; i++)
    {
        const TRDFile& file = catalog->files[i];
        const TRDFile& refFile = referenceFiles[i];
        const std::string refChecksum = referenceFileChecksums[i];

        // Verify file data integrity
        digestpp::sha256 sha256;

        // Get file location on disk image
        size_t currentTrack = file.startTrack;
        size_t currentSector = file.startSector;
        size_t sectorsRemaining = file.sizeInSectors;

        while (sectorsRemaining > 0)
        {
            DiskImage::Track* fileTrack = diskImage->getTrack(currentTrack);
            ASSERT_NE(fileTrack, nullptr) << "File track not found for track " << currentTrack;

            uint8_t* sectorData = fileTrack->getRawSector(currentSector)->data;
            ASSERT_NE(sectorData, nullptr) << "File sector data not found for track " << currentTrack << ", sector " << currentSector;

            // Add sector data to hash
            sha256.absorb(sectorData, TRD_SECTORS_SIZE_BYTES);

            currentSector++;
            if (currentSector >= TRD_SECTORS_PER_TRACK)
            {
                currentTrack++;
                currentSector = currentSector % TRD_SECTORS_PER_TRACK;
            }

            sectorsRemaining--;
        }

        // Get the final hash
        std::string fileHash = sha256.hexdigest();

        EXPECT_EQ(fileHash, refChecksum);

        // Print file info with hash
        std::cout << file.Dump() << "\n";
        std::cout << "    SHA-256: " << fileHash << "\n";
    }
}

/// endregion </Tests>

TEST_F(LoaderSCL_Test, addFile)
{
    // Create a test disk image with 80 tracks and 2 sides
    DiskImage diskImage = DiskImage(80, 2);
    LoaderTRD loaderTrd(_context, "addFile.trd");
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
    sector0Snapshot.assign(sector0Data, sector0Data + TRD_SECTORS_SIZE_BYTES);

    std::vector<uint8_t> sector8Snapshot;
    sector8Snapshot.assign(sector8Data, sector8Data + TRD_SECTORS_SIZE_BYTES);
    /// endregion </Make system sector snapshots>

    // Create test file data (1 sector)
    uint8_t testData[256];
    memset(testData, 0xAA, sizeof(testData));

    // Create a test file descriptor (SCL stripped header, without start track and sector)
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
    std::string message = DumpHelper::DumpBufferDifferences(sector0Data, sector0Snapshot.data(), TRD_SECTORS_SIZE_BYTES);
    EXPECT_NE(sector0Snapshot, std::vector<uint8_t>(sector0Data, sector0Data + TRD_SECTORS_SIZE_BYTES)) << message;

    // Verify catalog was updated
    TRDVolumeInfo* volumeInfo = (TRDVolumeInfo*)track->getSector(TRD_VOLUME_SECTOR)->data;
    EXPECT_EQ(1, volumeInfo->fileCount);
    EXPECT_EQ(TRD_FREE_SECTORS_ON_EMPTY_DISK - 1, volumeInfo->freeSectorCount);
    EXPECT_EQ(1, volumeInfo->firstFreeTrack);
    EXPECT_EQ(1, volumeInfo->firstFreeSector);

    // Verify file descriptor in catalog
    TRDOSDirectoryEntry* catalogEntry = (TRDOSDirectoryEntry*)track->getRawSector(0)->data;
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

    loaderTrd.setImage(&diskImage);
    loaderTrd.writeImage();
}
