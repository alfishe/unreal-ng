#include "cli-processor.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/state/devicestate.h"
#include "emulator/io/network/traffic/trafficaccess.h"
#include "emulator/io/network/vnet/ethernetaccess.h"
#include "common/filehelper.h"

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
            session.SendResponse(std::string(change.OnlyRemoteAccess()
                                                 ? "Network settings changed: the host listeners move at the next frame boundary "
                                                   "(connections stay)"
                                                 : "Network settings changed: applied at the next frame boundary (every connection closes)") +
                                 std::string(NEWLINE));
            return;
        }
        session.SendResponse("network set: " + error + std::string(NEWLINE));
        return;
    }
    if (!args.empty() && args[0] == "frames")
    {
        // network frames [link] [file.pcap] - the gateway's capture of the frame-level cards (EthernetAccess)
        const std::string link = args.size() > 1 && args[1].find('.') != std::string::npos &&
                                         args[1].size() > 5 && args[1].compare(args[1].size() - 5, 5, ".pcap") == 0
                                     ? std::string()
                                     : (args.size() > 1 ? args[1] : std::string());
        std::string file;
        for (size_t i = 1; i < args.size(); ++i)
        {
            if (args[i].size() > 5 && args[i].compare(args[i].size() - 5, 5, ".pcap") == 0)
                file = args[i];
        }
        if (!file.empty())
        {
            std::vector<uint8_t> pcap;
            std::string error;
            if (!EthernetAccess::Pcap(emulator->GetContext(), link, pcap, error))
            {
                session.SendResponse("network frames: " + error + std::string(NEWLINE));
                return;
            }
            std::ofstream out(FileHelper::ToFsPath(file), std::ios::binary);
            out.write(reinterpret_cast<const char*>(pcap.data()), static_cast<std::streamsize>(pcap.size()));
            session.SendResponse("Wrote " + std::to_string(pcap.size()) + " bytes of pcap to " + file + std::string(NEWLINE));
            return;
        }
        StateNode report = EthernetAccess::Frames(emulator->GetContext(), link, 32);
        // The text view: one line per frame (the hex stays in the JSON / Lua / Python views)
        std::stringstream ss;
        if (const StateNode* frames = report.find("frames"))
        {
            if (frames->items.empty())
                ss << "No frames captured yet (the capture keeps the last " << report.find("capacity")->i << ")" << NEWLINE;
            for (const StateNode& f : frames->items)
                ss << "#" << f.find("index")->i << " frame " << f.find("frame")->i << " "
                   << (f.find("direction")->s == "to_card" || f.find("direction")->s == "lan_in" ? "-> " : "<- ")
                   << f.find("port")->s << (f.find("direction")->s == "lan_out" ? " (to the LAN)" : "") << " "
                   << f.find("length")->i << " " << f.find("summary")->s << NEWLINE;
        }
        else
            ss << DeviceState::ToText(report);
        session.SendResponse(ss.str());
        return;
    }
    if (!args.empty() && args[0] == "traffic")
    {
        // network traffic [adapter] [N] | network traffic <file.pcapng> | network traffic start <file.pcapng> | stop | clear
        // - everything the adapters sent and received (TrafficAccess, network #91)
        auto ends = [](const std::string& s, const char* tail) {
            const size_t n = std::strlen(tail);
            return s.size() > n && s.compare(s.size() - n, n, tail) == 0;
        };
        if (args.size() > 1 && (args[1] == "stream" || args[1] == "stream-stop"))
        {
            // network traffic stream [port] - the live pcapng stream for Wireshark; network traffic stream-stop
            std::string error;
            const uint64_t port = args.size() > 2 ? std::strtoull(args[2].c_str(), nullptr, 10) : 0;
            if (!TrafficAccess::Control(emulator->GetContext(), args[1], "", port, error))
            {
                session.SendResponse("network traffic " + args[1] + ": " + error + std::string(NEWLINE));
                return;
            }
            TrafficAccess::Query q;
            q.last = 1;
            const StateNode report = TrafficAccess::Records(emulator->GetContext(), q);
            const StateNode* s = report.find("stream");
            if (args[1] == "stream" && s && s->find("wireshark"))
                session.SendResponse("Streaming on port " + std::to_string(s->find("port")->i) + ": " + s->find("wireshark")->s +
                                     std::string(NEWLINE));
            else
                session.SendResponse(std::string("Done") + NEWLINE);
            return;
        }
        if (args.size() > 1 && (args[1] == "start" || args[1] == "stop" || args[1] == "clear"))
        {
            std::string error;
            const std::string path = args.size() > 2 ? args[2] : std::string();
            if (!TrafficAccess::Control(emulator->GetContext(), args[1], path, 0, error))
                session.SendResponse("network traffic " + args[1] + ": " + error + std::string(NEWLINE));
            else
                session.SendResponse(args[1] == "start" ? "Recording every packet into " + path + std::string(NEWLINE)
                                                        : std::string("Done") + NEWLINE);
            return;
        }
        TrafficAccess::Query query;
        for (size_t i = 1; i < args.size(); ++i)
        {
            if (ends(args[i], ".pcapng"))
            {
                std::vector<uint8_t> file;
                std::string error;
                query.last = 0;
                if (!TrafficAccess::Pcapng(emulator->GetContext(), query, file, error))
                {
                    session.SendResponse("network traffic: " + error + std::string(NEWLINE));
                    return;
                }
                std::ofstream out(FileHelper::ToFsPath(args[i]), std::ios::binary);
                out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
                session.SendResponse("Wrote " + std::to_string(file.size()) + " bytes of pcapng to " + args[i] + std::string(NEWLINE));
                return;
            }
            if (!args[i].empty() && std::isdigit(static_cast<unsigned char>(args[i][0])))
                query.last = static_cast<unsigned>(std::strtoul(args[i].c_str(), nullptr, 10));
            else
                query.adapter = args[i];
        }
        if (query.last == 64)
            query.last = 32;
        const StateNode report = TrafficAccess::Records(emulator->GetContext(), query);
        std::stringstream ss;
        if (const StateNode* records = report.find("records"))
        {
            if (records->items.empty())
                ss << "No traffic recorded yet" << NEWLINE;
            for (const StateNode& r : records->items)
                ss << "#" << r.find("index")->i << " f " << r.find("frame")->i << " " << r.find("adapter")->s << " "
                   << (r.find("direction")->s == "out" ? "-> " : "<- ") << r.find("summary")->s << NEWLINE;
        }
        else
            ss << DeviceState::ToText(report);
        session.SendResponse(ss.str());
        return;
    }
    if (!args.empty() && args[0] == "adapters")
    {
        // network adapters - the host adapters the bridge can use (ethernet_mode=bridge, network SN6)
        const StateNode report = EthernetAccess::Adapters();
        std::stringstream ss;
        const std::string library = report.find("library")->s;
        const std::string error = report.find("error")->s;
        ss << "Packet library: " << (library.empty() ? std::string("not loaded") : library) << NEWLINE;
        if (!error.empty())
            ss << "Error: " << error << NEWLINE;
        for (const StateNode& a : report.find("adapters")->items)
        {
            ss << "  " << a.find("name")->s;
            std::string ips;
            for (const StateNode& ip : a.find("ipv4")->items)
                ips += (ips.empty() ? "" : ",") + ip.s;
            if (!ips.empty())
                ss << " " << ips;
            ss << (a.find("loopback")->b ? " loopback" : "") << (a.find("wireless")->b ? " wireless" : "")
               << (a.find("up")->b ? " up" : " down") << (a.find("bridgeable")->b ? "" : " (not bridgeable)");
            if (!a.find("description")->s.empty())
                ss << " - " << a.find("description")->s;
            ss << NEWLINE;
        }
        session.SendResponse(ss.str());
        return;
    }
    if (!args.empty() && args[0] == "frame")
    {
        // network frame <link> <hex> - a frame towards the card, as if from the wire
        if (args.size() < 3)
        {
            session.SendResponse("Usage: network frame <link> <hex bytes>" + std::string(NEWLINE));
            return;
        }
        std::string hex;
        for (size_t i = 2; i < args.size(); ++i)
            hex += args[i];
        std::string error;
        if (!EthernetAccess::Inject(emulator->GetContext(), args[1], hex, "CLI network frame", error))
            session.SendResponse("network frame: " + error + std::string(NEWLINE));
        else
            session.SendResponse("Queued for " + args[1] + ": offered at the next frame boundary" + std::string(NEWLINE));
        return;
    }
    if (!args.empty() && args[0] != "state" && args[0] != "show")
    {
        session.SendResponse("Usage: network [state] | network frames [link] [file.pcap] | network frame <link> <hex> | network adapters | network traffic [adapter] [N] [file.pcapng] | network traffic start <file.pcapng> | stop | clear | network traffic stream [port] | stream-stop | network set card=none|zxnetusb|zxwifi|atm2ioesp (a list with ',') host_access=on|off "
                             "dns_mode=host|pass hosts=name=ip,... forwards=tcp:host:guest,... remote_access=on|off (guest servers listen on 0.0.0.0, "
                             "off: 127.0.0.1 only) connect_timeout_ms=n "
                             "com_port=none|loopback|tcp:host:port|serial:device[,baud]|espnet[,baud]|at[,firmware][,baud]|modem[,guest port] (the machine's serial port; "
                             "an AT module's firmware esp32|esp8266|esp8266-at221|esp8266-at222 overrides esp_chip for it alone; "
                             "an ESP module's baud defaults to the port's: 38400 on the ATM Turbo 2+ controller, 115200 elsewhere) "
                             "zx_wifi=at|espnet|... (the ZX-WiFi card's ESP) com_modem_lines=on|off esp_chip=esp32|esp8266|esp8266-at221|esp8266-at222 "
                             "(the Sprinter's SprinterESP takes an ESP8266 build from here, else esp8266-at222) "
                             "isa1_peer=at|modem[,guest port]|loopback|tcp:host:port|serial:device[,baud] isa2_peer=... (Sprinter: what the UART card in that ISA slot "
                             "is wired to - SprinterESP default at, ISA modem default modem, SprinterSerial COM1 default none) isa1_peer_b= isa2_peer_b= "
                             "(SprinterSerial COM2) modem_phonebook=5551234=host:port,... (the numbers a Hayes modem peer dials with ATDT) "
                             "ethernet_mode=nat|bridge bridge_adapter=en0 (the frame cards: the gateway's NAT, or their frames on a "
                             "host adapter - see network adapters) "
                             "avr_firmware=baseconf|base2010|base2011-04|base2011-05|base2011-09|base2013|base2023|ts|ts2013|ts2016-02|ts2016-04 (ZX-Evo) "
                             "kbc_firmware=none|v22-7|v22-11|v22-12|v31-7|v31-11|v32-7|v32-11|v40|v41 (ATM Turbo 2+ keyboard controller) "
                             "atm2ioesp=at|espnet|... atm2ioesp_address=0xF0|0xF8 (the ATM2IOESP card on the ATM Turbo 2+ INTERNAL I/O connector) "
                             "zifi=none|at[,firmware]|zifi-native[,s3|esp01s]|loopback|tcp:host:port|serial:device[,baud] (TS-Conf, ZX-Evo with a TS firmware: the ZiFi board's ESP; "
                             "at = the original ESP-01, NonOS AT 1.7.4 unless esp_chip or the firmware names an ESP8266 build; zifi-native = the 2026 firmware, "
                             "s3 = ESP32-S3-Zero s3-native-0.6.94 (default), esp01s = ESP-01S native-0.2.2)" +
                             std::string(NEWLINE));
        return;
    }
    std::stringstream ss;
    ss << "Network adapters" << NEWLINE << "================" << NEWLINE
       << DeviceState::ToText(DeviceState::Network(emulator->GetContext()));
    session.SendResponse(ss.str());
}
