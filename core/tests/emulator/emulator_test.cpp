#include "stdafx.h"
#include "pch.h"

#include "emulator_test.h"

#include "common/modulelogger.h"
#include "common/timehelper.h"
#include "emulator/emulator.h"

/// region <SetUp / TearDown>

void Emulator_Test::SetUp()
{

}

void Emulator_Test::TearDown()
{
}

/// endregion </Setup / TearDown>

/// region <Helper methods>
void Emulator_Test::DestroyEmulator()
{
    if (_cpu != nullptr)
    {
        delete _cpu;
        _cpu = nullptr;
    }

    if (_context != nullptr)
    {
        delete _context;
        _context = nullptr;
    }
}
/// endergion </Helper methods>

/// region <Emulator re-entrability tests>
TEST_F(Emulator_Test, MultiInstance)
{
    char message[256];
    constexpr int iterations = 1000;

    int successCounter = 0;
    for (int i = 0; i < iterations; i++)
    {
        Emulator* emulator = new Emulator(LoggerLevel::LogError);
        if (emulator)
        {
            if (emulator->Init())
            {
                emulator->Stop();
                emulator->Release();

                successCounter++;
            }

            delete emulator;
        }
    }

    if (successCounter != iterations)
    {
        FAIL() << "Iterations made:" << iterations << " successful: " << successCounter << std::endl;
    }
}

TEST_F(Emulator_Test, MultiInstanceRun)
{
    char message[256];
    constexpr int iterations = 5;

    int successCounter = 0;
    for (int i = 0; i < iterations; i++)
    {
        Emulator* emulator = new Emulator(LoggerLevel::LogError);
        if (emulator)
        {
            if (i == 1)
                i = i;

            if (emulator->Init())
            {
                emulator->StartAsync();
                sleep_ms(100);

                //emulator->RunNCPUCycles(3 * 1000 * 1000);


                emulator->Stop();
                emulator->Release();

                successCounter++;
            }

            delete emulator;
        }
    }

    if (successCounter != iterations)
    {
        FAIL() << "Iterations made:" << iterations << " successful: " << successCounter << std::endl;
    }
}
/// endregion </Emulator re-entrability tests>

