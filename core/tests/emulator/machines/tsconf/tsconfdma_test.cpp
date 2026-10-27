// TS-Conf DMA (TSConf implementation-plan phase 5 DMA-1...14, TSU-8;
// hardware-spec §6, [V] common/dma.v).

#include "tsconffixture.h"

#include <vector>

#include "emulator/platforms/tsconf/tsconfarbiter.h"
#include "emulator/platforms/tsconf/tsconftsu.h"

class TsConfDma_Test : public TsConfFixture
{
protected:
    TsConfEngine& Engine() { return _decoder->GetEngine(); }
    TsConfDma& Dma() { return _decoder->GetDma(); }
    TsConfState& Ts() { return _decoder->GetState(); }

    uint8_t& Byte(uint32_t physical) { return _memory->RAMBase()[physical]; }

    void Source(uint32_t physical)
    {
        Reg(TsConfReg::DmaSAl, static_cast<uint8_t>(physical));
        Reg(TsConfReg::DmaSAh, static_cast<uint8_t>(physical >> 8));
        Reg(TsConfReg::DmaSAx, static_cast<uint8_t>(physical >> 14));
    }
    void Destination(uint32_t physical)
    {
        Reg(TsConfReg::DmaDAl, static_cast<uint8_t>(physical));
        Reg(TsConfReg::DmaDAh, static_cast<uint8_t>(physical >> 8));
        Reg(TsConfReg::DmaDAx, static_cast<uint8_t>(physical >> 14));
    }
    void Launch(uint32_t src, uint32_t dst, uint8_t len, uint8_t num, uint8_t ctrl)
    {
        Source(src);
        Destination(dst);
        Reg(TsConfReg::DmaLen, len);
        Reg(TsConfReg::DmaNum, num);
        Engine().OnMachineFrameRollover(TsConfEngine::kFrameTacts);
        _position = 0;
        Reg(TsConfReg::DmaCtrl, ctrl);
    }

    /// Let the engine run line by line until the transfer ends (cap: 3 frames)
    /// @return raster lines it took
    uint32_t RunDma(uint32_t maxLines = 3 * TsConfEngine::kLines)
    {
        uint32_t lines = 0;
        while (Dma().Busy() && lines < maxLines)
        {
            _position += TsConfEngine::kLineTacts;
            if (_position >= TsConfEngine::kFrameTacts)
            {
                Engine().OnMachineFrameRollover(TsConfEngine::kFrameTacts);
                _position = 0;
            }
            Engine().CatchUp(_position);
            lines++;
        }
        return lines;
    }

    /// Run the engine in steps of `step` tacts until the transfer ends (cap: 3 frames)
    /// @return raster tacts it took, to within one step
    uint32_t RunDmaTacts(uint32_t step)
    {
        uint32_t tacts = 0;
        while (Dma().Busy() && tacts < 3 * TsConfEngine::kFrameTacts)
        {
            _position += step;
            if (_position >= TsConfEngine::kFrameTacts)
            {
                Engine().OnMachineFrameRollover(TsConfEngine::kFrameTacts);
                _position -= TsConfEngine::kFrameTacts;
            }
            Engine().CatchUp(_position);
            tacts += step;
        }
        return tacts;
    }

    /// SPI -> RAM, 256 words (one sector), from the top of the graphics window in the given video mode
    uint32_t SpiSectorTacts(uint8_t vConfig)
    {
        Dma().SetSpi([](bool, uint8_t) -> uint8_t { return 0x5A; });
        Reg(TsConfReg::VConfig, vConfig);
        Engine().OnMachineFrameRollover(TsConfEngine::kFrameTacts);
        _position = 80 * TsConfEngine::kLineTacts;
        Engine().CatchUp(_position);
        Destination(0x50000);
        Reg(TsConfReg::DmaLen, 0xFF);
        Reg(TsConfReg::DmaNum, 0);
        Reg(TsConfReg::DmaCtrl, 0x02);
        return RunDmaTacts(4);
    }

    uint32_t _position = 0;
};

/// DMA-1: address = (AX << 14) | ((AH & 0x3F) << 8) | (AL & 0xFE)
TEST_F(TsConfDma_Test, DMA1_AddressFormula)
{
    Reg(TsConfReg::DmaSAx, 0x10);
    Reg(TsConfReg::DmaSAh, 0xC1);
    Reg(TsConfReg::DmaSAl, 0x03);
    EXPECT_EQ(Ts().dmaSrc * 2, 0x40102u);
}

/// DMA-2: RAM copy, LEN 3 (4 words) x NUM 1 (2 blocks) = 16 bytes, linear
TEST_F(TsConfDma_Test, DMA2_LinearCopy)
{
    for (uint32_t i = 0; i < 32; i++)
    {
        Byte(0x40000 + i) = static_cast<uint8_t>(0x80 + i);
        Byte(0x50000 + i) = 0;
    }
    Launch(0x40000, 0x50000, 3, 1, 0x01);
    RunDma();
    EXPECT_FALSE(Dma().Busy());
    for (uint32_t i = 0; i < 16; i++)
        EXPECT_EQ(Byte(0x50000 + i), 0x80 + i) << i;
    EXPECT_EQ(Byte(0x50000 + 16), 0) << "exactly 16 bytes";
}

/// DMA-2b: the linear step is the full 21-bit word address: it crosses 16 KB pages and wraps at 4 MB ([V] dma.v:344-349
/// s_addr_next from s_addr[20:7] + carry, 377-382 the same for d_addr; [U] tsconf.cpp:175-176 (+2) & 0x3FFFFF)
/// (TS-Conf audit, dma row 10)
TEST_F(TsConfDma_Test, DMA2b_LinearCrossesPagesAndWrapsAt4MB)
{
    Byte(0x7FFE) = 0x11;
    Byte(0x7FFF) = 0x12;
    Byte(0x8000) = 0x13;
    Byte(0x8001) = 0x14;
    Launch(0x7FFE, 0x50000, 1, 0, 0x01);  // 2 words across the page 1 / page 2 boundary
    RunDma();
    EXPECT_EQ(Byte(0x50000), 0x11);
    EXPECT_EQ(Byte(0x50002), 0x13) << "the source went on into page 2";

    Byte(0x3FFFFE) = 0x21;
    Byte(0x3FFFFF) = 0x22;
    Byte(0x000000) = 0x23;
    Byte(0x000001) = 0x24;
    Launch(0x3FFFFE, 0x60000, 1, 0, 0x01);  // from the last word of page #FF
    RunDma();
    EXPECT_EQ(Byte(0x60000), 0x21);
    EXPECT_EQ(Byte(0x60002), 0x23) << "word #1FFFFF + 1 = 0: the 4 MB wrap";
    EXPECT_EQ(Ts().dmaSrc, 1u);

    Launch(0x40000, 0x3FFFFE, 1, 0, 0x01);  // and the destination
    RunDma();
    EXPECT_EQ(Byte(0x3FFFFE), Byte(0x40000));
    EXPECT_EQ(Byte(0x000000), Byte(0x40002));
}

/// DMA-2c: the address counters keep running after a transfer: a launch without address writes goes on from where the
/// last one ended (the launch loads only the block counters, [V] dma.v:309-313; the addresses only on a register
/// write, :351-371, 384-403; [U] tsconf.cpp:146-158 leaves the registers at the end address) (TS-Conf audit, dma
/// row 14)
TEST_F(TsConfDma_Test, DMA2c_TheNextLaunchContinues)
{
    for (uint32_t i = 0; i < 16; i++)
    {
        Byte(0x40000 + i) = static_cast<uint8_t>(0x80 + i);
        Byte(0x50000 + i) = 0;
    }
    Launch(0x40000, 0x50000, 1, 0, 0x01);  // 2 words
    RunDma();
    Reg(TsConfReg::DmaCtrl, 0x01);         // again, no address written
    RunDma();
    for (uint32_t i = 0; i < 8; i++)
        EXPECT_EQ(Byte(0x50000 + i), 0x80 + i) << i;
    EXPECT_EQ(Byte(0x50008), 0x00);
}

/// DMA-3: S_ALGN: the second block starts one block (256 / 512 bytes) further
TEST_F(TsConfDma_Test, DMA3_SourceAlignment)
{
    for (uint32_t i = 0; i < 0x400; i++)
        Byte(0x40000 + i) = static_cast<uint8_t>(i ^ (i >> 8));
    Launch(0x40010, 0x50000, 1, 1, 0x01 | 0x20);  // 2 words x 2 blocks, S_ALGN, ASZ 0
    RunDma();
    const uint32_t expect256[] = {0x10, 0x11, 0x12, 0x13, 0x110, 0x111, 0x112, 0x113};
    for (uint32_t i = 0; i < 8; i++)
        EXPECT_EQ(Byte(0x50000 + i), Byte(0x40000 + expect256[i])) << i;

    Launch(0x40010, 0x50000, 1, 1, 0x01 | 0x20 | 0x08);  // ASZ 1: +0x200
    RunDma();
    const uint32_t expect512[] = {0x10, 0x11, 0x12, 0x13, 0x210, 0x211, 0x212, 0x213};
    for (uint32_t i = 0; i < 8; i++)
        EXPECT_EQ(Byte(0x50000 + i), Byte(0x40000 + expect512[i])) << i;
}

/// DMA-3: inside an aligned block the low word bits wrap
TEST_F(TsConfDma_Test, DMA3_AlignedBlockWraps)
{
    for (uint32_t i = 0; i < 0x200; i++)
        Byte(0x40000 + i) = static_cast<uint8_t>(i);
    Launch(0x400FC, 0x50000, 3, 0, 0x01 | 0x20);  // 4 words from 0xFC: 0xFC, 0xFE, then wrap to 0x00, 0x02
    RunDma();
    EXPECT_EQ(Byte(0x50000 + 4), Byte(0x40000));
    EXPECT_EQ(Byte(0x50000 + 6), Byte(0x40002));
}

/// DMA-3c: D_ALGN with ASZ 1 (512-byte blocks): inside a block the low 8 word bits wrap (#1FE -> #000), at the block
/// end the base steps #200 bytes and the low part reloads from the address as written ([V] dma.v:377-382
/// d_addr_add_h = {next_burst && asz, ...}, d_addr_next_l = next_burst ? d_addr_r : inc; [U] tsconf.cpp:117-118,
/// 142-158) (TS-Conf audit, dma rows 11-12)
TEST_F(TsConfDma_Test, DMA3c_DestinationAlignmentAsz1)
{
    for (uint32_t i = 0; i < 16; i++)
        Byte(0x40000 + i) = static_cast<uint8_t>(0xA0 + i);
    for (uint32_t i = 0; i < 0x400; i++)
        Byte(0x50000 + i) = 0;
    Launch(0x40000, 0x501FC, 3, 1, 0x01 | 0x10 | 0x08);  // 4 words x 2 blocks, D_ALGN, ASZ 1
    RunDma();
    const uint32_t expected[8] = {0x1FC, 0x1FE, 0x000, 0x002, 0x3FC, 0x3FE, 0x200, 0x202};
    for (uint32_t i = 0; i < 8; i++)
        EXPECT_EQ(Byte(0x50000 + expected[i]), 0xA0 + 2 * i) << "word " << i << " at +" << std::hex << expected[i];
}

/// DMA-4: BLT1 keeps the destination where the source byte / nibble is 0
TEST_F(TsConfDma_Test, DMA4_Blit1)
{
    Byte(0x40000) = 0x00;
    Byte(0x40001) = 0x55;
    Byte(0x50000) = 0xAA;
    Byte(0x50001) = 0xAA;
    Launch(0x40000, 0x50000, 0, 0, 0x81 | 0x08);  // BLT1 (code 0x9 = {ctrl[7], ctrl[2:0]}), ASZ 1 (bytes)
    RunDma();
    EXPECT_EQ(Byte(0x50000), 0xAA);
    EXPECT_EQ(Byte(0x50001), 0x55);

    Byte(0x40000) = 0x0F;
    Byte(0x50000) = 0xA5;
    Launch(0x40000, 0x50000, 0, 0, 0x81);  // ASZ 0 (nibbles)
    RunDma();
    EXPECT_EQ(Byte(0x50000), 0xAF);
}

/// DMA-5: BLT2 adds (wrapping, OPT saturates) - only in XTR_FEAT builds; the
/// emulated standard build hangs on it
TEST_F(TsConfDma_Test, DMA5_Blit2OnlyInXtrBuilds)
{
    Launch(0x40000, 0x50000, 0, 0, 0x06);
    RunDma(10);
    EXPECT_TRUE(Dma().Busy()) << "not built in the quartus firmware: hangs";

    Dma().SetBlt2Built(true);
    Dma().Reset();  // the hung transfer belongs to the other build
    auto blit = [&](uint8_t src, uint8_t dst, uint8_t ctrl) {
        Byte(0x40000) = src;
        Byte(0x50000) = dst;
        Launch(0x40000, 0x50000, 0, 0, ctrl);
        RunDma();
        return Byte(0x50000);
    };
    EXPECT_EQ(blit(0x80, 0x90, 0x06 | 0x08), 0x10);
    EXPECT_EQ(blit(0x80, 0x90, 0x06 | 0x08 | 0x40), 0xFF);
    EXPECT_EQ(blit(0x99, 0x99, 0x06), 0x22);
    EXPECT_EQ(blit(0x99, 0x99, 0x06 | 0x40), 0xFF);
}

/// DMA-6: FILL reads the source word once
TEST_F(TsConfDma_Test, DMA6_FillReadsTheSourceOnce)
{
    Byte(0x40000) = 0x34;
    Byte(0x40001) = 0x12;
    Byte(0x40002) = 0x99;
    Launch(0x40000, 0x50000, 3, 0, 0x04);
    RunDma();
    for (uint32_t i = 0; i < 4; i++)
    {
        EXPECT_EQ(Byte(0x50000 + i * 2), 0x34) << i;
        EXPECT_EQ(Byte(0x50000 + i * 2 + 1), 0x12) << i;
    }
}

/// DMA-7: RAM -> CRAM / SFILE, entry = destination word address [7:0]
TEST_F(TsConfDma_Test, DMA7_CramAndSfile)
{
    Byte(0x40000) = 0x21;
    Byte(0x40001) = 0x43;
    Launch(0x40000, 0x00002, 0, 0, 0x8C);
    RunDma();
    EXPECT_EQ(Ts().cram[1], 0x4321);

    Launch(0x40000, 0x7FF04, 0, 0, 0x8D);  // higher destination bits ignored
    RunDma();
    EXPECT_EQ(Ts().sfile[0x82], 0x4321);
}

/// DMA-8: DMA_STATUS busy while running; one DMA INT at completion
TEST_F(TsConfDma_Test, DMA8_StatusAndInterrupt)
{
    Reg(TsConfReg::IntMask, TsConfInt::Dma);
    Launch(0x40000, 0x50000, 0xFF, 3, 0x01);  // 1024 words: several lines
    EXPECT_EQ(In(0x27AF), 0x80);
    Engine().CatchUp(2 * TsConfEngine::kLineTacts);
    _position = 2 * TsConfEngine::kLineTacts;
    EXPECT_TRUE(Dma().Busy()) << "1024 words take more than two lines";
    EXPECT_EQ(Ts().intPending & TsConfInt::Dma, 0);
    RunDma();
    EXPECT_EQ(In(0x27AF), 0x00);
    EXPECT_NE(Ts().intPending & TsConfInt::Dma, 0);
    EXPECT_EQ(_decoder->GetInterrupts().AcknowledgeInterrupt(0), 0xFB);
}

/// DMA-8b: with INT_MASK bit 2 clear the end of a transfer latches nothing, and setting the bit afterwards does not
/// bring it back ([V] zint.v:97,151-153 dis_int_dma holds int_dma at 0 every clock; [U] tsconf.cpp:974,
/// io.cpp:1462-1464) (TS-Conf audit, dma row 36)
TEST_F(TsConfDma_Test, DMA8b_MaskedCompletionIsLost)
{
    Reg(TsConfReg::IntMask, TsConfInt::Frame);
    Launch(0x40000, 0x50000, 3, 0, 0x01);
    RunDma();
    ASSERT_FALSE(Dma().Busy());
    EXPECT_EQ(Ts().intPending & TsConfInt::Dma, 0) << "masked: not latched";
    Reg(TsConfReg::IntMask, TsConfInt::Frame | TsConfInt::Dma);
    EXPECT_EQ(Ts().intPending & TsConfInt::Dma, 0) << "unmasking does not raise it";
}

/// DMA-8c: a transfer that ends inside vdos latches its INT; the output is gated while vdos and the INT is served
/// after it, vector #FB ([V] zint.v:89-93 int_all ... && !vdos gates only the output, :151-157 the latch; [U]
/// tsconf.cpp:969-976 latched, z80_main.inl:282-289 handle_int gated by !vdos) (TS-Conf audit, dma row 37)
TEST_F(TsConfDma_Test, DMA8c_VdosDefersTheDmaInt)
{
    Reg(TsConfReg::IntMask, TsConfInt::Dma);
    TsConfInterrupts& ints = _decoder->GetInterrupts();
    Ts().vdos = 1;
    Launch(0x40000, 0x50000, 3, 0, 0x01);
    RunDma();
    ASSERT_FALSE(Dma().Busy());
    EXPECT_NE(Ts().intPending & TsConfInt::Dma, 0) << "latched inside vdos";
    EXPECT_FALSE(ints.IsIntAsserted(_position)) << "the output is gated";
    Ts().vdos = 0;
    EXPECT_TRUE(ints.IsIntAsserted(_position + 1)) << "served once vdos ends";
    EXPECT_EQ(ints.AcknowledgeInterrupt(_position + 1), 0xFB);
}

/// DMA-8d: a reset stops a running transfer without a DMA INT: the busy edge falls with the reset ([V] dma.v:300-306
/// n_ctr[8] set by !rst_n, :406-410 dma_act_r <= dma_act && rst_n, so int_start never fires; [U] tsconf.cpp:903).
/// The DMA INT stays away after the mask is set again (TS-Conf audit, dma row 38)
TEST_F(TsConfDma_Test, DMA8d_ResetStopsWithoutInt)
{
    Reg(TsConfReg::IntMask, TsConfInt::Dma);
    Launch(0x40000, 0x50000, 0xFF, 7, 0x01);  // 2048 words: several lines
    _position = TsConfEngine::kLineTacts;
    Engine().CatchUp(_position);
    ASSERT_TRUE(Dma().Busy());
    _decoder->reset();
    EXPECT_FALSE(Dma().Busy());
    EXPECT_EQ(In(0x27AF), 0x00);
    Reg(TsConfReg::IntMask, TsConfInt::Dma);
    Engine().OnMachineFrameRollover(TsConfEngine::kFrameTacts);
    Engine().CatchUp(10 * TsConfEngine::kLineTacts);
    EXPECT_EQ(Ts().intPending & TsConfInt::Dma, 0) << "no INT for the stopped transfer";
}

/// DMA-9: DMA_CTRL while busy relaunches; the aborted transfer raises no INT
TEST_F(TsConfDma_Test, DMA9_RelaunchWhileBusy)
{
    Reg(TsConfReg::IntMask, TsConfInt::Dma);
    Launch(0x40000, 0x50000, 0xFF, 7, 0x01);
    Engine().CatchUp(TsConfEngine::kLineTacts);
    ASSERT_TRUE(Dma().Busy());
    Reg(TsConfReg::DmaLen, 0);
    Reg(TsConfReg::DmaNum, 0);
    Reg(TsConfReg::DmaCtrl, 0x01);  // relaunch: one word
    EXPECT_EQ(Ts().intPending & TsConfInt::Dma, 0) << "no INT for the aborted run";
    _position = TsConfEngine::kLineTacts;
    RunDma();
    EXPECT_NE(Ts().intPending & TsConfInt::Dma, 0) << "one INT for the relaunched one";
}

/// DMA-10: address writes while busy move the live counter; DMA_LEN applies at the next block
TEST_F(TsConfDma_Test, DMA10_WritesDuringATransfer)
{
    Launch(0x40000, 0x50000, 0, 1, 0x01);
    Reg(TsConfReg::DmaLen, 1);  // the second block has 2 words
    Destination(0x60000);       // live: the first word already goes here
    Byte(0x40000) = 0x11;
    Byte(0x40002) = 0x22;
    Byte(0x40004) = 0x33;
    RunDma();
    EXPECT_EQ(Byte(0x60000), 0x11);
    EXPECT_EQ(Byte(0x60002), 0x22);
    EXPECT_EQ(Byte(0x60004), 0x33) << "3 words: 1 + 2 after the DMA_LEN reload";
}

/// DMA-10b: DMA_NUM is taken at the launch only: written during a transfer it changes nothing ([V] dma.v:309-313 n_ctr
/// loaded from b_num on dma_launch, :315-319 then only counts down; [U] io.cpp:1633-1641 drops the write while busy)
/// (TS-Conf audit, dma row 7)
TEST_F(TsConfDma_Test, DMA10b_NumIsLatchedAtLaunch)
{
    for (uint32_t i = 0; i < 16; i++)
    {
        Byte(0x40000 + i) = static_cast<uint8_t>(0x80 + i);
        Byte(0x50000 + i) = 0;
    }
    Launch(0x40000, 0x50000, 0, 1, 0x01);  // 1 word x 2 blocks
    Reg(TsConfReg::DmaNum, 5);
    RunDma();
    EXPECT_FALSE(Dma().Busy());
    EXPECT_EQ(Byte(0x50000), 0x80);
    EXPECT_EQ(Byte(0x50002), 0x82);
    EXPECT_EQ(Byte(0x50004), 0x00) << "2 blocks, as at the launch";
}

/// DMA-11: undefined codes hang with no INT until the next DMA_CTRL
TEST_F(TsConfDma_Test, DMA11_UndefinedCodesHang)
{
    Reg(TsConfReg::IntMask, TsConfInt::Dma);
    for (uint8_t ctrl : {0x00, 0x05, 0x80, 0x8E, 0x8F, 0x07, 0x03, 0x83})  // 0x3 / 0xB: no IDE board configured
    {
        Launch(0x40000, 0x50000, 0, 0, ctrl);
        RunDma(20);
        EXPECT_TRUE(Dma().Busy()) << "code " << int(ctrl);
        EXPECT_EQ(Ts().intPending & TsConfInt::Dma, 0) << "code " << int(ctrl);
    }
    Launch(0x40000, 0x50000, 0, 0, 0x01);
    RunDma();
    EXPECT_FALSE(Dma().Busy()) << "the next DMA_CTRL recovers";
}

/// DMA-12: the DMA gets what the graphics fetch leaves - slower under 256C
TEST_F(TsConfDma_Test, DMA12_PacingFollowsTheVideoBandwidth)
{
    auto linesFor = [&](uint8_t vConfig) {
        Reg(TsConfReg::VConfig, vConfig);
        Engine().OnMachineFrameRollover(TsConfEngine::kFrameTacts);
        _position = 80 * TsConfEngine::kLineTacts;  // the top of the graphics window
        Engine().CatchUp(_position);
        Source(0x40000);
        Destination(0x50000);
        Reg(TsConfReg::DmaLen, 0xFF);
        Reg(TsConfReg::DmaNum, 3);                  // 1024 words
        Reg(TsConfReg::DmaCtrl, 0x01);
        return RunDma();
    };
    const uint32_t noGfx = linesFor(0x20 | 0x02);
    const uint32_t full256 = linesFor(0x02 | 0xC0);  // 256C in the 360-wide window
    EXPECT_GT(full256, noGfx);
}

/// DMA-12b: the video takes its DRAM cycles inside its fetch window only ([V] arbiter.v:171-189): the left border
/// gives the DMA every cycle. The line's video cost was spread over all 224 tacts, so a transfer started in the
/// border ran slower there and faster in the window (TS-Conf audit, dma row 43)
TEST_F(TsConfDma_Test, DMA12b_BorderGivesFullRate)
{
    Reg(TsConfReg::VConfig, 0x42);  // 256C 320x200: lines 76..275, fetch from DRAM cycle ~103
    Engine().OnMachineFrameRollover(TsConfEngine::kFrameTacts);
    _position = 100 * TsConfEngine::kLineTacts;
    Engine().CatchUp(_position);
    const TsConfArbiter::Fetch fetch = TsConfArbiter::FetchOf(Engine().Line(100), 100);
    ASSERT_TRUE(fetch.active);
    ASSERT_GT(fetch.h0, 100u) << "tacts 0..49 = DRAM cycles 0..99 are all left of the fetch";

    Source(0x40000);
    Destination(0x50000);
    Reg(TsConfReg::DmaLen, 0xFF);
    Reg(TsConfReg::DmaNum, 0xFF);
    Reg(TsConfReg::DmaCtrl, 0x01);  // RAM copy, 2 cycles per word
    const uint32_t before = Ts().dmaDst;
    Engine().CatchUp(_position + 50);
    EXPECT_EQ(Ts().dmaDst - before, 50u) << "100 free DRAM cycles / 2";
    Dma().Reset();
}

/// DMA-13: DMA writes do not invalidate the CPU cache
TEST_F(TsConfDma_Test, DMA13_DmaLeavesTheCacheStale)
{
    Reg(TsConfReg::CacheConfig, 0x04);  // W2 = RAM page 2
    EXPECT_EQ(Peek(0x8000), 0x02);      // fills
    Byte(0x40000) = 0x77;
    Byte(0x40001) = 0x78;
    Launch(0x40000, 2 * PAGE_SIZE, 0, 0, 0x01);
    RunDma();
    EXPECT_EQ(Ram(2, 0x0000), 0x77) << "DRAM changed";
    EXPECT_EQ(Peek(0x8000), 0x02) << "the cache still answers the old word";
}

/// DMA-14: SPI -> RAM, two exchanges sending #FF per word, low byte first
TEST_F(TsConfDma_Test, DMA14_SpiToRam)
{
    std::vector<uint8_t> sent;
    uint32_t reads = 0;
    uint8_t next = 0;
    Dma().SetSpi([&](bool read, uint8_t out) -> uint8_t {
        if (read)
        {
            reads++;
            return next++;
        }
        sent.push_back(out);
        return 0xFF;
    });
    Launch(0, 0x50000, 0xFF, 0, 0x02);  // 256 words = one 512-byte sector
    RunDma();
    EXPECT_EQ(reads, 512u);
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(Byte(0x50000), 0x00);
    EXPECT_EQ(Byte(0x50001), 0x01);
    EXPECT_EQ(Byte(0x501FF), 0xFF);

    sent.clear();
    Byte(0x40000) = 0xAB;
    Byte(0x40001) = 0xCD;
    Launch(0x40000, 0, 0, 0, 0x8A);  // RAM -> SPI
    RunDma();
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[0], 0xAB);
    EXPECT_EQ(sent[1], 0xCD);
}

/// TSU-8: objects past the line's DRAM budget are dropped, in processing
/// order (one cut by the budget shows its fetched words, TSU-8b)
TEST_F(TsConfDma_Test, TSU8_StarvedObjectsAreDropped)
{
    const uint8_t spritePage = 0x20;
    std::memset(_memory->RAMPageAddress(spritePage), 0x11, PAGE_SIZE);  // every pixel nibble 1
    Reg(TsConfReg::SGPage, spritePage);
    Reg(TsConfReg::TConfig, 0x80);
    for (uint32_t d = 0; d < 3; d++)  // three 8x8 sprites at x 0, 16, 32; 2 accesses each
    {
        Ts().sfile[d * 3] = 0x2000;
        Ts().sfile[d * 3 + 1] = static_cast<uint16_t>(d * 16);
        Ts().sfile[d * 3 + 2] = static_cast<uint16_t>((d + 1) << 12);
    }
    TsConfLine set;
    TsConfTsu::MapRing ring{};
    std::vector<uint8_t> out(256);
    uint32_t used = 0;
    TsConfTsu::RenderLine(Ts(), set, _memory->RAMBase(), ring, 0, 256, out.data(), 4, used);
    EXPECT_EQ(out[0], 0x11);
    EXPECT_EQ(out[16], 0x21);
    EXPECT_EQ(out[32], 0x00) << "the third sprite no longer fits";
    EXPECT_EQ(used, 4u);
}

/// TSU-8b: an object the budget cuts is drawn up to the cut, 4 pixels per DRAM word in bitmap order ([V]
/// video_ts_render.v:84-107; [U] render_tile per cycle): it was dropped whole (TS-Conf audit, tsu row 39). With an
/// X flip the bitmap's first pixels are the sprite's right side
TEST_F(TsConfDma_Test, TSU8b_TheCutObjectIsDrawnUpToTheCut)
{
    const uint8_t spritePage = 0x20;
    std::memset(_memory->RAMPageAddress(spritePage), 0x11, PAGE_SIZE);
    Reg(TsConfReg::SGPage, spritePage);
    Reg(TsConfReg::TConfig, 0x80);
    for (uint32_t d = 0; d < 3; d++)
    {
        Ts().sfile[d * 3] = 0x2000;
        Ts().sfile[d * 3 + 1] = static_cast<uint16_t>(d * 16);
        Ts().sfile[d * 3 + 2] = static_cast<uint16_t>((d + 1) << 12);
    }
    for (bool flip : {false, true})
    {
        SCOPED_TRACE(flip);
        Ts().sfile[2 * 3 + 1] = static_cast<uint16_t>(32 | (flip ? 0x8000 : 0));
        TsConfLine set;
        TsConfTsu::MapRing ring{};
        std::vector<uint8_t> out(256);
        uint32_t used = 0;
        TsConfTsu::RenderLine(Ts(), set, _memory->RAMBase(), ring, 0, 256, out.data(), 5, used);
        EXPECT_EQ(used, 5u);
        for (uint32_t x = 32; x < 40; x++)
        {
            const bool drawn = flip ? x >= 36 : x < 36;
            EXPECT_EQ(out[x], drawn ? 0x31 : 0x00) << "x " << x;
        }
    }
}

/// TTD-4: a transfer captured half-way (TsConfState + RAM) and restored ends
/// with the same bytes on the same line
TEST_F(TsConfDma_Test, TTD4_RestoreMidTransfer)
{
    for (uint32_t i = 0; i < 0x1000; i++)
        Byte(0x40000 + i) = static_cast<uint8_t>(i * 7);
    Launch(0x40000, 0x50000, 0xFF, 7, 0x01);  // 2048 words
    _position = TsConfEngine::kLineTacts;
    Engine().CatchUp(_position);
    ASSERT_TRUE(Dma().Busy());

    const TsConfState saved = Ts();
    const std::vector<uint8_t> savedRam(&Byte(0x50000), &Byte(0x50000) + 0x1000);
    const uint32_t savedPosition = _position;

    const uint32_t lines = RunDma();
    const std::vector<uint8_t> result(&Byte(0x50000), &Byte(0x50000) + 0x1000);

    Ts() = saved;
    _decoder->ApplyState();
    std::copy(savedRam.begin(), savedRam.end(), &Byte(0x50000));
    _position = savedPosition;
    EXPECT_EQ(RunDma(), lines);
    EXPECT_EQ(std::vector<uint8_t>(&Byte(0x50000), &Byte(0x50000) + 0x1000), result);
}

/// TIM-3: DRAM cycles per word ([V] dma.v, spi.v): RAM copy 2, BLT 3, fill 1
/// (2 for the first word), SPI and IDE 1 - their device phase is time, not DRAM
/// (DeviceFclk: SPI two 17-fclk exchanges, the DRAM write overlapping the
/// second; IDE the ~6-fclk bus cycle + the DRAM cycle)
TEST_F(TsConfDma_Test, TIM3_WordCosts)
{
    Launch(0x40000, 0x50000, 0xFF, 0xFF, 0x01);
    EXPECT_EQ(Dma().WordCost(), 2u) << "RAM";
    EXPECT_EQ(Dma().DeviceFclk(), 0u) << "RAM: DRAM-bound only";
    Dma().Reset();
    Dma().SetSpi([](bool, uint8_t) -> uint8_t { return 0xFF; });
    Launch(0, 0x50000, 0xFF, 0xFF, 0x02);
    EXPECT_EQ(Dma().WordCost(), 1u) << "SPI -> RAM: one DRAM write";
    EXPECT_EQ(Dma().DeviceFclk(), 34u) << "two SPI exchanges";
    Dma().Reset();
    Launch(0x40000, 0x50000, 0xFF, 0xFF, 0x81);  // BLT1 (ASZ)
    EXPECT_EQ(Dma().WordCost(), 3u) << "blit";
    Dma().Reset();
}

/// TIM-3b: an SPI word takes 34 fclk whatever the DRAM load ([V] spi.v: an exchange is the start clock + 16;
/// dma.v: the DMA's spi_stb is the SPI start, so the DRAM write of a word overlaps its second byte's shift).
/// It was charged 10 DRAM cycles of the line's free budget: 15% slow without video, ~1.8x slow in 256C
/// (TS-Conf audit, dma rows 30-31)
TEST_F(TsConfDma_Test, TIM3b_SpiWordIs34Fclk)
{
    const uint32_t tacts = SpiSectorTacts(0x20 | 0x02);  // NOGFX
    EXPECT_NEAR(static_cast<double>(tacts), 256 * 34 / 8.0, 6.0) << "256 words x 34 fclk = 1088 tacts";
}

TEST_F(TsConfDma_Test, TIM3c_SpiPacingIgnoresTheVideo)
{
    const uint32_t noGfx = SpiSectorTacts(0x20 | 0x02);
    const uint32_t full256 = SpiSectorTacts(0x02 | 0xC0);  // 256C in the 360-wide window
    EXPECT_NEAR(static_cast<double>(full256), static_cast<double>(noGfx), 8.0)
        << "the video leaves the DMA its one DRAM cycle per 34 fclk";
}

/// The VDAC builds are XTR_FEAT builds: BLT2 is there after a reset
TEST_F(TsConfDma_Test, DMA5_Blit2InTheVdacBuilds)
{
    _context->config.ts_vdac = 3;
    _decoder->reset();
    Launch(0x40000, 0x50000, 0, 0, 0x06);  // BLT2 (add), one word
    EXPECT_NE(Dma().WordCost(), 0u) << "BLT2 runs";
    Dma().Reset();
    _context->config.ts_vdac = 0;
    _decoder->reset();
    Launch(0x40000, 0x50000, 0, 0, 0x06);
    EXPECT_EQ(Dma().WordCost(), 0u) << "the standard build hangs on BLT2";
    Dma().Reset();
}
