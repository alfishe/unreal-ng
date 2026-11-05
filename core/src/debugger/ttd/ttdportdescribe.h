#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/ports/portdecoder.h"

namespace ttd
{

/// One port access in words, for a reader (port-events hits on every surface): the device the machine's port map
/// gives the port, the row's tags, and what the access did, decoded where the device is known. Examples:
///   IN  #FDFE -> #BE   "keyboard: half-row A-G, keys down: a"
///   OUT #FFFD <- #07   "AY: select register 7 (mixer)"
///   OUT #BFFD <- #38   "AY: write mixer = #38 (tone on: A B C, noise on: none)"
///   OUT #FF1F <- #80   "FDC: command Read sector (#80)" (from the TR-DOS ROM)
///   OUT #7FFD <- #13   "paging: RAM 3 at #C000, screen 5, ROM 1"
///   OUT #00FE <- #01   "border blue, MIC 0, beeper 0"
struct TTDPortAccessText
{
    std::string device;              ///< the port map row's device ("" when no row decodes the port)
    std::vector<std::string> tags;   ///< the row's tag names (PortTagSetToStrings)
    std::string what;                ///< what the access did
};

class PortAccessDescriber
{
public:
    /// The machine's port map; null (a .ttd file searched without loading it) describes by the standard Spectrum
    /// ports only (the ULA's even ports, the AY's #FFFD / #BFFD)
    explicit PortAccessDescriber(const PortDecoder* decoder);

    /// `pc`: the instruction's address (the Beta 128's ports answer only while the TR-DOS ROM runs, below #4000:
    /// IN #1F there is the FDC's status, elsewhere the Kempston joystick); `ayRegister`: the AY register selected
    /// at the access (-1: not known)
    TTDPortAccessText Describe(bool out, uint16_t port, uint8_t value, uint16_t pc, int ayRegister = -1) const;

private:
    const PortMapEntry* RowOf(uint16_t port) const;

    std::vector<PortMapEntry> _rows;
    std::string _betaDevice;  ///< the port map's Beta 128 / WD1793 device ("" when the machine has none)
    bool _standard = false;   ///< no port map: the standard ports stand in
};

/// The AY-3-8910 register's name ("tone A fine", "mixer", "envelope shape"...), "" past 15
const char* AyRegisterName(int reg);

}  // namespace ttd
