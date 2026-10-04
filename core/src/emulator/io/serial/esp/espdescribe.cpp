#include "emulator/io/serial/esp/espdescribe.h"

#include <cstdio>
#include <string>

#include "emulator/io/serial/esp/atmodule.h"
#include "emulator/io/serial/esp/espmodule.h"
#include "emulator/io/serial/esp/zifinativemodule.h"

namespace
{
std::string MacText(const std::array<uint8_t, 6>& m)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return text;
}
}  // namespace

namespace espdescribe
{

void Describe(const EspModule& esp, StateNode& e)
{
    const auto* at = dynamic_cast<const AtModule*>(&esp);
    const auto* native = dynamic_cast<const ZiFiNativeModule*>(&esp);
    if (at)
        e["firmware"] = std::string(EspModule::FirmwareName(at->GetFirmware()));
    else if (native)
        e["firmware"] = std::string("ZIFI-NATIVE ") + ZiFiNativeModule::VariantName(native->GetVariant()) + " (" +
                        ZiFiNativeModule::FirmwareVersion(native->GetVariant()) + ")";
    else
        e["firmware"] = std::string(esp.Kind());
    e["state"] = esp.ResetHeld() ? "reset held" : esp.DownloadMode() ? "ROM download mode (GPIO0 was low)" : "running";
    e["hardware_resets"] = static_cast<uint64_t>(esp.HardwareResets());
    static const char* const kWifi[] = {"idle", "connecting", "got_ip"};
    e["wifi"] = kWifi[static_cast<int>(esp.GetWifi()) % 3];
    e["ssid"] = esp.Ssid();
    e["ip"] = NetIpToString(esp.Ip());
    e["mac"] = MacText(esp.Mac());
    e["baud"] = static_cast<uint64_t>(esp.Baud());
    e["factory_baud"] = static_cast<uint64_t>(esp.FactoryBaud());
    e["flow_control"] = esp.HonorsRts();
    e["line_mismatch"] = esp.LineMismatch();
    e["requests"] = esp.RequestsServed();
    if (at)
    {
        StateNode s = StateNode::Object();
        s["echo"] = at->Echo();
        s["mux"] = at->Mux();
        s["passive_receive"] = at->Passive();
        s["sysstore"] = at->SysStore();
        s["syslog"] = at->SysLog();
        s["flash"] = at->GetFlash() == atdialect::Flash::OneMb ? "1MB (no OTA)" : "2MB+";
        if (at->ManualDns(0))
            s["dns"] = NetIpToString(at->ManualDns(0)) + (at->ManualDns(1) ? "," + NetIpToString(at->ManualDns(1)) : "");
        StateNode links = StateNode::Array();
        for (int i = 0; i < AtModule::kLinks; ++i)
        {
            if (!at->LinkOpen(i))
                continue;
            const EspStack::Slot& slot = at->Stack().GetSlot(i);
            StateNode l = StateNode::Object();
            l["link"] = i;
            l["proto"] = at->LinkUdp(i) ? "udp" : "tcp";
            l["remote"] = NetIpToString(slot.remote.addr) + ":" + std::to_string(slot.remote.port);
            l["rx_pending"] = static_cast<uint64_t>(slot.rx.size());
            l["peer_closed"] = slot.finSeen;
            links.push(l);
        }
        s["links"] = links;
        e["at_session"] = s;
    }
    if (native)
    {
        StateNode s = StateNode::Object();
        s["variant"] = ZiFiNativeModule::VariantName(native->GetVariant());
        s["activity"] = native->Activity();
        char step[8];
        std::snprintf(step, sizeof(step), "#%02X", native->LastStep());
        s["last_step"] = std::string(step);
        s["last_error"] = native->LastError();
        s["client_open"] = native->ClientOpen();
        if (native->ClientOpen())
        {
            const EspStack::Slot& slot = native->Stack().GetSlot(0);
            s["client_remote"] = NetIpToString(slot.remote.addr) + ":" + std::to_string(slot.remote.port);
            s["client_rx_pending"] = static_cast<uint64_t>(slot.rx.size());
            s["client_peer_closed"] = slot.finSeen;
        }
        s["time_zone"] = static_cast<int>(native->TimeZone());
        if (!native->ProxyHost().empty())
        {
            static const char* const kProxy[] = {"off", "on", "unreachable"};
            s["proxy"] = native->ProxyHost() + ":" + std::to_string(native->ProxyPort()) + " (" +
                         kProxy[native->ProxyStatus() % 3] + ")";
        }
        s["bad_checksums"] = static_cast<uint64_t>(native->BadChecksums());
        s["resyncs"] = static_cast<uint64_t>(native->Resyncs());
        e["native_session"] = s;
    }
    StateNode ex = StateNode::Array();
    for (const EspModule::Exchange& x : esp.RecentExchanges())
    {
        StateNode one = StateNode::Object();
        one["request"] = x.request;
        one["reply"] = x.reply;
        ex.push(one);
    }
    e["exchanges"] = ex;
}

}  // namespace espdescribe
