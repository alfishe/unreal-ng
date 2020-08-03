#pragma once
#include "stdafx.h"

#include "3rdparty/ownshell/ownshell.h"

// All module headers should be registered here
#include "shell/commands/moduleemulator.h"

using namespace ownshell;

class Shell
{
    /// region <Constants>
protected:
    const char* CMD_EXIT = "exit";
    const char* CMD_HELP = "help";
    /// endregion </Constants>

    /// region <Fields>
    ShellApp* _shell = nullptr;
    ShellEnv* _shellEnvironment = nullptr;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    Shell();
    virtual ~Shell();

    /// endregion </Constructors / Destructors>

    /// region <Initialization>
public:
    void Init();
    void Release();
    /// endregion </Initialization>

public:
    void Run();
};
