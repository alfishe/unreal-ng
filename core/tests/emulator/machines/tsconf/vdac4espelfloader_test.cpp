// VDAC4 firmware ELF loader port (design.md §6, research-app-abi.md §4):
// layout and relocated words as the board's elf_loader 1.3.3 produces them.

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/platforms/tsconf/vdac4/esp/espelfloader.h"

using namespace vdac4;

namespace
{

constexpr uint32_t kPsramBase = 0x3C100000;
constexpr uint32_t kTrapBase = 0x40000400;

/// A bump-allocated PSRAM and a firmware export table of trap addresses
class FakeHost : public IEspElfHost
{
public:
    std::vector<uint8_t> psram = std::vector<uint8_t>(4u << 20);
    uint32_t next = kPsramBase;
    std::map<uint32_t, uint32_t> blocks;
    std::map<std::string, uint32_t> exports;
    bool exportEverything = true;

    uint32_t AllocPsram(uint32_t size) override
    {
        if (next + size > kPsramBase + psram.size())
            return 0;
        const uint32_t address = next;
        blocks[address] = size;
        next = (next + size + 15u) & ~15u;
        return address;
    }
    void FreePsram(uint32_t address) override { blocks.erase(address); }
    uint8_t* Bytes(uint32_t address, uint32_t size) override
    {
        EXPECT_GE(address, kPsramBase);
        EXPECT_LE(address - kPsramBase + size, psram.size());
        return psram.data() + (address - kPsramBase);
    }
    uint32_t FindSymbol(const std::string& name) override
    {
        auto it = exports.find(name);
        if (it != exports.end())
            return it->second;
        if (!exportEverything)
            return 0;
        const uint32_t address = kTrapBase + static_cast<uint32_t>(exports.size()) * 4;
        exports[name] = address;
        return address;
    }

    uint32_t Word(uint32_t address)
    {
        uint32_t value = 0;
        std::memcpy(&value, Bytes(address, 4), 4);
        return value;
    }
};

std::vector<uint8_t> ReadFile(const std::string& relative)
{
    std::ifstream file(TestPathHelper::GetTestDataPath(relative), std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

}  // namespace

/// HSD.ELF (HMM2's tiny bench app): the sections, the entry on the I-bus, and the words
TEST(Vdac4EspElfLoader_Test, HsdLayoutAndWords)
{
    const std::vector<uint8_t> elf = ReadFile("vdac4/HSD.ELF");
    ASSERT_EQ(elf.size(), 3744u);
    EXPECT_EQ(EspElfImageProblem(elf.data(), elf.size()), nullptr);

    FakeHost host;
    EspElfImage image;
    std::string error;
    ASSERT_TRUE(EspElfLoad(elf.data(), elf.size(), host, image, error)) << error;

    const EspElfSection& text = image.sections[EspElfImage::Text];
    EXPECT_EQ(text.vaddr, 0x32Cu);
    EXPECT_EQ(text.size, 0x640u) << ".text rounded up to 4";
    EXPECT_EQ(text.addr, kPsramBase);
    // data | rodata | data.rel.ro | bss in that order in the second block (no .data here)
    EXPECT_EQ(image.sections[EspElfImage::Rodata].addr, image.dataBlock);
    EXPECT_EQ(image.sections[EspElfImage::RelRo].addr, image.dataBlock + 0x1E3);
    EXPECT_EQ(image.sections[EspElfImage::Bss].addr, image.dataBlock + 0x1E3 + 0x2C);
    EXPECT_EQ(image.entry, kPsramBase + (0x954 - 0x32C) + kEspIbusOffset);
    EXPECT_EQ(image.relocations, 38u + 2u);
    EXPECT_EQ(image.imports, 2u);

    const uint32_t rodata = image.sections[EspElfImage::Rodata].addr;
    const uint32_t bss = image.sections[EspElfImage::Bss].addr;
    auto at = [&](uint32_t vaddr) { return host.Word(image.MapSymbol(vaddr)); };
    // A pointer into .bss: plain D-bus
    EXPECT_EQ(at(0x348), bss + (0x5C94 - 0x1C64));
    // A pointer to .rodata's first byte: the rounded .text covers it, so the board
    // hands out the I-bus alias of the text byte (the loader checks .text first)
    EXPECT_EQ(at(0x344), text.addr + (0x969 - 0x32C) + kEspIbusOffset);
    // Further into .rodata: D-bus
    EXPECT_EQ(at(0x36C), rodata + (0x97F - 0x969));
    // JMP_SLOT literals: the firmware's export addresses, untouched (outside the app's .text)
    EXPECT_EQ(at(0x368), host.exports.at("memset"));
    EXPECT_EQ(at(0x398), host.exports.at("__udivdi3"));
    // RTLD writes nothing
    uint32_t original = 0;
    std::memcpy(&original, elf.data() + 0x32C, 4);
    EXPECT_EQ(at(0x32C), original);
}

/// DOOM.ELF: the whole relocation table goes through, 28 imports from the firmware
TEST(Vdac4EspElfLoader_Test, DoomLoads)
{
    const std::vector<uint8_t> elf = ReadFile("vdac4/DOOM.ELF");
    ASSERT_EQ(elf.size(), 371812u);
    FakeHost host;
    EspElfImage image;
    std::string error;
    ASSERT_TRUE(EspElfLoad(elf.data(), elf.size(), host, image, error)) << error;
    EXPECT_EQ(image.relocations, 3710u + 28u + 2u);
    EXPECT_EQ(image.imports, 28u);
    EXPECT_EQ(host.exports.size(), 28u);
    EXPECT_EQ(host.exports.count("setjmp"), 1u);
    EXPECT_EQ(image.entry - kEspIbusOffset - image.textBlock, 0xF340u - image.sections[EspElfImage::Text].vaddr);
}

/// A name the firmware does not export fails the load and frees the blocks
TEST(Vdac4EspElfLoader_Test, MissingImportFails)
{
    const std::vector<uint8_t> elf = ReadFile("vdac4/HSD.ELF");
    FakeHost host;
    host.exportEverything = false;
    host.exports["memset"] = kTrapBase;
    EspElfImage image;
    std::string error;
    EXPECT_FALSE(EspElfLoad(elf.data(), elf.size(), host, image, error));
    EXPECT_EQ(error, "can't find symbol __udivdi3");
    EXPECT_TRUE(host.blocks.empty());
}

/// The firmware's image check refuses what its loader would trip on
TEST(Vdac4EspElfLoader_Test, ImageProblems)
{
    std::vector<uint8_t> elf = ReadFile("vdac4/HSD.ELF");
    EXPECT_STREQ(EspElfImageProblem(elf.data(), 10), "short header");
    std::vector<uint8_t> bad = elf;
    bad[18] = 3;  // EM_386
    EXPECT_STREQ(EspElfImageProblem(bad.data(), bad.size()), "not Xtensa");
    bad = elf;
    bad[24] = 0x00;  // entry 0: before .text
    bad[25] = 0x00;
    EXPECT_STREQ(EspElfImageProblem(bad.data(), bad.size()), "entry outside .text");
}
