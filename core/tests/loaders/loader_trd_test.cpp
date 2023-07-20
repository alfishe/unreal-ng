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
TEST_F(LoaderTRD_Test, Load)
{
    std::string filepath = "../../../tests/loaders/trd/EyeAche.trd";
    filepath = FileHelper::AbsolutePath(filepath);
    LoaderTRDCUT loaderTrd(_context, filepath);
    bool result = loaderTrd.loadImage();

    EXPECT_EQ(result, true) << StringHelper::Format("File '%s' was not loaded", filepath.c_str()) << std::endl;
    EXPECT_EQ(loaderTrd._diskImage->getLoaded(), true);
}
/// endregion </Tests>
