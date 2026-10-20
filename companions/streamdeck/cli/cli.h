#pragma once
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "sdb/device.h"

namespace cli
{

struct Args
{
    std::vector<std::string> pos;
    std::map<std::string, std::string> opt;  // --key value | --flag (value "1")
    [[nodiscard]] bool Has(const std::string& k) const
    {
        return opt.count(k) != 0;
    }
    [[nodiscard]] std::string Str(const std::string& k, std::string d = {}) const
    {
        auto i = opt.find(k);
        return i == opt.end() ? d : i->second;
    }
    [[nodiscard]] int Num(const std::string& k, int d) const
    {
        auto i = opt.find(k);
        return i == opt.end() ? d : std::atoi(i->second.c_str());
    }
    [[nodiscard]] double Dbl(const std::string& k, double d) const
    {
        auto i = opt.find(k);
        return i == opt.end() ? d : std::atof(i->second.c_str());
    }
};

inline Args Parse(int argc, char** argv, int from)
{
    Args a;
    for (int i = from; i < argc; ++i)
    {
        std::string s = argv[i];
        if (s.rfind("--", 0) == 0)
        {
            auto key = s.substr(2);
            if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0)
                a.opt[key] = argv[++i];
            else
                a.opt[key] = "1";
        }
        else
            a.pos.push_back(s);
    }
    return a;
}

// Opens the device selected with --device <serial-substring> (and --emulate <model>); prints the error, null on
// failure.
std::unique_ptr<sdb::Device> OpenOrExplain(const Args& a);
// The device to target, without opening it. With --emulate the returned Found presents the virtual model.
// --out (render only) works without hardware: a + XL is assumed. Prints and returns nullopt on errors.
std::optional<sdb::Found> FindTarget(const Args& a);

int CmdBench(const Args& a);
int CmdSetKey(const Args& a);
int CmdSetLcd(const Args& a);
int CmdLcdScene(const Args& a);
int CmdSetGrid(const Args& a);
int CmdPair(const Args& a);
int CmdHidTest(const Args& a);
int CmdRun(const Args& a);
int CmdScenario(const Args& a);

// Exit codes shared by the set-* commands.
enum Exit : int
{
    kOk = 0,
    kDeviceError = 1,
    kBadUsage = 2,
    kBadCoordinates = 3,
    kBadContent = 4,
    kNoDevice = 5
};
int CmdLcdTest(const Args& a);

}  // namespace cli
