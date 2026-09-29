// 29F040B flash chip (neogs-tdd.md §3.8)

#include <gtest/gtest.h>

#include <vector>

#include "emulator/io/flash/flash29f040b.h"

namespace
{
constexpr double kUnits = 1e6; // 1 unit = 1 us

void unlock(Flash29F040B& f, uint32_t page, int64_t t)
{
    // Unlock cycles compare A10..A0 only: any page works
    f.write(page * 0x4000 + 0x555, 0xAA, t);
    f.write(page * 0x4000 + 0x2AA, 0x55, t);
}

void program(Flash29F040B& f, uint32_t offset, uint8_t value, int64_t t)
{
    unlock(f, 0, t);
    f.write(0x555, 0xA0, t);
    f.write(offset, value, t);
}

void sectorErase(Flash29F040B& f, uint32_t sector, int64_t t)
{
    unlock(f, 0, t);
    f.write(0x555, 0x80, t);
    unlock(f, 0, t);
    f.write(sector * Flash29F040B::SECTOR_SIZE, 0x30, t);
}
} // namespace

TEST(Flash29F040B, LoadPadsWithErasedBytes)
{
    Flash29F040B f(kUnits);
    const uint8_t data[4] = {1, 2, 3, 4};
    f.load(data, sizeof data);
    EXPECT_EQ(f.read(0, 0), 1);
    EXPECT_EQ(f.read(3, 0), 4);
    EXPECT_EQ(f.read(4, 0), 0xFF);
    EXPECT_TRUE(f.arrayMode());
    EXPECT_FALSE(f.modified());
}

TEST(Flash29F040B, AutoselectIdsForBothVendors)
{
    for (auto vendor : {Flash29F040B::Vendor::ST, Flash29F040B::Vendor::AMD})
    {
        Flash29F040B f(kUnits, vendor);
        unlock(f, 5, 0);
        f.write(5 * 0x4000 + 0x555, 0x90, 0);
        EXPECT_FALSE(f.arrayMode());
        EXPECT_EQ(f.read(0, 0), vendor == Flash29F040B::Vendor::ST ? 0x20 : 0x01);
        EXPECT_EQ(f.read(1, 0), vendor == Flash29F040B::Vendor::ST ? 0xE2 : 0xA4);
        EXPECT_EQ(f.read(2, 0), 0x00);
        f.write(0x1234, 0xF0, 0);
        EXPECT_TRUE(f.arrayMode());
    }
}

TEST(Flash29F040B, ProgramOnlyClearsBitsAndPollsUntilDone)
{
    Flash29F040B f(kUnits);
    program(f, 0x100, 0x5A, 0);
    EXPECT_FALSE(f.arrayMode());
    const uint8_t s1 = f.read(0x70000, 1);
    const uint8_t s2 = f.read(0x00000, 2);
    EXPECT_EQ(s1 & 0x80, (~0x5A) & 0x80) << "DQ7 = complement of the data bit";
    EXPECT_NE(s1 & 0x40, s2 & 0x40) << "DQ6 toggles on every read, at any address";
    EXPECT_EQ(f.read(0x100, 10), 0x5A);
    EXPECT_TRUE(f.arrayMode());
    EXPECT_TRUE(f.modified());

    program(f, 0x100, 0xF0, 20);          // 0x5A & 0xF0 = 0x50
    EXPECT_NE(f.read(0x100, 30) & 0x20, 0) << "a 0 -> 1 request fails: DQ5 set, status until reset";
    EXPECT_FALSE(f.arrayMode());
    f.write(0, 0xF0, 32);
    EXPECT_EQ(f.read(0x100, 33), 0x50);
}

TEST(Flash29F040B, SectorEraseWithCommandWindowAndDq3)
{
    Flash29F040B f(kUnits);
    std::vector<uint8_t> zeros(Flash29F040B::SIZE, 0x00);
    f.load(zeros.data(), zeros.size());
    sectorErase(f, 2, 0);
    EXPECT_EQ(f.read(0, 10) & 0x08, 0) << "DQ3 = 0 while the window is open";
    f.write(3 * Flash29F040B::SECTOR_SIZE, 0x30, 20); // a second sector in the window
    EXPECT_EQ(f.read(0, 100) & 0x08, 0x08) << "window closed: erase running";
    EXPECT_EQ(f.read(0, 100) & 0x80, 0) << "DQ7 = 0 during erase";
    EXPECT_FALSE(f.arrayMode());
    const int64_t done = f.busyUntil();
    EXPECT_EQ(done, 70 + 2'000'000) << "1 s per sector after the 50 us window";
    EXPECT_EQ(f.read(2 * Flash29F040B::SECTOR_SIZE, done), 0xFF);
    EXPECT_EQ(f.read(3 * Flash29F040B::SECTOR_SIZE + 5, done), 0xFF);
    EXPECT_EQ(f.read(1 * Flash29F040B::SECTOR_SIZE, done), 0x00);
    EXPECT_EQ(f.read(4 * Flash29F040B::SECTOR_SIZE, done), 0x00);
}

TEST(Flash29F040B, ChipErase)
{
    Flash29F040B f(kUnits);
    std::vector<uint8_t> zeros(Flash29F040B::SIZE, 0x00);
    f.load(zeros.data(), zeros.size());
    unlock(f, 0, 0);
    f.write(0x555, 0x80, 0);
    unlock(f, 0, 0);
    f.write(0x555, 0x10, 0);
    EXPECT_EQ(f.busyUntil(), 8'000'000);
    EXPECT_EQ(f.read(Flash29F040B::SIZE - 1, 8'000'000), 0xFF);
}

TEST(Flash29F040B, WrongSequencesFallBackToReadArray)
{
    Flash29F040B f(kUnits);
    f.write(0x555, 0xAA, 0);
    f.write(0x2AB, 0x55, 0); // wrong address
    f.write(0x555, 0xA0, 0);
    f.write(0x100, 0x00, 0);
    EXPECT_TRUE(f.arrayMode());
    EXPECT_EQ(f.read(0x100, 0), 0xFF);
}

TEST(Flash29F040B, WriteDisabledIgnoresEverything)
{
    Flash29F040B f(kUnits);
    f.setWritable(false);
    program(f, 0x10, 0x00, 0);
    EXPECT_TRUE(f.arrayMode());
    EXPECT_EQ(f.read(0x10, 100), 0xFF);
}

TEST(Flash29F040B, FlasherSequenceEraseThenProgramBlock)
{
    // flasher_ngs.a80: sector erase, a 50 us wait, DQ6 polling, byte programs
    Flash29F040B f(kUnits);
    int64_t t = 0;
    sectorErase(f, 1, t);
    t += 50;
    uint8_t last = f.read(0x8000, t);
    int polls = 0;
    while (true)
    {
        t += 100;
        const uint8_t now = f.read(0x8000, t);
        if ((now & 0x40) == (last & 0x40))
            break;
        last = now;
        polls++;
    }
    EXPECT_GT(polls, 1000);
    for (uint32_t i = 0; i < 256; i++)
    {
        program(f, 0x10000 + i, static_cast<uint8_t>(i), t);
        t += 10;
    }
    for (uint32_t i = 0; i < 256; i++)
        EXPECT_EQ(f.read(0x10000 + i, t), static_cast<uint8_t>(i));
}

TEST(Flash29F040B, StateRoundTrip)
{
    Flash29F040B a(kUnits);
    program(a, 0x42, 0x00, 0);
    uint8_t state[Flash29F040B::STATE_SIZE];
    a.saveState(state);
    Flash29F040B b(kUnits);
    b.loadState(state);
    EXPECT_FALSE(b.arrayMode());
    EXPECT_EQ(b.busyUntil(), a.busyUntil());
}
