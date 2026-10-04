#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "debugger/ttd/ttdserializable.h"
#include "emulator/sound/chips/ayioport.h"

namespace sam2695
{
class Synth;
}

/// Everything automation surfaces show about a MIDI line (Describe is their single source)
struct MidiLineReport
{
    int port = AyIoPort::PortA;
    int bit = 0;
    bool connected = false;      // a synthesizer receives the line
    bool level = true;           // current line level (idle high)
    uint64_t lastChangeTime = 0; // time of the last level change (0 = none since power-on)
    uint64_t edges = 0;          // level changes since power-on
};

/// One bit of a sound chip's I/O port wired to a serial MIDI IN (tdd-midi-line.md §2.2).
///
/// The MultiSound wires YM chip 1 IOA2 directly to the SAM2695 MIDI IN (hardware-reference.md §4.4),
/// the 128K convention of AY register 14 bit 2. MidiLine listens to the chip's pins
/// (IAyIoPortListener) and forwards every level change of its bit, at the time of the register write
/// that caused it, as sam2695::Synth::WriteLine(t, level). The synthesizer's UART then assembles or
/// rejects bytes from that timing exactly as the real chip would.
///
/// Time: the owner's axis is passed through unchanged; the synthesizer must be configured with the
/// same tick rate (SynthConfig::hostTickRate). A card that runs the synthesizer on another axis
/// converts before the pins reach this object.
///
/// TTD: the line level, the time of the last change and the edge counter. The UART state lives in
/// the synthesizer's own blob.
class MidiLine : public IAyIoPortListener, public ttd::TTDSerializable
{
public:
    /// TTD blob layout version (first byte of the blob)
    static constexpr uint8_t kStateVersion = 1;
    /// version + level + last change time + edges
    static constexpr size_t kBlobSize = 1 + 1 + 8 + 8;

    /// The MultiSound wiring: port A, bit 2
    static constexpr int kMultiSoundPort = AyIoPort::PortA;
    static constexpr int kMultiSoundBit = 2;

    MidiLine(int port = kMultiSoundPort, int bit = kMultiSoundBit);
    ~MidiLine() override = default;
    MidiLine(const MidiLine&) = delete;
    MidiLine& operator=(const MidiLine&) = delete;

    /// The synthesizer that receives the line (nullptr: nothing receives it). Not owned
    void Connect(sam2695::Synth* synth) { _synth = synth; }
    sam2695::Synth* ConnectedSynth() const { return _synth; }

    /// Board reset at time t: the chip's ports become inputs and the pull-ups take the line high.
    /// A low line rises at t (and the synthesizer sees that edge); the edge counter keeps counting
    void Reset(uint64_t t);

    /// IAyIoPortListener: forwards a change of the selected bit
    void OnIoPortPins(uint64_t t, int port, uint8_t pins) override;

    int Port() const { return _port; }
    int Bit() const { return _bit; }
    bool Level() const { return _level; }
    uint64_t LastChangeTime() const { return _lastChangeTime; }
    uint64_t Edges() const { return _edges; }

    void Describe(MidiLineReport& out) const;

    /// region <TTDSerializable>
    /// Layout (kStateVersion 1, 18 bytes, little-endian):
    ///   0  1  version
    ///   1  1  level (0 / 1)
    ///   2  8  last change time
    ///   10 8  edges
    /// Not registered under a PeripheralId: the MultiSound card carries it inside its blob set
    size_t TTDStateSize() const override { return kBlobSize; }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "MidiLine"; }
    uint64_t TTDHashState() const override;
    /// endregion </TTDSerializable>

private:
    void SetLevel(uint64_t t, bool level);

    int _port;
    int _bit;
    sam2695::Synth* _synth = nullptr;
    bool _level = true;
    uint64_t _lastChangeTime = 0;
    uint64_t _edges = 0;
};
