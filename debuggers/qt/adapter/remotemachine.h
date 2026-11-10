// unreal-debugger: a running emulator (unreal-qt, the headless one) behind eCat3's debugger API, through its WebAPI.
// Like unrealmachine.h, only the shim types are visible here.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "ecatshim.h"
#include "machineaction.h"

class QObject;

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

// The GUI control plane: the emulator's id on the WebAPI (empty while there is none) and the WebAPI's address
std::string RemoteMachineId(ecatshim::Emulator * e);
std::string RemoteMachineEndpoint(ecatshim::Emulator * e);
// A core action on the emulator, POST /api/v1/controllers/actions/{action} (params: a JSON object's text), without
// blocking: done runs on context's thread, never after context is gone
void RemoteMachineAction(ecatshim::Emulator * e, const std::string &action, const std::string &params, QObject * context,
                         MachineActionDone done);
