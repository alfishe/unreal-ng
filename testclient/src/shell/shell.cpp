#include "stdafx.h"

#include "shell.h"

/// region <Constructors / Destructors>
Shell::Shell()
{

}

Shell::~Shell()
{
    Release();
};

/// endregion </Constructors / Destructors>

/// region <Initialization>

void Shell::Init()
{
    _shellEnvironment = new ShellEnv("Unreal Speccy command shell");

    /// region <Create Modules with Commands>
    ShellModule* moduleEmulator = new ShellModule(_shellEnvironment, "emulator", "main emulator commands");
    ShellCmd* cmdLoadROM = new Emulator_LoadROM_Cmd(_shellEnvironment, "loadrom", "Loads ROM from a file", "loadrom <path/to/file>");
    ShellCmd* cmdStart = new Emulator_Start_Cmd(_shellEnvironment, "start", "Starts emulator", "start");
    ShellCmd* cmdStop = new Emulator_Stop_Cmd(_shellEnvironment, "stop", "Stops emulator", "stop");
    ShellCmd* cmdPause = new Emulator_Pause_Cmd(_shellEnvironment, "pause", "Pause execution", "pause");
    ShellCmd* cmdResume = new Emulator_Resume_Cmd(_shellEnvironment, "resume", "Resume execution", "resume");
    ShellCmd* cmdReset = new Emulator_Reset_Cmd(_shellEnvironment, "reset", "Reset emulator", "reset");
    ShellCmd* cmdStat = new Emulator_Stat_Cmd(_shellEnvironment, "stat", "Statistics", "stat");

    moduleEmulator->add(cmdStart);
    moduleEmulator->add(cmdStop);
    moduleEmulator->add(cmdPause);
    moduleEmulator->add(cmdResume);
    moduleEmulator->add(cmdReset);
    moduleEmulator->add(cmdStat);
    moduleEmulator->add(cmdLoadROM);
    /// endregion </Create Modules with Commands>


    _shell = new ShellApp(_shellEnvironment, "Unreal Speccy Shell", "unreal>", moduleEmulator);
    _shell->setExitCommand(CMD_EXIT);
    _shell->setHelpCommand(CMD_HELP);
}

void Shell::Release()
{
    if (_shell != nullptr)
    {
        delete _shell;
        _shell = nullptr;
    }

    if (_shellEnvironment != nullptr)
    {
        delete _shellEnvironment;
        _shellEnvironment = nullptr;
    }
}

/// endregion </Initialization>

void Shell::Run()
{
    _shell->loop();
}