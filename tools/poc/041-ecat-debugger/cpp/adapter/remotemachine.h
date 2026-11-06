// POC 041: a running emulator (unreal-qt, the headless one) behind eCat3's debugger API, through its WebAPI.
// Like unrealmachine.h, only the shim types are visible here.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "ecatshim.h"

struct RemoteOptions {
    std::string host = "127.0.0.1";
    uint16_t port = 8090;
    std::string emulator;   // an id or symbolic id; empty: the first one the WebAPI lists
};

// Connects, finds the emulator, takes a run-control session. Null with the reason in error
std::unique_ptr<ecatshim::Emulator> CreateRemoteMachine(const RemoteOptions &options, std::string &error);

// The emulator's frame counter, from the last snapshot (the self-check waits for the boot)
uint64_t RemoteMachineFrames(ecatshim::Emulator * e);
// What the connection did: requests and bytes received since the start
void RemoteMachineTraffic(ecatshim::Emulator * e, uint64_t &requests, uint64_t &bytes);
