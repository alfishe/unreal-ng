// The NeoGS ZX-DMA trace report (gszxdmatrace.h, gs-debugger N6): on an emulated Pentagon the host moves bytes with
// LDIR through #0000-#3FFF while the GS port trace captures; the report shows one row a byte with the host's time,
// the card's, the address, the value and the wait. The module's arithmetic (waits, drops) is in neogszxdma_test.cpp.

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/sound/chips/gs/gszxdmatrace.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicecatalog.h"

namespace
{
constexpr uint32_t kCardBase = 0x013FF8; // the block crosses the page boundary #014000
constexpr uint16_t kHostBuffer = 0x9000;
constexpr uint16_t kHostCode = 0x8000;
constexpr uint16_t kHostIdle = 0x8100;

uint8_t CardByte(int i)
{
    return static_cast<uint8_t>(i * 7 + 3);
}

const StateNode& Field(const StateNode& node, const char* key)
{
    static const StateNode none;
    const StateNode* v = node.find(key);
    return v ? *v : none;
}

class GSZxDmaTrace_Test : public ::testing::Test
{
protected:
    SoundCardScope _gs{TestSound::GeneralSound};
    Emulator* _emulator = nullptr;
    EmulatorContext* _ctx = nullptr;
    Z80* _z80 = nullptr;
    SoundChip_NeoGS* _card = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON");
        ASSERT_NE(_emulator, nullptr);
        _ctx = _emulator->GetContext();
        _z80 = _ctx->pCore->GetZ80();
        ASSERT_TRUE(FitGeneralSoundCard(_ctx->pSoundManager, GSTypeKind::NGS));
        _card = dynamic_cast<SoundChip_NeoGS*>(_ctx->pSoundManager->getGeneralSound());
        ASSERT_NE(_card, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void frame() { reinterpret_cast<MainLoop_CUT*>(_ctx->pMainLoop)->RunFrame(); }

    void hostWrite(uint16_t at, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            _z80->DirectWrite(at++, b);
    }

    /// The card selects the ZX module at kCardBase and starts it; the host idles two frames (the card's frame base)
    void startModule()
    {
        const std::vector<uint8_t> program = {
            0xF3,                                       // DI
            0x3E, 0x01, 0xD3, 0x1B,                     // DMA_MOD = 1 (ZX)
            0x3E, uint8_t(kCardBase >> 16), 0xD3, 0x1C, // HAD
            0x3E, uint8_t(kCardBase >> 8), 0xD3, 0x1D,  // MAD
            0x3E, uint8_t(kCardBase), 0xD3, 0x1E,       // LAD
            0x3E, 0x80, 0xD3, 0x1F,                     // CST = #80: start
            0x18, 0xFE};                                // JR $
        _card->flash().load(program.data(), program.size());
        _card->reset();
        for (int i = 0; i < 64; i++)
            _card->memory().ram()[kCardBase + i] = CardByte(i);
        hostWrite(kHostIdle, {0xF3, 0x18, 0xFE}); // DI : JR $
        _z80->pc = kHostIdle;
        _z80->iff1 = _z80->iff2 = 0;
        frame();
        frame();
        ASSERT_EQ(_card->zxDma().mode(), NeoGSZxDma::Mode::Divert);
    }

    /// LDIR of `count` bytes from `from` to `to`, then JR $; run until the host parks there and one more frame
    void ldir(uint16_t from, uint16_t to, uint16_t count)
    {
        hostWrite(kHostCode, {0x21, uint8_t(from), uint8_t(from >> 8), 0x11, uint8_t(to), uint8_t(to >> 8), 0x01,
                              uint8_t(count), uint8_t(count >> 8), 0xED, 0xB0, 0x18, 0xFE});
        _z80->pc = kHostCode;
        for (int guard = 0; guard < 10 && _z80->pc != kHostCode + 11; guard++)
            frame();
        ASSERT_EQ(_z80->pc, kHostCode + 11);
        frame();
    }

    StateNode report(size_t last = 256, uint64_t since = 0)
    {
        GSZxDmaTraceQuery query;
        query.last = last;
        query.since = since;
        return DeviceState::GsZxDmaTrace(_ctx, query);
    }
};
} // namespace

TEST_F(GSZxDmaTrace_Test, ReadRowsCarryTheHostTimeTheCardAddressAndTheWait)
{
    startModule();
    _card->startPortTrace();
    constexpr uint16_t n = 16;
    ldir(0x0000, kHostBuffer, n + 1); // a dummy read first: each read returns the byte the previous one fetched

    const StateNode r = report();
    ASSERT_TRUE(Field(r, "available").b);
    EXPECT_TRUE(Field(r, "capturing").b);
    EXPECT_EQ(Field(r, "total_bytes_kept").i, n + 1);
    const StateNode& rows = Field(r, "rows");
    ASSERT_EQ(rows.size(), n + 1u);
    const int64_t frameT = static_cast<int64_t>(_ctx->GetFrameTStates());
    int64_t waits = 0;
    for (size_t k = 0; k < rows.size(); k++)
    {
        const StateNode& row = rows.items[k];
        EXPECT_EQ(Field(row, "direction").s, "read");
        EXPECT_EQ(Field(row, "card_address").i, static_cast<int64_t>(kCardBase + k)) << "consecutive card addresses";
        EXPECT_EQ(Field(row, "page").i, static_cast<int64_t>((kCardBase + k) >> 14));
        if (k > 0)
        {
            EXPECT_EQ(Field(row, "value").i, CardByte(static_cast<int>(k) - 1)) << "row " << k;
            EXPECT_GT(Field(row, "seq").i, Field(rows.items[k - 1], "seq").i);
            const bool sameFrame = Field(row, "frame").i == Field(rows.items[k - 1], "frame").i;
            if (sameFrame)
                EXPECT_GT(Field(row, "host_t").i, Field(rows.items[k - 1], "host_t").i) << "the host's time moves on";
            EXPECT_GE(Field(row, "card_time").i, Field(rows.items[k - 1], "card_time").i);
        }
        EXPECT_GE(Field(row, "host_t").i, 0);
        EXPECT_LT(Field(row, "host_t").i, frameT) << "a T-state inside the frame";
        EXPECT_EQ(Field(row, "host_pc").i, kHostCode + 9) << "the LDIR";
        EXPECT_FALSE(Field(row, "dropped").b);
        waits += Field(row, "wait").i;
    }
    EXPECT_EQ(Field(rows.items[0], "value").i, 0xFF) << "the first read gets the old latch";
    EXPECT_NE(Field(rows.items[9], "page").i, Field(rows.items[7], "page").i) << "the block crosses a page";
    EXPECT_EQ(Field(Field(r, "counters"), "bytes_read").i, n + 1);
    EXPECT_EQ(waits, Field(Field(r, "counters"), "wait_t_states").i) << "the rows' waits are the module's total";
    EXPECT_EQ(static_cast<uint64_t>(waits), _card->zxDma().waitTStates());

    // The text: a header, a line a byte
    const std::string text = Field(r, "text").s;
    EXPECT_NE(text.find("ZX-DMA trace"), std::string::npos) << text;
    EXPECT_NE(text.find("read  #013FF9"), std::string::npos) << text;
    EXPECT_NE(text.find("PC #8009"), std::string::npos) << text;
}

TEST_F(GSZxDmaTrace_Test, AWriteRowCarriesTheHostsByte)
{
    startModule();
    for (int i = 0; i < 4; i++)
        _z80->DirectWrite(static_cast<uint16_t>(kHostBuffer + i), static_cast<uint8_t>(0xA0 + i));
    _card->startPortTrace();
    ldir(kHostBuffer, 0x0000, 4);

    const StateNode trace = report();
    const StateNode& rows = Field(trace, "rows");
    ASSERT_EQ(rows.size(), 4u);
    for (size_t k = 0; k < 4; k++)
    {
        EXPECT_EQ(Field(rows.items[k], "direction").s, "write");
        EXPECT_EQ(Field(rows.items[k], "value").i, 0xA0 + static_cast<int>(k));
        EXPECT_EQ(Field(rows.items[k], "card_address").i, static_cast<int64_t>(kCardBase + k));
    }
    EXPECT_EQ(_card->memory().ram()[kCardBase + 3], 0xA3);
}

TEST_F(GSZxDmaTrace_Test, NothingIsRecordedWhileTheTraceDoesNotCapture)
{
    startModule();
    ldir(0x0000, kHostBuffer, 8);
    const StateNode r = report();
    ASSERT_TRUE(Field(r, "available").b);
    EXPECT_FALSE(Field(r, "capturing").b);
    EXPECT_EQ(Field(r, "rows").size(), 0u);
    EXPECT_EQ(Field(Field(r, "counters"), "bytes_read").i, 8) << "the module moved them all the same";
    EXPECT_NE(Field(r, "how_to_start").s.find("porttrace"), std::string::npos);
    EXPECT_NE(Field(r, "text").s.find("not capturing"), std::string::npos) << Field(r, "text").s;
}

TEST_F(GSZxDmaTrace_Test, LastAndSinceSelectTheRows)
{
    startModule();
    _card->startPortTrace();
    ldir(0x0000, kHostBuffer, 10);

    const StateNode all = report(0);
    const StateNode& rows = Field(all, "rows");
    ASSERT_EQ(rows.size(), 10u);
    const StateNode tail = report(3);
    ASSERT_EQ(Field(tail, "rows").size(), 3u);
    EXPECT_EQ(Field(Field(tail, "rows").items[0], "seq").i, Field(rows.items[7], "seq").i) << "the newest three";
    EXPECT_EQ(Field(tail, "total_bytes_kept").i, 10);

    // A poller: the rows after the last seq it saw, then none until new bytes come
    const StateNode after = report(0, static_cast<uint64_t>(Field(rows.items[5], "seq").i));
    ASSERT_EQ(Field(after, "rows").size(), 4u);
    EXPECT_EQ(Field(Field(after, "rows").items[0], "card_address").i, Field(rows.items[6], "card_address").i);
    const uint64_t lastSeq = static_cast<uint64_t>(Field(all, "last_seq").i);
    EXPECT_GE(lastSeq, static_cast<uint64_t>(Field(rows.items[9], "seq").i));
    EXPECT_EQ(Field(report(0, lastSeq), "rows").size(), 0u);
    EXPECT_FALSE(Field(report(0, lastSeq), "restarted").b);

    // A seq past the history's: a new session, read from the start
    const StateNode fresh = report(0, lastSeq + 1000);
    EXPECT_TRUE(Field(fresh, "restarted").b);
    EXPECT_EQ(Field(fresh, "rows").size(), 10u);
}

TEST_F(GSZxDmaTrace_Test, AWaitAndADroppedByteShowInTheRows)
{
    // The packed host side and the drop flag as the report decodes them (the module's own waits and drops:
    // neogszxdma_test.cpp)
    startModule();
    _card->startPortTrace();
    _card->zxTraceForTest(false, 0x012345, 0x5A, 7, false);
    _card->zxTraceForTest(true, 0x012346, 0x5B, 1000, true);

    const StateNode trace = report();
    const StateNode& rows = Field(trace, "rows");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(Field(rows.items[0], "wait").i, 7);
    EXPECT_EQ(Field(rows.items[0], "card_address").i, 0x012345);
    EXPECT_EQ(Field(rows.items[0], "page").i, 0x04);
    EXPECT_FALSE(Field(rows.items[0], "dropped").b);
    EXPECT_EQ(Field(rows.items[1], "direction").s, "write");
    EXPECT_TRUE(Field(rows.items[1], "dropped").b);
    EXPECT_EQ(Field(rows.items[1], "wait").i, 255) << "capped";
    EXPECT_EQ(Field(rows.items[1], "card_address").i, 0x012346);
    EXPECT_NE(Field(report(), "text").s.find("DROPPED"), std::string::npos);
}

TEST_F(GSZxDmaTrace_Test, TheDacFetchesDoNotEvictTheBytes)
{
    // The ZX-DMA bytes have a ring of their own: a port trace ring overrun by DAC fetches keeps them
    startModule();
    _card->startPortTrace();
    _card->zxTraceForTest(false, 0x012345, 0x5A, 0, false);
    GSHistory* history = _ctx->pSoundManager->gsHistory();
    ASSERT_NE(history, nullptr);
    GSTraceEvent dac;
    dac.side = GSTraceSide::DacFetch;
    for (size_t i = 0; i < GSPortTraceRecorder::kDefaultCapacity + 16; i++)
        history->Push(dac);
    ASSERT_GT(_card->getPortTraceTotalEvicted(), 0u) << "the port trace's ring overran";

    const StateNode trace = report();
    ASSERT_EQ(Field(trace, "rows").size(), 1u);
    EXPECT_EQ(Field(Field(trace, "rows").items[0], "card_address").i, 0x012345);
    EXPECT_EQ(Field(trace, "evicted").i, 0);
}

TEST_F(GSZxDmaTrace_Test, TheCatalogListsItWithTheNeoGS)
{
    const StateNode catalog = DeviceState::Catalog(_ctx);
    const StateNode* entry = nullptr;
    for (const StateNode& r : Field(catalog, "reports").items)
        if (Field(r, "id").s == "gs-zxdma")
            entry = &r;
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(Field(*entry, "title").s, "NeoGS ZX-DMA trace");
    EXPECT_EQ(Field(*entry, "route").s, "state/audio/gs/zxdma");
    EXPECT_EQ(Field(*entry, "cli").s, "gs zxdma");
    EXPECT_EQ(Field(*entry, "script").s, "gs_zxdma");
    EXPECT_EQ(Field(*entry, "mcp").s, "audio_gs_zxdma");
}

TEST_F(GSZxDmaTrace_Test, TheClassicGSHasNoZxDma)
{
    ASSERT_TRUE(FitGeneralSoundCard(_ctx->pSoundManager, GSTypeKind::Z80));
    const StateNode r = report();
    EXPECT_FALSE(Field(r, "available").b);
    EXPECT_NE(Field(r, "description").s.find("NeoGS"), std::string::npos) << Field(r, "description").s;
    for (const StateNode& entry : Field(DeviceState::Catalog(_ctx), "reports").items)
        EXPECT_NE(Field(entry, "id").s, "gs-zxdma") << "listed only with a NeoGS";
}
