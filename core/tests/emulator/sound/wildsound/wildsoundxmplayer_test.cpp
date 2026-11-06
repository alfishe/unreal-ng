// The Wild Sound card's XM player (wildsoundxmplayer.h; docs/inprogress/2026-10-09-wildsound/design.md §5, §11):
// row timing, pitch on both frequency tables, key-off, position jump, volume slide, the state round trip

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "emulator/sound/wildsound/wildsoundstorage.h"
#include "emulator/sound/wildsound/wildsoundxmplayer.h"

using wildsound::WildSoundStorage;
using wildsound::WildSoundXmPlayer;

namespace
{

constexpr uint32_t kRate = 44100;

struct Cell
{
    uint8_t note = 0, ins = 0, vol = 0, eff = 0, param = 0;
};

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

/// One-channel patterns (each a list of rows) and an order list; `linear` sets the frequency table
void UploadTrack(WildSoundStorage& st, const std::vector<std::vector<Cell>>& patterns, const std::vector<uint8_t>& order,
                 bool linear = false, uint16_t speed = 6, uint16_t bpm = 125)
{
    std::vector<std::vector<uint8_t>> packed;
    for (const auto& rows : patterns)
    {
        std::vector<uint8_t> p;
        for (const Cell& c : rows)
            p.insert(p.end(), {c.note, c.ins, c.vol, c.eff, c.param});  // unpacked form: note byte, then 4
        packed.push_back(p);
    }
    const uint32_t header = WildSoundStorage::kHeaderSize + WildSoundStorage::kPatternRecordSize * patterns.size();
    std::vector<uint8_t> pack(header, 0);
    Put16(pack, 24, 1);
    Put16(pack, 26, speed);
    Put16(pack, 28, bpm);
    Put16(pack, 30, linear ? 1 : 0);
    Put16(pack, 32, static_cast<uint16_t>(order.size()));
    for (size_t i = 0; i < order.size(); ++i)
        pack[36 + i] = order[i];
    uint32_t at = header;
    for (size_t p = 0; p < packed.size(); ++p)
    {
        const size_t rec = WildSoundStorage::kHeaderSize + p * WildSoundStorage::kPatternRecordSize;
        Put32(pack, rec, at);
        Put32(pack, rec + 4, static_cast<uint32_t>(packed[p].size()));
        Put16(pack, rec + 8, static_cast<uint16_t>(patterns[p].size()));
        at += static_cast<uint32_t>(packed[p].size());
    }
    for (const auto& p : packed)
        pack.insert(pack.end(), p.begin(), p.end());
    Put32(pack, 0, at - header);
    ASSERT_TRUE(st.BeginTrack(static_cast<uint32_t>(pack.size())));
    st.AppendTrack(pack.data(), pack.size());
    ASSERT_TRUE(st.EndTrack()) << st.Error();
}

/// Instrument 1: a 32-byte square wave looped forward (one period = 32 samples: C-4 plays 8363 / 32 = 261.3 Hz)
void UploadSquare(WildSoundStorage& st)
{
    std::vector<uint8_t> table(WildSoundStorage::kRecordSize, 0);
    Put32(table, 0, WildSoundStorage::kRecordSize);
    Put32(table, 4, 32);
    Put32(table, 8, 0);
    Put32(table, 12, 32);
    table[16] = 64;
    table[18] = 1;
    table[19] = 0x80;
    ASSERT_TRUE(st.BeginInstruments(static_cast<uint32_t>(table.size())));
    st.AppendInstruments(table.data(), table.size());
    ASSERT_TRUE(st.EndInstruments()) << st.Error();
    for (int i = 0; i < 32; ++i)
        st.PutSampleByte(static_cast<uint8_t>(i < 16 ? 0x80 + 100 : 0x80 - 100));
}

std::vector<int16_t> Render(WildSoundXmPlayer& player, const WildSoundStorage& st, size_t frames)
{
    std::vector<int16_t> out(frames * 2);
    // In frame-sized pieces, as the mixer asks
    for (size_t done = 0; done < frames; done += 882)
        player.Render(st, out.data() + done * 2, std::min<size_t>(882, frames - done));
    return out;
}

double LeftHz(const std::vector<int16_t>& s, size_t from, size_t frames)
{
    int crossings = 0;
    for (size_t i = from + 1; i < from + frames; ++i)
        if ((s[2 * (i - 1)] < 0) != (s[2 * i] < 0))
            ++crossings;
    return crossings / 2.0 * kRate / frames;
}

double Rms(const std::vector<int16_t>& s, size_t from, size_t frames)
{
    double sum = 0;
    for (size_t i = from; i < from + frames; ++i)
        sum += double(s[2 * i]) * s[2 * i];
    return std::sqrt(sum / frames);
}

std::vector<Cell> Rows(size_t n, Cell first = {49, 1, 0, 0, 0})
{
    std::vector<Cell> rows(n);
    rows[0] = first;
    return rows;
}

}  // namespace

TEST(WildSoundXmPlayer_Test, RowTimingFollowsSpeedAndBpm)
{
    WildSoundStorage st;
    UploadTrack(st, {Rows(64)}, {0});
    UploadSquare(st);
    WildSoundXmPlayer player;
    player.SetOutputRate(kRate);
    player.Start(st);
    Render(player, st, kRate);
    // speed 6, BPM 125: a row is 6 x 882 = 5292 samples; rows start at 0, 5292, ... 42336: 9 in one second
    EXPECT_EQ(player.GetState().rows, 9u);
}

TEST(WildSoundXmPlayer_Test, PitchOnBothTables)
{
    for (bool linear : {false, true})
    {
        WildSoundStorage st;
        UploadTrack(st, {Rows(64)}, {0}, linear);
        UploadSquare(st);
        WildSoundXmPlayer player;
        player.SetOutputRate(kRate);
        player.Start(st);
        const auto out = Render(player, st, kRate / 2);
        EXPECT_NEAR(LeftHz(out, 2000, 16000), 8363.0 / 32, 3.0) << (linear ? "linear" : "amiga");
    }
    EXPECT_EQ(WildSoundXmPlayer::NotePeriod(true, 49, 0), 4608);
    EXPECT_EQ(WildSoundXmPlayer::NotePeriod(false, 49, 0), 1712);
    EXPECT_NEAR(WildSoundXmPlayer::PeriodToHz(false, WildSoundXmPlayer::NotePeriod(false, 61, 0)), 2 * 8363.0, 10);
}

TEST(WildSoundXmPlayer_Test, KeyOffCutsANoteWithoutEnvelope)
{
    WildSoundStorage st;
    std::vector<Cell> rows = Rows(64);
    rows[1] = {97, 0, 0, 0, 0};
    UploadTrack(st, {rows}, {0});
    UploadSquare(st);
    WildSoundXmPlayer player;
    player.SetOutputRate(kRate);
    player.Start(st);
    const auto out = Render(player, st, 3 * 5292);
    EXPECT_GT(Rms(out, 1000, 3000), 1000.0);
    EXPECT_LT(Rms(out, 5292 + 200, 4000), 1.0) << "row 1 keys off";
}

TEST(WildSoundXmPlayer_Test, PositionJumpGoesToTheNamedOrder)
{
    WildSoundStorage st;
    std::vector<Cell> p1 = Rows(4, {49, 1, 0, 0x0B, 0x00});  // B00 on row 0 of the second order
    UploadTrack(st, {Rows(4), p1}, {0, 1, 0, 1});
    UploadSquare(st);
    WildSoundXmPlayer player;
    player.SetOutputRate(kRate);
    player.Start(st);
    // Order 0 (4 rows), order 1 row 0 with B00, then order 0 again (not 1: the firmware's off-by-one)
    Render(player, st, 5 * 5292 + 100);
    EXPECT_EQ(player.GetState().order, 0);
    EXPECT_EQ(player.GetState().row, 0);
}

TEST(WildSoundXmPlayer_Test, VolumeSlideFadesTheNote)
{
    WildSoundStorage st;
    std::vector<Cell> rows = Rows(64);
    for (size_t r = 1; r < rows.size(); ++r)
        rows[r] = {0, 0, 0, 0x0A, 0x0F};
    UploadTrack(st, {rows}, {0});
    UploadSquare(st);
    WildSoundXmPlayer player;
    player.SetOutputRate(kRate);
    player.Start(st);
    const auto out = Render(player, st, 6 * 5292);
    EXPECT_GT(Rms(out, 1000, 3000), 4 * (Rms(out, 3 * 5292, 3000) + 1));
}

TEST(WildSoundXmPlayer_Test, StateRoundTripRendersTheSame)
{
    WildSoundStorage st;
    std::vector<Cell> rows = Rows(16);
    rows[2] = {61, 1, 0, 0x04, 0x48};  // vibrato
    rows[6] = {56, 1, 0x30, 0x03, 0x10};  // tone portamento
    UploadTrack(st, {rows}, {0});
    UploadSquare(st);
    WildSoundXmPlayer a;
    a.SetOutputRate(kRate);
    a.Start(st);
    Render(a, st, 3 * 5292 + 77);
    std::vector<uint8_t> state(WildSoundXmPlayer::StateSize());
    a.SaveState(state.data());
    WildSoundXmPlayer b;
    b.LoadState(state.data());
    const auto ra = Render(a, st, 4 * 5292);
    const auto rb = Render(b, st, 4 * 5292);
    EXPECT_EQ(ra, rb);
}
