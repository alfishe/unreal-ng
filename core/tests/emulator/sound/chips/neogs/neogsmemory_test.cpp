// NeoGS memory map (neogs-tdd.md §3.2; FPGA memmap.v, ports.v:410-442)

#include <gtest/gtest.h>

#include <memory>

#include "emulator/io/flash/flash29f040b.h"
#include "emulator/sound/chips/neogs/neogsmemory.h"

namespace
{
constexpr size_t PAGE = NeoGSMemory::PAGE_SIZE;

struct MemFixture
{
    Flash29F040B flash{120e6};
    std::unique_ptr<NeoGSMemory> mem;

    explicit MemFixture(size_t ramKB = 4096)
    {
        // Flash page p holds 0xF0 | p at every byte; RAM page p holds p
        std::vector<uint8_t> image(Flash29F040B::SIZE);
        for (size_t i = 0; i < image.size(); i++)
            image[i] = static_cast<uint8_t>(0xF0 | ((i / PAGE) & 0x0F));
        flash.load(image.data(), image.size());
        mem = std::make_unique<NeoGSMemory>(ramKB, &flash);
        mem->powerOn();
        for (size_t p = 0; p < mem->ramPages(); p++)
            std::fill_n(mem->ram() + p * PAGE, PAGE, static_cast<uint8_t>(p));
    }
};
} // namespace

TEST(NeoGSMemory, ResetMapIsLoaderInFlashPageZero)
{
    MemFixture f;
    EXPECT_EQ(f.mem->page(0), 0);
    EXPECT_EQ(f.mem->page(1), 3);
    EXPECT_EQ(f.mem->page(2), 0); // emulator's documented power-on choice
    EXPECT_EQ(f.mem->page(3), 2);
    EXPECT_TRUE(f.mem->isFlash(0));
    EXPECT_FALSE(f.mem->isFlash(1)); // window 1 is always RAM
    EXPECT_EQ(f.mem->read(0x0000, 0), 0xF0);  // flash page 0
    EXPECT_EQ(f.mem->read(0x4000, 0), 3);     // RAM page 3
    EXPECT_EQ(f.mem->read(0xC000, 0), 0xF2);  // flash page 2
}

TEST(NeoGSMemory, RegisterResetKeepsPg2AndPg3)
{
    MemFixture f;
    f.mem->writePage(0, 9);
    f.mem->writePage(1, 10);
    f.mem->writePage(2, 11);
    f.mem->writePage(3, 12);
    f.mem->resetRegisters();
    EXPECT_EQ(f.mem->page(0), 0);
    EXPECT_EQ(f.mem->page(1), 3);
    EXPECT_EQ(f.mem->page(2), 11);
    EXPECT_EQ(f.mem->page(3), 12);
}

TEST(NeoGSMemory, MpagNormalAndExtendedPaging)
{
    MemFixture f;
    f.mem->setConfig(0x01); // RAM mode
    f.mem->writeMpag(5);
    EXPECT_EQ(f.mem->page(2), 10);
    EXPECT_EQ(f.mem->page(3), 11);
    f.mem->writeMpag(0x85); // d7 is dropped in normal mode
    EXPECT_EQ(f.mem->page(2), 10);
    EXPECT_EQ(f.mem->page(3), 11);

    f.mem->setConfig(0x01 | NeoGSMemory::CFG_EXPAG);
    f.mem->writeMpag(0x85); // rotate left: {d6..d0, d7}
    EXPECT_EQ(f.mem->page(2), 0x0B);
    EXPECT_EQ(f.mem->page(3), 11) << "MPAG leaves PG3 alone in EXPAG mode";
    f.mem->writeMpagEx(0x40);
    EXPECT_EQ(f.mem->page(3), 0x80);

    f.mem->setConfig(0x01);
    f.mem->writeMpagEx(0x01);
    EXPECT_EQ(f.mem->page(3), 0x80) << "MPAGEX does nothing outside EXPAG";
}

TEST(NeoGSMemory, RomModeShowsFlashInWindowsZeroTwoThree)
{
    MemFixture f;
    f.mem->writePage(0, 0x21); // flash uses bits 4:0 only
    f.mem->writePage(2, 0x03);
    f.mem->writePage(3, 0xE4);
    EXPECT_EQ(f.mem->read(0x0000, 0), 0xF1);
    EXPECT_EQ(f.mem->read(0x8000, 0), 0xF3);
    EXPECT_EQ(f.mem->read(0xC000, 0), 0xF4);
    EXPECT_EQ(f.mem->read(0x4000, 0), 3);

    f.mem->setConfig(0x01);
    EXPECT_EQ(f.mem->read(0x0000, 0), 0x21);
    EXPECT_EQ(f.mem->read(0x8000, 0), 0x03);
    EXPECT_EQ(f.mem->read(0xC000, 0), 0xE4);
}

TEST(NeoGSMemory, WritesToFlashWindowsDoNotReachRam)
{
    MemFixture f;
    f.mem->write(0x8123, 0x55, 0);
    EXPECT_EQ(f.mem->read(0x8123, 0), 0xF0);
    EXPECT_EQ(f.mem->ram()[0x0123], 0);
}

TEST(NeoGSMemory, RamroProtectsPagesZeroOneAnd128And129InEveryWindow)
{
    MemFixture f;
    f.mem->setConfig(0x03); // RAM mode + RAMRO
    for (uint8_t page : {0, 1, 128, 129})
    {
        for (int window = 0; window < 4; window++)
        {
            f.mem->writePage(window, page);
            const uint16_t addr = static_cast<uint16_t>(window * PAGE + 0x100);
            f.mem->write(addr, 0xAA, 0);
            EXPECT_EQ(f.mem->read(addr, 0), page) << "page " << int(page) << " window " << window;
        }
    }
    f.mem->writePage(2, 2);
    f.mem->write(0x8100, 0xAA, 0);
    EXPECT_EQ(f.mem->read(0x8100, 0), 0xAA) << "page 2 is writable";

    // RAMRO needs RAM mode; in ROM mode window 1 page 0 is writable
    f.mem->setConfig(0x02);
    f.mem->writePage(1, 0);
    f.mem->write(0x4100, 0xBB, 0);
    EXPECT_EQ(f.mem->read(0x4100, 0), 0xBB);
}

TEST(NeoGSMemory, MainRomMpag40IsProtectedOnFourMegabytes)
{
    // The quirk the real firmware lives with: GSCFG0 = #23, MPAG #40 maps
    // pages 128/129, which RAMRO protects
    MemFixture f(4096);
    f.mem->setConfig(0x23);
    f.mem->writeMpag(0x40);
    EXPECT_EQ(f.mem->page(2), 128);
    f.mem->write(0x8000, 0x77, 0);
    EXPECT_EQ(f.mem->read(0x8000, 0), 128);
}

TEST(NeoGSMemory, TwoMegabyteBoardIgnoresBankBit)
{
    MemFixture f(2048);
    f.mem->setConfig(0x01);
    f.mem->writePage(2, 0x85);
    EXPECT_EQ(f.mem->read(0x8000, 0), 0x05) << "page 0x85 mirrors page 0x05";
    EXPECT_EQ(f.mem->ramSize(), 2048u * 1024u);
}

TEST(NeoGSMemory, WorkedExampleMainRomMapping)
{
    // neogs-tdd.md §3.2: GSCFG0 = #23, MPAG = 5
    MemFixture f;
    f.mem->setConfig(0x23);
    f.mem->writeMpag(5);
    EXPECT_EQ(f.mem->physical(0x0000), 0x000000u);
    EXPECT_EQ(f.mem->physical(0x4000), 0x00C000u);
    EXPECT_EQ(f.mem->physical(0x8000), 0x028000u);
    EXPECT_EQ(f.mem->physical(0xC000), 0x02C000u);
}

TEST(NeoGSMemory, FlashStatusModeFallsBackToTheChip)
{
    MemFixture f;
    // Start a byte program through the window: reads return status until done
    f.mem->write(0x0555, 0xAA, 0);
    f.mem->write(0x02AA, 0x55, 0);
    f.mem->write(0x0555, 0xA0, 0);
    f.mem->write(0x0010, 0x30, 0); // 0xF0 -> 0x30 only clears bits
    const uint8_t s1 = f.mem->read(0x0000, 1);
    const uint8_t s2 = f.mem->read(0x8000, 2);
    EXPECT_NE(s1 & 0x40, s2 & 0x40) << "DQ6 toggles, at any address";
    // 10 us later the byte is programmed and reads are fast again
    EXPECT_EQ(f.mem->read(0x0010, 1200), 0x30);
    EXPECT_EQ(f.mem->read(0x0011, 1201), 0xF0);
}
