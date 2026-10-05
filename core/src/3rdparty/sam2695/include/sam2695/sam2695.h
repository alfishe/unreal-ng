// libsam2695 - Dream SAM2695 General MIDI synthesizer: public API.
//
// Time: every input call carries a timestamp on the host axis (SynthConfig::hostTickRate). The chip
// runs on its own grid of kInternalRate samples; a MIDI byte takes effect at the first internal
// sample at or after its time. Run(t) synthesizes complete control blocks (kControlBlock samples)
// before t, so how the host slices Run() calls never changes the output. Render() resamples what
// Run() produced to the output rate; a turbo host may call Run() and never Render().
//
// Determinism: the same input gives the same output on the same build. SaveState captures the chip
// (UART, parser, queued bytes, channels, voices) in a platform-independent blob that names the bank by
// its SHA-256; the render layer (resampler history, produced-but-unrendered audio) is not captured.
//
// No allocation on the audio path: Configure() and LoadBank() size every buffer.
#pragma once

#include "sam2695/sam2695config.h"
#include "sam2695/soundbank.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace sam2695
{

struct SynthReport
{
    struct ChannelView
    {
        uint8_t program = 0;
        uint8_t bankMsb = 0;
        bool rhythm = false;
        int32_t preset = -1;           // index into the bank's preset list, -1 = none
        uint8_t volume = 0, pan = 0, expression = 0;
        uint16_t pitchBend = 0x2000;
        uint8_t activeVoices = 0;
        bool muted = false;
        uint8_t rxChannel = 0;         // MIDI channel the part receives (16 = none)
        uint8_t voiceReserve = 0;      // GS voice reserve
    };
    std::array<ChannelView, 16> channels{}; // the 16 GS parts (part i receives channel i at power-up)
    uint32_t polyphonyLimit = 0;
    uint32_t activeVoices = 0;         // voices that count against the limit
    uint32_t fadingVoices = 0;         // stolen / exclusive-class fades in the extra slots
    uint8_t effectsWord = 0;           // NRPN 375Fh
    // effects and output (README "Effects")
    uint8_t reverbProgram = 0, reverbCharacter = 0, chorusProgram = 0;
    double reverbDecaySeconds = 0.0;   // tail to -60 dB of the reverb program (0 for the delay programs)
    uint8_t masterVolume = 0;          // NRPN 3707h
    uint8_t gmVolume = 0, gmPan = 0;   // NRPN 3722h / 3723h (GM / GS master volume and pan)
    float masterTuneCents = 0.0f;      // GS master tune
    int8_t keyShift = 0;               // GS master key shift, semitones
    uint8_t deviceId = 0;              // NRPN 3757h (20h = all)
    bool softClip = true;              // NRPN 3713h
    double codecGainDb = 0.0;          // codec port 12h OUTG
    bool codecMuted = false;
    // UART and parser counters since the last Reset
    uint64_t bytesReceived = 0;
    uint64_t framingErrors = 0;
    uint64_t bytesDroppedBusy = 0;     // arrived during the 50 ms after a reset
    uint64_t bytesDroppedQueueFull = 0;
    uint64_t sysExOverflows = 0;
    uint64_t sysExReceived = 0;
    uint64_t voicesStolen = 0;
    uint64_t notesDropped = 0;         // no voice could be found at all
    uint64_t streamOverruns = 0;       // internal frames discarded because Render() lagged
};

class Synth
{
public:
    Synth();
    ~Synth();
    Synth(const Synth&) = delete;
    Synth& operator=(const Synth&) = delete;

    bool Configure(const SynthConfig& cfg);
    const SynthConfig& Config() const;

    // Outside the audio path. Replaces the bank and resets the chip state (the state of one bank does
    // not apply to another). Null unloads (silence).
    bool LoadBank(std::shared_ptr<const ISoundBank> bank);
    const ISoundBank* Bank() const;

    // Power-on state at time t: channels, voices, effects word, UART and parser.
    void Reset(uint64_t t);

    // Serial MIDI IN, idle high. The UART decodes 8N1 at 31 250 baud from the line edges.
    void WriteLine(uint64_t t, bool level);
    // Whole bytes (a host-side UART, a parallel port, a file player).
    void WriteByte(uint64_t t, uint8_t byte);

    // Synthesize every control block that ends at or before t.
    void Run(uint64_t t);

    // Interleaved stereo float at outputRate, normalized to +-1.0. Returns the frames written; fewer
    // than maxFrames when Run() has not produced enough.
    size_t Render(float* stereo, size_t maxFrames);
    // Drop produced audio without rendering (turbo hosts).
    void DiscardPendingAudio();

    // Live output-rate change at a frame boundary: only the render layer is rebuilt.
    void SetOutputRate(uint32_t rate);
    // Interpolation is a render option, not chip state; it may change at any time.
    void SetInterpolation(Interpolation mode);

    size_t StateSize() const;               // constant after Configure + LoadBank
    void SaveState(uint8_t* out) const;
    bool LoadState(const uint8_t* in, size_t size); // refuses another bank, another layout
    // The bank a state blob names (its SHA-256), without a synthesizer; false when the blob is not one
    static bool StateBank(const uint8_t* in, size_t size, BankDigest& digest);

    // Taps for the UI and tests. Mute acts on the mix only, never on chip state.
    void SetChannelMute(int channel, bool mute);
    void Describe(SynthReport& out) const;

    // The internal sample the next Run() block starts at (for tests and timing views).
    uint64_t InternalPosition() const;
    // The host time of internal sample n (the first host tick at or after it).
    uint64_t HostTimeOfSample(uint64_t n) const;

    struct Impl;

private:
    std::unique_ptr<Impl> _impl;
};

} // namespace sam2695
