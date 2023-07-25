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
    std::string filepath = "../../../tests/loaders/trd/EyeAche.trd";
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
    std::string filepath = "../../../tests/loaders/trd/EyeAche.trd";
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

/// endregion </Tests>
