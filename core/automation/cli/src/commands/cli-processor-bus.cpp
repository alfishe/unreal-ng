/**
 * @file cli-processor-bus.cpp
 * @brief Bus analyzer command handlers for the CLI processor
 *
 * Handles the `bus` command family:
 *   bus [list]                                   every bus, its devices, trace and checks state
 *   bus <id|all> on [checks|nochecks] [protocol] [capacity=N]  trace (and checks, default on)
 *   bus <id|all> off | clear
 *   bus <id> events [kinds=] [slot=] [since=] [from=] [to=] [limit=]
 *   bus <id> findings [rules=] [since=] [limit=]
 *   bus <id> sd-timing [ideal|realistic|slow] [seed=N] [slot=N]
 *   bus <id> export <vcd|jsonl> <file> [kinds=] [slot=] [since=] [from=] [to=] [limit=]
 *
 * Design: docs/inprogress/2026-10-11-bus-analyzer/technical-design.md §7. Every surface goes through
 * DeviceState::Bus*, so all of them answer the same.
 */

#include "cli-processor.h"

#include <fstream>
#include <map>
#include <sstream>

#include "emulator/bus/bustrace.h"
#include "emulator/emulatorcontext.h"
#include "emulator/state/devicestate.h"

namespace
{
/// key=value arguments after `first`
std::map<std::string, std::string> Options(const std::vector<std::string>& args, size_t first)
{
    std::map<std::string, std::string> options;
    for (size_t i = first; i < args.size(); i++)
    {
        const size_t eq = args[i].find('=');
        if (eq != std::string::npos)
            options[args[i].substr(0, eq)] = args[i].substr(eq + 1);
    }
    return options;
}

std::string Get(const std::map<std::string, std::string>& options, const char* key)
{
    auto it = options.find(key);
    return it != options.end() ? it->second : std::string();
}
}  // namespace

void CLIProcessor::ShowBusHelp(const ClientSession& session)
{
    std::ostringstream out;
    out << "bus - peripheral bus analyzer (SPI: Z-Controller zc.spi, Next next.spi, DivMMC divmmc.spi, NeoGS neogs.*; ATA: ide0, ide1)"
        << NEWLINE
        << "  bus [list]                                  every bus: devices, trace, checks" << NEWLINE
        << "  bus <id|all> on [checks|nochecks] [protocol] [capacity=N]  record events (and run the checks, default on);" << NEWLINE
        << "                                              protocol: no single bytes / data words / status reads" << NEWLINE
        << "  bus <id|all> off | clear" << NEWLINE
        << "  bus <id> events [kinds=spi,sd,ata,...] [slot=N] [since=SEQ] [from=F] [to=F] [limit=N]" << NEWLINE
        << "  bus <id> findings [rules=...] [since=SEQ] [limit=N]" << NEWLINE
        << "  bus <id> sd-timing [ideal|realistic|slow] [seed=N] [slot=N]   SD card timing profile" << NEWLINE
        << "  bus <id> export <vcd|jsonl> <file> [kinds=...] [since=SEQ] ...  VCD for GTKWave / PulseView" << NEWLINE
        << "Kinds: select, deselect, byte, early, truncated, command, read, write, reg_read, reg_write, ata_command, data" << NEWLINE
        << "       (groups spi, sd, ata)." << NEWLINE
        << "Rules: early_access, select_mid_byte, deselect_mid_command, command_while_busy, command_drops_answer," << NEWLINE
        << "       crc_error, dma_collision, ata_data_without_drq, ata_command_drops_data, ata_write_while_busy." << NEWLINE;
    session.SendResponse(out.str());
}

void CLIProcessor::HandleBus(const ClientSession& session, const std::vector<std::string>& args)
{
    if (!args.empty() && (args[0] == "help" || args[0] == "?"))
    {
        ShowBusHelp(session);
        return;
    }
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select' or 'list' to manage emulators." + std::string(NEWLINE));
        return;
    }
    EmulatorContext* context = emulator->GetContext();
    if (args.empty() || args[0] == "list")
    {
        session.SendResponse(DeviceState::BusText(DeviceState::BusList(context)) + NEWLINE);
        return;
    }
    if (args.size() < 2)
    {
        ShowBusHelp(session);
        return;
    }
    const std::string& bus = args[0];
    const std::string& verb = args[1];
    const auto options = Options(args, 2);
    std::string error;

    if (verb == "on" || verb == "off" || verb == "clear")
    {
        int checks = verb == "on" ? 1 : (verb == "off" ? 0 : -1);
        int protocolOnly = verb == "on" ? 0 : -1;
        for (size_t i = 2; i < args.size(); i++)
        {
            if (args[i] == "nochecks")
                checks = 0;
            else if (args[i] == "checks")
                checks = 1;
            else if (args[i] == "protocol")
                protocolOnly = 1;
        }
        const int enable = verb == "on" ? 1 : (verb == "off" ? 0 : -1);
        const std::string capacity = Get(options, "capacity");
        const size_t cap = capacity.empty() ? 0 : static_cast<size_t>(std::strtoull(capacity.c_str(), nullptr, 10));
        session.SendResponse(
            DeviceState::BusText(DeviceState::BusControl(context, bus, enable, checks, verb == "clear", cap, protocolOnly)) + NEWLINE);
        return;
    }
    if (verb == "events")
    {
        BusEventQuery query;
        if (!BusEventQueryFromStrings(Get(options, "kinds"), Get(options, "slot"), Get(options, "since"), Get(options, "from"),
                                      Get(options, "to"), options.count("limit") ? Get(options, "limit") : "60", query, error))
        {
            session.SendResponse("bus events: " + error + NEWLINE);
            return;
        }
        session.SendResponse(DeviceState::BusText(DeviceState::BusEvents(context, bus, query)) + NEWLINE);
        return;
    }
    if (verb == "findings")
    {
        BusFindingQuery query;
        if (!BusFindingQueryFromStrings(Get(options, "rules"), Get(options, "since"),
                                        options.count("limit") ? Get(options, "limit") : "60", query, error))
        {
            session.SendResponse("bus findings: " + error + NEWLINE);
            return;
        }
        session.SendResponse(DeviceState::BusText(DeviceState::BusFindings(context, bus, query)) + NEWLINE);
        return;
    }
    if (verb == "sd-timing" || verb == "sdtiming")
    {
        std::string profile;
        if (args.size() > 2 && args[2].find('=') == std::string::npos)
            profile = args[2];
        const std::string slot = Get(options, "slot");
        const std::string seed = Get(options, "seed");
        session.SendResponse(DeviceState::BusText(DeviceState::BusSdTiming(context, bus, slot.empty() ? -1 : std::atoi(slot.c_str()), profile,
                                                                           seed.empty() ? 0 : std::strtoll(seed.c_str(), nullptr, 10))) +
                             NEWLINE);
        return;
    }
    if (verb == "export")
    {
        if (args.size() < 4)
        {
            session.SendResponse("Usage: bus <id> export <vcd|jsonl> <file> [kinds=...] [since=SEQ] ..." + std::string(NEWLINE));
            return;
        }
        BusEventQuery query;
        const auto exportOptions = Options(args, 4);
        if (!BusEventQueryFromStrings(Get(exportOptions, "kinds"), Get(exportOptions, "slot"), Get(exportOptions, "since"),
                                      Get(exportOptions, "from"), Get(exportOptions, "to"),
                                      exportOptions.count("limit") ? Get(exportOptions, "limit") : "0", query, error))
        {
            session.SendResponse("bus export: " + error + NEWLINE);
            return;
        }
        const std::string text = DeviceState::BusExport(context, bus, args[2], query, error);
        if (!error.empty())
        {
            session.SendResponse("bus export: " + error + NEWLINE);
            return;
        }
        std::ofstream file(args[3], std::ios::binary);
        if (!file)
        {
            session.SendResponse("bus export: '" + args[3] + "' cannot be written" + std::string(NEWLINE));
            return;
        }
        file << text;
        session.SendResponse("exported " + bus + " as " + args[2] + " to " + args[3] + " (" + std::to_string(text.size()) + " bytes)" +
                             NEWLINE);
        return;
    }
    ShowBusHelp(session);
}
