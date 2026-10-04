// libsam2695 - MIDI channel (GS "part") state.
#pragma once

#include <array>
#include <cstdint>

namespace sam2695
{

// RPN / NRPN numbers are kept as MSB << 8 | LSB, the datasheet notation ("NRPN 375Fh" = CC 99 37h, CC 98 5Fh)
constexpr uint16_t kNullParameter = 0x7F7F;

struct Channel
{
    uint8_t program = 0;
    uint8_t bankMsb = 0;            // latched from CC 0 at the last Program Change
    bool rhythm = false;            // plays drum sets (channel 10 at power-up)
    int32_t preset = -1;            // index into the bank's presets; -1 = silent
    std::array<uint8_t, 128> cc{};
    std::array<uint8_t, 128> polyPressure{};
    uint8_t channelPressure = 0;
    uint16_t pitchBend = 0x2000;
    uint16_t rpn = kNullParameter;
    uint16_t nrpn = kNullParameter;
    bool nrpnSelected = false;      // the last parameter number written was an NRPN
    uint8_t bendRangeSemitones = 2; // RPN 0
    uint8_t bendRangeCents = 0;
    uint16_t fineTune = 0x2000;     // RPN 1, 14-bit: 0 = -100 cents, 2000h = 0, 3FFFh = +99.99
    int16_t coarseTuneSemitones = 0;// RPN 2: -64 .. +63
    bool mono = false;              // CC 126 / 127
    uint32_t modulationVersion = 1; // bumped whenever a modulator source of this channel changes

    void ResetControllers();        // RP-015 "Reset All Controllers"
    void PowerOn(int index);

    double PitchWheelSensitivity() const { return bendRangeSemitones + bendRangeCents / 100.0; }
    bool Sustain() const { return cc[64] >= 64; }

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(program);
        ar(bankMsb);
        ar(rhythm);
        ar(preset);
        ar(cc);
        ar(polyPressure);
        ar(channelPressure);
        ar(pitchBend);
        ar(rpn);
        ar(nrpn);
        ar(nrpnSelected);
        ar(bendRangeSemitones);
        ar(bendRangeCents);
        ar(fineTune);
        ar(coarseTuneSemitones);
        ar(mono);
        ar(modulationVersion);
    }
};

inline void Channel::ResetControllers()
{
    // GM RP-015: volume, pan, bank select, program and the effect sends are kept
    cc[1] = 0;     // modulation
    cc[11] = 127;  // expression
    cc[64] = 0;    // sustain
    cc[65] = 0;    // portamento
    cc[66] = 0;    // sostenuto
    cc[67] = 0;    // soft
    channelPressure = 0;
    polyPressure.fill(0);
    pitchBend = 0x2000;
    rpn = kNullParameter;
    nrpn = kNullParameter;
    nrpnSelected = false;
    modulationVersion++;
}

inline void Channel::PowerOn(int index)
{
    program = 0;
    bankMsb = 0;
    rhythm = index == 9;
    preset = -1;
    cc.fill(0);
    cc[7] = 100;   // volume (datasheet p.26: default 100)
    cc[10] = 64;   // pan center
    cc[11] = 127;  // expression
    cc[91] = 40;   // reverb send: the GS power-up level
    cc[93] = 0;    // chorus send
    bendRangeSemitones = 2;
    bendRangeCents = 0;
    fineTune = 0x2000;
    coarseTuneSemitones = 0;
    mono = false;
    ResetControllers();
}

} // namespace sam2695
