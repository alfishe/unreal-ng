#pragma once

#include <cstdint>
#include <cstddef>
#include "emulator/sound/audio.h"
#include "emulator/ports/portdecoder.h"
#include "common/modulelogger.h"

class EmulatorContext;
struct blip_t;

/// SOUNDRIVE 1.05 / COVOX - 4-channel 8-bit DAC with band-limited synthesis.
///
/// Ports: #F1 (Left A), #F3 (Left B), #F9 (Right A), #FB (Right B)
/// Decoding: bits[7:4]=1111, bit2=0, bit0=1
///
/// Each DAC write computes a stereo delta and inserts it into blip_buf
/// at the exact T-state position. At frame end, blip_buf produces
/// alias-free 44.1 kHz output via windowed-sinc interpolation.
///
/// Stereo mixing: Left = (LeftA + LeftB) * 128, Right = (RightA + RightB) * 128
/// The ×128 scaling (instead of ×256) provides 0.5× headroom per channel-pair,
/// preventing hard clipping when both channels on one side are at full amplitude.
/// Mono COVOX programs (writing only to #FB/RightB) produce centered output
/// naturally — idle channels stay at midpoint (0x80) and contribute zero.
class Covox : public PortDevice
{
public:
    // Port addresses for 4 channels
    static constexpr uint16_t PORT_LEFT_A  = 0x00F1;  // Left channel A
    static constexpr uint16_t PORT_LEFT_B  = 0x00F3;  // Left channel B
    static constexpr uint16_t PORT_RIGHT_A = 0x00F9;  // Right channel A
    static constexpr uint16_t PORT_RIGHT_B = 0x00FB;  // Right channel B

    // Port decoding mask/match (catches all 4 ports)
    static constexpr uint8_t PORT_MASK  = 0xF5;  // Check bits 7-4, 2, 0
    static constexpr uint8_t PORT_MATCH = 0xF1;  // 1111x0x1

    enum class Channel { LeftA = 0, LeftB = 1, RightA = 2, RightB = 3, Count = 4 };

protected:
    EmulatorContext* _context;

    // Audio buffer (one frame of stereo int16)
    AudioFrameDescriptor _audioDescriptor;
    int16_t* const _buffer = reinterpret_cast<int16_t*>(_audioDescriptor.memoryBuffer);

    // blip_buf accumulators (one per stereo output channel)
    blip_t* _blipL = nullptr;
    blip_t* _blipR = nullptr;

    // Per-channel DAC state
    uint8_t _dacValue[4] = {0x80, 0x80, 0x80, 0x80};  // Start at midpoint (silence)

    // Last stereo amplitudes written to blip_buf (for computing deltas)
    int32_t _lastL = 0;
    int32_t _lastR = 0;

    // Per-channel mute (for UI)
    bool _channelMute[4] = {false, false, false, false};

    // DC offset removal (optional, applied post-blip)
    bool _dcRemovalEnabled = false;
    float _dcAccumL = 0.0f;
    float _dcAccumR = 0.0f;
    static constexpr float DC_COEF = 0.995f;  // ~7 Hz cutoff @ 44.1 kHz


public:
    Covox() = delete;
    explicit Covox(EmulatorContext* context);
    virtual ~Covox();

    // Buffer access for registry
    int16_t* getBuffer() { return _buffer; }
    const int16_t* getBuffer() const { return _buffer; }

    // Frame lifecycle
    void reset();
    void handleFrameStart();
    void handleFrameEnd();

    // DC removal control (for UI section)
    void setDCRemovalEnabled(bool enabled) { _dcRemovalEnabled = enabled; }
    bool isDCRemovalEnabled() const { return _dcRemovalEnabled; }

    // Per-channel mute control
    void setChannelMute(Channel ch, bool mute) { _channelMute[static_cast<int>(ch)] = mute; }
    bool isChannelMuted(Channel ch) const { return _channelMute[static_cast<int>(ch)]; }

    // PortDevice interface
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;

    // Determine which channel a port address maps to
    static Channel portToChannel(uint16_t port);

private:
    /// Compute stereo amplitudes from current DAC values.
    /// Each side sums two channels with 0.5× scaling to prevent clipping.
    void computeStereoAmplitudes(int32_t& outL, int32_t& outR) const;
};
