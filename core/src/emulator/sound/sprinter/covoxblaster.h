#pragma once

#include <cstddef>
#include <cstdint>

#include "emulator/sound/audio.h"
#include "emulator/sound/modelaudiosource.h"

class EmulatorContext;
struct blip_t;

/// The Covox-Blaster's state (Sprinter tdd-accel-sound-input §2). Plain
/// fixed-width fields, no padding: the TTD blob (id 32) is the struct.
/// Names follow the PLD (SP2_1K30.TDF "COVOX" section) and MAME.
struct CovoxBlasterState
{
    uint16_t ring[256];      ///< CBL (lpm_ram_dp 256 x 16): unsigned samples, #8000 = silence
    uint8_t control;         ///< CBL_XX (code #89): 7 CBL on, 6 stereo, 5 16-bit, 4 INT on, 3-0 rate
    uint8_t cnt;             ///< CBL_CNT: play index (cleared while CBL is off)
    uint8_t wa;              ///< CBL_WA: write index (0 while CBL or its INT is off)
    uint8_t cbd;             ///< CBD: the low byte of a 16-bit sample, waiting for its high byte
    uint8_t waeFlip;         ///< the CBL_WAE flip-flop before the 16-bit gate: 1 = the next write is a low byte
    uint8_t intPending;      ///< CBL_INT asserted (a half of the ring was played), until the INT acknowledge
    uint16_t levelL;         ///< the DAC word the left channel outputs now (CBL_R)
    uint16_t levelR;         ///< ... the right channel
    uint16_t reserved0;
    uint32_t nextTick;       ///< base T-state (3.5 MHz, frame-relative) of the next play tick (CBL_CTX = 0)
    uint32_t ticks;          ///< statistics: play ticks while CBL was on
    uint32_t ringWrites;     ///< statistics: words written into the ring
    uint32_t covoxWrites;    ///< statistics: Covox (CBL off) writes
    uint32_t intRequests;    ///< statistics: half-ring interrupts raised
};

static_assert(sizeof(CovoxBlasterState) == 512 + 32, "CovoxBlasterState must stay padding-free (TTD blob)");

/// The Sprinter Sp2000's sound DAC path: plain Covox and the Covox-Blaster
/// (CBL), one 16-bit stereo DAC (TDA1543) the PLD feeds together with the AY
/// (hardware-reference §8; MAN §5.2-5.3; INC SP2000.inc:136-220; PLD
/// SP2_1K30.TDF:1066-1166; MAME sprinter.cpp:785-814, :1069-1087, :1696-1701,
/// :1748-1765). Owned by PortDecoder_Sprinter; mixed by SoundManager in the
/// COVOX slot (IModelAudioSource).
///
/// Control (code #89, port #4E / 16-bit #0046): bit 7 CBL on (0 = Covox), 6
/// stereo, 5 16-bit, 4 INT on, 3-0 rate. The PLD's divider table:
///   rate:    0   1   2-7  8      9       10      11      12     13     14      15
///   CBL_TAB: 13  9   0    27     19      13      9       6      4      3       1
///   kHz:     15.6 21.9 218.75 7.8125 10.9375 15.625 21.875 31.25 43.75 54.6875 109.375
/// A play tick every 16 x (CBL_TAB + 1) base T-states (42 MHz / 192 / (TAB + 1);
/// 42 MHz = 12 x 3.5 MHz, so one 218.75 kHz step = 16 T at 3.5 MHz). Rates 2-7
/// are "reserved" in the INC; the PLD plays them at 218.75 kHz (MAME: never).
///
/// Data (code #88, port #FB / #4F; and the accelerator's copy writes into RAM
/// page #FD when the CBL INT is on):
///   - CBL off (Covox): the byte goes to the DAC at once, both channels
///     (CBL_R = D << 8). The ring is written too (the PLD does not gate it);
///   - CBL on: the byte enters the ring. 8-bit: entry = byte << 8. 16-bit: the
///     first byte is latched (CBD), the second makes the entry
///     ((byte XOR #80) << 8) | CBD - a signed little-endian sample.
///     Write address: with the INT on, CBL_WA (+1 per entry); with the INT off,
///     ~A15..A8 of the access (OTIR with B = 0 fills 0, 1, ..., 255) and CBL_WA = 0.
///
/// Play: on each tick CBL_CNT += 1 (mono) or 2 (stereo); the DAC outputs
/// ring[CNT] (mono, both channels) or ring[CNT & #FE] left / ring[CNT | 1]
/// right (stereo), continuously - a write into the entry being played is heard
/// at once (the PLD reads the RAM every 42 MHz clock). 16-bit and 8-bit play
/// the same way (MAME swaps the channels in 16-bit stereo; the PLD does not).
///
/// INT (bit 4): CNT bit 6 falling (CNT passes #7F -> #80 or #FF -> #00, every
/// 128 entries) raises the request; the PLD's INT acknowledge clears it (vector
/// #FF through the Sprinter INT source). While it is pending CBL_WA is held at
/// the start of the other half (WA = ~CNT & #80) and the 16-bit phase restarts.
///
/// Port #FE (code #40) in CBL mode: bit 7 = CNT7 XOR WA7 (the half that needs
/// data), bit 5 = beam below line 272 (MAME).
///
/// Worked example: control #9A (CBL on, mono, 8-bit, INT on, 15.625 kHz):
/// a tick every 16 x 14 = 224 T (one per scan line); 128 ticks = 28 672 T
/// after the start the INT asks for the first half; the handler sends 128
/// bytes with OTIR to #FB, which land at 0..127 while 128..255 plays.
class CovoxBlaster : public IModelAudioSource
{
public:
    static constexpr uint8_t kControlCbl = 0x80;
    static constexpr uint8_t kControlStereo = 0x40;
    static constexpr uint8_t kControl16Bit = 0x20;
    static constexpr uint8_t kControlInt = 0x10;

    /// CBL_TAB per rate nibble (SP2_1K30.TDF:1082-1101)
    static constexpr uint8_t kDivider[16] = {13, 9, 0, 0, 0, 0, 0, 0, 27, 19, 13, 9, 6, 4, 3, 1};
    /// Base T-states per 218.75 kHz step
    static constexpr uint32_t kStepTstates = 16;
    /// Base T-states per scan line, and the first line whose #FE bit 5 reads 1
    static constexpr uint32_t kLineTstates = 224;
    static constexpr uint32_t kBelowPictureLine = 272;

    explicit CovoxBlaster(EmulatorContext* context, size_t sampleRate = AUDIO_SAMPLING_RATE);
    ~CovoxBlaster() override;
    CovoxBlaster(const CovoxBlaster&) = delete;
    CovoxBlaster& operator=(const CovoxBlaster&) = delete;

    /// The PLD's /RESET: control 0 (Covox), play and write indices 0, no request,
    /// the DAC at #8000; the ring keeps its contents
    void Reset();

    /// region <Bus side: `t` = base T-state (3.5 MHz) of the access within the frame>
    /// Code #89
    void WriteControl(uint32_t t, uint8_t value);
    /// Code #88 (`addrHigh` = A15..A8 of the port) or an accelerator store into page #FD (A15..A8 of the address)
    void WriteData(uint32_t t, uint8_t value, uint8_t addrHigh);
    /// Whether an accelerator store into page #FD reaches the ring (PLD: CBL_INT_ENA and ACC_DIR bit 1)
    bool AcceptsPageWrites() const { return (_s.control & kControlInt) != 0; }
    /// Port #FE bits 7 and 5 in CBL mode; `keyboard` is returned unchanged with CBL off
    uint8_t ApplyFeBits(uint32_t t, uint8_t keyboard);
    /// The CBL INT is enabled (control bit 4): the per-instruction INT check's gate, before the time division
    bool IntEnabled() const { return (_s.control & kControlInt) != 0; }
    /// Whether the CBL INT is requested at `t` (no other side effect than reaching `t`)
    bool IntRequested(uint32_t t)
    {
        if (!(_s.control & kControlInt))
            return false;
        if ((_s.control & kControlCbl) && !_s.intPending && t >= _s.nextTick)
            Advance(t);
        return _s.intPending != 0;
    }
    /// The PLD's INT acknowledge (vector #FF): the request ends, CBL_WA starts the half to fill
    void Acknowledge(uint32_t t);
    /// endregion

    /// Advance to `frameLength` base T-states, render `samples` (0 = none) and rebase to the next frame
    void EndFrame(uint32_t frameLength, size_t samples);

    /// Play ticks per second for a control byte (0 = the CBL is off)
    static double RateHz(uint8_t control);
    /// Base T-states between play ticks for a control byte
    static uint32_t TickTstates(uint8_t control) { return kStepTstates * (kDivider[control & 0x0F] + 1u); }

    CovoxBlasterState& State() { return _s; }
    const CovoxBlasterState& State() const { return _s; }
    /// TTD restore: the state as a whole; the audio stream restarts from the restored levels
    void RestoreState(const CovoxBlasterState& state);
    /// The PLD blob's copy of the control byte (TTD): the register alone, no side effects
    void RestoreControl(uint8_t control) { _s.control = control; }
    /// The write index as the PLD sees it now (held while a request is pending)
    uint8_t EffectiveWriteIndex() const;

    /// region <IModelAudioSource>
    const char* AudioSourceName() const override { return "Covox-Blaster"; }
    void AudioFrameStart(bool synthesisSuppressed) override;
    void AudioFrameEnd(size_t samples) override;
    int16_t* AudioBuffer() override { return _buffer; }
    bool AudioHadSoundLastFrame() const override { return _hadSoundLastFrame; }
    void AudioSetSampleRate(size_t rate) override;
    std::vector<std::pair<std::string, std::string>> AudioStateFields() const override;
    /// endregion

    /// Signed output of a DAC word, as mixed (#8000 = 0, full scale +-16384)
    static int32_t Amplitude(uint16_t level) { return (static_cast<int32_t>(level) - 0x8000) / 2; }

private:
    /// Run the play ticks up to and including base T-state `t`
    void Advance(uint32_t t);
    /// The DAC words while CBL is on (ring at the play index)
    void OutputFromRing(uint32_t t);
    void SetLevels(uint32_t t, uint16_t left, uint16_t right);

    EmulatorContext* _context = nullptr;
    CovoxBlasterState _s{};

    AudioFrameDescriptor _audioDescriptor;
    int16_t* const _buffer = reinterpret_cast<int16_t*>(_audioDescriptor.memoryBuffer);
    blip_t* _blipL = nullptr;
    blip_t* _blipR = nullptr;
    size_t _sampleRate;
    /// Host speed multiplier of the frame (blip positions are base T x host, the Covox axis)
    uint32_t _host = 1;
    bool _synthesisSuppressed = false;
    int32_t _lastL = 0;
    int32_t _lastR = 0;
    bool _frameHadSound = false;
    bool _hadSoundLastFrame = false;
};
