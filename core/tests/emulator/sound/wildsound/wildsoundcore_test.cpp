// The Wild Sound card's bus side on its own (wildsoundcore.h; docs/inprogress/2026-10-09-wildsound/design.md §11:
// the read pipeline, chip select, unlock, identification, the status frame and its CRC, packets, the upload sequence
// of the author's loaders and the storage limits)

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "emulator/sound/wildsound/wildsoundcore.h"

using wildsound::WildSoundCore;
using wildsound::WildSoundEvent;
using wildsound::WildSoundMode;
using wildsound::WildSoundStorage;

namespace
{

/// The host side as the author's Z80 loaders drive the card (XMPLOAD.AZ8 / prosiak.exe): register select, data,
/// reads, the 32-byte status frame with the Z80's own CRC check
class Host
{
public:
    explicit Host(WildSoundCore& card) : _card(card) {}

    void Select(uint8_t reg) { _card.WriteAddress(reg, ++_t); }
    void Data(uint8_t v) { _card.WriteData(v, ++_t); }
    uint8_t In() { return _card.Read(++_t); }
    void Write(uint8_t reg, uint8_t v)
    {
        Select(reg);
        Data(v);
    }

    /// #F3, 32 reads; true when bytes 29 / 30 are FA 3D and byte 31 is the CRC the Z80 computes (XMPLOAD.AZ8:155-214)
    bool Status(std::vector<uint8_t>& frame)
    {
        Select(0xF3);
        frame.resize(32);
        for (uint8_t& b : frame)
            b = In();
        uint8_t r = 0;
        for (uint8_t p = 0; p < 31; ++p)
        {
            const uint8_t b = frame[p];
            switch (p & 3)
            {
                case 0: r ^= static_cast<uint8_t>(b + p); break;
                case 1: r ^= static_cast<uint8_t>(b - p); break;
                case 2: r = static_cast<uint8_t>(r - static_cast<uint8_t>(b + p)); break;
                default: r = static_cast<uint8_t>(r + static_cast<uint8_t>(b - p)); break;
            }
        }
        return frame[29] == 0xFA && frame[30] == 0x3D && frame[31] == r;
    }

    void Command(uint16_t cmd, uint32_t size, bool withSize = true)
    {
        Select(0xF1);
        Data(static_cast<uint8_t>(cmd));
        Data(static_cast<uint8_t>(cmd >> 8));
        if (withSize)
            for (int i = 0; i < 4; ++i)
                Data(static_cast<uint8_t>(size >> (8 * i)));
    }

    /// The loader's flow control: packets no longer than the free FIFO space the frame reports, then an empty
    /// packet that closes the command
    void Send(const std::vector<uint8_t>& data)
    {
        size_t at = 0;
        std::vector<uint8_t> frame;
        while (at < data.size())
        {
            ASSERT_TRUE(Status(frame));
            ASSERT_EQ(frame[1] & 0x80, 0) << "busy";
            const size_t n = std::min<size_t>(frame[10], data.size() - at);
            ASSERT_GT(n, 0u);
            Select(0xF2);
            Data(static_cast<uint8_t>(n));
            for (size_t i = 0; i < n; ++i)
                Data(data[at + i]);
            at += n;
        }
        Packet({});
    }

    void Packet(const std::vector<uint8_t>& bytes)
    {
        Select(0xF2);
        Data(static_cast<uint8_t>(bytes.size()));
        for (uint8_t b : bytes)
            Data(b);
    }

    uint64_t Time() const { return _t; }

private:
    WildSoundCore& _card;
    uint64_t _t = 0;
};

uint32_t Le32(const std::vector<uint8_t>& f, size_t at)
{
    return f[at] | (f[at + 1] << 8) | (f[at + 2] << 16) | (static_cast<uint32_t>(f[at + 3]) << 24);
}

void Put16(std::vector<uint8_t>& v, size_t at, uint16_t x)
{
    v[at] = static_cast<uint8_t>(x);
    v[at + 1] = static_cast<uint8_t>(x >> 8);
}

void Put32(std::vector<uint8_t>& v, size_t at, uint32_t x)
{
    Put16(v, at, static_cast<uint16_t>(x));
    Put16(v, at + 2, static_cast<uint16_t>(x >> 16));
}

/// A one-pattern track pack (reference-firmware.md §C.1): `channels` channels, `rows` rows, every row a C-4 of
/// instrument 1 on channel 0 and empty on the others
std::vector<uint8_t> MakePack(uint16_t channels = 4, uint16_t rows = 4, uint16_t songLength = 1)
{
    std::vector<uint8_t> rowsData;
    for (int r = 0; r < rows; ++r)
    {
        rowsData.insert(rowsData.end(), {0x83, 49, 1});  // note + instrument
        for (int c = 1; c < channels; ++c)
            rowsData.push_back(0x80);
    }
    const uint32_t header = WildSoundStorage::kHeaderSize + WildSoundStorage::kPatternRecordSize;
    std::vector<uint8_t> pack(header, 0);
    Put32(pack, 0, static_cast<uint32_t>(rowsData.size()));
    std::memcpy(pack.data() + 4, "Test", 4);
    Put16(pack, 24, channels);
    Put16(pack, 26, 6);
    Put16(pack, 28, 125);
    Put16(pack, 30, 1);
    Put16(pack, 32, songLength);
    Put16(pack, 34, 0);
    Put32(pack, WildSoundStorage::kHeaderSize, header);
    Put32(pack, WildSoundStorage::kHeaderSize + 4, static_cast<uint32_t>(rowsData.size()));
    Put16(pack, WildSoundStorage::kHeaderSize + 8, rows);
    pack.insert(pack.end(), rowsData.begin(), rowsData.end());
    return pack;
}

/// `count` instrument records of `length`-byte samples, back to back in the sample stream
std::vector<uint8_t> MakeTable(int count, uint32_t length)
{
    std::vector<uint8_t> t(count * WildSoundStorage::kRecordSize, 0);
    for (int i = 0; i < count; ++i)
    {
        const size_t at = i * WildSoundStorage::kRecordSize;
        Put32(t, at, static_cast<uint32_t>(t.size() + i * length));
        Put32(t, at + 4, length);
        Put32(t, at + 8, 0);
        Put32(t, at + 12, length);
        t[at + 16] = 64;
        t[at + 18] = 1;
        t[at + 19] = 0x80;
    }
    return t;
}

}  // namespace

TEST(WildSoundCore_Test, ReadReturnsTheBytePreparedByThePreviousEvent)
{
    WildSoundCore card;
    Host host(card);
    host.Write(0x07, 0x38);
    host.Write(0x20, 0x5A);
    host.Select(0x07);
    EXPECT_EQ(host.In(), 0x38);  // AY register, raw
    host.Select(0x20);
    EXPECT_EQ(host.In(), 0x5A);  // shared echo area
    host.Write(0x01, 0xFF);
    host.Select(0x01);
    EXPECT_EQ(host.In(), 0xFF) << "no AY masking on the card";
}

TEST(WildSoundCore_Test, ChipSelectKeepsEachChipsLatchAndRegisters)
{
    WildSoundCore card;
    Host host(card);
    host.Write(0x08, 0x0F);  // chip 0, volume A
    host.Select(0xFE);       // chip 1
    EXPECT_EQ(card.SelectedChip(), 1);
    host.Write(0x08, 0x05);
    host.Select(0xFF);
    EXPECT_EQ(card.SelectedChip(), 0);
    EXPECT_EQ(card.ChipRegister(0, 8), 0x0F);
    EXPECT_EQ(card.ChipRegister(1, 8), 0x05);
    host.Select(0x08);
    EXPECT_EQ(host.In(), 0x0F);

    // The writes reach the audio side with their chip
    int writes = 0;
    for (const WildSoundEvent& e : card.Events())
        if (e.kind == WildSoundEvent::AyWrite && e.reg == 8)
            writes += e.chip == 0 ? (e.value == 0x0F) : (e.value == 0x05);
    EXPECT_EQ(writes, 2);
}

TEST(WildSoundCore_Test, UnlockAnswersDfThenTheVersionString)
{
    WildSoundCore card;
    Host host(card);
    host.Select(0xF0);
    for (const char* c = WildSoundCore::kLockString; *c; ++c)
        host.Data(static_cast<uint8_t>(*c));
    host.Data(0x00);  // mode byte
    host.Data(0x00);
    EXPECT_TRUE(card.Unlocked());
    EXPECT_EQ(host.In(), 0xDF);
    std::string version;
    for (int i = 0; i < 32; ++i)
    {
        const uint8_t b = host.In();
        if (!b)
            break;
        version.push_back(static_cast<char>(b));
    }
    EXPECT_EQ(version, WildSoundCore::kVersionString);
    EXPECT_EQ(host.In(), 0x00) << "the pointer stops at the terminator";
}

TEST(WildSoundCore_Test, WrongUnlockStaysLocked)
{
    WildSoundCore card;
    Host host(card);
    host.Select(0xF0);
    for (const char* c = "Wild Sound Enabl!"; *c; ++c)
        host.Data(static_cast<uint8_t>(*c));
    host.Data(0x00);
    host.Data(0x00);
    EXPECT_FALSE(card.Unlocked());
    EXPECT_NE(host.In(), 0xDF);

    // A non-zero last byte also keeps the card locked
    host.Select(0xF0);
    for (const char* c = WildSoundCore::kLockString; *c; ++c)
        host.Data(static_cast<uint8_t>(*c));
    host.Data(0x00);
    host.Data(0x01);
    EXPECT_FALSE(card.Unlocked());
}

TEST(WildSoundCore_Test, IdentificationRegisters)
{
    WildSoundCore card;
    Host host(card);
    host.Select(0xE0);
    EXPECT_EQ(host.In(), 0x55);
    EXPECT_EQ(host.In(), 0xAA);
    EXPECT_EQ(host.In(), 0x55);

    host.Select(0xED);
    std::string s;
    for (uint8_t b = host.In(); b; b = host.In())
        s.push_back(static_cast<char>(b));
    EXPECT_EQ(s, WildSoundCore::kCopyrightString);

    host.Select(0xEC);
    uint8_t v[6];
    for (uint8_t& b : v)
        b = host.In();
    EXPECT_EQ(v[0] | (v[1] << 8), 12104);
    EXPECT_EQ(v[4] | (v[5] << 8), 15931);

    host.Select(0xEF);
    uint32_t clk = 0;
    for (int i = 0; i < 4; ++i)
        clk |= static_cast<uint32_t>(host.In()) << (8 * i);
    EXPECT_EQ(clk, WildSoundCore::kCoreClockHz);
}

TEST(WildSoundCore_Test, StatusFrameLayoutAndCrc)
{
    WildSoundCore card;
    Host host(card);
    host.Command(WildSoundCore::kCmdXmAStart, 0x12345);
    std::vector<uint8_t> f;
    ASSERT_TRUE(host.Status(f));
    EXPECT_EQ(f[0] | (f[1] << 8), WildSoundCore::kCmdXmAStart);
    EXPECT_EQ(Le32(f, 2), 0x12345u);
    EXPECT_EQ(Le32(f, 6), 0u);
    EXPECT_EQ(f[10], 127);
    EXPECT_EQ(f[11], 0);

    // The frames repeat every 32 reads, each valid
    std::vector<uint8_t> next(32);
    for (uint8_t& b : next)
        b = host.In();
    EXPECT_EQ(next[29], 0xFA);
    EXPECT_EQ(next[31], f[31]);

    // #F1 read gives the same frame (it falls through to #F3)
    host.Select(0xF1);
    std::vector<uint8_t> viaF1(32);
    for (uint8_t& b : viaF1)
        b = host.In();
    EXPECT_EQ(viaF1, f);
    EXPECT_EQ(card.SelectedRegister(), 0xF3);
}

TEST(WildSoundCore_Test, PacketOverflowIsDroppedLikeTheCard)
{
    WildSoundCore card;
    Host host(card);
    host.Command(WildSoundCore::kCmdXmBData, 200);
    host.Packet(std::vector<uint8_t>(200 & 0xFF, 0x80));  // 200 bytes into a 127-byte FIFO
    std::vector<uint8_t> f;
    ASSERT_TRUE(host.Status(f));
    EXPECT_EQ(Le32(f, 6), 127u) << "only what fitted was consumed";
    EXPECT_EQ(f[1] & 0x80, 0) << "never seen busy";
}

TEST(WildSoundCore_Test, UploadSequenceOfTheLoadersMakesTheCardPlayable)
{
    WildSoundCore card;
    Host host(card);
    // XMPLOAD.AZ8: NOP, CLEAR, ERASE, then the three streams and DIGPLAY, each command closed by an empty packet
    for (uint16_t c : {WildSoundCore::kCmdNop, WildSoundCore::kCmdClear, WildSoundCore::kCmdErase})
    {
        host.Command(c, 0, false);
        host.Packet({});
    }
    const std::vector<uint8_t> pack = MakePack();
    host.Command(WildSoundCore::kCmdXmAStart, static_cast<uint32_t>(pack.size()));
    host.Send(pack);
    std::vector<uint8_t> f;
    ASSERT_TRUE(host.Status(f));
    EXPECT_EQ(Le32(f, 6), pack.size()) << "POS = SIZE";
    ASSERT_TRUE(card.Storage().Track().valid) << card.Storage().Error();
    EXPECT_EQ(card.Storage().Track().name, "Test");
    EXPECT_EQ(card.Storage().Track().channels, 4);

    const std::vector<uint8_t> table = MakeTable(2, 300);
    host.Command(WildSoundCore::kCmdXmBStart, static_cast<uint32_t>(table.size()));
    host.Send(table);
    ASSERT_EQ(card.Storage().Instruments().size(), 2u);

    std::vector<uint8_t> samples(600);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<uint8_t>(i < 300 ? 0x90 : 0x70);
    // Two XMBDO commands: the sample pointer carries over (the loader sends one per 32 KB chunk)
    host.Command(WildSoundCore::kCmdXmBData, 250);
    host.Send(std::vector<uint8_t>(samples.begin(), samples.begin() + 250));
    host.Command(WildSoundCore::kCmdXmBData, 350);
    host.Send(std::vector<uint8_t>(samples.begin() + 250, samples.end()));
    EXPECT_TRUE(card.Storage().InstrumentsComplete());
    const auto& ins = card.Storage().Instruments();
    EXPECT_EQ(card.Storage().SampleData(ins[0])[299], 0x90);
    EXPECT_EQ(card.Storage().SampleData(ins[1])[0], 0x70);

    card.Events().clear();
    host.Command(WildSoundCore::kCmdDigPlay, 0, false);
    host.Packet({});
    EXPECT_EQ(card.Mode(), WildSoundMode::Xm);
    ASSERT_FALSE(card.Events().empty());
    EXPECT_EQ(card.Events().back().kind, WildSoundEvent::PlayXm);

    host.Command(WildSoundCore::kCmdClear, 0, false);
    host.Packet({});
    EXPECT_EQ(card.Mode(), WildSoundMode::Ay);
    EXPECT_EQ(card.Events().back().kind, WildSoundEvent::StopXm);
}

TEST(WildSoundCore_Test, DigPlayWithoutTrackIsRefused)
{
    WildSoundCore card;
    Host host(card);
    host.Command(WildSoundCore::kCmdDigPlay, 0, false);
    host.Packet({});
    EXPECT_EQ(card.Mode(), WildSoundMode::Ay);
    EXPECT_FALSE(card.LastError().empty());
}

TEST(WildSoundCore_Test, BadPackIsRefused)
{
    WildSoundCore card;
    Host host(card);
    std::vector<uint8_t> pack = MakePack();
    Put32(pack, WildSoundStorage::kHeaderSize, 1000);  // pattern record 0 not at the header's end
    host.Command(WildSoundCore::kCmdXmAStart, static_cast<uint32_t>(pack.size()));
    host.Send(pack);
    EXPECT_FALSE(card.Storage().Track().valid);
    EXPECT_FALSE(card.LastError().empty());
}

TEST(WildSoundCore_Test, StorageLimits)
{
    WildSoundCore card;
    Host host(card);
    // 64 descriptors: without a track the table, instrument 0 and 62 instruments fit; the 63rd fails
    const std::vector<uint8_t> table = MakeTable(63, 16);
    host.Command(WildSoundCore::kCmdXmBStart, static_cast<uint32_t>(table.size()));
    host.Send(table);
    EXPECT_FALSE(card.LastError().empty());
    EXPECT_EQ(card.Storage().DescriptorsUsed(), WildSoundStorage::kDescriptors);

    // A track bigger than the store is refused and consumes nothing
    host.Command(WildSoundCore::kCmdErase, 0, false);
    host.Packet({});
    host.Command(WildSoundCore::kCmdXmAStart, WildSoundStorage::kFlashSize + 1);
    host.Packet({1, 2, 3});
    std::vector<uint8_t> f;
    ASSERT_TRUE(host.Status(f));
    EXPECT_EQ(Le32(f, 6), 0u);
}

TEST(WildSoundCore_Test, ResetForgetsUploadsAndLocks)
{
    WildSoundCore card;
    Host host(card);
    const std::vector<uint8_t> pack = MakePack();
    host.Command(WildSoundCore::kCmdXmAStart, static_cast<uint32_t>(pack.size()));
    host.Send(pack);
    ASSERT_TRUE(card.Storage().Track().valid);
    card.Reset(host.Time());
    EXPECT_FALSE(card.Storage().Track().valid);
    EXPECT_EQ(card.Storage().DescriptorsUsed(), 0u);
    EXPECT_FALSE(card.Unlocked());
    EXPECT_EQ(card.ChipRegister(0, 0x18), 0x80);
}

TEST(WildSoundCore_Test, StateRoundTrip)
{
    WildSoundCore a;
    Host host(a);
    const std::vector<uint8_t> pack = MakePack(8, 16);
    host.Command(WildSoundCore::kCmdXmAStart, static_cast<uint32_t>(pack.size()));
    host.Send(pack);
    const std::vector<uint8_t> table = MakeTable(3, 40);
    host.Command(WildSoundCore::kCmdXmBStart, static_cast<uint32_t>(table.size()));
    host.Send(table);
    host.Command(WildSoundCore::kCmdXmBData, 70);
    host.Packet(std::vector<uint8_t>(70, 0x81));  // instrument 1 complete, instrument 2 half
    host.Write(0x05, 0x0C);

    std::vector<uint8_t> state(WildSoundCore::kStateSize);
    a.SaveState(state.data());
    WildSoundCore b;
    b.Storage().Flash() = a.Storage().Flash();
    b.LoadState(state.data());

    std::vector<uint8_t> again(WildSoundCore::kStateSize);
    b.SaveState(again.data());
    EXPECT_EQ(state, again);
    EXPECT_TRUE(b.Storage().Track().valid);
    EXPECT_EQ(b.Storage().Track().channels, 8);
    ASSERT_EQ(b.Storage().Instruments().size(), 3u);
    EXPECT_EQ(b.Storage().Instruments()[0].received, 40u);
    EXPECT_EQ(b.Storage().Instruments()[1].received, 30u);
    EXPECT_EQ(b.ChipRegister(0, 5), 0x0C);
    EXPECT_EQ(b.Peek(), a.Peek());
}
