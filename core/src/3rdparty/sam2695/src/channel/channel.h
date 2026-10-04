// libsam2695 - GS part state ("Channel": one of the chip's 16 parts).
//
// A part receives one MIDI channel (GS SysEx 40 1p 02; power-up: part i receives channel i, so part 10
// plays the drums on channel 10) and plays either sounds or a drum set (40 1p 15). Everything a part
// knows lives here: program, controllers, RPN / NRPN, the GS part parameters (voice reserve, velocity
// sense, scale tuning, assignable controllers, the controller destination matrix, the GS NRPN offsets).
#pragma once

#include <array>
#include <cstdint>

namespace sam2695
{

// RPN / NRPN numbers are kept as MSB << 8 | LSB, the datasheet notation ("NRPN 375Fh" = CC 99 37h, CC 98 5Fh)
constexpr uint16_t kNullParameter = 0x7F7F;
// Receive channel value of a part that listens to nothing (GS 40 1p 02 = 10h)
constexpr uint8_t kPartOff = 16;

// GS controller destination matrix (SysEx 40 2p xx, datasheet p.28-29): five sources, seven destinations.
enum class CtrlSource : uint8_t
{
    Mod,  // CC 1 (40 2p 00-06)
    Bend, // pitch wheel (40 2p 10-16)
    Caf,  // channel aftertouch (40 2p 20-26)
    Cc1,  // assignable controller 1 (40 2p 40-46)
    Cc2,  // assignable controller 2 (40 2p 50-56)
    Count
};
enum class CtrlDest : uint8_t
{
    Pitch,     // -24 .. +24 semitones (40h = 0)
    Cutoff,    // TVF cutoff, 150 cents per step (40h = 0)
    Amplitude, // -100 % .. +100 % (40h = 0)
    LfoRate,   // LFO1 rate, -10 .. +10 Hz (40h = 0)
    LfoPitch,  // LFO1 pitch depth, 0 .. 600 cents
    LfoCutoff, // LFO1 TVF depth, 0 .. 2400 cents
    LfoAmp,    // LFO1 TVA depth, 0 .. 100 %
    Count
};
constexpr int kCtrlSources = static_cast<int>(CtrlSource::Count);
constexpr int kCtrlDests = static_cast<int>(CtrlDest::Count);

// GS part NRPNs 01xxh (datasheet p.27), relative: 40h = no change
enum class PartNrpn : uint8_t
{
    VibratoRate,  // 0108h
    VibratoDepth, // 0109h
    VibratoDelay, // 010Ah
    Cutoff,       // 0120h
    Resonance,    // 0121h
    Attack,       // 0163h
    Decay,        // 0164h
    Release,      // 0166h
    Count
};
constexpr int kPartNrpns = static_cast<int>(PartNrpn::Count);

struct Channel
{
    uint8_t program = 0;
    uint8_t bankMsb = 0;            // latched from CC 0 at the last Program Change
    bool rhythm = false;            // plays drum sets (part 10 at power-up)
    int32_t preset = -1;            // index into the bank's presets; -1 = silent
    std::array<uint8_t, 128> cc{};
    std::array<uint8_t, 128> polyPressure{};
    uint8_t channelPressure = 0;
    uint16_t pitchBend = 0x2000;
    uint16_t rpn = kNullParameter;
    uint16_t nrpn = kNullParameter;
    bool nrpnSelected = false;      // the last parameter number written was an NRPN
    uint8_t bendRangeSemitones = 2; // RPN 0 (= GS "bend pitch control" 40 2p 10)
    uint8_t bendRangeCents = 0;
    uint16_t fineTune = 0x2000;     // RPN 1, 14-bit: 0 = -100 cents, 2000h = 0, 3FFFh = +99.99
    int16_t coarseTuneSemitones = 0;// RPN 2: -64 .. +63
    bool mono = false;              // CC 126 / 127
    uint32_t modulationVersion = 1; // bumped whenever a modulator source of this part changes

    // GS part parameters (SysEx 40 1p xx / 40 2p xx)
    uint8_t rxChannel = 0;          // MIDI channel received (0-15), kPartOff = none
    uint8_t voiceReserve = 0;       // 40 01 10: voices guaranteed to this part
    uint8_t velocitySlope = 0x40;   // 40 1p 1A: velocity sense depth
    uint8_t velocityOffset = 0x40;  // 40 1p 1B: velocity sense offset
    uint8_t cc1Number = 0x10;       // 40 1p 1F: assignable controller 1
    uint8_t cc2Number = 0x11;       // 40 1p 20: assignable controller 2
    std::array<uint8_t, 12> scaleTuning{};                       // 40 1p 40-4B, cents + 40h, C .. B
    std::array<uint8_t, kCtrlSources * kCtrlDests> ctrl{};       // 40 2p xx, [source][dest]
    std::array<uint8_t, kPartNrpns> partNrpn{};                  // NRPN 01xxh, 40h = no change
    uint8_t lastKey = 0xFF;         // sounding key of the last Note On (portamento source)

    void ResetControllers();        // RP-015 "Reset All Controllers"
    void PowerOn(int index);        // also the GS / GM reset state of a part

    double PitchWheelSensitivity() const { return bendRangeSemitones + bendRangeCents / 100.0; }
    bool Sustain() const { return cc[64] >= 64; }
    bool Sostenuto() const { return cc[66] >= 64; }
    bool Soft() const { return cc[67] >= 64; }
    bool Portamento() const { return cc[65] >= 64; }
    uint8_t Ctrl(CtrlSource s, CtrlDest d) const { return ctrl[static_cast<int>(s) * kCtrlDests + static_cast<int>(d)]; }
    uint8_t& Ctrl(CtrlSource s, CtrlDest d) { return ctrl[static_cast<int>(s) * kCtrlDests + static_cast<int>(d)]; }
    int PartNrpnDelta(PartNrpn p) const { return static_cast<int>(partNrpn[static_cast<int>(p)]) - 0x40; }

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
        ar(rxChannel);
        ar(voiceReserve);
        ar(velocitySlope);
        ar(velocityOffset);
        ar(cc1Number);
        ar(cc2Number);
        ar(scaleTuning);
        ar(ctrl);
        ar(partNrpn);
        ar(lastKey);
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
    // GS part defaults (datasheet p.27-29): part i receives channel i; voice reserve 2 for parts 1-10,
    // 0 for parts 11-16; velocity sense 40h / 40h; CC1 = controller 10h, CC2 = 11h; chromatic scale
    rxChannel = static_cast<uint8_t>(index);
    voiceReserve = index <= 9 ? 2 : 0;
    velocitySlope = 0x40;
    velocityOffset = 0x40;
    cc1Number = 0x10;
    cc2Number = 0x11;
    scaleTuning.fill(0x40);
    // controller matrix: pitch / cutoff / amplitude / LFO rate centered (40h), LFO depths 0, except the
    // modulation wheel's LFO1 pitch depth 0Ah (about 47 cents); bend's pitch control is bendRangeSemitones
    for (int s = 0; s < kCtrlSources; s++)
        for (int d = 0; d < kCtrlDests; d++)
            ctrl[s * kCtrlDests + d] = d <= static_cast<int>(CtrlDest::LfoRate) ? 0x40 : 0x00;
    Ctrl(CtrlSource::Mod, CtrlDest::LfoPitch) = 0x0A;
    partNrpn.fill(0x40);
    lastKey = 0xFF;
    ResetControllers();
}

} // namespace sam2695
