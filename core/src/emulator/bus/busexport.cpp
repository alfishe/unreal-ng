#include "stdafx.h"

#include "emulator/bus/busexport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace
{
std::string JsonString(const std::string& text)
{
    std::string out = "\"";
    for (char c : text)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                }
                else
                    out += c;
        }
    }
    return out + "\"";
}

/// A VCD identifier: printable characters from '!'
std::string VcdId(size_t index)
{
    std::string id;
    do
    {
        id += static_cast<char>('!' + index % 94);
        index /= 94;
    } while (index);
    return id;
}

std::string SignalName(const std::string& device)
{
    std::string name;
    for (char c : device)
        name += (std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    return name.empty() ? std::string("dev") : name;
}
}  // namespace

std::string BusExportJsonLines(const BusTapStatus& status, const std::vector<BusTraceEvent>& events)
{
    std::ostringstream out;
    out << "{\"bus\":" << JsonString(status.id) << ",\"protocol\":\"" << BusProtocolName(status.protocol) << "\",\"label\":"
        << JsonString(status.label) << ",\"byte_time_ns\":" << status.byteTimeNs << ",\"devices\":[";
    for (size_t i = 0; i < status.devices.size(); i++)
        out << (i ? "," : "") << "{\"slot\":" << int(status.devices[i].slot) << ",\"name\":" << JsonString(status.devices[i].name)
            << "}";
    out << "]}\n";
    for (const BusTraceEvent& e : events)
    {
        out << "{\"seq\":" << e.seq << ",\"epoch\":" << e.epoch << ",\"frame\":" << e.frame << ",\"t\":" << e.t << ",\"pc\":" << e.pc
            << ",\"source\":\"" << BusSourceName(e.source) << "\",\"slot\":" << int(e.slot) << ",\"kind\":\"" << BusEventKindName(e.kind)
            << "\",\"a\":" << int(e.a) << ",\"b\":" << int(e.b) << ",\"flags\":" << int(e.flags) << ",\"arg\":" << e.arg
            << ",\"block\":" << e.block;
        if (e.kind == BusEventKind::SdCommand)
            out << ",\"command\":\"" << SdCommandName(e.a, (e.flags & BusEventFlag::App) != 0) << "\"";
        out << "}\n";
    }
    return out.str();
}

std::string BusExportVcd(const BusTapStatus& status, const std::vector<BusTraceEvent>& events, const BusExportTiming& timing)
{
    std::ostringstream out;
    out << "$date unreal-ng bus analyzer $end\n";
    out << "$version unreal-ng " << status.id << " " << status.label << " $end\n";
    out << "$timescale 1ns $end\n";
    out << "$scope module " << SignalName(status.id) << " $end\n";
    const std::string sck = VcdId(0), mosi = VcdId(1), miso = VcdId(2);
    out << "$var wire 1 " << sck << " sck $end\n";
    out << "$var wire 1 " << mosi << " mosi $end\n";
    out << "$var wire 1 " << miso << " miso $end\n";
    std::vector<std::pair<uint8_t, std::string>> cs;
    for (size_t i = 0; i < status.devices.size(); i++)
    {
        cs.emplace_back(status.devices[i].slot, VcdId(3 + i));
        out << "$var wire 1 " << cs.back().second << " cs_n_" << SignalName(status.devices[i].name) << " $end\n";
    }
    out << "$upscope $end\n$enddefinitions $end\n";

    // Initial values: SCK low, lines high, every device deselected
    out << "#0\n$dumpvars\n0" << sck << "\n1" << mosi << "\n1" << miso << "\n";
    for (const auto& entry : cs)
        out << "1" << entry.second << "\n";
    out << "$end\n";

    const double nsPerT = timing.cpuHz > 0 ? 1e9 / timing.cpuHz : 0;
    const double byteNs = status.byteTimeNs > 0 ? status.byteTimeNs : 1000.0;
    const double bitNs = byteNs / 8.0;
    bool haveBase = false;
    double base = 0;
    uint64_t cursor = 0;   // the time the VCD has reached
    uint64_t written = 0;  // the last "#time" written
    auto at = [&](uint64_t time) {
        time = std::max(time, cursor);
        if (time != written)
        {
            out << "#" << time << "\n";
            written = time;
        }
        cursor = time;
    };
    auto csId = [&](uint8_t slot) -> const std::string* {
        for (const auto& entry : cs)
            if (entry.first == slot)
                return &entry.second;
        return nullptr;
    };

    for (const BusTraceEvent& e : events)
    {
        // A reset restarts the frame counter: each epoch is placed 1000 s after the previous one
        const double absolute = (static_cast<double>(e.epoch) * 1e12) +
                                (static_cast<double>(e.frame) * timing.frameTStates + e.t) * nsPerT;
        if (!haveBase)
        {
            base = absolute;
            haveBase = true;
        }
        const uint64_t time = static_cast<uint64_t>(std::llround(std::max(0.0, absolute - base)));
        switch (e.kind)
        {
            case BusEventKind::SpiSelect:
            case BusEventKind::SpiDeselect:
                if (const std::string* id = csId(e.slot))
                {
                    at(time);
                    out << (e.kind == BusEventKind::SpiSelect ? "0" : "1") << *id << "\n";
                }
                break;
            case BusEventKind::SpiByte:
            {
                const uint64_t start = std::max(time, cursor);
                for (int bit = 0; bit < 8; bit++)
                {
                    const uint64_t low = start + static_cast<uint64_t>(std::llround(bit * bitNs));
                    const uint64_t high = start + static_cast<uint64_t>(std::llround((bit + 0.5) * bitNs));
                    at(low);
                    out << "0" << sck << "\n" << ((e.a >> (7 - bit)) & 1) << mosi << "\n" << ((e.b >> (7 - bit)) & 1) << miso << "\n";
                    at(high);
                    out << "1" << sck << "\n";
                }
                at(start + static_cast<uint64_t>(std::llround(byteNs)));
                out << "0" << sck << "\n";
                break;
            }
            default:
                break;
        }
    }
    return out.str();
}
