#include "stdafx.h"

#include "moduleemulator.h"

#include "emulator/emulator.h"
#include <iostream>

extern Emulator* g_emulator;

/// region <Reset>
std::string Emulator_Reset_Cmd::run(std::vector<std::string> args)
{
    g_emulator->Reset();

    _result = "Emulator reset";

    return _result;
}
/// endregion </Reset>

/// region <Start>
std::string Emulator_Start_Cmd::run(std::vector<std::string> args)
{
    if (!g_emulator->IsRunning())
    {
        g_emulator->Start();

        _result = "Started";
    }
    else
    {
        _result = "Already running";
    }

    return _result;
}
/// endregion </Start>

/// region <Pause>
std::string Emulator_Pause_Cmd::run(std::vector<std::string> args)
{
    if (!g_emulator->IsPaused())
    {

        g_emulator->Pause();

        _result = "Paused";
    }
    else
    {
        _result = "Already on pause";
    }

    return _result;
}
/// endregion </Pause>


/// region <Resume>
std::string Emulator_Resume_Cmd::run(std::vector<std::string> args)
{
    if (g_emulator->IsPaused())
    {
        g_emulator->Resume();

        _result = "Resumed";
    }
    else
    {
        _result = "Already resumed";
    }

    return _result;
}
/// endregion </Resume>

/// region <Stop>
std::string Emulator_Stop_Cmd::run(std::vector<std::string> args)
{
    if (g_emulator->IsRunning() || g_emulator->IsPaused())
    {
        g_emulator->Stop();

        _result = "Stopped";
    }
    else
    {
        _result = "Already stopped / not started";
    }

    return _result;
}
/// region </Stop>

/// region <Stats>
std::string Emulator_Stat_Cmd::run(std::vector<std::string> args)
{
    _result = g_emulator->GetStatistics();

    return _result;
}
/// region </Stats>


/// region <LoadROM>

std::string Emulator_LoadROM_Cmd::run(std::vector<std::string> args)
{
    /// region <Debug>
    std::cout << getName() << " called with " << args.size() << " arguments" << endl;
    if (args.size())
    {
        cout << "Args: ";

        for (vector<string>::iterator it = args.begin(); it != args.end(); ++it)
        {
            cout << *it << " ";
        }
        cout << endl;
    }
    /// endregion </Debug>

    std::string filepath;
    if (args.size() > 0)
    {
        filepath = args[0];
    }

    if (g_emulator != nullptr)
    {
        if (filepath.length() > 0)
        {
            _result = "Loading ROM: " + filepath + " ...";
            g_emulator->LoadROM(filepath);
        }
        else
        {
            _result = "No ROM file specified";
        }
    }
    else
    {
        _result = "Error: Emulator was not created properly";
    }


    return _result;
}

/// endregion </LoadROM>
