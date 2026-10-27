/// @file ttdv1tests_test.cpp
/// @brief Which TTD implementation a test's machines record with (Phase 5
/// Step 4): the engine, as in the application, unless the test's file is one
/// of v1's own (ttdv1tests.h)

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/ttdv1tests.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

TEST(TtdV1Tests_Test, ATestRecordsWithTheEngine)
{
    EXPECT_FALSE(ttdtest::IsV1TestFile(__FILE__));
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    EXPECT_NE(context->pTimeTravelController, nullptr);
    EXPECT_EQ(static_cast<void*>(context->pTimeTravelHooks), static_cast<void*>(context->pTimeTravelController))
        << "the core calls the engine's controller";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(TtdV1Tests_Test, TheListNamesFilesBelowTheTestsFolder)
{
    EXPECT_TRUE(ttdtest::IsV1TestFile("/any/checkout/core/tests/debugger/ttd/ttdmanager_test.cpp"));
    EXPECT_TRUE(ttdtest::IsV1TestFile("C:\\checkout\\core\\tests\\debugger\\ttd\\ttdmanager_test.cpp"));
    EXPECT_FALSE(ttdtest::IsV1TestFile("/any/checkout/core/tests/debugger/ttd/other_ttdmanager_test.cpp"));
    EXPECT_FALSE(ttdtest::IsV1TestFile(nullptr));
}
