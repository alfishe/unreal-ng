#include "z80_test.h"

#include "common/dumphelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "pch.h"

/// region <SetUp / TearDown>

void Z80_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _context = new EmulatorContext(LoggerLevel::LogError);  // Filter out all messages with level below error

    _cpu = new Core(_context);
    if (_cpu->Init())
    {
        // Use Spectrum48K / Pentagon memory layout
        _cpu->GetMemory()->DefaultBanksFor48k();

        // Instantiate opcode test helper
        _opcode = new OpcodeTest();
    }
    else
    {
        throw std::logic_error("Z80_Test::SetUp - _core->Init() failed");
    }
}

void Z80_Test::TearDown()
{
    if (_opcode != nullptr)
    {
        delete _opcode;
        _opcode = nullptr;
    }

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
}

/// endregion </Setup / TearDown>

/// region <Helper methods>
void Z80_Test::DumpFirst256ROMBytes()
{
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;

    std::cout << std::endl << "48k ROM [256 bytes]" << std::endl;
    std::string dump = DumpHelper::HexDumpBuffer(memory, 256);
    std::cout << dump << std::endl;
}

void Z80_Test::ResetCPUAndMemory()
{
    Z80& z80 = *_cpu->GetZ80();
    z80.Reset();

    // Reset all other registers to 0 for a predictable state
    z80.bc = 0;
    z80.de = 0;
    z80.hl = 0;
    z80.ix = 0;
    z80.iy = 0;
    z80.alt.af = 0;
    z80.alt.bc = 0;
    z80.alt.de = 0;
    z80.alt.hl = 0;

    // Reset memory banking to default 48k layout
    _cpu->GetMemory()->DefaultBanksFor48k();

    // Clear emulator state flags (like CF_TRDOS)
    _context->emulatorState.flags = 0;
}
/// endregion </Helper methods>

TEST_F(Z80_Test, Z80Reset)
{
    Z80* cpu = _cpu->GetZ80();

    cpu->Reset();
    EXPECT_EQ(cpu->pc, 0x0000);
    EXPECT_EQ(cpu->sp, 0xFFFF);
    EXPECT_EQ(cpu->af, 0xFFFF);

    EXPECT_EQ(cpu->ir_, 0x0000);
    EXPECT_EQ(cpu->int_flags, 0);
    EXPECT_EQ(cpu->int_pending, false);
    EXPECT_EQ(cpu->int_gate, true);
    EXPECT_EQ(cpu->last_branch, 0x0000);

    // Reset procedure should take 3 clock cycles
    EXPECT_EQ(cpu->t, 3);
}

TEST_F(Z80_Test, Z80OpcodeTimings)
{
    Z80& z80 = *_cpu->GetZ80();
    uint32_t start_cycles = 0;
    uint32_t finish_cycles = 0;
    uint32_t delta_cycles = 0;

    // Use 48k (SOS) ROM for testing purposes
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    if (memory == nullptr)
    {
        FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
    }

    // Test no-prefix opcode instructions
    for (uint16_t i = 0; i <= 0xFF;
         i++)  // uint16_t is used since MSVC and GCC compilers are going crazy for 1 byte type and type overflow
               // condition wnen comparing with 0xFF after increment to 0x00 and loosing overflow flag
    {
        // Exclude prefixed command prefixes
        if (i == 0xCB || i == 0xDD || i == 0xED || i == 0xFD)
            continue;

        const std::string message = StringHelper::Format("Opcode: 0x%02X", i);

        // Perform reset to get clean results for each instruction
        ResetCPUAndMemory();

        // Prepare instruction in ROM (0x0000 address)
        OpDescriptor& descriptor = _opcode->_noprefix[i];
        try
        {
            uint8_t len = _opcode->PrepareInstruction(0x00, (uint8_t)i, memory);
            EXPECT_EQ(len, descriptor.bytes) << message << std::endl;
            ;
        }
        catch (const char* err)
        {
            FAIL() << err << std::endl;
        }

        // Capture clock cycle counter before instruction execution
        start_cycles = z80.t;

        // Execute single instruction
        z80.Z80Step();

        // Measure instruction execution in clock cycles
        finish_cycles = z80.t;
        delta_cycles = finish_cycles - start_cycles;

        EXPECT_EQ(delta_cycles, descriptor.cycles) << message << std::endl;
    }
}

TEST_F(Z80_Test, Z80OpcodeTimings_ED)
{
    Z80& z80 = *_cpu->GetZ80();
    uint32_t start_cycles = 0;
    uint32_t finish_cycles = 0;
    uint32_t delta_cycles = 0;

    // Use 48k (SOS) ROM for testing purposes
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    if (memory == nullptr)
    {
        FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
    }

    // Test 0xED prefixed opcodes
    for (uint16_t i = 0; i <= 0xFF; i++)  // uint16_t required since C++ compilers unable to make loop [0:255] working
                                          // using 1 byte counter due to inability to utilize overflow flag
    {
        std::string message = StringHelper::Format("Opcode: 0xED 0x%02X", i);

        // Perform reset to get clean results for each instruction
        ResetCPUAndMemory();

        // Prepare instruction in ROM (0x0000 address)
        OpDescriptor& descriptor = _opcode->_prefixED[i];
        try
        {
            uint8_t len = _opcode->PrepareInstruction(0xED, (uint8_t)i, memory);
        }
        catch (const char* err)
        {
            FAIL() << err << std::endl;
        }

        // Capture clock cycle counter before instruction execution
        start_cycles = z80.t;

        // Execute single instruction
        z80.Z80Step();

        // Measure instruction execution in clock cycles
        finish_cycles = z80.t;
        delta_cycles = finish_cycles - start_cycles;

        EXPECT_EQ(delta_cycles, descriptor.cycles) << message << std::endl;
    }
}

TEST_F(Z80_Test, Z80OpcodeTimings_CB)
{
    Z80& z80 = *_cpu->GetZ80();
    uint32_t start_cycles = 0;
    uint32_t finish_cycles = 0;
    uint32_t delta_cycles = 0;

    // Use 48k (SOS) ROM for testing purposes
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    if (memory == nullptr)
    {
        FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
    }

    // Test 0xCB prefixed opcodes
    for (uint16_t i = 0; i <= 0xFF; i++)  // uint16_t required since C++ compilers unable to make loop [0:255] working
                                          // using 1 byte counter due to inability to utilize overflow flag
    {
        std::string message = StringHelper::Format("Opcode: 0xCB 0x%02X", i);

        // Perform reset to get clean results for each instruction
        ResetCPUAndMemory();

        // Prepare instruction in ROM (0x0000 address)
        OpDescriptor& descriptor = _opcode->_prefixCB[i];
        try
        {
            uint8_t len = _opcode->PrepareInstruction(0xCB, (uint8_t)i, memory);
        }
        catch (const char* err)
        {
            FAIL() << err << std::endl;
        }

        // Capture clock cycle counter before instruction execution
        start_cycles = z80.t;

        // Execute single instruction
        z80.Z80Step();

        // Measure instruction execution in clock cycles
        finish_cycles = z80.t;
        delta_cycles = finish_cycles - start_cycles;

        EXPECT_EQ(delta_cycles, descriptor.cycles) << message << std::endl;
    }
}

TEST_F(Z80_Test, Z80OpcodeTimings_DD)
{
    Z80& z80 = *_cpu->GetZ80();
    uint32_t start_cycles = 0;
    uint32_t finish_cycles = 0;
    uint32_t delta_cycles = 0;
    static char message[256];

    // Use 48k (SOS) ROM for testing purposes
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    if (memory == nullptr)
    {
        FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
    }

    // Test 0xDD prefixed opcodes
    for (uint16_t i = 0; i <= 0xFF; i++)  // uint16_t required since C++ compilers unable to make loop [0:255] working
                                          // using 1 byte counter due to inability to utilize overflow flag
    {
        // Perform reset to get clean results for each instruction
        ResetCPUAndMemory();

        // Skip unpopulated test table entries
        OpDescriptor& descriptor = _opcode->_prefixDD[i];
        if (descriptor.bytes == 0)
            continue;

        try
        {
            uint8_t len = _opcode->PrepareInstruction(0xDD, (uint8_t)i, memory);
        }
        catch (const char* err)
        {
            FAIL() << err << std::endl;
        }

        // Capture clock cycle counter before instruction execution
        start_cycles = z80.t;

        // Execute single instruction
        z80.Z80Step();

        // Measure instruction execution in clock cycles
        finish_cycles = z80.t;
        delta_cycles = finish_cycles - start_cycles;

        snprintf(message, sizeof message, "Opcode: 0xDD 0x%02X", i);
        EXPECT_EQ(delta_cycles, descriptor.cycles) << message << std::endl;
    }
}

TEST_F(Z80_Test, Z80OpcodeTimings_DDCB)
{
    Z80& z80 = *_cpu->GetZ80();
    uint32_t start_cycles = 0;
    uint32_t finish_cycles = 0;
    uint32_t delta_cycles = 0;
    static char message[256];

    // Use 48k (SOS) ROM for testing purposes
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    if (memory == nullptr)
    {
        FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
    }

    // Test 0xDD 0xCB prefixed opcodes
    for (uint16_t i = 0; i <= 0xFF; i++)  // uint16_t required since C++ compilers unable to make loop [0:255] working
                                          // using 1 byte counter due to inability to utilize overflow flag
    {
        // Perform reset to get clean results for each instruction
        ResetCPUAndMemory();

        // Skip unpopulated test table entries
        OpDescriptor& descriptor = _opcode->_prefixDDCB[i];
        if (descriptor.bytes == 0)
            continue;

        try
        {
            uint8_t len = _opcode->PrepareInstruction(0xDD, (uint8_t)i, memory, 0xCB);
        }
        catch (const char* err)
        {
            FAIL() << err << std::endl;
        }

        // Capture clock cycle counter before instruction execution
        start_cycles = z80.t;

        // Execute single instruction
        z80.Z80Step();

        // Measure instruction execution in clock cycles
        finish_cycles = z80.t;
        delta_cycles = finish_cycles - start_cycles;

        snprintf(message, sizeof message, "Opcode: 0xDD 0xCB 0x%02X", i);
        EXPECT_EQ(delta_cycles, descriptor.cycles) << message << std::endl;
    }
}

TEST_F(Z80_Test, Z80OpcodeTimings_FD)
{
    Z80& z80 = *_cpu->GetZ80();
    uint32_t start_cycles = 0;
    uint32_t finish_cycles = 0;
    uint32_t delta_cycles = 0;
    static char message[256];

    // Use 48k (SOS) ROM for testing purposes
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    if (memory == nullptr)
    {
        FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
    }

    // Test 0xFD prefixed opcodes
    for (uint16_t i = 0; i <= 0xFF; i++)  // uint16_t required since C++ compilers unable to make loop [0:255] working
                                          // using 1 byte counter due to inability to utilize overflow flag
    {
        // Perform reset to get clean results for each instruction
        ResetCPUAndMemory();

        // Skip unpopulated test table entries
        OpDescriptor& descriptor = _opcode->_prefixFD[i];
        if (descriptor.bytes == 0)
            continue;

        try
        {
            uint8_t len = _opcode->PrepareInstruction(0xFD, (uint8_t)i, memory);
        }
        catch (const char* err)
        {
            FAIL() << err << std::endl;
        }

        // Capture clock cycle counter before instruction execution
        start_cycles = z80.t;

        // Execute single instruction
        z80.Z80Step();

        // Measure instruction execution in clock cycles
        finish_cycles = z80.t;
        delta_cycles = finish_cycles - start_cycles;

        snprintf(message, sizeof message, "Opcode: 0xFD 0x%02X", i);
        EXPECT_EQ(delta_cycles, descriptor.cycles) << message << std::endl;
    }
}

TEST_F(Z80_Test, Z80OpcodeTimings_FDCB)
{
    Z80& z80 = *_cpu->GetZ80();
    uint32_t start_cycles = 0;
    uint32_t finish_cycles = 0;
    uint32_t delta_cycles = 0;
    static char message[256];

    // Use 48k (SOS) ROM for testing purposes
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    if (memory == nullptr)
    {
        FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
    }

    // Test 0xFD 0xCB prefixed opcodes
    for (uint16_t i = 0; i <= 0xFF; i++)  // uint16_t required since C++ compilers unable to make loop [0:255] working
                                          // using 1 byte counter due to inability to utilize overflow flag
    {
        // Perform reset to get clean results for each instruction
        ResetCPUAndMemory();

        // Skip unpopulated test table entries
        OpDescriptor& descriptor = _opcode->_prefixFDCB[i];
        if (descriptor.bytes == 0)
            continue;

        try
        {
            uint8_t len = _opcode->PrepareInstruction(0xFD, (uint8_t)i, memory, 0xCB);
        }
        catch (const char* err)
        {
            FAIL() << err << std::endl;
        }

        // Capture clock cycle counter before instruction execution
        start_cycles = z80.t;

        // Execute single instruction
        z80.Z80Step();

        // Measure instruction execution in clock cycles
        finish_cycles = z80.t;
        delta_cycles = finish_cycles - start_cycles;

        snprintf(message, sizeof message, "Opcode: 0xFD 0xCB 0x%02X", i);
        EXPECT_EQ(delta_cycles, descriptor.cycles) << message << std::endl;
    }
}

/// region <Z80 XCF Tests - Q Register and CCF/SCF Undocumented Flag Behavior>
///
/// These tests verify genuine Zilog Z80 behavior for the undocumented YF/XF flags
/// during CCF and SCF instructions. The Q register mechanism was discovered in 2018-2024
/// and is tested by the XCF Flavor v1.6 test program.
///
/// Formula: undoc_flags = (A | (F & ~Q)) & 0x28
/// - Flag-modifying instruction: Q = F & 0x28
/// - Non-flag-modifying instruction: Q = 0

// Test Q register initialization after reset
TEST_F(Z80_Test, XCF_Q_InitializedOnReset)
{
    Z80& z80 = *_cpu->GetZ80();
    z80.Reset();
    
    EXPECT_EQ(z80.q, 0) << "Q register should be 0 after reset";
}

// Test Q register is updated after flag-modifying instruction (DEC)
TEST_F(Z80_Test, XCF_Q_UpdatedAfterFlagModifyingInstruction)
{
    Z80& z80 = *_cpu->GetZ80();
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    
    ResetCPUAndMemory();
    z80.a = 0x00;  // DEC A will produce 0xFF with YF/XF set
    
    // DEC A (opcode 0x3D) - modifies flags
    memory[0] = 0x3D;
    z80.Z80Step();
    
    // After DEC A producing 0xFF, both YF (bit 5) and XF (bit 3) should be set
    EXPECT_EQ(z80.q & 0x28, 0x28) << "Q should capture YF/XF after flag-modifying instruction";
    EXPECT_EQ(z80.a, 0xFF) << "A should be 0xFF after DEC from 0";
}

// Test Q register is cleared after non-flag-modifying instruction (LD)
TEST_F(Z80_Test, XCF_Q_ClearedAfterNonFlagModifyingInstruction)
{
    Z80& z80 = *_cpu->GetZ80();
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    
    ResetCPUAndMemory();
    z80.a = 0x00;
    
    // First execute DEC A to set Q = 0x28
    memory[0] = 0x3D;  // DEC A
    z80.Z80Step();
    EXPECT_EQ(z80.q & 0x28, 0x28) << "Q should be set after DEC";
    
    // Now reset and execute LD A, n which doesn't modify flags
    z80.pc = 0;
    memory[0] = 0x3E;  // LD A, n
    memory[1] = 0x00;  // immediate value 0
    z80.Z80Step();
    
    EXPECT_EQ(z80.q, 0) << "Q should be 0 after non-flag-modifying instruction";
}

// Test SCF with Q=0, F=0, A=0 -> YX should be 00
TEST_F(Z80_Test, XCF_SCF_Q0_F0_A0)
{
    Z80& z80 = *_cpu->GetZ80();
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    
    ResetCPUAndMemory();
    z80.a = 0x00;
    z80.f = 0x00;
    z80.q = 0x00;
    
    // SCF (opcode 0x37)
    memory[0] = 0x37;
    z80.Z80Step();
    
    EXPECT_EQ(z80.f & 0x28, 0x00) << "SCF: (Q=0,F=0,A=0) -> YX should be 00";
    EXPECT_TRUE(z80.f & 0x01) << "SCF should set carry flag";
}

// Test SCF with Q=0, F has YX=1, A=0 -> YX should be 11 (Zilog behavior)
TEST_F(Z80_Test, XCF_SCF_Q0_F1_A0_Zilog)
{
    Z80& z80 = *_cpu->GetZ80();
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    
    ResetCPUAndMemory();
    z80.a = 0x00;
    z80.f = 0x28;  // YF and XF set
    z80.q = 0x00;  // Q is 0 (previous instruction didn't modify flags)
    
    // SCF (opcode 0x37)
    memory[0] = 0x37;
    z80.Z80Step();
    
    // Zilog formula: (A | (F & ~Q)) & 0x28 = (0 | (0x28 & ~0)) & 0x28 = 0x28
    EXPECT_EQ(z80.f & 0x28, 0x28) << "SCF: (Q=0,F=1,A=0) -> YX should be 11 (Zilog behavior)";
}

// Test SCF with Q=1, F=1 (same), A=0 -> YX should be 00 
TEST_F(Z80_Test, XCF_SCF_Q1_F1_A0)
{
    Z80& z80 = *_cpu->GetZ80();
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    
    ResetCPUAndMemory();
    z80.a = 0x00;
    z80.f = 0x28;  // YF and XF set
    z80.q = 0x28;  // Q equals F
    
    // SCF (opcode 0x37)
    memory[0] = 0x37;
    z80.Z80Step();
    
    // Zilog formula: (A | (F & ~Q)) & 0x28 = (0 | (0x28 & ~0x28)) & 0x28 = 0
    EXPECT_EQ(z80.f & 0x28, 0x00) << "SCF: (Q=1,F=1,A=0) -> YX should be 00 when Q=F";
}

// Test SCF with A=0xFF (YX bits set) -> YX should be 11 regardless of Q/F
TEST_F(Z80_Test, XCF_SCF_A_Contributes)
{
    Z80& z80 = *_cpu->GetZ80();
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    
    ResetCPUAndMemory();
    z80.a = 0xFF;  // All bits set including YF/XF
    z80.f = 0x00;
    z80.q = 0x00;
    
    // SCF (opcode 0x37)
    memory[0] = 0x37;
    z80.Z80Step();
    
    EXPECT_EQ(z80.f & 0x28, 0x28) << "SCF: A register should contribute YX flags";
}

// Test CCF with Q=0, F=0, A=0 -> YX should be 00
TEST_F(Z80_Test, XCF_CCF_Q0_F0_A0)
{
    Z80& z80 = *_cpu->GetZ80();
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    
    ResetCPUAndMemory();
    z80.a = 0x00;
    z80.f = 0x01;  // Carry set (will be complemented)
    z80.q = 0x00;
    
    // CCF (opcode 0x3F)
    memory[0] = 0x3F;
    z80.Z80Step();
    
    EXPECT_EQ(z80.f & 0x28, 0x00) << "CCF: (Q=0,F=0,A=0) -> YX should be 00";
    EXPECT_FALSE(z80.f & 0x01) << "CCF should complement carry flag";
}

// Test CCF with Q=0, F has YX=1, A=0 -> YX should be 11 (Zilog behavior)
TEST_F(Z80_Test, XCF_CCF_Q0_F1_A0_Zilog)
{
    Z80& z80 = *_cpu->GetZ80();
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    
    ResetCPUAndMemory();
    z80.a = 0x00;
    z80.f = 0x28;  // YF and XF set, no carry
    z80.q = 0x00;
    
    // CCF (opcode 0x3F)
    memory[0] = 0x3F;
    z80.Z80Step();
    
    // Zilog formula: (A | (F & ~Q)) & 0x28 = (0 | (0x28 & ~0)) & 0x28 = 0x28
    EXPECT_EQ(z80.f & 0x28, 0x28) << "CCF: (Q=0,F=1,A=0) -> YX should be 11 (Zilog behavior)";
}

// Test CCF with Q=1, F=1 (same), A=0 -> YX should be 00
TEST_F(Z80_Test, XCF_CCF_Q1_F1_A0)
{
    Z80& z80 = *_cpu->GetZ80();
    uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
    
    ResetCPUAndMemory();
    z80.a = 0x00;
    z80.f = 0x28;  // YF and XF set
    z80.q = 0x28;  // Q equals F
    
    // CCF (opcode 0x3F)
    memory[0] = 0x3F;
    z80.Z80Step();
    
    // Zilog formula: (A | (F & ~Q)) & 0x28 = (0 | (0x28 & ~0x28)) & 0x28 = 0
    EXPECT_EQ(z80.f & 0x28, 0x00) << "CCF: (Q=1,F=1,A=0) -> YX should be 00 when Q=F";
}

/// endregion </Z80 XCF Tests>