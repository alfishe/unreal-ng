/// @file smucnvram_test.cpp
/// @brief The SMUC serial EEPROM link's state (SMUCNvram::LinkState, TTD blob
/// Smuc): a transfer cut anywhere and resumed from the saved state ends as the
/// uninterrupted one.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "emulator/io/rtc/smucnvram.h"

namespace
{
// #FFBA bits: SDA out = 4, SCL = 6, WP = 5 (kept low: writes allowed)
constexpr uint8_t kSda = 0x10;
constexpr uint8_t kScl = 0x40;

/// The #FFBA writes of one I2C byte write: START, select (1010 ddd 0),
/// address low byte, data, each with its ACK clock, STOP
std::vector<uint8_t> WriteByteSequence(uint16_t address, uint8_t value)
{
    std::vector<uint8_t> out;
    auto lines = [&](bool sda, bool scl) { out.push_back(static_cast<uint8_t>((sda ? kSda : 0) | (scl ? kScl : 0))); };
    auto byte = [&](uint8_t b) {
        for (int bit = 7; bit >= 0; --bit)
        {
            const bool sda = (b >> bit) & 1;
            lines(sda, false);
            lines(sda, true);
            lines(sda, false);
        }
        lines(true, false);   // ACK clock, the line released
        lines(true, true);
        lines(true, false);
    };
    lines(true, true);
    lines(false, true);       // START
    lines(false, false);
    byte(static_cast<uint8_t>(0xA0 | ((address >> 7) & 0x0E)));
    byte(static_cast<uint8_t>(address & 0xFF));
    byte(value);
    lines(false, false);
    lines(false, true);
    lines(true, true);        // STOP: the page is committed
    return out;
}
}  // namespace

TEST(SMUCNvram_Test, LinkStateRoundTripsEveryField)
{
    SMUCNvram nvram;
    SMUCNvram::LinkState s{};
    s.mode = 3;
    s.flags = 0x0F;
    s.bitCount = 6;
    s.data = 0x9C;
    s.addressLow = 0x34;
    s.addressHigh = 0x05;
    s.writePos = 17;   // kept as is: the write path masks it
    s.sda = 0;
    s.scl = 1;
    for (uint8_t i = 0; i < 16; ++i)
        s.writeBuffer[i] = static_cast<uint8_t>(i * 7);
    nvram.SetLinkState(s);
    const SMUCNvram::LinkState back = nvram.GetLinkState();
    EXPECT_EQ(std::memcmp(&back, &s, sizeof(s)), 0);
}

TEST(SMUCNvram_Test, TransferCutAnywhereResumesFromTheSavedState)
{
    constexpr uint16_t kAddress = 0x2A5;
    constexpr uint8_t kValue = 0xC3;
    const std::vector<uint8_t> seq = WriteByteSequence(kAddress, kValue);

    SMUCNvram whole;
    for (uint8_t v : seq)
        whole.WriteSerialLink(v);
    ASSERT_EQ(whole.GetEEPROMByte(kAddress), kValue) << "the uninterrupted write lands";

    for (size_t cut = 1; cut < seq.size(); ++cut)
    {
        SMUCNvram first;
        for (size_t i = 0; i < cut; ++i)
            first.WriteSerialLink(seq[i]);
        SMUCNvram resumed;   // a fresh device: only the link state carries over (nothing committed before STOP)
        resumed.SetLinkState(first.GetLinkState());
        for (size_t i = cut; i < seq.size(); ++i)
            resumed.WriteSerialLink(seq[i]);
        ASSERT_EQ(resumed.GetEEPROMByte(kAddress), kValue) << "cut after write " << cut;
        const SMUCNvram::LinkState a = whole.GetLinkState();
        const SMUCNvram::LinkState b = resumed.GetLinkState();
        ASSERT_EQ(std::memcmp(&a, &b, sizeof(a)), 0) << "cut after write " << cut;
    }
}
