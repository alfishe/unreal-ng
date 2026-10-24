#pragma once

/// @file autodecision.h
/// @brief Step 2 of the profile order (mapping-and-profiles.md §6.1, D7): the retro device a program wants, from
/// the core's port-read evidence (retro-devices.md §6). Decisive only over a full window, on reads in nearly every
/// frame; a program that polls every half-row (the ROM's key scan, text input) wants the keyboard instead. NR #B2
/// (the Next's MD pad extras) makes a Next pad a 6-button one; the row bits tell Cursor from two Sinclair ports;
/// AY R14 reads are a hint only (the devices on port A, a Timex stick, the 128 keypad, an AY mouse, are not built).

#include <string>

#include "emulator/io/controllers/portreadwatch.h"

namespace controllers
{

struct AutoDecision
{
    bool decisive = false;
    bool keyboard = false;   ///< the program polls the whole keyboard: hint, not a plug
    std::string device;      ///< RetroDeviceKindName ("kempston-joystick", "sinclair-joystick", ...); "kempston-mouse"
    std::string connector;   ///< the connector it wants when it is a fixed one ("kempston", "next.right"); "" = auto
    std::string option;      ///< the plug option ("port"), with optionValue ("1")
    std::string optionValue;
    std::string label;       ///< "Kempston", "Sinclair 1"
    std::string reason;      ///< "#1F read in 50 of 50 frames (1.0 per frame)"
    /// Evidence for a device the emulator has not got (AY R14: a Timex joystick, the 128 keypad, an AY mouse); never
    /// decisive
    std::string hint;
};

/// Frames of the window a class must be read in to count as "every frame"
constexpr unsigned kEveryFrame = 45;

AutoDecision DecideFromPortReads(const PortReadEvidence& evidence);

}  // namespace controllers
