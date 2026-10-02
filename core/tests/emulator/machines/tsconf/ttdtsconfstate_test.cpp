// TTD-1 (TSConf implementation-plan phase 1): the TsConfState blob round-trips
// and restores the derived machinery (banks, FM window).

#include "tsconffixture.h"

#include "debugger/ttd/tsconf/ttdtsconfstate.h"

class TTDTsConfState_Test : public TsConfFixture
{
};

TEST_F(TTDTsConfState_Test, TTD1_RoundTripRestoresStateAndMapping)
{
    const auto ids = _decoder->GetTTDModelStateIds();
    ASSERT_EQ(ids.size(), 5u);
    EXPECT_EQ(ids[0], ttd::PeripheralId::TsConfPaging);
    EXPECT_EQ(ids[1], ttd::PeripheralId::EvoSdCard) << "the Z-Controller SD slot, shared with ATM3";
    EXPECT_EQ(ids[2], ttd::PeripheralId::Ds12887);
    EXPECT_EQ(ids[3], ttd::PeripheralId::EvoPs2) << "the AVR's PS/2 keyboard log, shared with ATM3";
    EXPECT_EQ(ids[4], ttd::PeripheralId::EvoMouse) << "the AVR's PS/2 mouse, shared with ATM3";

    Reg(TsConfReg::MemConfig, 0x40);
    Out(0x7FFD, 0x23);                  // 128K rule, lock48
    Reg(TsConfReg::FMaps, 0x14);
    Poke(0x4002, 0x21);                 // stash only
    Reg(TsConfReg::CacheConfig, 0x04);
    (void)Peek(0x8000);
    _decoder->GetState().cram[7] = 0x7C00;
    _decoder->GetState().sfile[9] = 0x0102;

    ttd::TTDTsConfState serializer(*_decoder);
    std::vector<uint8_t> blob(serializer.TTDStateSize());
    serializer.TTDSaveState(blob.data());
    const uint64_t hash = serializer.TTDHashState();

    _decoder->reset();
    _decoder->GetState().cram[7] = 0;
    EXPECT_NE(serializer.TTDHashState(), hash);
    EXPECT_EQ(_core->GetBusOverlayCount(), 0u);
    EXPECT_EQ(Tag(0xC000), 0x00);

    serializer.TTDLoadState(blob.data());
    EXPECT_EQ(serializer.TTDHashState(), hash);
    std::vector<uint8_t> again(serializer.TTDStateSize());
    serializer.TTDSaveState(again.data());
    EXPECT_EQ(again, blob);

    EXPECT_EQ(Tag(0xC000), 0x03);
    EXPECT_TRUE(_decoder->IsPagingLocked());
    EXPECT_EQ(_decoder->GetState().fmStash, 0x21);
    EXPECT_EQ(_core->GetBusOverlayCount(), 2u) << "FM window + cache snoop";
}
