// The Next with less RAM than the 2 MB of the expanded board (a 512 KB or 1 MB machine for software that needs no more): the MMU banks
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
constexpr unsigned kRamKb512 = 512, kRamKb1024 = 1024;
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
    Machine machine(kRamKb512);
    ASSERT_NE(machine.ports, nullptr);
    EXPECT_EQ(machine.context->config.ramsize, kRamKb512);
    const unsigned pages = kRamKb512 / kKbPerPage;
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

TEST(NextRamSize_Test, OneMegabyteAndTwoMegabytesWireTheirOwnBanks)
{
    constexpr uint8_t kBank1MbEdge = 0x80;  // 128 banks of 8 KB are 1 MB: bank 0x80 is the first the 1 MB board lacks
    {
        Machine one(kRamKb1024);
        ASSERT_NE(one.ports, nullptr);
        one.ports->Board().WriteNextReg(kRegSlot2, kBank1MbEdge - 1);
        EXPECT_STREQ(one.memory->SlotKind(2), "ram");
        one.ports->Board().WriteNextReg(kRegSlot2, kBank1MbEdge);
        EXPECT_STREQ(one.memory->SlotKind(2), "unmapped");
    }
    {
        Machine two(2048);
        ASSERT_NE(two.ports, nullptr);
        two.ports->Board().WriteNextReg(kRegSlot2, kBank1MbEdge);
        EXPECT_STREQ(two.memory->SlotKind(2), "ram") << "the expanded board has them";
    }
}

TEST(NextRamSize_Test, TheSizeIsOfferedByTheModelTable)
{
    unsigned offered = 0;
    for (const auto& model : Config::GetAvailableModels())
        if (model.Model == MM_NEXT)
            offered = model.AvailRAMs;
    EXPECT_TRUE(offered & RAM_512);
    EXPECT_TRUE(offered & RAM_1024);
    EXPECT_TRUE(offered & RAM_2048);
}
