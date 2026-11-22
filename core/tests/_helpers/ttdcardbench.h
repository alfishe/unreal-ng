#pragma once

/// @file ttdcardbench.h
/// @brief A machine for TTD positions on a sound card's CPU (gs-debugger design §8): the card (classic GS, NeoGS or
/// the ZX-MultiSound's GS), time travel on, the main CPU in an LDIR loop that reads the card's status port, and the
/// forward-run oracle (GSCardTarget's scan copying the card at one instruction count while the recording runs).
///
/// Example: `ttdtest::CardBench b; ttdtest::BuildCardBench(b, ttdtest::Card::NeoGS); ttdtest::RecordCardBench(b, 4);`

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/ttdslotcards.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/targets/gscardtarget.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"

namespace ttdtest
{

enum class Card
{
    GS,          ///< the classic General Sound (its own Z80)
    NeoGS,       ///< NeoGS
    MultiSound,  ///< the ZX-MultiSound's General Sound (DebugCpuId::MultiSoundGS)
};

inline const char* CardName(Card card)
{
    switch (card)
    {
        case Card::GS:
            return "GS";
        case Card::NeoGS:
            return "NeoGS";
        default:
            return "MultiSound";
    }
}

inline constexpr uint16_t kLdirPc = 0x8009;

/// A machine with the card, the LDIR loop, and time travel on
struct Bench
{
    std::optional<SoundCardScope> scope;
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;
    ttd::TimeTravelController* ttd = nullptr;
    DebugCpuId cpu = DebugCpuId::GS;
    GSCardTarget* target = nullptr;

    Bench() = default;
    Bench(const Bench&) = delete;
    Bench& operator=(const Bench&) = delete;
    ~Bench()
    {
        if (emulator)
            EmulatorTestHelper::CleanupEmulator(emulator);
    }

    GeneralSoundCard* card() const { return target ? target->Card() : nullptr; }
    Z80* z80() const { return context->pCore->GetZ80(); }

    uint64_t Count() const
    {
        uint64_t count = 0;
        EXPECT_TRUE(card()->cardInstructionCount(count));
        return count;
    }

    GSCardTarget::CardState CardNow() const
    {
        GSCardTarget::CardState state;
        GSCardTarget::SaveCardState(card(), state);
        return state;
    }

    /// The registers a card state holds (loaded into the card for the read, then the card put back)
    Z80CpuRegisters RegistersOf(const GSCardTarget::CardState& state) const
    {
        const GSCardTarget::CardState now = CardNow();
        GSCardTarget::LoadCardState(card(), state);
        Z80CpuRegisters regs{};
        card()->cardRegisters(regs);
        GSCardTarget::LoadCardState(card(), now);
        return regs;
    }
};

/// LDIR #C000 -> #C001 1 KB, IN A,(#BB), again (interrupts off)
inline void WriteProgram(Bench& b)
{
    static const uint8_t program[] = {
        0xF3,              // 7FFF DI
        0x21, 0x00, 0xC0,  // 8000 LD HL,#C000
        0x11, 0x01, 0xC0,  // 8003 LD DE,#C001
        0x01, 0x00, 0x04,  // 8006 LD BC,#0400
        0xED, 0xB0,        // 8009 LDIR
        0xDB, 0xBB,        // 800B IN A,(#BB)
        0x18, 0xF1,        // 800D JR #8000
    };
    Z80* z80 = b.z80();
    for (size_t i = 0; i < sizeof(program); i++)
        z80->DirectWrite(static_cast<uint16_t>(0x7FFF + i), program[i]);
    z80->pc = 0x7FFF;
}

/// Builds the machine (PENTAGON unless given), its card and the program; false with a failure recorded
inline bool Build(Bench& b, Card card, const std::string& model = "PENTAGON")
{
    if (card == Card::MultiSound)
    {
        // The card in ZX-bus slot 1 (the slot set of a create request), no MIDI bank (its synthesizer is not used)
        ttd::TTDRecordedMachine machine;
        machine.model = model;
        machine.devices.push_back({{ttd::TTDDeviceType::MultiSound, "zxbus.1.multisound"}, ttd::PeripheralId::MultiSound});
        std::function<void(CONFIG&)> slots;
        std::string error;
        if (!ttdtest::SlotSetOverride(ttdtest::SlotCardsOf(machine), slots, error))
        {
            ADD_FAILURE() << "slot set: " << error;
            return false;
        }
        b.emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Random, slots);
        b.cpu = DebugCpuId::MultiSoundGS;
    }
    else
    {
        b.scope.emplace(TestSound::GeneralSound);
        b.emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        b.cpu = DebugCpuId::GS;
    }
    if (!b.emulator)
    {
        ADD_FAILURE() << "no " << model;
        return false;
    }
    b.context = b.emulator->GetContext();
    b.ttd = b.context->pTimeTravelController;
    if (card != Card::MultiSound &&
        !FitGeneralSoundCard(b.context->pSoundManager, card == Card::GS ? GSTypeKind::Z80 : GSTypeKind::NGS))
    {
        ADD_FAILURE() << "cannot fit the " << CardName(card);
        return false;
    }
    FeatureManager* features = b.emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    b.context->pMemory->UpdateFeatureCache();
    b.target = b.context->pDebugManager->GetCardTarget(b.cpu);
    if (!b.target || !b.card())
    {
        ADD_FAILURE() << "no card CPU target for the " << CardName(card);
        return false;
    }
    b.emulator->RunNFrames(3);
    WriteProgram(b);
    b.emulator->RunNFrames(1);
    return true;
}

/// The card's own program for the step over / step out tests: nested routines in the card's RAM at #4000 (the classic
/// card's RAM page 1, the NeoGS's RAM page 3), interrupts off, so the card calls and returns all the time (the
/// firmware right after its boot runs its RAM test without a CALL for frames on end):
///   #4000 CALL #4010 / #4003 NOP / #4004 JR #4000
///   #4010 CALL #4020 / #4013 NOP / #4014 RET
///   #4020 NOP / NOP / NOP / #4023 RET
inline bool LoadCardRoutines(Bench& b)
{
    struct Chunk
    {
        uint16_t address;
        std::vector<uint8_t> bytes;
    };
    const Chunk chunks[] = {
        {0x4000, {0xCD, 0x10, 0x40, 0x00, 0x18, 0xFA}},
        {0x4010, {0xCD, 0x20, 0x40, 0x00, 0xC9}},
        {0x4020, {0x00, 0x00, 0x00, 0xC9}},
    };
    GeneralSoundCard* card = b.card();
    for (const Chunk& chunk : chunks)
        for (size_t i = 0; i < chunk.bytes.size(); i++)
        {
            const uint16_t address = static_cast<uint16_t>(chunk.address + i);
            uint8_t back = 0;
            if (!card->pokeCardMemory(address, chunk.bytes[i]) || !card->peekCardMemory(address, back) ||
                back != chunk.bytes[i])
            {
                ADD_FAILURE() << "the card's RAM at " << address << " takes no program byte";
                return false;
            }
        }
    Z80CpuRegisters regs{};
    if (!card->cardRegisters(regs))
    {
        ADD_FAILURE() << "no card registers";
        return false;
    }
    regs.pc = 0x4000;
    regs.sp = 0x7F00;
    regs.iff1 = regs.iff2 = 0;
    regs.halted = 0;
    if (!card->setCardRegisters(regs))
    {
        ADD_FAILURE() << "the card's registers take no program start";
        return false;
    }
    b.emulator->RunNFrames(1);
    return true;
}

/// The card's instructions per frame where the machine stands (one frame run)
inline uint64_t PerFrame(Bench& b)
{
    const uint64_t from = b.Count();
    b.emulator->RunNFrames(1);
    return b.Count() - from;
}

/// Records @p frames frames; with @p captureAt the recording's own run copies the card at that count (the oracle)
inline GSCardTarget::ScanCapture Record(Bench& b, int frames, std::optional<uint64_t> captureAt = std::nullopt)
{
    EXPECT_TRUE(b.ttd->StartRecording());
    GSCardTarget::ScanCapture capture;
    if (captureAt)
    {
        const GSCardTarget::ScanScope scan(*b.target, *captureAt);
        b.emulator->RunNFrames(frames);
        capture = b.target->Capture();
    }
    else
        b.emulator->RunNFrames(frames);
    b.ttd->StopRecording();
    return capture;
}

/// Where a capture puts the main CPU: its instruction start (frame, TTD t) and that instruction's PC
inline ttd::TTDTimePoint MainBoundaryOf(const GSCardTarget::ScanCapture& c)
{
    const uint64_t frame = (c.frameEnd || c.t < c.instrStartT) && c.frameCounter > 0 ? c.frameCounter - 1 : c.frameCounter;
    return {frame, c.ttdInstrStart};
}

/// The card's whole state equal: blob byte for byte, every region
inline void ExpectSameCard(const GSCardTarget::CardState& actual, const GSCardTarget::CardState& expected, const std::string& what)
{
    ASSERT_TRUE(actual.valid && expected.valid) << what;
    ASSERT_EQ(actual.blob.size(), expected.blob.size()) << what;
    for (size_t i = 0; i < actual.blob.size(); i++)
        if (actual.blob[i] != expected.blob[i])
        {
            ADD_FAILURE() << what << ": the card's blob differs first at byte " << i << " (" << int(actual.blob[i])
                          << " vs " << int(expected.blob[i]) << ")";
            break;
        }
    ASSERT_EQ(actual.regions.size(), expected.regions.size()) << what;
    for (size_t r = 0; r < actual.regions.size(); r++)
        EXPECT_TRUE(actual.regions[r] == expected.regions[r]) << what << ": card memory region " << r << " differs";
}

inline void ExpectSameRegisters(const Z80CpuRegisters& a, const Z80CpuRegisters& e, const std::string& what)
{
    EXPECT_EQ(a.af, e.af) << what;
    EXPECT_EQ(a.bc, e.bc) << what;
    EXPECT_EQ(a.de, e.de) << what;
    EXPECT_EQ(a.hl, e.hl) << what;
    EXPECT_EQ(a.afAlt, e.afAlt) << what;
    EXPECT_EQ(a.bcAlt, e.bcAlt) << what;
    EXPECT_EQ(a.deAlt, e.deAlt) << what;
    EXPECT_EQ(a.hlAlt, e.hlAlt) << what;
    EXPECT_EQ(a.ix, e.ix) << what;
    EXPECT_EQ(a.iy, e.iy) << what;
    EXPECT_EQ(a.pc, e.pc) << what;
    EXPECT_EQ(a.sp, e.sp) << what;
    EXPECT_EQ(a.memptr, e.memptr) << what;
    EXPECT_EQ(a.i, e.i) << what;
    EXPECT_EQ(a.r, e.r) << what;
    EXPECT_EQ(a.im, e.im) << what;
    EXPECT_EQ(a.iff1, e.iff1) << what;
    EXPECT_EQ(a.iff2, e.iff2) << what;
    EXPECT_EQ(a.halted, e.halted) << what;
}

/// The machine at a card position equal to the oracle's capture: the card's state, the main CPU at the capture's
/// instruction start
inline void ExpectAtCapture(Bench& b, const GSCardTarget::ScanCapture& oracle, const std::string& what)
{
    ExpectSameCard(b.CardNow(), oracle.state, what);
    ExpectSameRegisters(b.RegistersOf(b.CardNow()), b.RegistersOf(oracle.state), what);
    const ttd::TTDTimePoint boundary = MainBoundaryOf(oracle);
    EXPECT_EQ(b.ttd->CurrentPosition().frame, boundary.frame) << what;
    EXPECT_EQ(b.ttd->CurrentPosition().tInFrame, boundary.tInFrame) << what;
    EXPECT_EQ(b.z80()->pc, oracle.m1pc) << what;
    const ttd::TTDCardPosition p = b.ttd->CardPositionNow(b.cpu);
    EXPECT_TRUE(p.cardPosition) << what;
    EXPECT_EQ(p.instruction, oracle.count) << what;
    EXPECT_EQ(p.pc, oracle.pc) << what;
    EXPECT_EQ(p.mainPc, oracle.m1pc) << what;
}

}  // namespace ttdtest
