#include "trdostesthelper_test.h"
#include "trdostesthelper.h"
#include "emulator/emulator.h"

void TRDOSTestHelper_Test::SetUp()
{
    _emulator = new Emulator(LoggerLevel::LogError);
    
    // Initialize emulator (loads Pentagon ROM with TR-DOS)
    if (!_emulator->Init())
    {
        delete _emulator;
        _emulator = nullptr;
    }
}

void TRDOSTestHelper_Test::TearDown()
{
    if (_emulator)
    {
        delete _emulator;
        _emulator = nullptr;
    }
}

TEST_F(TRDOSTestHelper_Test, Constructor_ValidEmulator)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    TRDOSTestHelper helper(_emulator);
    
    // Helper should be constructed successfully
    EXPECT_TRUE(true);
}

TEST_F(TRDOSTestHelper_Test, IsTRDOSActive_Initially48K)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    TRDOSTestHelper helper(_emulator);
    
    // Initially should be in 48K mode, not TR-DOS
    EXPECT_FALSE(helper.isTRDOSActive());
}

TEST_F(TRDOSTestHelper_Test, ActivateTRDOSMenu_SwitchesToTRDOS)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    TRDOSTestHelper helper(_emulator);
    
    // Activate TR-DOS menu
    bool result = helper.activateTRDOSMenu();
    
    // Should succeed
    EXPECT_TRUE(result);
    
    // TR-DOS should now be active
    EXPECT_TRUE(helper.isTRDOSActive());
}

TEST_F(TRDOSTestHelper_Test, ExecuteBasicCommand_SimplePrint)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    TRDOSTestHelper helper(_emulator);
    
    // Execute a simple PRINT command
    uint64_t cycles = helper.executeBasicCommand("PRINT \"TEST\"");
    
    // Should execute some cycles
    EXPECT_GT(cycles, 0);
}

TEST_F(TRDOSTestHelper_Test, VerifyTRDOSVariables_AfterInit)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    TRDOSTestHelper helper(_emulator);
    
    // System variables should be valid after emulator init
    EXPECT_TRUE(helper.verifyTRDOSVariables());
}

TEST_F(TRDOSTestHelper_Test, GetTRDOSError_NoError)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    TRDOSTestHelper helper(_emulator);
    
    // Initially should have no error (0xFF = no error)
    uint8_t error = helper.getTRDOSError();
    EXPECT_EQ(error, 0xFF);
}
