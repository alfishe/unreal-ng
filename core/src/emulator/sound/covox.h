#pragma once

#include "emulator/sound/audio.h"
#include "emulator/ports/portdecoder.h"
#include "common/modulelogger.h"

class EmulatorContext;

/// SOUNDRIVE 1.05 / COVOX - 4-channel 8-bit DAC
/// Ports: #F1 (Left A), #F3 (Left B), #F9 (Right A), #FB (Right B)
/// Decoding: bits[7:4]=1111, bit2=0, bit0=1
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

    // Per-channel DAC state
    uint8_t _dacValue[4] = {0x80, 0x80, 0x80, 0x80};  // Start at midpoint (silence)

    // Per-channel mute (for UI)
    bool _channelMute[4] = {false, false, false, false};

    // DC offset removal (optional)
    bool _dcRemovalEnabled = false;
    float _dcAccumL = 0.0f;
    float _dcAccumR = 0.0f;
    static constexpr float DC_COEF = 0.995f;  // ~7 Hz cutoff @ 44.1 kHz


public:
    Covox() = delete;
    explicit Covox(EmulatorContext* context);
    virtual ~Covox() = default;

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
    void renderFromSample(size_t startSample);
    void computeMixedOutput(int16_t& left, int16_t& right) const;
    int16_t dacToSample(uint8_t value) const;
};
