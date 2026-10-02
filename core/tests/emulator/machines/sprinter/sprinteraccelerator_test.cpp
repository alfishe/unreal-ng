#include "sprinterfixture.h"

#include "emulator/memory/sprinter/sprinteraccelerator.h"
#include "emulator/video/sprinter/sprintervideoram.h"

/// T-ACC (Sprinter test-plan §2.8, tdd-accel-sound-input §1): the block
/// accelerator on the Z84C15 engine. Code runs from window 2 (#8000, RAM); the
/// data lives in window 1 (#4000, RAM cell #E9 or a graphics page). The fixture
/// clears the Z84C15 waits, so at 3.5 MHz every instruction has its book timing.
class SprinterAccelerator_Test : public SprinterFixture
{
protected:
    void SetUp() override
    {
        SprinterFixture::SetUp();
        OpenDcp();
        Pld().allMode = 0x01;  // ALL_MODE bit 0: accelerator on (also turns the Spectrum shadow off)
        ASSERT_NE(Acc(), nullptr);
        ASSERT_TRUE(IsRam(0x4000));
        ASSERT_TRUE(IsRam(0x8000));
    }

    SprinterAccelerator* Acc() { return _decoder->GetAccelerator(); }
    SprinterAccelState& St() { return Acc()->State(); }

    uint8_t Page1() { return Pld().Cell(0xE9); }
    uint8_t& Data(uint16_t addr) { return Ram(Page1(), static_cast<uint16_t>(addr - 0x4000)); }

    /// Run `code` and return the CPU clocks it took
    uint32_t Timed(const std::vector<uint8_t>& code, uint32_t startClock = 600)
    {
        _z80->t = startClock;
        RunCode(code);
        return _z80->t - startClock;
    }

    /// LD HL,addr : LD A,value : LD D,D : LD E,length : <mode> : <store or load ...> : LD B,B
    static std::vector<uint8_t> Program(uint16_t hl, uint8_t a, uint8_t length, uint8_t mode, std::vector<uint8_t> body)
    {
        std::vector<uint8_t> code = {0x21, static_cast<uint8_t>(hl), static_cast<uint8_t>(hl >> 8), 0x3E, a,
                                     0x52, 0x1E, length, mode};
        code.insert(code.end(), body.begin(), body.end());
        code.push_back(0x40);
        return code;
    }

    void SetTurbo21()
    {
        SetCode(0x007C, false, SprinterCode::SysCnf);
        Out(0x007C, 0x03);  // CNF/SYS bit 1 = 1: turbo = bit 0
        ASSERT_EQ(_context->emulatorState.hw_turbo_ratio, 6);
    }
};

// Mode select: same-register LD r,r on an unprefixed opcode fetch; HALT is "off"
TEST_F(SprinterAccelerator_Test, ModeSelect_SameRegisterLdOnly)
{
    const uint8_t opcodes[8] = {0x40, 0x49, 0x52, 0x5B, 0x64, 0x6D, 0x76, 0x7F};
    for (uint8_t m = 0; m < 8; m++)
    {
        Acc()->OnOpcodeFetch(0x8000, 0x49);
        Acc()->OnOpcodeFetch(0x8000, opcodes[m]);
        EXPECT_EQ(St().mode, m) << "opcode " << int(opcodes[m]);
        EXPECT_EQ(St().dir, SprinterAccelerator::kDir[m]);
        EXPECT_EQ(Acc()->watchData, SprinterAccelerator::kDir[m] != 0);
    }
    EXPECT_EQ(St().dir, 0x17) << "LD A,A: vertical copy";

    // Not a mode: LD B,C (#41), LD (HL),B (#70), a prefixed LD C,C (DD 49, CB 49, ED 49)
    Acc()->OnOpcodeFetch(0x8000, 0x49);
    for (uint8_t other : {0x41, 0x70, 0x00, 0xFF})
    {
        Acc()->OnOpcodeFetch(0x8000, other);
        EXPECT_EQ(St().mode, 1) << int(other);
    }
    for (uint8_t prefix : {0xDD, 0xFD, 0xCB, 0xED})
    {
        Acc()->OnOpcodeFetch(0x8000, prefix);
        Acc()->OnOpcodeFetch(0x8000, 0x52);
        EXPECT_EQ(St().mode, 1) << "prefix " << int(prefix);
    }
    Acc()->OnOpcodeFetch(0x8000, 0x52);
    EXPECT_EQ(St().mode, 2) << "unprefixed again";
}

// ALL_MODE bit 0 = 0: no mode can be set, and clearing it disarms (ACC_MODE.clrn = ACC_ENA)
TEST_F(SprinterAccelerator_Test, AllModeBit0_DisablesAndClears)
{
    Acc()->OnOpcodeFetch(0x8000, 0x49);
    ASSERT_EQ(St().mode, 1);
    Pld().allMode = 0x00;
    Data(0x4100) = 0x00;
    Data(0x4101) = 0x00;
    RunCode({0x21, 0x00, 0x41, 0x3E, 0xAA, 0x77});  // LD HL,#4100 : LD A,#AA : LD (HL),A
    EXPECT_EQ(Data(0x4100), 0xAA);
    EXPECT_EQ(Data(0x4101), 0x00) << "plain store";
    EXPECT_EQ(St().mode, 0);
    Acc()->OnOpcodeFetch(0x8000, 0x49);
    EXPECT_EQ(St().mode, 0) << "no mode while disabled";
}

// The logic function follows every opcode fetch (ACCELER.TDF FN_ACC): #A6 AND, #AE XOR, #B6 OR, #BE plain;
// #86 / #8E / #96 alias them; any other opcode is plain
TEST_F(SprinterAccelerator_Test, Function_SetByEveryFetch)
{
    const struct { uint8_t op; uint8_t fn; } cases[] = {
        {0xA6, 3}, {0xAE, 2}, {0xB6, 1}, {0xBE, 0}, {0x86, 3}, {0x8E, 2}, {0x96, 1}, {0x9E, 0}, {0xA0, 3}, {0x7E, 0}, {0x00, 0},
    };
    for (const auto& c : cases)
    {
        Acc()->OnOpcodeFetch(0x8000, c.op);
        EXPECT_EQ(St().fn & 3, c.fn) << int(c.op);
    }
    Acc()->OnOpcodeFetch(0x8000, 0xDD);
    Acc()->OnOpcodeFetch(0x8000, 0xA6);
    EXPECT_EQ(St().fn, 0) << "AND (IX+d): prefixed, plain";
}

// Length: LD D,D then any operand / data access loads it (here LD E,n's operand)
TEST_F(SprinterAccelerator_Test, Length_LoadedByTheOperandRead)
{
    RunCode({0x52, 0x1E, 0x07, 0x40});  // LD D,D : LD E,7 : LD B,B
    EXPECT_EQ(St().length, 7);
    RunCode({0x52, 0x21, 0x34, 0x12, 0x40});  // LD D,D : LD HL,#1234: both operand bytes, the last wins
    EXPECT_EQ(St().length, 0x12);
}

// Fill: LD C,C : LD (HL),A stores A length times from HL; 3.5 MHz charge ceil((n-1) x 6 / 12)
TEST_F(SprinterAccelerator_Test, Fill_StoresLengthBytes_ChargesTheExtraAccesses)
{
    for (uint16_t a = 0x4100; a < 0x4108; a++)
        Data(a) = 0x11;
    const uint32_t accelerated = Timed(Program(0x4100, 0xAA, 5, 0x49, {0x77}));
    for (uint16_t a = 0x4100; a < 0x4105; a++)
        EXPECT_EQ(Data(a), 0xAA) << std::hex << a;
    EXPECT_EQ(Data(0x4105), 0x11);
    EXPECT_EQ(_z80->hl, 0x4100) << "the CPU's registers do not move";
    EXPECT_EQ(St().lastExtraClocks, 2u);

    // Same program with length 1: no extra access, no charge
    const uint32_t single = Timed(Program(0x4100, 0xBB, 1, 0x49, {0x77}));
    EXPECT_EQ(Data(0x4100), 0xBB);
    EXPECT_EQ(Data(0x4101), 0xAA);
    EXPECT_EQ(accelerated - single, 2u);
    // Book timing of the single run: 10 + 7 + 4 + 7 + 4 + 7 + 4
    EXPECT_EQ(single, 43u);
}

TEST_F(SprinterAccelerator_Test, Fill_LengthZeroIs256)
{
    for (uint32_t a = 0x4100; a < 0x4202; a++)
        Data(static_cast<uint16_t>(a)) = 0x11;
    Timed(Program(0x4100, 0x5C, 0, 0x49, {0x77}));
    for (uint32_t a = 0x4100; a < 0x4200; a++)
        ASSERT_EQ(Data(static_cast<uint16_t>(a)), 0x5C) << std::hex << a;
    EXPECT_EQ(Data(0x4200), 0x11);
    EXPECT_EQ(St().lastExtraClocks, (255u * 6u + 11u) / 12u);
}

TEST_F(SprinterAccelerator_Test, ExtraClocks_PerCpuClock)
{
    EXPECT_EQ(SprinterAccelerator::ExtraClocks(1, 6), 0u);
    EXPECT_EQ(SprinterAccelerator::ExtraClocks(2, 6), 3u);
    EXPECT_EQ(SprinterAccelerator::ExtraClocks(256, 6), 765u);
    EXPECT_EQ(SprinterAccelerator::ExtraClocks(2, 1), 1u);
    EXPECT_EQ(SprinterAccelerator::ExtraClocks(3, 1), 1u);
    EXPECT_EQ(SprinterAccelerator::ExtraClocks(256, 1), 128u);
}

// 21 MHz: 3 CPU clocks per extra access, on top of the turbo waits (which keep their 6-clock phase)
TEST_F(SprinterAccelerator_Test, Fill_At21MHz_ThreeClocksPerExtraAccess)
{
    SetTurbo21();
    const uint32_t five = Timed(Program(0x4100, 0xAA, 5, 0x49, {0x77}), 600);
    EXPECT_EQ(St().lastExtraClocks, 12u);
    const uint32_t one = Timed(Program(0x4100, 0xAA, 1, 0x49, {0x77}), 600);
    EXPECT_EQ(five - one, 12u);
}

// Copy: LD L,L : LD A,(HL) loads the buffer (A = the last byte read), LD (HL),A stores the buffer
TEST_F(SprinterAccelerator_Test, Copy_LoadsAndStoresTheBuffer)
{
    for (uint16_t i = 0; i < 8; i++)
    {
        Data(static_cast<uint16_t>(0x4200 + i)) = static_cast<uint8_t>(0x30 + i);
        Data(static_cast<uint16_t>(0x4300 + i)) = 0x11;
    }
    // LD HL,#4200 : LD D,D : LD A,6 (length 6) : LD L,L : LD A,(HL) : LD B,B
    RunCode({0x21, 0x00, 0x42, 0x52, 0x3E, 0x06, 0x6D, 0x7E, 0x40});
    EXPECT_EQ(_z80->a, 0x35) << "the CPU gets the last byte read";
    RunCode({0x21, 0x00, 0x43, 0x6D, 0x77, 0x40});  // LD HL,#4300 : LD L,L : LD (HL),A : LD B,B
    for (uint16_t i = 0; i < 6; i++)
        EXPECT_EQ(Data(static_cast<uint16_t>(0x4300 + i)), 0x30 + i) << i;
    EXPECT_EQ(Data(0x4306), 0x11);
}

// Copy-mode operand reads load the buffer too: the reason programs disarm with LD B,B between the steps
TEST_F(SprinterAccelerator_Test, Copy_OperandReadsAreAccelerated)
{
    RunCode({0x52, 0x3E, 0x02, 0x6D, 0x3E, 0x99, 0x40});  // length 2 : LD L,L : LD A,#99 : LD B,B
    // The operand of LD A,n sits at #8005; the PLD reads #8005 and #8006 and the CPU gets #8006 (LD B,B = #40)
    EXPECT_EQ(_z80->a, 0x40);
}

// AND / XOR / OR combine the loaded bytes with the buffer; CP (HL) is a plain load
TEST_F(SprinterAccelerator_Test, LogicFunctions_CombineWithTheBuffer)
{
    const uint8_t src1[4] = {0xF0, 0x0F, 0xAA, 0xFF};
    const uint8_t src2[4] = {0x3C, 0x3C, 0x0F, 0x00};
    struct Case { uint8_t op; uint8_t (*f)(uint8_t, uint8_t); };
    const Case cases[] = {
        {0xA6, [](uint8_t a, uint8_t b) { return static_cast<uint8_t>(a & b); }},
        {0xAE, [](uint8_t a, uint8_t b) { return static_cast<uint8_t>(a ^ b); }},
        {0xB6, [](uint8_t a, uint8_t b) { return static_cast<uint8_t>(a | b); }},
        {0xBE, [](uint8_t, uint8_t b) { return b; }},
        {0x86, [](uint8_t a, uint8_t b) { return static_cast<uint8_t>(a & b); }},  // ADD A,(HL): the PLD's AND alias
    };
    for (const Case& c : cases)
    {
        for (uint16_t i = 0; i < 4; i++)
        {
            Data(static_cast<uint16_t>(0x4200 + i)) = src1[i];
            Data(static_cast<uint16_t>(0x4210 + i)) = src2[i];
        }
        // LD D,D : LD A,4 : LD B,B (length 4; disarmed, or LD HL's operands would load it again)
        // : LD HL,#4200 : LD L,L : LD A,(HL) : LD B,B : LD HL,#4210 : LD L,L : <op> : LD B,B
        // : LD HL,#4220 : LD L,L : LD (HL),A : LD B,B
        RunCode({0x52, 0x3E, 0x04, 0x40, 0x21, 0x00, 0x42, 0x6D, 0x7E, 0x40, 0x21, 0x10, 0x42, 0x6D, c.op, 0x40,
                 0x21, 0x20, 0x42, 0x6D, 0x77, 0x40});
        for (uint16_t i = 0; i < 4; i++)
            EXPECT_EQ(Data(static_cast<uint16_t>(0x4220 + i)), c.f(src1[i], src2[i])) << "op " << int(c.op) << " i " << i;
    }
}

// The function lasts one instruction (PLD): a plain LD A,(HL) after AND (HL) loads plainly
// (MAME keeps the function until the next mode select)
TEST_F(SprinterAccelerator_Test, LogicFunctions_LastOneInstruction)
{
    for (uint16_t i = 0; i < 2; i++)
    {
        Data(static_cast<uint16_t>(0x4200 + i)) = 0x0F;
        Data(static_cast<uint16_t>(0x4210 + i)) = 0xF0;
    }
    // length 2 : HL=#4200 : LD L,L : AND (HL) : LD B,B : HL=#4210 : LD L,L : LD A,(HL) : LD B,B
    RunCode({0x52, 0x3E, 0x02, 0x40, 0x21, 0x00, 0x42, 0x6D, 0xA6, 0x40, 0x21, 0x10, 0x42, 0x6D, 0x7E, 0x40});
    EXPECT_EQ(St().buffer[2], 0xF0);
    EXPECT_EQ(St().buffer[1], 0xF0);
}

// Vertical fill on a graphics page: the address stays, PORT_Y advances; the store reaches video RAM
TEST_F(SprinterAccelerator_Test, VerticalFill_GraphicsPage_PortYAdvances)
{
    Pld().Cell(0xE9) = 0x50;
    _decoder->UpdateBanks();
    Pld().portY = 10;
    SprinterVideoRam& vram = _decoder->GetVideoRam();

    RunCode(Program(0x4005, 0x6E, 4, 0x5B, {0x77}));  // LD E,E : LD (HL),A at column 5
    for (uint32_t y = 10; y < 14; y++)
    {
        EXPECT_EQ(vram.Read(y * 1024 + 5), 0x6E) << y;
        EXPECT_EQ(_memory->RAMBase()[0x50 * PAGE_SIZE + y * 1024 + 5], 0x6E) << y;
    }
    EXPECT_EQ(vram.Read(14 * 1024 + 5), 0x00);
    EXPECT_EQ(vram.Read(10 * 1024 + 6), 0x00) << "the column does not move";
    EXPECT_EQ(Pld().portY, 14) << "PORT_Y +1 per access";
}

// Vertical copy: a column is read down PORT_Y into the buffer and written down another column
TEST_F(SprinterAccelerator_Test, VerticalCopy_ColumnToColumn)
{
    Pld().Cell(0xE9) = 0x50;
    _decoder->UpdateBanks();
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    for (uint32_t y = 0; y < 3; y++)
        _memory->RAMBase()[0x50 * PAGE_SIZE + (20 + y) * 1024 + 7] = static_cast<uint8_t>(0xC0 + y);

    Pld().portY = 20;
    // length 3 : HL=#4007 : LD A,A : LD A,(HL) : LD B,B
    RunCode({0x52, 0x3E, 0x03, 0x40, 0x21, 0x07, 0x40, 0x7F, 0x7E, 0x40});
    EXPECT_EQ(_z80->a, 0xC2);
    EXPECT_EQ(Pld().portY, 23);
    Pld().portY = 40;
    RunCode({0x21, 0x09, 0x40, 0x7F, 0x77, 0x40});  // HL=#4009 : LD A,A : LD (HL),A : LD B,B
    for (uint32_t y = 0; y < 3; y++)
        EXPECT_EQ(vram.Read((40 + y) * 1024 + 9), 0xC0 + y) << y;
}

// Transparent graphics page (#58): a buffer byte #FF is not stored, as for a CPU write
TEST_F(SprinterAccelerator_Test, Fill_TransparentPageSkipsFF)
{
    Pld().Cell(0xE9) = 0x58;
    _decoder->UpdateBanks();
    Pld().portY = 0;
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    vram.Write(0x10, 0x12);
    RunCode(Program(0x4010, 0xFF, 3, 0x49, {0x77}));
    EXPECT_EQ(vram.Read(0x10), 0x12);
    EXPECT_EQ(vram.Read(0x11), 0x00);
}

// LD H,H: a write also stores the other byte of the 16-bit word (MAME addr ^ 1); no extra time
TEST_F(SprinterAccelerator_Test, Double_StoresTheOtherByteLane)
{
    Data(0x4100) = Data(0x4101) = Data(0x4102) = Data(0x4103) = 0x11;
    RunCode(Program(0x4100, 0x77, 5, 0x64, {0x77}));
    EXPECT_EQ(Data(0x4100), 0x77);
    EXPECT_EQ(Data(0x4101), 0x77);
    EXPECT_EQ(Data(0x4102), 0x11) << "not a block: one word";
    RunCode(Program(0x4103, 0x66, 5, 0x64, {0x77}));
    EXPECT_EQ(Data(0x4102), 0x66);
    EXPECT_EQ(Data(0x4103), 0x66);
    EXPECT_EQ(St().lastExtraClocks, 0u);
}

// ROM windows are not reached: the block runs (and takes its time), the ROM stays
TEST_F(SprinterAccelerator_Test, Fill_RomWindowNotReached)
{
    ASSERT_FALSE(IsRam(0x0000));
    const uint8_t before = _memory->DirectReadFromZ80Memory(0x0101);
    RunCode(Program(0x0100, 0x00, 4, 0x49, {0x77}));
    EXPECT_EQ(_memory->DirectReadFromZ80Memory(0x0101), before);
    EXPECT_EQ(St().lastExtraClocks, 2u);
}

/// region <INT suspend (tdd-accel-sound-input §1.3, ACCELER.TDF ACC_BLK)>

// An INT acknowledge blocks; the handler's stores (here the pushes) are plain; the mode is kept
TEST_F(SprinterAccelerator_Test, IntSuspend_AcknowledgeBlocks_PushesArePlain)
{
    ASSERT_EQ(_context->config.sprinter.accel_int_suspend, 1) << "default on";
    RunCode({0x52, 0x3E, 0x04, 0x49});  // len 4 : LD C,C (left armed)
    for (uint16_t a = 0x40F8; a < 0x4108; a++)
        Data(a) = 0x11;
    _z80->sp = 0x4100;
    _z80->im = 1;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8234;
    _z80->HandleINT(0xFF);
    EXPECT_EQ(St().blocked, 1);
    EXPECT_EQ(St().mode, 1) << "the mode is kept";
    EXPECT_EQ(Data(0x40FF), 0x82);
    EXPECT_EQ(Data(0x40FE), 0x34);
    EXPECT_EQ(Data(0x4100), 0x11) << "the push was not a fill";
    EXPECT_EQ(Data(0x40FD), 0x11);
}

TEST_F(SprinterAccelerator_Test, IntSuspendOff_PushesAreFilled)
{
    _context->config.sprinter.accel_int_suspend = 0;
    RunCode({0x52, 0x3E, 0x04, 0x49});
    for (uint16_t a = 0x40F8; a < 0x4108; a++)
        Data(a) = 0x11;
    _z80->sp = 0x4100;
    _z80->im = 1;
    _z80->iff1 = _z80->iff2 = 1;
    _z80->pc = 0x8234;
    _z80->HandleINT(0xFF);
    EXPECT_EQ(St().blocked, 0);
    // PUSH: (SP-1) = #82 filled up to #4102, then (SP-2) = #34 filled up to #4101 (MAME's behavior)
    EXPECT_EQ(Data(0x40FE), 0x34);
    EXPECT_EQ(Data(0x4101), 0x34);
    EXPECT_EQ(Data(0x4102), 0x82);
    EXPECT_EQ(Data(0x4103), 0x11);
}

// While blocked a store is plain; RETI's own pops are still blocked; the first fetch after RETI unblocks
TEST_F(SprinterAccelerator_Test, IntSuspend_RetiUnblocksAtTheNextFetch)
{
    RunCode({0x21, 0x00, 0x41, 0x3E, 0xAA, 0x52, 0x1E, 0x03, 0x49});  // HL = #4100, A = #AA, length 3, LD C,C
    Acc()->OnInterruptAcknowledge();
    ASSERT_EQ(St().blocked, 1);
    for (uint16_t a = 0x4100; a < 0x4108; a++)
        Data(a) = 0x11;

    RunCode({0x77});  // LD (HL),A in the "handler": plain, and an LD r,r there still changes the mode
    EXPECT_EQ(Data(0x4100), 0xAA);
    EXPECT_EQ(Data(0x4101), 0x11);
    EXPECT_EQ(St().mode, 1);

    // RETN (ED 45) does not unblock
    Data(0x40F0) = 0x04;
    Data(0x40F1) = 0x80;  // return address #8004
    _z80->sp = 0x40F0;
    _memory->DirectWriteToZ80Memory(0x8000, 0xED);
    _memory->DirectWriteToZ80Memory(0x8001, 0x45);
    _memory->DirectWriteToZ80Memory(0x8004, 0x00);
    _z80->pc = 0x8000;
    Step();
    ASSERT_EQ(_z80->pc, 0x8004);
    Step();  // NOP
    EXPECT_EQ(St().blocked, 1) << "RETN does not unblock";

    // RETI (ED 4D): its pops are plain (blocked), then the next instruction's fetch unblocks
    Data(0x40F0) = 0x04;
    Data(0x40F1) = 0x80;
    _z80->sp = 0x40F0;
    _memory->DirectWriteToZ80Memory(0x8001, 0x4D);
    _memory->DirectWriteToZ80Memory(0x8004, 0x77);
    _z80->pc = 0x8000;
    Step();
    ASSERT_EQ(_z80->pc, 0x8004);
    EXPECT_EQ(St().blocked, 1) << "still blocked until the next opcode fetch";
    Step();  // LD (HL),A: unblocked at its M1, so it fills
    EXPECT_EQ(St().blocked, 0);
    EXPECT_EQ(Data(0x4101), 0xAA);
    EXPECT_EQ(Data(0x4102), 0xAA);
    EXPECT_EQ(Data(0x4103), 0x11);
}

// An NMI has no acknowledge cycle: it does not block
TEST_F(SprinterAccelerator_Test, IntSuspend_NmiDoesNotBlock)
{
    RunCode({0x52, 0x3E, 0x04, 0x49});
    for (uint16_t a = 0x40F8; a < 0x4108; a++)
        Data(a) = 0x11;
    _z80->sp = 0x4100;
    _z80->pc = 0x8234;
    ASSERT_NE(_z80->GetEngine(), nullptr);
    _z80->GetEngine()->AcknowledgeNmi();  // the Z80's NMI boundary hands it to the engine
    EXPECT_EQ(St().blocked, 0);
    EXPECT_EQ(Data(0x4102), 0x82) << "the NMI's pushes are filled";
}

// Length loads are not blocked (RGACC is not gated by ACC_BLK)
TEST_F(SprinterAccelerator_Test, IntSuspend_LengthStillLoads)
{
    Acc()->OnInterruptAcknowledge();
    RunCode({0x52, 0x1E, 0x09, 0x40});
    EXPECT_EQ(St().length, 9);
    EXPECT_EQ(St().blocked, 1);
}

/// endregion </INT suspend>

// Code #C7 / #CF: the alternate buffer addressing (XCNT:XAGR += AAGR per access)
TEST_F(SprinterAccelerator_Test, Scale_AlternateBufferIndex)
{
    SetCode(0x08C7, false, SprinterCode::Scale);  // A15..A10 = 2 -> XCNT = 2; A9 A8 = 0; data #80 -> step 0.5
    for (uint16_t i = 0; i < 4; i++)
        St().buffer[i] = static_cast<uint8_t>(0xA0 + i);
    Out(0x08C7, 0x80);
    EXPECT_EQ(St().alt, 1);
    EXPECT_EQ(St().xcnt, 2);
    EXPECT_EQ(St().aagr, 0x80);
    // length 4 : HL = #4100 : LD L,L : LD (HL),A : LD B,B -> buffer[2], [2], [3], [3]
    RunCode({0x52, 0x3E, 0x04, 0x40, 0x21, 0x00, 0x41, 0x6D, 0x77, 0x40});
    EXPECT_EQ(Data(0x4100), 0xA2);
    EXPECT_EQ(Data(0x4101), 0xA2);
    EXPECT_EQ(Data(0x4102), 0xA3);
    EXPECT_EQ(Data(0x4103), 0xA3);
}

// The module supplies the accelerator (hook 4); a reset clears the mode and the block, keeps the buffer
TEST_F(SprinterAccelerator_Test, StandardModuleSupplies_ResetClears)
{
    EXPECT_EQ(Acc(), &_decoder->StandardAccelerator());
    EXPECT_EQ(_decoder->GetRegistry().Standard().Accelerator(*_decoder), Acc());
    Acc()->OnOpcodeFetch(0x8000, 0x6D);
    Acc()->OnInterruptAcknowledge();
    St().buffer[5] = 0x42;
    _core->Reset();
    EXPECT_EQ(St().mode, 0);
    EXPECT_EQ(St().blocked, 0);
    EXPECT_EQ(St().buffer[5], 0x42);
    EXPECT_STREQ(SprinterAccelerator::ModeName(5), "copy");
    EXPECT_STREQ(SprinterAccelerator::FunctionName(3), "and");
}
