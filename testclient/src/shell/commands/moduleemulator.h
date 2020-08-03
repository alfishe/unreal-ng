#pragma once
#include "stdafx.h"

#include "3rdparty/ownshell/ownshell.h"
#include <cstring>

using namespace ownshell;

/// region <Reset>
class Emulator_Reset_Cmd : public ShellCmd
{
private:
    std::string _result;

public:
    Emulator_Reset_Cmd(ShellEnv* env, std::string name, std::string description, std::string result = "") : ShellCmd(env, name, description)
    {
        _result = result;
    };
    virtual  std::string run(std::vector<std::string> args);
};
/// endregion <Reset>

/// region <Start>
class Emulator_Start_Cmd : public ShellCmd
{
private:
    std::string _result;

public:
    Emulator_Start_Cmd(ShellEnv* env, std::string name, std::string description, std::string result = "") : ShellCmd(env, name, description)
    {
        _result = result;
    };
    virtual  std::string run(std::vector<std::string> args);
};
/// endregion </Start>

/// region <Pause>
class Emulator_Pause_Cmd : public ShellCmd
{
private:
    std::string _result;

public:
    Emulator_Pause_Cmd(ShellEnv* env, std::string name, std::string description, std::string result = "") : ShellCmd(env, name, description)
    {
        _result = result;
    };
    virtual  std::string run(std::vector<std::string> args);
};
/// endregion </Pause>

/// region <Resume>
class Emulator_Resume_Cmd : public ShellCmd
{
private:
    std::string _result;

public:
    Emulator_Resume_Cmd(ShellEnv* env, std::string name, std::string description, std::string result = "") : ShellCmd(env, name, description)
    {
    _result = result;
    };
    virtual  std::string run(std::vector<std::string> args);
};
/// endregion </Resume>

/// region <Stop>
class Emulator_Stop_Cmd : public ShellCmd
{
private:
    std::string _result;

public:
    Emulator_Stop_Cmd(ShellEnv* env, std::string name, std::string description, std::string result = "") : ShellCmd(env, name, description)
    {
        _result = result;
    };
    virtual  std::string run(std::vector<std::string> args);
};
/// endregion </Stop>

/// region <Stats>
class Emulator_Stat_Cmd : public ShellCmd
{
private:
    std::string _result;

public:
    Emulator_Stat_Cmd(ShellEnv* env, std::string name, std::string description, std::string result = "") : ShellCmd(env, name, description)
    {
        _result = result;
    };
    virtual  std::string run(std::vector<std::string> args);
};
/// endregion </Stats>

/// region <LoadROM>
class Emulator_LoadROM_Cmd : public ShellCmd
{
private:
    std::string _result;

public:
    Emulator_LoadROM_Cmd(ShellEnv* env, std::string name, std::string description, std::string result = "") : ShellCmd(env, name, description)
    {
        _result = result;
    };
    virtual  std::string run(std::vector<std::string> args);
};
/// endregion </LoadROM>
