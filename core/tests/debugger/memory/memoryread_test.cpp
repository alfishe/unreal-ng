// MemoryRead (memoryread.h): the spaces, the limits, the wrap at #FFFF, a page read cut at the page's end and the
// window syntax. A bare 128K memory: ROM 0 at #0000, RAM 5 at #4000, RAM 2 at #8000, RAM 0 at #C000 after a reset.

#include <gtest/gtest.h>

#include <cstring>

#include "3rdparty/message-center/messagecenter.h"
#include "common/modulelogger.h"
#include "debugger/memory/memoryread.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

class MemoryRead_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();
        _context = new EmulatorContext(LoggerLevel::LogError);
        _context->config.ramsize = 128;
        _memory = new Memory(_context);
        _context->pMemory = _memory;
        _memory->Reset();
        for (int p = 0; p < 8; p++)
        {
            uint8_t* page = _memory->RAMPageAddress(static_cast<uint16_t>(p));
            for (uint32_t i = 0; i < PAGE_SIZE; i++)
                page[i] = static_cast<uint8_t>(p * 16 + (i & 0x0F));   // page number in the high nibble
        }
    }
    void TearDown() override
    {
        delete _memory;
        _context->pMemory = nullptr;
        delete _context;
        MessageCenter::DisposeDefaultMessageCenter();
    }

    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
};

TEST_F(MemoryRead_Test, CpuViewWrapsAtFFFF)
{
    MemoryRead::Result r = MemoryRead::Bytes(_context, "", 0x8000, 4);
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_EQ(r.space, "cpu");
    EXPECT_EQ(r.bytes, (std::vector<uint8_t>{0x20, 0x21, 0x22, 0x23})) << "RAM 2 at #8000";
    r = MemoryRead::Bytes(_context, "cpu", 0xFFFE, 4);
    ASSERT_EQ(r.bytes.size(), 4u);
    EXPECT_EQ(r.bytes[1], 0x0F) << "#FFFF: RAM 0";
    EXPECT_EQ(r.bytes[2], _memory->DirectReadFromZ80Memory(0x0000)) << "then #0000, as the CPU wraps";
    r = MemoryRead::Bytes(_context, "cpu", 0, 65536);
    EXPECT_EQ(r.bytes.size(), 65536u) << "the whole address space in one read";
}

TEST_F(MemoryRead_Test, PageAndAllRam)
{
    MemoryRead::Result r = MemoryRead::Bytes(_context, "ram3", 0x10, 2);
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_EQ(r.space, "ram3");
    EXPECT_EQ(r.bytes, (std::vector<uint8_t>{0x30, 0x31})) << "a page that is not paged in";
    r = MemoryRead::Bytes(_context, "ram3", 0x3FFE, 100);
    EXPECT_EQ(r.bytes.size(), 2u) << "a page read stops at the page's end";
    r = MemoryRead::Bytes(_context, "ram", 0x4000 * 6 + 0x3FFF, 2);
    ASSERT_EQ(r.bytes.size(), 2u);
    EXPECT_EQ(r.bytes[0], 0x6F) << "RAM 6's last byte";
    EXPECT_EQ(r.bytes[1], 0x70) << "then RAM 7's first: all RAM is the pages back to back";
    r = MemoryRead::Bytes(_context, "ram", 0x4000 * 7 + 0x3FF0, 100);
    EXPECT_EQ(r.bytes.size(), 16u) << "stops at the last page";
}

TEST_F(MemoryRead_Test, RefusalsSayWhy)
{
    EXPECT_EQ(MemoryRead::Bytes(_context, "cpu", 0, 0).error, "the length must be 1 to 65536");
    EXPECT_EQ(MemoryRead::Bytes(_context, "cpu", 0, 65537).error, "the length must be 1 to 65536");
    EXPECT_EQ(MemoryRead::Bytes(_context, "cpu", 0x10000, 1).error, "a CPU address is 0 to #FFFF");
    EXPECT_EQ(MemoryRead::Bytes(_context, "ram9", 0, 1).error, "this machine has no page ram9");
    EXPECT_EQ(MemoryRead::Bytes(_context, "ram5", 0x4000, 1).error, "a page offset is 0 to #3FFF");
    EXPECT_EQ(MemoryRead::Bytes(_context, "ram", 0x20000, 1).error, "past the last RAM page");
    EXPECT_FALSE(MemoryRead::Bytes(_context, "flash", 0, 1).error.empty());
}

TEST_F(MemoryRead_Test, WindowSyntax)
{
    std::string space, error;
    uint32_t address = 0, length = 0;
    ASSERT_TRUE(MemoryRead::ParseWindow("cpu:0x8000:256", space, address, length, error)) << error;
    EXPECT_EQ(space, "cpu");
    EXPECT_EQ(address, 0x8000u);
    EXPECT_EQ(length, 256u);
    ASSERT_TRUE(MemoryRead::ParseWindow("ram5:#1800:768", space, address, length, error));
    EXPECT_EQ(address, 0x1800u);
    EXPECT_FALSE(MemoryRead::ParseWindow("cpu:0x8000", space, address, length, error));
    EXPECT_FALSE(MemoryRead::ParseWindow("cpu:zz:1", space, address, length, error));
    EXPECT_FALSE(MemoryRead::ParseWindow("cpu:0:70000", space, address, length, error));
}
