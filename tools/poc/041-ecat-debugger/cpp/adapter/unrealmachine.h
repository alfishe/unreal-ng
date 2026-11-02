// POC 041: an unreal-ng machine behind eCat3's debugger API. Only the shim types are visible here, so the Qt side
// (main.cpp and the ecat views) never includes unreal-ng's headers, whose Emulator / Memory classes share names
// with ecat's.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ecatshim.h"

struct MachineOptions {
    std::string model = "PENTAGON";
    std::string file;       // a snapshot, tape or disk to load; empty: none
    std::string card = "neogs";  // the card in zxbus.1 ("neogs", "multisound", ...); empty: the model's own slots
};

// Creates the emulator, loads the file, starts it on its own thread. Null with the reason in error
std::unique_ptr<ecatshim::Emulator> CreateUnrealMachine(const MachineOptions &options, std::string &error);

// The picture of the last frame as 0xAARRGGBB, for the POC's screen window
bool UnrealMachineScreen(ecatshim::Emulator * e, std::vector<uint32_t> &pixels, int &width, int &height);
// Frames emulated since the start (the cost measurement)
uint64_t UnrealMachineFrames(ecatshim::Emulator * e);

// The Z80 listing decoder without a machine (no labels): the remote machine's
ecatshim::DisAsm * CreatePlainZ80DisAsm();
