// The Next with less RAM than the 2 MB of the expanded board (a 1 MB board of Issue 1): the MMU banks
// beyond the installed RAM are not wired (the slot reads as an unmapped one), the ones below work as always, and everything that sizes
// itself by the RAM (TTD, the pixel inspector, the video renderer) follows config.ramsize.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace
{
constexpr unsigned kRamKb1024 = 1024, kRamKb2048 = 2048, kRamKb4096 = 4096;
constexpr unsigned kAddressableBanks = 256;                 // 8-bit MMU pages of 8 KB: the core's 2 MB
constexpr unsigned kKbPerPage = PAGE_SIZE / 1024;          // a 16 KB RAM page
constexpr unsigned kBanksPerPage = 2;                       // 8 KB MMU banks in a 16 KB page
constexpr uint8_t kRegSlot2 = NextBoard::kRegMmu0 + 2;      // the 8 KB slot at #4000
constexpr uint16_t kSlot2Address = 0x4000;
constexpr uint8_t kByte = 0xA7;

struct Machine
{
    std::shared_ptr<Emulator> emulator;
    EmulatorContext* context = nullptr;
    PortDecoder_Next* ports = nullptr;
    NextMemory* memory = nullptr;
    explicit Machine(unsigned ramKb)
    {
        EmulatorManager* manager = EmulatorManager::GetInstance();
        emulator = manager->CreateEmulatorWithModelAndRAM("next-ram-test", "NEXT", ramKb, LoggerLevel::LogError);
        if (!emulator)
            return;
        context = emulator->GetContext();
        ports = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
        memory = static_cast<NextMemory*>(context->pMemory);
    }
    ~Machine()
    {
        if (emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
    }
};
}  // namespace

TEST(NextRamSize_Test, TheBanksBeyondTheInstalledRamAreNotWired)
{
    Machine machine(kRamKb1024);
    ASSERT_NE(machine.ports, nullptr);
    EXPECT_EQ(machine.context->config.ramsize, kRamKb1024);
    const unsigned pages = kRamKb1024 / kKbPerPage;
    EXPECT_EQ(machine.memory->InstalledRamPages(), pages);

    const unsigned lastBank = pages * kBanksPerPage - 1;  // the last 8 KB bank the chips hold
    NextBoard& board = machine.ports->Board();
    board.WriteNextReg(kRegSlot2, static_cast<uint8_t>(lastBank));
    EXPECT_STREQ(machine.memory->SlotKind(2), "ram");
    machine.memory->DirectWriteToZ80Memory(kSlot2Address, kByte);
    EXPECT_EQ(machine.memory->DirectReadFromZ80Memory(kSlot2Address), kByte);

    board.WriteNextReg(kRegSlot2, static_cast<uint8_t>(lastBank + 1));
    EXPECT_STREQ(machine.memory->SlotKind(2), "unmapped") << "the first bank past the installed RAM";
    machine.memory->DirectWriteToZ80Memory(kSlot2Address, kByte);
    board.WriteNextReg(kRegSlot2, static_cast<uint8_t>(lastBank));
    EXPECT_EQ(machine.memory->DirectReadFromZ80Memory(kSlot2Address), kByte) << "the write to the missing bank went nowhere, the real one is as it was";
}

TEST(NextRamSize_Test, TwoAndFourMegabytesWireTheSameBanksTheCoreAddresses)
{
    constexpr uint8_t kBank1MbEdge = 0x80;  // 128 banks of 8 KB are 1 MB: bank 0x80 is the first the 1 MB board lacks
    const unsigned expectedPages = kRamKb2048 / kKbPerPage;
    {
        Machine two(kRamKb2048);
        ASSERT_NE(two.ports, nullptr);
        EXPECT_EQ(two.memory->InstalledRamPages(), expectedPages);
        two.ports->Board().WriteNextReg(kRegSlot2, kBank1MbEdge);
        EXPECT_STREQ(two.memory->SlotKind(2), "ram") << "the expanded board has them";
    }
    {
        Machine four(kRamKb4096);  // Issue 3: 4 MB; this core's MMU reaches 2 MB, so it is created as the 2 MB machine
        ASSERT_NE(four.ports, nullptr);
        EXPECT_EQ(four.context->config.ramsize, kRamKb2048);
        EXPECT_EQ(four.memory->InstalledRamPages(), expectedPages);
        four.ports->Board().WriteNextReg(kRegSlot2, kBank1MbEdge);
        EXPECT_STREQ(four.memory->SlotKind(2), "ram");
        four.ports->Board().WriteNextReg(kRegSlot2, kAddressableBanks - 1);  // #FF: no ROM in slot 2, and no RAM either (zxnext.vhd)
        EXPECT_STREQ(four.memory->SlotKind(2), "unmapped");
    }
}

TEST(NextRamSize_Test, A4096RequestIsCreatedAs2048WithANote)
{
    std::string note;
    EXPECT_EQ(Config::EffectiveRamSize(MM_NEXT, kRamKb4096, note), kRamKb2048);
    EXPECT_FALSE(note.empty());
    EXPECT_NE(note.find("2048"), std::string::npos) << note;
    note.clear();
    EXPECT_EQ(Config::EffectiveRamSize(MM_NEXT, kRamKb1024, note), kRamKb1024);
    EXPECT_EQ(Config::EffectiveRamSize(MM_TSL, kRamKb4096, note), kRamKb4096) << "another model's 4 MB is its own";
    EXPECT_TRUE(note.empty());
}

TEST(NextRamSize_Test, TheSizeIsOfferedByTheModelTable)
{
    unsigned offered = 0;
    for (const auto& model : Config::GetAvailableModels())
        if (model.Model == MM_NEXT)
            offered = model.AvailRAMs;
    EXPECT_FALSE(offered & RAM_512) << "no 512 KB board exists";
    EXPECT_TRUE(offered & RAM_1024);
    EXPECT_TRUE(offered & RAM_2048);
    EXPECT_FALSE(offered & RAM_4096) << "not an offered size: no menu entry, not in available_ram_sizes_kb";
}
