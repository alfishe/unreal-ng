#pragma once

/// @file fakehostnet.h
/// @brief IHostNet test double: records every command, lets the test inject
/// host events (network adapters tests; no real sockets, no threads).

#include <deque>
#include <string>
#include <vector>

#include "common/network/hostnet.h"

class FakeHostNet : public IHostNet
{
public:
    struct Command
    {
        std::string op;
        uint16_t socket = 0;
        NetEndpoint endpoint;
        std::vector<uint8_t> data;
    };

    std::vector<Command> commands;
    std::deque<HostNetEvent> events;

    void TcpConnect(uint16_t socket, const NetEndpoint& to) override { commands.push_back({"connect", socket, to, {}}); }
    void TcpSend(uint16_t socket, const uint8_t* data, uint32_t length) override
    {
        commands.push_back({"send", socket, {}, std::vector<uint8_t>(data, data + length)});
    }
    void TcpShutdownWrite(uint16_t socket) override { commands.push_back({"shutdown", socket, {}, {}}); }
    void TcpListen(uint16_t socket, uint16_t hostPort) override
    {
        commands.push_back({"listen", socket, NetEndpoint{0, hostPort}, {}});
    }
    void UdpSend(uint16_t socket, const NetEndpoint& to, const uint8_t* data, uint32_t length) override
    {
        commands.push_back({"udp", socket, to, std::vector<uint8_t>(data, data + length)});
    }
    void DnsQuery(uint16_t socket, const NetEndpoint& server, const uint8_t* query, uint32_t length) override
    {
        commands.push_back({"dns", socket, server, std::vector<uint8_t>(query, query + length)});
    }
    void IcmpEcho(uint16_t socket, const NetEndpoint& to, const uint8_t* data, uint32_t length) override
    {
        commands.push_back({"ping", socket, to, std::vector<uint8_t>(data, data + length)});
    }
    void SerialOpen(uint16_t socket, const std::string& device, uint32_t baud) override
    {
        commands.push_back({"serial", socket, NetEndpoint{baud, 0}, std::vector<uint8_t>(device.begin(), device.end())});
    }
    void SerialConfigure(uint16_t socket, const SerialLine& line) override
    {
        commands.push_back({"serial-line", socket, NetEndpoint{line.baud, static_cast<uint16_t>(line.dataBits * 100 + line.stopBits)},
                            std::vector<uint8_t>{static_cast<uint8_t>(line.parity)}});
    }
    void SerialModemLines(uint16_t socket, bool rts, bool dtr) override
    {
        commands.push_back({"serial-lines", socket, NetEndpoint{static_cast<uint32_t>((rts ? 2 : 0) | (dtr ? 1 : 0)), 0}, {}});
    }
    void Close(uint16_t socket) override { commands.push_back({"close", socket, {}, {}}); }
    void CloseAll() override { commands.push_back({"closeall", 0, {}, {}}); }

    bool PollEvent(HostNetEvent& out) override
    {
        if (events.empty())
            return false;
        out = events.front();
        events.pop_front();
        return true;
    }

    void Push(NetEventType type, uint16_t socket, NetEventStatus status = NetEventStatus::Ok, NetEndpoint peer = {},
              std::vector<uint8_t> data = {})
    {
        HostNetEvent ev;
        ev.type = type;
        ev.status = status;
        ev.socket = socket;
        ev.peer = peer;
        ev.data = std::move(data);
        events.push_back(std::move(ev));
    }

    const Command* Last(const std::string& op) const
    {
        for (auto it = commands.rbegin(); it != commands.rend(); ++it)
        {
            if (it->op == op)
                return &*it;
        }
        return nullptr;
    }
};
