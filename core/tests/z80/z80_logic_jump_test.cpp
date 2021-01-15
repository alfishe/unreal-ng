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

    _cpu = new CPU(_context);
    bool init = _cpu->Init();

    // Use Spectrum48K / Pentagon memory layout
    _cpu->GetMemory()->InternalSetBanks();

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