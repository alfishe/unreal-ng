// unreal-streamdeck: the Stream Deck companion of unreal-ng. Runs a scenario on the attached deck (menus, actions,
// the strip), pairs with an emulator, checks and converts scenario files.
// Design: docs/inprogress/2026-10-19-streamdeck-integration/daemon.md
#include <cstdio>
#include <exception>
#include <string>

#include "cli.h"

namespace
{

// to stdout; exit code 0 when asked for (--help: the package smoke tests), kBadUsage otherwise
int Usage(int code = cli::kBadUsage)
{
    std::puts(
        "unreal-streamdeck run [scenario: .json .yaml .csv .tsv] [--watch] [--api PORT] [--wait] [--dry-run]\n"
        "                  [--seconds N] [--device S|emu[:host:port]] [--mirror [host:port] | --no-mirror]\n"
        "                  [--emulate MODEL] [--viewers [PORT] | --no-viewers] [--with-parent]\n"
        "unreal-streamdeck pair [--webapi URL] [--name NAME] [--file F]   (pair as an unreal-ng companion)\n"
        "unreal-streamdeck scenario check <file> | convert <in> <out> | extract <in> <node> <out> | resolve <in> "
        "<out>");
    return code;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
        return Usage();

    const std::string cmd = argv[1];
    if (cmd == "--help" || cmd == "-h" || cmd == "help")
        return Usage(0);
    const auto args = cli::Parse(argc, argv, 1);
    try
    {
        if (cmd == "run")
            return cli::CmdRun(args);
        if (cmd == "pair")
            return cli::CmdPair(args);
        if (cmd == "scenario")
            return cli::CmdScenario(args);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "error: %s\n", e.what());
        return cli::kDeviceError;
    }
    return Usage();
}
