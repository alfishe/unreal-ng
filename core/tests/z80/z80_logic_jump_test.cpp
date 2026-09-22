#include "pch.h"

#include "z80_logic_jump_test.h"
#include "opcode_test.h"

#include "common/modulelogger.h"
#include <string>

/// region <SetUp / TearDown>

void Z80_Logic_Jump_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _context = new EmulatorContext(LoggerLevel::LogError);

    _cpu = new Core(_context);
    bool init = _cpu->Init();

    // Use Spectrum48K / Pentagon memory layout
    _cpu->GetMemory()->DefaultBanksFor48k();

    // Instantiate opcode test helper
    _opcode = new OpcodeTest();
}

void Z80_Logic_Jump_Test::TearDown()
{
    if (_cpu != nullptr)
    {
        delete _cpu;
        _cpu = nullptr;
    }

    if (_context != nullptr)
    {
        delete _context;
        _context = nullptr;
    }

    if (_opcode != nullptr)
    {
        delete _opcode;
        _opcode = nullptr;
    }
}

/// endregion </Setup / TearDown>

// Relative jumps change PC in range of +129 or -126
// 0x00 = 0 => offset +2
// 0x7F = 127 =? offset +129
// 0x80 = -128 => offset -126
// 0xFF = -1 => offset +1
TEST_F(Z80_Logic_Jump_Test, Z80RelativeJumps)
{
    /// region <Initialization>
    Z80 &z80 = *_cpu->GetZ80();

    // Use 48k (SOS) ROM for testing purposes
    uint8_t *memory = _cpu->GetMemory()->base_sos_rom;
    if (memory == nullptr)
    {
        FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
    }
    /// endregion </Initialization>


    /// region <Test JR e - 0x18 <ee>>
    {
        std::vector<std::vector<uint8_t>> testData_0x18 =
        {
            {0x18, 0x00},               // jr +0 - PC should remain the same (point at the very next to jr instruction)
            {0x18, 0x03},               // jr +3 - PC will point to address of JR + 5
            {0x18, 0x7F},               // jr +127 - max allowed forward jump. PC will be <addr of jr> + 129
            {0x18, 0xFA},               // jr -6 - PC will be <addr of jr> - 4
            {0x18, 0x80},               // jr -128 - min allowed reverse jump. PC = <addr of jr> - 126
            {0x18, 0xFF}                // jr -1   - max allowed reverse jump. PC = <addr of jr> + 1
        };

        std::vector<uint16_t> reference_pc_0x18 =
        {
            0x0002,                       // Offset = 0
            0x0005,                       // Offset = 3
            0x0081,                       // Offset = 127
            0xFFFC,                       // Offset = -6
            0xFF82,                       // Offset = -128
            0x0001                        // Offset = -1
        };

        // Cover all test case data records
        int i = 0;
        int j = 0;
        for (i = 0; i < testData_0x18.size(); i++)
        {
            // 1. Create CPU command  (ROM bank, 0x0000)
            std::vector<uint8_t> testCommand = testData_0x18[i];

            for (j = 0; j < testCommand.size(); j++)
            {
                memory[j] = testCommand[j];
            }

            // 2. Put HALT (0x76) command after
            memory[j + 1] = 0x76;

            // 3. Perform reset to get clean results for each instruction
            z80.Reset();

            // 4. Execute single JR command
            z80.Z80Step();

            EXPECT_EQ(z80.pc, reference_pc_0x18[i]);
        }
    }
    /// endregion </Test JR, e - 0x18 <ee>>

    /// region <Test JR, z,x - 0x28 <xx>>
    std::vector<std::vector<uint8_t>> testData_0x28 =
    {
        { 0x28, 0x00 },               // jr +0 - PC should remain the same (point at the very next to jr instruction)
        { 0x28, 0x03 },               // jr +3 - PC will point to address of JR + 5
        { 0x28, 0x7F },               // jr +127 - max allowed forward jump. PC will be <addr of jr> + 129
        { 0x28, 0xFA },               // jr -6 - PC will be <addr of jr> - 4
        { 0x28, 0x80 },               // jr -128 - min allowed reverse jump. PC = <addr of jr> - 126
        { 0x28, 0xFF }                // jr -1   - max allowed reverse jump. PC = <addr of jr> + 1
    };

    uint8_t testRegF_Active_0x28 = z80_test::FLAG_ZF;
    uint8_t testRegF_Inactive_0x28 = 0x00;
    uint16_t reference_pc_nojump_0x28 = 0x0002;

    std::vector<uint16_t> reference_pc_0x28 =
    {
        0x0002,                       // Offset = 0
        0x0005,                       // Offset = 3
        0x0081,                       // Offset = 127
        0xFFFC,                       // Offset = -6
        0xFF82,                       // Offset = -128
        0x0001                        // Offset = -1
    };

    // Positive cases (Flag condition met)
    {
        int i = 0;
        int j = 0;
        for (i = 0; i < testData_0x28.size(); i++)
        {
            // 1. Create CPU command (ROM bank, 0x0000)
            std::vector<uint8_t> testCommand = testData_0x28[i];

            for (j = 0; j < testCommand.size(); j++)
            {
                memory[j] = testCommand[j];
            }

            // 2. Put HALT (0x76) command after
            memory[j + 1] = 0x76;

            // 3. Perform reset to get clean results for each instruction
            z80.Reset();

            // 4. Set F register with condition flag (met)
            z80.f = testRegF_Active_0x28;

            // 5. Execute single JR command
            z80.Z80Step();

            EXPECT_EQ(z80.pc, reference_pc_0x28[i]);
        }
    }

    // Negative cases (Flag condition not met)
    {
        int i = 0;
        int j = 0;
        for (i = 0; i < testData_0x28.size(); i++)
        {
            // 1. Create CPU command
            std::vector<uint8_t> testCommand = testData_0x28[i];

            for (j = 0; j < testCommand.size(); j++)
            {
                memory[j] = testCommand[j];
            }

            // 2. Put HALT (0x76) command after
            memory[j + 1] = 0x76;

            // 3. Perform reset to get clean results for each instruction
            z80.Reset();

            // 4. Set F register with condition flag (not met)
            z80.f = testRegF_Inactive_0x28;

            // 4. Execute single JR command
            z80.Z80Step();

            EXPECT_EQ(z80.pc, reference_pc_nojump_0x28);
        }
    }

    /// endregion </Test JR z,xx - 0x28 <xx>>

    /// region <Test JR c,xx - 0x38 <xx>>
    std::vector<std::vector<uint8_t>> testData_0x38 =
    {
        { 0x38, 0x00 },               // jr +0 - PC should remain the same (point at the very next to jr instruction)
        { 0x38, 0x03 },               // jr +3 - PC will point to address of JR + 5
        { 0x38, 0x7F },               // jr +127 - max allowed forward jump. PC will be <addr of jr> + 129
        { 0x38, 0xFA },               // jr -6 - PC will be <addr of jr> - 4
        { 0x38, 0x80 },               // jr -128 - min allowed reverse jump. PC = <addr of jr> - 126
        { 0x38, 0xFF }                // jr -1   - max allowed reverse jump. PC = <addr of jr> + 1
    };

    std::vector<uint16_t> reference_pc_0x38 =
    {
        0x0002,                       // Offset = 0
        0x0005,                       // Offset = 3
        0x0081,                       // Offset = 127
        0xFFFC,                       // Offset = -6
        0xFF82,                       // Offset = -128
        0x0001                        // Offset = -1
    };

    uint8_t testRegF_Active_0x38 = z80_test::FLAG_CF;
    uint8_t testRegF_Inactive_0x38 = 0x00;
    uint16_t reference_pc_nojump_0x38 = 0x0002;

    // Positive cases (Flag condition met)
    {
        int i = 0;
        int j = 0;
        for (i = 0; i < testData_0x38.size(); i++)
        {
            // 1. Create CPU command
            std::vector<uint8_t> testCommand = testData_0x38[i];

            for (j = 0; j < testCommand.size(); j++)
            {
                memory[j] = testCommand[j];
            }

            // 2. Put HALT (0x76) command after
            memory[j + 1] = 0x76;

            // 3. Perform reset to get clean results for each instruction
            z80.Reset();

            // 4. Set F register with condition flag (met)
            z80.f = testRegF_Active_0x38;

            // 5. Execute single JR command
            z80.Z80Step();

            EXPECT_EQ(z80.pc, reference_pc_0x38[i]);
        }
    }

    // Negative cases (Flag condition not met)
    {
        int i = 0;
        int j = 0;
        for (i = 0; i < testData_0x38.size(); i++)
        {
            // 1. Create CPU command
            std::vector<uint8_t> testCommand = testData_0x38[i];

            for (j = 0; j < testCommand.size(); j++)
            {
                memory[j] = testCommand[j];
            }

            // 2. Put HALT (0x76) command after
            memory[j + 1] = 0x76;

            // 3. Perform reset to get clean results for each instruction
            z80.Reset();

            // 4. Set F register with condition flag (not met)
            z80.f = testRegF_Inactive_0x38;

            // 5. Execute single JR command
            z80.Z80Step();

            EXPECT_EQ(z80.pc, reference_pc_nojump_0x38);
        }
    }

    /// endregion </Test JR c,x - 0x38 <xx>>

    /// region <Test JR, nz,x - 0x20 <xx>>
    std::vector<std::vector<uint8_t>> testData_0x20 =
    {
        { 0x20, 0x00 },               // jr +0 - PC should remain the same (point at the very next to jr instruction)
        { 0x20, 0x03 },               // jr +3 - PC will point to address of JR + 5
        { 0x20, 0x7F },               // jr +127 - max allowed forward jump. PC will be <addr of jr> + 129
        { 0x20, 0xFA },               // jr -6 - PC will be <addr of jr> - 4
        { 0x20, 0x80 },               // jr -128 - min allowed reverse jump. PC = <addr of jr> - 126
        { 0x20, 0xFF }                // jr -1   - max allowed reverse jump. PC = <addr of jr> + 1
    };

    uint8_t testRegF_Active_0x20 = 0x00;
    uint8_t testRegF_Inactive_0x20 = z80_test::FLAG_ZF;;
    uint16_t reference_pc_nojump_0x20 = 0x0002;

    std::vector<uint16_t> reference_pc_0x20 =
    {
        0x0002,                       // Offset = 0
        0x0005,                       // Offset = 3
        0x0081,                       // Offset = 127
        0xFFFC,                       // Offset = -6
        0xFF82,                       // Offset = -128
        0x0001                        // Offset = -1
    };

    // Positive cases (Flag condition met)
    {
        int i = 0;
        int j = 0;
        for (i = 0; i < testData_0x20.size(); i++)
        {
            // 1. Create CPU command (ROM bank, 0x0000)
            std::vector<uint8_t> testCommand = testData_0x20[i];

            for (j = 0; j < testCommand.size(); j++)
            {
                memory[j] = testCommand[j];
            }

            // 2. Put HALT (0x76) command after
            memory[j + 1] = 0x76;

            // 3. Perform reset to get clean results for each instruction
            z80.Reset();

            // 4. Set F register with condition flag (met)
            z80.f = testRegF_Active_0x20;

            // 5. Execute single JR command
            z80.Z80Step();

            EXPECT_EQ(z80.pc, reference_pc_0x20[i]);
        }
    }

    // Negative cases (Flag condition not met)
    {
        int i = 0;
        int j = 0;
        for (i = 0; i < testData_0x20.size(); i++)
        {
            // 1. Create CPU command
            std::vector<uint8_t> testCommand = testData_0x20[i];

            for (j = 0; j < testCommand.size(); j++)
            {
                memory[j] = testCommand[j];
            }

            // 2. Put HALT (0x76) command after
            memory[j + 1] = 0x76;

            // 3. Perform reset to get clean results for each instruction
            z80.Reset();

            // 4. Set F register with condition flag (not met)
            z80.f = testRegF_Inactive_0x20;

            // 4. Execute single JR command
            z80.Z80Step();

            EXPECT_EQ(z80.pc, reference_pc_nojump_0x20);
        }
    }

    /// endregion </Test JR nz,xx - 0x20 <xx>>

    /// region <Test JR nc,xx - 0x30 <xx>>
    std::vector<std::vector<uint8_t>> testData_0x30 =
    {
        { 0x30, 0x00 },               // jr +0 - PC should remain the same (point at the very next to jr instruction)
        { 0x30, 0x03 },               // jr +3 - PC will point to address of JR + 5
        { 0x30, 0x7F },               // jr +127 - max allowed forward jump. PC will be <addr of jr> + 129
        { 0x30, 0xFA },               // jr -6 - PC will be <addr of jr> - 4
        { 0x30, 0x80 },               // jr -128 - min allowed reverse jump. PC = <addr of jr> - 126
        { 0x30, 0xFF }                // jr -1   - max allowed reverse jump. PC = <addr of jr> + 1
    };

    std::vector<uint16_t> reference_pc_0x30 =
    {
        0x0002,                       // Offset = 0
        0x0005,                       // Offset = 3
        0x0081,                       // Offset = 127
        0xFFFC,                       // Offset = -6
        0xFF82,                       // Offset = -128
        0x0001                        // Offset = -1
    };

    uint8_t testRegF_Active_0x30 = 0x00;
    uint8_t testRegF_Inactive_0x30 = z80_test::FLAG_CF;
    uint16_t reference_pc_nojump_0x30 = 0x0002;

    // Positive cases (Flag condition met)
    {
        int i = 0;
        int j = 0;
        for (i = 0; i < testData_0x30.size(); i++)
        {
            // 1. Create CPU command
            std::vector<uint8_t> testCommand = testData_0x30[i];

            for (j = 0; j < testCommand.size(); j++)
            {
                memory[j] = testCommand[j];
            }

            // 2. Put HALT (0x76) command after
            memory[j + 1] = 0x76;

            // 3. Perform reset to get clean results for each instruction
            z80.Reset();

            // 4. Set F register with condition flag (met)
            z80.f = testRegF_Active_0x30;

            // 5. Execute single JR command
            z80.Z80Step();

            EXPECT_EQ(z80.pc, reference_pc_0x30[i]);
        }
    }

    // Negative cases (Flag condition not met)
    {
        int i = 0;
        int j = 0;
        for (i = 0; i < testData_0x30.size(); i++)
        {
            // 1. Create CPU command
            std::vector<uint8_t> testCommand = testData_0x30[i];

            for (j = 0; j < testCommand.size(); j++)
            {
                memory[j] = testCommand[j];
            }

            // 2. Put HALT (0x76) command after
            memory[j + 1] = 0x76;

            // 3. Perform reset to get clean results for each instruction
            z80.Reset();

            // 4. Set F register with condition flag (not met)
            z80.f = testRegF_Inactive_0x30;

            // 5. Execute single JR command
            z80.Z80Step();

            EXPECT_EQ(z80.pc, reference_pc_nojump_0x30);
        }
    }

    /// endregion </Test JR c,x - 0x38 <xx>>
}


// Conditional absolute jumps, calls and returns, plus DJNZ. Every condition code (NZ, Z, NC, C,
// PO, PE, P, M) is exercised with the flag set to satisfy it and to fail it; flags never change
TEST_F(Z80_Logic_Jump_Test, Z80ConditionalJumps)
{
    /// region <Initialization>
    Z80 &z80 = *_cpu->GetZ80();

    // Use 48k (SOS) ROM for testing purposes
    uint8_t *memory = _cpu->GetMemory()->base_sos_rom;
    if (memory == nullptr)
    {
        FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
    }
    /// endregion </Initialization>

    struct Condition
    {
        const char* name;
        uint8_t flag;   // Flag bit tested
        bool whenSet;   // true: condition met when the flag is 1
    };

    // Index = cc field of the opcode (bits 5..3)
    static const Condition conditions[8] = {
        {"NZ", z80_test::FLAG_ZF, false}, {"Z", z80_test::FLAG_ZF, true},
        {"NC", z80_test::FLAG_CF, false}, {"C", z80_test::FLAG_CF, true},
        {"PO", z80_test::FLAG_PV, false}, {"PE", z80_test::FLAG_PV, true},
        {"P", z80_test::FLAG_SF, false},  {"M", z80_test::FLAG_SF, true},
    };

    constexpr uint16_t kTarget = 0x1234;
    constexpr uint16_t kStackTop = 0x9000;      // RAM
    constexpr uint16_t kReturnAddress = 0x4321;  // Value planted on the stack for RET cc

    for (int cc = 0; cc < 8; cc++)
    {
        const Condition& cond = conditions[cc];

        for (int met = 0; met <= 1; met++)
        {
            // Flags: only the tested bit is set to satisfy/violate the condition; unrelated
            // flag bits are all ones so a wrongly decoded condition would be noticed
            const uint8_t others = 0xFF & ~cond.flag;
            const bool flagBit = met ? cond.whenSet : !cond.whenSet;
            const uint8_t flags = others | (flagBit ? cond.flag : 0);
            const std::string ctx = std::string(cond.name) + (met ? " (met)" : " (not met)");

            /// region <JP cc,nn - 0xC2 + cc*8>
            {
                memory[0] = 0xC2 + cc * 8;
                memory[1] = kTarget & 0xFF;
                memory[2] = kTarget >> 8;
                memory[3] = 0x76;

                z80.Reset();
                z80.f = flags;
                z80.sp = kStackTop;
                z80.Z80Step();

                EXPECT_EQ(z80.pc, met ? kTarget : 0x0003) << "JP " << ctx;
                EXPECT_EQ(z80.sp, kStackTop) << "JP must not touch SP, " << ctx;
                EXPECT_EQ(z80.f, flags) << "JP must not change flags, " << ctx;
            }
            /// endregion </JP cc,nn>

            /// region <CALL cc,nn - 0xC4 + cc*8>
            {
                memory[0] = 0xC4 + cc * 8;
                memory[1] = kTarget & 0xFF;
                memory[2] = kTarget >> 8;
                memory[3] = 0x76;

                z80.Reset();
                z80.f = flags;
                z80.sp = kStackTop;
                z80.Z80Step();

                EXPECT_EQ(z80.f, flags) << "CALL must not change flags, " << ctx;
                if (met)
                {
                    EXPECT_EQ(z80.pc, kTarget) << "CALL " << ctx;
                    EXPECT_EQ(z80.sp, kStackTop - 2) << "CALL pushes the return address, " << ctx;

                    // Return address (next instruction = 0x0003) is stored little-endian
                    uint8_t* stack = _cpu->GetMemory()->MapZ80AddressToPhysicalAddress(z80.sp);
                    EXPECT_EQ(stack[0], 0x03) << "CALL return address low byte, " << ctx;
                    EXPECT_EQ(stack[1], 0x00) << "CALL return address high byte, " << ctx;
                }
                else
                {
                    EXPECT_EQ(z80.pc, 0x0003) << "CALL " << ctx;
                    EXPECT_EQ(z80.sp, kStackTop) << "CALL not taken must leave SP, " << ctx;
                }
            }
            /// endregion </CALL cc,nn>

            /// region <RET cc - 0xC0 + cc*8>
            {
                memory[0] = 0xC0 + cc * 8;
                memory[1] = 0x76;

                uint8_t* stack = _cpu->GetMemory()->MapZ80AddressToPhysicalAddress(kStackTop);
                stack[0] = kReturnAddress & 0xFF;
                stack[1] = kReturnAddress >> 8;

                z80.Reset();
                z80.f = flags;
                z80.sp = kStackTop;
                z80.Z80Step();

                EXPECT_EQ(z80.pc, met ? kReturnAddress : 0x0001) << "RET " << ctx;
                EXPECT_EQ(z80.sp, met ? kStackTop + 2 : kStackTop) << "RET SP, " << ctx;
                EXPECT_EQ(z80.f, flags) << "RET must not change flags, " << ctx;
            }
            /// endregion </RET cc>
        }
    }

    /// region <DJNZ e - 0x10 <ee>>
    {
        struct DjnzCase
        {
            uint8_t b;
            uint8_t offset;
            uint16_t expectedPc;
            uint8_t expectedB;
        };

        const DjnzCase cases[] = {
            {2, 0x03, 0x0005, 1},     // B != 0 after decrement: jump +3 (to <jr addr> + 5)
            {2, 0xFA, 0xFFFC, 1},     // backward jump -6
            {0x80, 0x7F, 0x0081, 0x7F},  // max forward
            {1, 0x03, 0x0002, 0},     // B reaches 0: fall through
            {0, 0x03, 0x0005, 0xFF},  // B = 0 wraps to 255: jump taken
        };

        for (const DjnzCase& c : cases)
        {
            memory[0] = 0x10;
            memory[1] = c.offset;
            memory[2] = 0x76;

            z80.Reset();
            z80.b = c.b;
            z80.f = 0xFF;
            z80.Z80Step();

            EXPECT_EQ(z80.pc, c.expectedPc) << "DJNZ with B=" << (int)c.b << " offset=" << (int)c.offset;
            EXPECT_EQ(z80.b, c.expectedB) << "DJNZ B after decrement, B=" << (int)c.b;
            EXPECT_EQ(z80.f, 0xFF) << "DJNZ must not change flags";
        }
    }
    /// endregion </DJNZ e>
}
