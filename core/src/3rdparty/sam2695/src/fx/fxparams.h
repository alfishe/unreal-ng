// libsam2695 - the chip's effect and output settings as MIDI sets them (NRPN 37xxh, GS SysEx 40 01 3x,
// CC 80 / 81, the codec SysEx), with the datasheet's power-up values. Values are kept in the units the
// messages carry (7-bit, the codec's 16-bit ports); the processors derive their coefficients from them.
#pragma once

#include <array>
#include <cstdint>

namespace sam2695
{

constexpr int kReverbPrograms = 8; // room1 room2 room3 hall1 hall2 plate delay pan-delay
constexpr int kChorusPrograms = 8; // chorus1-4, feedback chorus, flanger, short delay, feedback delay

// Per-program defaults, datasheet p.23-24. The level defaults are given there in parallel-control
// units (0-FFh); the SysEx / GS units used here are half (90h -> 48h, C0h -> 60h, FFh -> 7Fh).
constexpr std::array<uint8_t, kReverbPrograms> kReverbLevelDefault = {0x48, 0x48, 0x48, 0x60, 0x48, 0x48, 0x7F, 0x7F};
constexpr std::array<uint8_t, kReverbPrograms> kReverbTimeDefault = {0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x18, 0x7F};
constexpr std::array<uint8_t, kReverbPrograms> kReverbFeedbackDefault = {0, 0, 0, 0, 0, 0, 0x22, 0x26};
constexpr std::array<uint8_t, kChorusPrograms> kChorusLevelDefault = {0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x7F, 0x7F};
constexpr std::array<uint8_t, kChorusPrograms> kChorusDelayDefault = {0x4B, 0x40, 0x40, 0x2B, 0x7F, 0x56, 0x7F, 0x7F};
constexpr std::array<uint8_t, kChorusPrograms> kChorusFeedbackDefault = {0x00, 0x07, 0x09, 0x0C, 0x48, 0x7F, 0x00, 0x50};
constexpr std::array<uint8_t, kChorusPrograms> kChorusRateDefault = {0x03, 0x09, 0x03, 0x09, 0x02, 0x01, 0x00, 0x00};
constexpr std::array<uint8_t, kChorusPrograms> kChorusDepthDefault = {0x05, 0x13, 0x13, 0x10, 0x0C, 0x03, 0x00, 0x00};

constexpr uint16_t kCodec0Default = 0x1B79; // port 12h: OUTG 39h (0 dB), DACSEL 1, DACMUTE 0 (p.36)
constexpr uint16_t kCodec1Default = 0x077D; // port 14h: ADC side only (mic boost, high-pass)

struct FxParams
{
    // reverb (CC 80, GS 40 01 30-35, parallel controls 69h / 3Ah / 78h / 79h)
    uint8_t reverbType = 4;       // the program last selected (macro)
    uint8_t reverbCharacter = 4;  // the algorithm (GS "reverb character"); the program sets it
    uint8_t reverbLevel = 0x48;
    uint8_t reverbTime = 0x7F;
    uint8_t reverbFeedback = 0;   // delay programs 6 / 7 only
    // chorus (CC 81, GS 40 01 38-3E)
    uint8_t chorusType = 2;
    uint8_t chorusLevel = 0x48;
    uint8_t chorusDelay = 0x40;
    uint8_t chorusFeedback = 0x09;
    uint8_t chorusRate = 0x03;
    uint8_t chorusDepth = 0x13;
    // equalizer (NRPN 3700h-3703h levels, 3708h-370Bh corners), bands LB MLB MHB HB
    std::array<uint8_t, 4> eqLevel = {0x60, 0x40, 0x40, 0x60};
    std::array<uint8_t, 4> eqFreq = {0x0C, 0x1B, 0x72, 0x40};
    // post effects (spatial + EQ) routing (NRPN 3718h-371Ah)
    uint8_t postGm = 0x7F;
    uint8_t postMike = 0x00;
    uint8_t postFx = 0x7F;
    // spatial effect (NRPN 3720h / 372Ch / 372Dh)
    uint8_t spatialVolume = 0x00;
    uint8_t spatialDelay = 0x1D;
    uint8_t spatialInput = 0x00; // 0 stereo (L - R), 7Fh mono (L + R)
    // output
    uint8_t clipMode = 0x00;     // NRPN 3713h: 0 soft, 7Fh hard
    uint8_t reverbSend = 0x40;   // NRPN 3715h: GM reverb send scaling, 40h = as sent
    uint8_t chorusSend = 0x40;   // NRPN 3716h
    uint8_t masterVolume = 0x7F; // NRPN 3707h
    uint8_t gmVolume = 0x7F;     // NRPN 3722h = GM / GS SysEx master volume
    uint8_t gmPan = 0x40;        // NRPN 3723h = GS SysEx master pan (40 00 06)
    uint16_t codec0 = kCodec0Default;
    uint16_t codec1 = kCodec1Default;

    void SelectReverb(uint8_t program)
    {
        reverbType = reverbCharacter = program & 7;
        reverbLevel = kReverbLevelDefault[reverbType];
        reverbTime = kReverbTimeDefault[reverbType];
        reverbFeedback = kReverbFeedbackDefault[reverbType];
    }

    void SelectChorus(uint8_t program)
    {
        chorusType = program & 7;
        chorusLevel = kChorusLevelDefault[chorusType];
        chorusDelay = kChorusDelayDefault[chorusType];
        chorusFeedback = kChorusFeedbackDefault[chorusType];
        chorusRate = kChorusRateDefault[chorusType];
        chorusDepth = kChorusDepthDefault[chorusType];
    }

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(reverbType);
        ar(reverbCharacter);
        ar(reverbLevel);
        ar(reverbTime);
        ar(reverbFeedback);
        ar(chorusType);
        ar(chorusLevel);
        ar(chorusDelay);
        ar(chorusFeedback);
        ar(chorusRate);
        ar(chorusDepth);
        ar(eqLevel);
        ar(eqFreq);
        ar(postGm);
        ar(postMike);
        ar(postFx);
        ar(spatialVolume);
        ar(spatialDelay);
        ar(spatialInput);
        ar(clipMode);
        ar(reverbSend);
        ar(chorusSend);
        ar(masterVolume);
        ar(gmVolume);
        ar(gmPan);
        ar(codec0);
        ar(codec1);
    }
};

} // namespace sam2695
