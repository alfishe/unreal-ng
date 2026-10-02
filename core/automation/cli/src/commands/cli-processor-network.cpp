#include "cli-processor.h"

#include <sstream>

#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/state/devicestate.h"

/// network [state|show] - network adapters: the card, the W5300 sockets and the
/// virtual network (DeviceState::Network, the report every interface shares)
void CLIProcessor::HandleNetwork(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    if (!args.empty() && args[0] == "set")
    {
        // network set key=value ... (NetworkManager::ParseChange: the keys every interface takes)
        std::vector<std::pair<std::string, std::string>> settings;
        for (size_t i = 1; i < args.size(); ++i)
        {
            const size_t eq = args[i].find('=');
            if (eq == std::string::npos)
            {
                session.SendResponse("network set: expected key=value, got '" + args[i] + "'" + std::string(NEWLINE));
                return;
            }
            settings.emplace_back(args[i].substr(0, eq), args[i].substr(eq + 1));
        }
        NetworkManager* manager = emulator->GetContext()->pCore ? emulator->GetContext()->pCore->GetNetworkManager() : nullptr;
        NetworkManager::Change change;
        std::string error;
        if (!manager)
            error = "no network support in this machine";
        else if (NetworkManager::ParseChange(settings, change, error) && manager->RequestChange(change, error))
        {
            session.SendResponse("Network settings changed: applied at the next frame boundary (every connection closes)" +
                                 std::string(NEWLINE));
            return;
        }
        session.SendResponse("network set: " + error + std::string(NEWLINE));
        return;
    }
    if (!args.empty() && args[0] != "state" && args[0] != "show")
    {
        session.SendResponse("Usage: network [state] | network set card=none|zxnetusb|zxwifi|atm2ioesp (a list with ',') host_access=on|off "
                             "dns_mode=host|pass hosts=name=ip,... forwards=tcp:host:guest,... connect_timeout_ms=n "
                             "com_port=none|loopback|tcp:host:port|serial:device[,baud]|espnet[,baud]|at[,baud] (the machine's serial port; "
                             "an ESP module's baud defaults to the port's: 38400 on the ATM Turbo 2+ controller, 115200 elsewhere) "
                             "zx_wifi=at|espnet|... (the ZX-WiFi card's ESP) com_modem_lines=on|off esp_chip=esp32|esp8266 "
                             "avr_firmware=baseconf|base2010|base2011-04|base2011-05|base2011-09|base2013|base2023|ts|ts2013|ts2016-02|ts2016-04 (ZX-Evo) "
                             "kbc_firmware=none|v22-7|v22-11|v22-12|v31-7|v31-11|v32-7|v32-11|v40|v41 (ATM Turbo 2+ keyboard controller) "
                             "atm2ioesp=at|espnet|... atm2ioesp_address=0xF0|0xF8 (the ATM2IOESP card on the ATM Turbo 2+ INTERNAL I/O connector)" +
                             std::string(NEWLINE));
        return;
    }
    std::stringstream ss;
    ss << "Network adapters" << NEWLINE << "================" << NEWLINE
       << DeviceState::ToText(DeviceState::Network(emulator->GetContext()));
    session.SendResponse(ss.str());
}
