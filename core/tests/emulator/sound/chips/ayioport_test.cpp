// AY I/O port pins (core/src/emulator/sound/chips/ayioport.h) as SoundChip_AY8910 drives them:
// tdd-midi-line.md §2.1 / ML-1. The pins follow R14 / R15 only while R7 bit 6 / 7 makes the port an
// output; an input port presents #FF (on-chip pull-ups); the listener hears pin changes only.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/ayioport.h"
#include "emulator/sound/chips/soundchip_ay8910.h"

namespace
{
struct PinEvent
{
    uint64_t t;
    int port;
    uint8_t pins;

    bool operator==(const PinEvent& o) const { return t == o.t && port == o.port && pins == o.pins; }
};

void PrintTo(const PinEvent& e, std::ostream* os)
{
    *os << "{t=" << e.t << " port=" << e.port << " pins=#" << std::hex << int(e.pins) << std::dec << "}";
}

class RecordingListener : public IAyIoPortListener
{
public:
    void OnIoPortPins(uint64_t t, int port, uint8_t pins) override { events.push_back({t, port, pins}); }
    std::vector<PinEvent> events;
};
}  // namespace

class AyIoPort_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _chip = new SoundChip_AY8910(_context);
    }

    void TearDown() override
    {
        delete _chip;
        _chip = nullptr;
        delete _context;
        _context = nullptr;
    }

    EmulatorContext* _context = nullptr;
    SoundChip_AY8910* _chip = nullptr;
};

TEST_F(AyIoPort_Test, PinsFollowDirection)
{
    RecordingListener listener;

    // Both ports inputs (R7 bits 6-7 = 0, the real chip's reset state): pulled up
    _chip->writeRegister(AY_MIXER_CONTROL, 0x3F, 0);
    EXPECT_EQ(_chip->ioPortPins(AyIoPort::PortA), 0xFF);
    EXPECT_EQ(_chip->ioPortPins(AyIoPort::PortB), 0xFF);

    _chip->setIoPortListener(&listener);

    // Input port: the latch takes the value, the pins stay pulled up
    _chip->writeRegister(AY_PORTA, 0x55, 10);
    EXPECT_EQ(_chip->ioPortPins(AyIoPort::PortA), 0xFF);
    EXPECT_EQ(_chip->readRegister(AY_PORTA), 0x55);
    EXPECT_TRUE(listener.events.empty());

    // Port A to output: the latch appears on the pins
    _chip->writeRegister(AY_MIXER_CONTROL, 0x7F, 20);
    _chip->writeRegister(AY_PORTA, 0xAA, 30);
    // Port B latch while B is an input: nothing on the pins
    _chip->writeRegister(AY_PORTB, 0x12, 40);
    // Port B to output (A stays output)
    _chip->writeRegister(AY_MIXER_CONTROL, 0xFF, 50);
    // Both back to input: both pulled up
    _chip->writeRegister(AY_MIXER_CONTROL, 0x3F, 60);

    const std::vector<PinEvent> expected = {
        {20, AyIoPort::PortA, 0x55},
        {30, AyIoPort::PortA, 0xAA},
        {50, AyIoPort::PortB, 0x12},
        {60, AyIoPort::PortA, 0xFF},
        {60, AyIoPort::PortB, 0xFF},
    };
    EXPECT_EQ(listener.events, expected);
    EXPECT_EQ(_chip->ioPortPins(AyIoPort::PortA), 0xFF);
    EXPECT_EQ(_chip->ioPortPins(AyIoPort::PortB), 0xFF);

    // Reads stay the register file (no input path is modeled)
    EXPECT_EQ(_chip->readRegister(AY_PORTA), 0xAA);
    EXPECT_EQ(_chip->readRegister(AY_PORTB), 0x12);
}

TEST_F(AyIoPort_Test, NoListenerNoCost)
{
    // Reference chip with a listener, chip under test without: the same writes leave the same chip
    // state (the TTD blob covers registers, generators and the generator-side register view)
    SoundChip_AY8910 withListener(_context);
    RecordingListener listener;
    withListener.setIoPortListener(&listener);

    const uint8_t writes[][2] = {
        {AY_PORTA, 0xFB}, {AY_MIXER_CONTROL, 0x78}, {AY_PORTA, 0xFF}, {AY_A_VOLUME, 0x0F},
        {AY_PORTB, 0x81}, {AY_MIXER_CONTROL, 0xB8}, {AY_PORTA, 0x04}, {AY_A_FINE, 0x34},
    };
    uint64_t t = 0;
    for (const auto& w : writes)
    {
        _chip->writeRegister(w[0], w[1], t);
        withListener.writeRegister(w[0], w[1], t);
        EXPECT_EQ(_chip->readRegister(w[0]), w[1]) << "register " << int(w[0]);
        t += 112;
    }
    EXPECT_FALSE(listener.events.empty());

    std::vector<uint8_t> a(_chip->TTDStateSize()), b(withListener.TTDStateSize());
    _chip->TTDSaveState(a.data());
    withListener.TTDSaveState(b.data());
    EXPECT_EQ(a, b);

    // A listener attached later starts from the current pins: no event for writes it did not see
    RecordingListener late;
    _chip->setIoPortListener(&late);
    _chip->writeRegister(AY_PORTB, 0x81, t);  // same latch as before: pins unchanged
    EXPECT_TRUE(late.events.empty());
    _chip->writeRegister(AY_PORTB, 0x80, t + 1);
    ASSERT_EQ(late.events.size(), 1u);
    EXPECT_EQ(late.events[0], (PinEvent{t + 1, AyIoPort::PortB, 0x80}));

    _chip->setIoPortListener(nullptr);
    EXPECT_EQ(_chip->ioPortListener(), nullptr);
}

TEST_F(AyIoPort_Test, OnlyChangesNotify)
{
    RecordingListener listener;
    _chip->writeRegister(AY_MIXER_CONTROL, 0x3F, 0);  // both ports inputs
    _chip->setIoPortListener(&listener);

    // Latch #FF while input, then output: #FF on the pins either way, no event
    _chip->writeRegister(AY_PORTA, 0xFF, 1);
    _chip->writeRegister(AY_MIXER_CONTROL, 0x40, 2);
    EXPECT_TRUE(listener.events.empty());

    // The same value again and again: one event
    for (uint64_t i = 0; i < 4; i++)
        _chip->writeRegister(AY_PORTA, 0xFB, 10 + i);
    // R7 writes that keep the direction (tone / noise bits only): nothing
    _chip->writeRegister(AY_MIXER_CONTROL, 0x7F, 20);
    _chip->writeRegister(AY_MIXER_CONTROL, 0x40, 21);
    // Port B latch while B is an input: nothing
    _chip->writeRegister(AY_PORTB, 0x00, 22);
    // Writes to other registers: nothing
    _chip->writeRegister(AY_A_VOLUME, 0x0F, 23);
    _chip->writeRegister(AY_ENVELOPE_SHAPE, 0x0E, 24);

    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.events[0], (PinEvent{10, AyIoPort::PortA, 0xFB}));

    // Reset is silent (the owner resets its listeners on its own time axis) and leaves the cache equal to
    // the pins of the reset register file: rewriting the reset latch reports nothing
    _chip->reset();
    EXPECT_EQ(listener.events.size(), 1u);
    _chip->writeRegister(AY_PORTA, _chip->readRegister(AY_PORTA), 30);
    EXPECT_EQ(listener.events.size(), 1u);
    _chip->writeRegister(AY_MIXER_CONTROL, _chip->readRegister(AY_MIXER_CONTROL), 31);
    EXPECT_EQ(listener.events.size(), 1u);
}
