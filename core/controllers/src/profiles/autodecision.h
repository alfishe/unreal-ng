#pragma once

/// @file autodecision.h
/// @brief Step 2 of the profile order (mapping-and-profiles.md §6.1, D7): the retro device a program wants, from
/// the core's device access evidence (docs/inprogress/2026-11-02-device-access-watch). The evidence is the machine's
/// own decode: a read counts for the input role its registry claim declares (a Kempston joystick, the key matrix's
/// half-rows, an AY's R14, the Next's NR #B2), never for a port number, so a TR-DOS #1F or a Next DAC write is no
/// joystick. Decisive only over a full window, on reads in nearly every frame; a program that polls every half-row
/// (the ROM's key scan, text input) wants the keyboard instead. NR #B2 makes a Next pad a 6-button one; the tested
/// bits tell Cursor from two Sinclair ports; AY R14 reads are a hint only (a Timex stick, the 128 keypad, an AY mouse
/// are not built).

#include <functional>
#include <memory>
#include <string>

#include "emulator/ports/deviceaccesswatch.h"

namespace controllers
{

struct AutoDecision
{
    bool decisive = false;
    bool keyboard = false;   ///< the program polls the whole keyboard: hint, not a plug
    std::string device;      ///< RetroDeviceKindName ("kempston-joystick", "sinclair-joystick", ...); "kempston-mouse",
                             ///< "amx-mouse" (a mouse: said, not plugged)
    std::string connector;   ///< the connector it wants when it is a fixed one ("kempston", "next.right"); "" = auto
    std::string option;      ///< the plug option ("port"), with optionValue ("1")
    std::string optionValue;
    std::string label;       ///< "Kempston", "Sinclair 1"
    std::string reason;      ///< "kempston-joystick #1F read in 50 of 50 frames (1.0 per frame)"
    /// Evidence for a device the emulator has not got (AY R14: a Timex joystick, the 128 keypad, an AY mouse); never
    /// decisive
    std::string hint;
};

/// Frames of the window a class must be read in to count as "every frame"
constexpr unsigned kEveryFrame = 45;

AutoDecision DecideFromAccess(const DeviceAccessEvidence& evidence);

/// What a signature matcher is asked: the evidence (its code sites are where the program reads its input), the code
/// as the CPU sees it, the loaded media's identity
struct SignatureQuery
{
    const DeviceAccessEvidence* evidence = nullptr;
    std::function<uint8_t(uint16_t)> code;  ///< the byte at an address, no side effects; empty when not known
    std::string mediaSha256;                ///< the loaded file's SHA-256 (hex); "" when not known
};

/// The signature library's place (D16, swsignature): it knows programs and the device each wants, by their media or
/// by their input code. Asked before the evidence table; a match is decisive
class ISignatureMatcher
{
public:
    virtual ~ISignatureMatcher() = default;
    /// True with the program's decision in @p decision (its reason says what matched)
    virtual bool Match(const SignatureQuery& query, AutoDecision& decision) const = 0;
};

/// The matcher every auto decision asks (none until the signature library registers one); any thread
void SetSignatureMatcher(std::shared_ptr<const ISignatureMatcher> matcher);
std::shared_ptr<const ISignatureMatcher> GetSignatureMatcher();

/// The auto decision: the registered signature matcher first, then the evidence table (DecideFromAccess)
AutoDecision DecideAuto(const DeviceAccessEvidence& evidence, SignatureQuery query = {});

}  // namespace controllers
