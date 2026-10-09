// The Next's /INT (core/src/emulator/io/z80n/nextinterrupts.h): the pulse-mode ULA and line interrupts, the
// hardware IM2 controller (vectors, priorities, RETI), NR #22 / #23 / #C0-#CA, and the whole path through the CPU

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextctc.h"
#include "emulator/io/z80n/nexti2c.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"

class NextInterrupts_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    NextMemory* _memory = nullptr;
    PortDecoder_Next* _ports = nullptr;
    NextInterruptSource* _int = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = dynamic_cast<NextMemory*>(_context->pMemory);
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_TRUE(_memory && _ports);
        _int = &_ports->Interrupts();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void NextReg(uint8_t reg, uint8_t value)
    {
        _ports->DecodePortOut(0x243B, reg, 0);
        _ports->DecodePortOut(0x253B, value, 0);
    }
    uint8_t ReadReg(uint8_t reg)
    {
        _ports->DecodePortOut(0x243B, reg, 0);
        return _ports->DecodePortIn(0x253B, 0);
    }
};

TEST_F(NextInterrupts_Test, PulseModeIsTheUlaWindow)
{
    // 128K family: INT at 1845, 36 T long
    EXPECT_FALSE(_int->IsIntAsserted(1844));
    EXPECT_TRUE(_int->IsIntAsserted(1845));
    EXPECT_TRUE(_int->IsIntAsserted(1845 + 35));
    EXPECT_FALSE(_int->IsIntAsserted(1845 + 36));
    NextReg(0x22, 0x04);  // NR #22 bit 2: the ULA interrupt is off
    EXPECT_FALSE(_int->IsIntAsserted(1845));
}

TEST_F(NextInterrupts_Test, LineInterruptPulsesAtItsLine)
{
    NextReg(0x23, 100);
    NextReg(0x22, 0x02 | 0x04);  // line interrupt on, ULA off, value 100: counter line 99
    // the counter's origin puts the ULA interrupt (line 1, pixel clock 128) at 1845: 1845 - 292 = 1553
    const uint32_t t = 1553 + 99 * 228;
    EXPECT_FALSE(_int->IsIntAsserted(t - 1));
    EXPECT_TRUE(_int->IsIntAsserted(t));
    EXPECT_FALSE(_int->IsIntAsserted(t + 36));
    NextReg(0x23, 0);  // target 0: the last line of the frame
    NextReg(0x22, 0x02 | 0x04);
    const uint32_t last = (1553 + 310 * 228) % 70908;
    EXPECT_TRUE(_int->IsIntAsserted(last));
}

TEST_F(NextInterrupts_Test, HardwareModeLatchesUntilAcknowledged)
{
    NextReg(0xC0, 0x61);  // vector base #60, hardware mode
    EXPECT_FALSE(_int->IsIntAsserted(100));
    EXPECT_TRUE(_int->IsIntAsserted(1846)) << "the ULA request latches when the position is crossed";
    EXPECT_TRUE(_int->IsIntAsserted(5000)) << "and stays after the pulse length";
    EXPECT_EQ(ReadReg(0xC8) & 1, 1);
    EXPECT_EQ(_int->AcknowledgeInterrupt(5000), 0x60 | (NextInterruptSource::kUla << 1));
    EXPECT_FALSE(_int->IsIntAsserted(5001)) << "served; the next frame brings the next request";
    EXPECT_EQ(ReadReg(0xC8) & 1, 0);
    _int->OnReti();
    EXPECT_EQ(_int->InServiceMask(), 0);
    EXPECT_FALSE(_int->IsIntAsserted(100)) << "the frame rolled over";
    EXPECT_TRUE(_int->IsIntAsserted(1846)) << "next frame";
}

TEST_F(NextInterrupts_Test, PrioritiesAndNesting)
{
    NextReg(0xC0, 0x01);
    NextReg(0xC5, 0xFF);  // CTC 0-7 enabled
    NextReg(0xC4, 0x83);  // ULA and line enabled
    _int->IsIntAsserted(1846);  // ULA pending
    EXPECT_EQ(_int->AcknowledgeInterrupt(1846), (NextInterruptSource::kUla << 1)) << "the ULA is served first";
    EXPECT_FALSE(_int->IsIntAsserted(1847));
    _int->Raise(static_cast<NextInterruptSource::Source>(NextInterruptSource::kCtc0 + 2));
    EXPECT_TRUE(_int->IsIntAsserted(1848)) << "CTC 2 outranks the ULA in service: it may nest";
    EXPECT_EQ(_int->AcknowledgeInterrupt(1848), (NextInterruptSource::kCtc0 + 2) << 1);
    _int->Raise(static_cast<NextInterruptSource::Source>(NextInterruptSource::kCtc0 + 5));
    EXPECT_FALSE(_int->IsIntAsserted(1849)) << "CTC 5 is below the CTC 2 in service";
    _int->Raise(NextInterruptSource::kLine);
    EXPECT_TRUE(_int->IsIntAsserted(1850)) << "the line interrupt outranks everything";
    EXPECT_EQ(_int->AcknowledgeInterrupt(1850), 0);
    _int->OnReti();  // line done
    _int->OnReti();  // CTC 2 done
    EXPECT_TRUE(_int->IsIntAsserted(1851)) << "CTC 5 goes now: only the ULA is in service and CTC 5 outranks it";
    EXPECT_EQ(_int->AcknowledgeInterrupt(1851), (NextInterruptSource::kCtc0 + 5) << 1);
}

TEST_F(NextInterrupts_Test, DisabledSourceDoesNotRequest)
{
    NextReg(0xC0, 0x01);
    _int->Raise(NextInterruptSource::kCtc0);  // NR #C5 bit 0 is clear
    EXPECT_FALSE(_int->IsIntAsserted(10));
    NextReg(0xC5, 0x01);
    _int->Raise(NextInterruptSource::kCtc0);
    EXPECT_TRUE(_int->IsIntAsserted(11));
    NextReg(0xC9, 0x01);  // write 1 to clear
    EXPECT_FALSE(_int->IsIntAsserted(12));
}

// The whole path: IM 2, EI, HALT; the service routine counts the interrupts; the table is read through I and the
// vector of the ULA source; RETI is seen by the controller
TEST_F(NextInterrupts_Test, Im2ThroughTheCpu)
{
    NextReg(0xC0, 0x01);
    auto poke = [&](uint16_t a, uint8_t b) { _memory->PokeSlot(a, b); };
    // vector table: the ULA vector is #00 | 11 << 1 = #16, so the word at #8116 -> #9000
    poke(0x8116, 0x00);
    poke(0x8117, 0x90);
    const uint8_t isr[] = {0x3A, 0x00, 0xA0,  // LD A,(#A000)
                           0x3C,              // INC A
                           0x32, 0x00, 0xA0,  // LD (#A000),A
                           0xFB,              // EI
                           0xED, 0x4D};       // RETI
    for (size_t i = 0; i < sizeof isr; i++)
        poke(static_cast<uint16_t>(0x9000 + i), isr[i]);
    const uint8_t main[] = {0x3E, 0x81,        // LD A,#81
                            0xED, 0x47,        // LD I,A
                            0xED, 0x5E,        // IM 2
                            0xFB,              // EI
                            0x76,              // HALT
                            0x18, 0xFD};       // JR HALT
    for (size_t i = 0; i < sizeof main; i++)
        poke(static_cast<uint16_t>(0x8000 + i), main[i]);
    _z80->pc = 0x8000;
    _z80->sp = 0xBF00;
    for (int f = 0; f < 4; f++)
        _emulator->RunFrame(true);
    EXPECT_GE(_memory->PeekSlot(0xA000), 3) << "one interrupt per frame";
    EXPECT_LE(_memory->PeekSlot(0xA000), 5);
    EXPECT_EQ(_int->InServiceMask(), 0) << "RETI reached the controller";
}

// N5: the CTC (four channels, the 28 MHz clock, the chain) and the I2C bus with the DS1307
TEST(NextCtc_Test, TimerCountsPrescaledSystemClocksAndInterrupts)
{
    NextCtc ctc;
    unsigned fired = 0;
    ctc.onInterrupt = [&](unsigned ch) { fired |= 1u << ch; };
    ctc.Write(0, NextCtc::kInterrupt | NextCtc::kTimeConstantFollows | NextCtc::kControl, 0);  // timer, /16, auto start
    ctc.Write(0, 10, 0);                                                                          // time constant 10
    ctc.Advance(159);
    EXPECT_EQ(fired, 0u);
    EXPECT_EQ(ctc.Read(0, 80), 5);
    ctc.Advance(160);
    EXPECT_EQ(fired, 1u);
    EXPECT_EQ(ctc.Zeros(0), 1u);
    ctc.Advance(160 * 3 + 5);
    EXPECT_EQ(ctc.Zeros(0), 3u) << "reloads and keeps counting";
}

TEST(NextCtc_Test, Prescaler256AndNoInterruptWhenDisabled)
{
    NextCtc ctc;
    unsigned fired = 0;
    ctc.onInterrupt = [&](unsigned ch) { fired |= 1u << ch; };
    ctc.Write(1, NextCtc::kPrescale256 | NextCtc::kTimeConstantFollows | NextCtc::kControl, 0);
    ctc.Write(1, 2, 0);
    ctc.Advance(511);
    EXPECT_EQ(ctc.Zeros(1), 0u);
    ctc.Advance(512);
    EXPECT_EQ(ctc.Zeros(1), 1u);
    EXPECT_EQ(fired, 0u);
}

TEST(NextCtc_Test, ZeroCountOfOneChannelClocksTheNextOne)
{
    NextCtc ctc;
    unsigned fired = 0;
    ctc.onInterrupt = [&](unsigned ch) { fired |= 1u << ch; };
    ctc.Write(1, NextCtc::kInterrupt | NextCtc::kCounterMode | NextCtc::kTimeConstantFollows | NextCtc::kControl, 0);
    ctc.Write(1, 3, 0);  // counter mode: three pulses
    ctc.Write(0, NextCtc::kTimeConstantFollows | NextCtc::kControl, 0);
    ctc.Write(0, 2, 0);  // channel 0 reaches zero every 32 clocks
    ctc.Advance(32 * 2);
    EXPECT_EQ(fired, 0u);
    ctc.Advance(32 * 3);
    EXPECT_EQ(fired, 2u) << "channel 1 saw its third pulse";
}

TEST_F(NextInterrupts_Test, CtcPortsRaiseTheHardwareInterrupt)
{
    NextReg(0xC0, 0x01);
    NextReg(0xC5, 0x01);
    _ports->DecodePortOut(0x183B, 0x85, 0);  // channel 0: interrupt, timer, /16, time constant follows
    _ports->DecodePortOut(0x183B, 0x04, 0);  // 4 x 16 = 64 clocks of 28 MHz = 8 T-states
    _z80->t = 4;
    EXPECT_FALSE(_int->IsIntAsserted(4));
    _z80->t = 40;
    EXPECT_TRUE(_int->IsIntAsserted(40));
    EXPECT_EQ(ReadReg(0xC9) & 1, 1);
    EXPECT_EQ(_int->AcknowledgeInterrupt(40), NextInterruptSource::kCtc0 << 1);
    // channels 4-7 do not exist
    EXPECT_EQ(_ports->DecodePortIn(0x1C3B, 0), 0xFF);
}

namespace
{
struct I2cMaster
{
    NextI2c& bus;
    void Scl(bool v) { bus.Write(NextI2c::kPortScl, v ? 1 : 0); }
    void Sda(bool v) { bus.Write(NextI2c::kPortSda, v ? 1 : 0); }
    bool SdaLevel() { return bus.Read(NextI2c::kPortSda) & 1; }
    void Start()
    {
        Sda(true);
        Scl(true);
        Sda(false);
        Scl(false);
    }
    void Stop()
    {
        Sda(false);
        Scl(true);
        Sda(true);
    }
    bool WriteByte(uint8_t b)  // returns true when acknowledged
    {
        for (int i = 7; i >= 0; i--)
        {
            Sda((b >> i) & 1);
            Scl(true);
            Scl(false);
        }
        Sda(true);
        Scl(true);
        const bool ack = !SdaLevel();
        Scl(false);
        return ack;
    }
    uint8_t ReadByte(bool ack)
    {
        Sda(true);
        uint8_t b = 0;
        for (int i = 0; i < 8; i++)
        {
            Scl(true);
            b = static_cast<uint8_t>((b << 1) | (SdaLevel() ? 1 : 0));
            Scl(false);
        }
        Sda(!ack);
        Scl(true);
        Scl(false);
        Sda(true);
        return b;
    }
};
}  // namespace

TEST(NextI2c_Test, Ds1307ReadsBackTheTimeAndKeepsWrittenBytes)
{
    NextI2c bus;
    I2cMaster m{bus};
    bus.Rtc().SetTime(2026, 10, 9, 23, 45, 6);
    m.Start();
    EXPECT_TRUE(m.WriteByte(0xD0)) << "address #68, write";
    EXPECT_TRUE(m.WriteByte(0x00)) << "register pointer";
    m.Start();
    EXPECT_TRUE(m.WriteByte(0xD1)) << "read";
    EXPECT_EQ(m.ReadByte(true), 0x06);   // seconds
    EXPECT_EQ(m.ReadByte(true), 0x45);   // minutes
    EXPECT_EQ(m.ReadByte(true), 0x23);   // hours
    EXPECT_EQ(m.ReadByte(true), 0x01);   // day of week
    EXPECT_EQ(m.ReadByte(true), 0x09);   // date
    EXPECT_EQ(m.ReadByte(true), 0x10);   // month
    EXPECT_EQ(m.ReadByte(false), 0x26);  // year
    m.Stop();

    // RAM: write 3 bytes at #08, read them back
    m.Start();
    EXPECT_TRUE(m.WriteByte(0xD0));
    EXPECT_TRUE(m.WriteByte(0x08));
    EXPECT_TRUE(m.WriteByte(0xA5));
    EXPECT_TRUE(m.WriteByte(0x5A));
    m.Stop();
    EXPECT_EQ(bus.Rtc().Reg(8), 0xA5);
    EXPECT_EQ(bus.Rtc().Reg(9), 0x5A);
    // another address is not acknowledged
    m.Start();
    EXPECT_FALSE(m.WriteByte(0xA0));
    m.Stop();
}
