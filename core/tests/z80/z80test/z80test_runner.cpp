/**
 * Z80Test Verification Runner
 * 
 * Iterates through test vectors, executes opcodes, computes CRC, and compares.
 */

#include <gtest/gtest.h>
#include "z80test_vectors.h"
#include "z80test_iterator.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include <memory>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <map>
#include <filesystem>
#include "_helpers/test_path_helper.h"

// Relative path from project root to reference CSVs
static const std::string REFERENCE_REL_PATH = "tools/verification/z80/reference/";

// Helper function to construct CSV filename from test index and name
// Matches format used by GenerateReferenceCSVs test
static std::string getCSVFilename(size_t index, const char* testName)
{
    std::string filename = testName;
    for (char& c : filename) {
        if (c == ' ' || c == ',' || c == '(' || c == ')' || c == '[' || c == ']' || c == '+' || c == '\'') c = '_';
        if (c == '/') c = '-';
    }
    return std::to_string(index) + "_" + filename + ".csv";
}

class Z80TestVerification : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Use EmulatorManager for proper initialization
        manager_ = EmulatorManager::GetInstance();
        ASSERT_NE(manager_, nullptr);
        
        // Create a dedicated emulator for verification
        emulator_ = manager_->CreateEmulator("PENTAGON");
        ASSERT_NE(emulator_, nullptr);
        
        // Get the context with Z80 and memory
        auto context = emulator_->GetContext();
        ASSERT_NE(context, nullptr);
        ASSERT_NE(context->pCore, nullptr);
        
        z80_ = context->pCore->GetZ80();
        memory_ = context->pMemory;
        ASSERT_NE(z80_, nullptr);
        ASSERT_NE(memory_, nullptr);
        
        init_crc32_table();
    }
    
    void TearDown() override
    {
        if (emulator_)
        {
            emulator_->Stop();
            std::string uuid = emulator_->GetUUID();
            manager_->RemoveEmulator(uuid);
        }
    }
    
    /**
     * Execute a single test iteration.
     * Sets up Z80 state from combined vector, executes opcode, returns F register.
     * 
     * @param base The original test vector (used for fixed addresses like mem location)
     * @param combined The combined test vector (base XOR counter XOR shifter)
     */
    uint8_t executeIteration(const std::array<uint8_t, VEC_SIZE>& base, 
                             const std::array<uint8_t, VEC_SIZE>& combined)
    {
        // Extract opcode bytes from vector positions 0-3
        uint8_t opcode[4] = {combined[0], combined[1], combined[2], combined[3]};
        
        // Set registers directly (Z80Registers fields)
        z80_->f = combined[4];
        z80_->a = combined[5];
        z80_->bc = combined[6] | (combined[7] << 8);
        z80_->de = combined[8] | (combined[9] << 8);
        z80_->hl = combined[10] | (combined[11] << 8);
        z80_->ix = combined[12] | (combined[13] << 8);
        z80_->iy = combined[14] | (combined[15] << 8);
        z80_->sp = combined[18] | (combined[19] << 8);
        
        // z80test writes the entire 16-byte data area (F,A,BC,DE,HL,IX,IY,mem,SP)
        // to consecutive addresses starting at data.regs. When registers like HL
        // are shifted, they may point into any part of this data area. We need to
        // write all 16 combined bytes so reads from any offset work correctly.
        // The data area starts at base HL (data.mem location, which HL points to).
        // Layout: bytes 4-19 of combined vector go to addresses [base_hl-12..base_hl+3]
        // But for simplicity, we write just the mem region at base_hl which is
        // where most tests read from. For tests that shift HL by small amounts,
        // having mem_lo at base_hl and mem_hi at base_hl+1 covers +/- 1 shifts.
        uint16_t base_hl = base[10] | (base[11] << 8);
        
        // Write full 16-byte data region around the mem location
        // z80test layout: regs(12) + mem(2) + sp(2) = 16 bytes
        // mem is at offset 12 in the data area, so regs start at base_hl - 12
        // BUT: this is complex. Simple fix: write mem (2 bytes) AND surrounding data
        // For now, write bytes 4-19 to addresses base_hl-12 to base_hl+3
        // This way any read within this region gets correct combined value
        uint16_t data_start = base_hl - 12;  // regs start 12 bytes before mem
        for (int i = 0; i < 16; i++) {
            z80_->DirectWrite(data_start + i, combined[4 + i]);
        }
        
        // Handle EX AF,AF' (opcode 0x08) - bytes 16-17 contain F'/A' instead of MEM
        if (opcode[0] == 0x08)
        {
            z80_->alt.f = combined[16];
            z80_->alt.a = combined[17];
        }
        
        // Handle LD A,I (ED 57) and LD A,R (ED 5F) - set I/R registers
        // For these tests, I is in position 16, and we need to set IFF2
        if (opcode[0] == 0xED && (opcode[1] == 0x57 || opcode[1] == 0x5F))
        {
            z80_->i = combined[16];
            z80_->r_low = combined[16] & 0x7F;
            z80_->r_hi = combined[16] & 0x80;
            z80_->iff2 = (combined[17] != 0) ? 1 : 0;  // IFF2 affects P/V flag
        }
        
        // Handle indexed memory ops - write at base IX/IY + base displacement
        // Use base index register AND base displacement for fixed address
        // This matches z80test where mem is at fixed data.mem location
        if (opcode[0] == 0xDD || opcode[0] == 0xFD) {
            uint16_t base_ix = base[12] | (base[13] << 8);
            uint16_t base_iy = base[14] | (base[15] << 8);
            uint16_t idx_base = (opcode[0] == 0xDD) ? base_ix : base_iy;
            int8_t base_d = static_cast<int8_t>(base[2]);  // Use BASE displacement, not combined
            uint16_t addr = idx_base + base_d;
            z80_->DirectWrite(addr, combined[16]);
        }
        
        // Place opcode at PC
        constexpr uint16_t TEST_PC = 0x8000;
        z80_->pc = TEST_PC;
        z80_->DirectWrite(TEST_PC, opcode[0]);
        z80_->DirectWrite(TEST_PC + 1, opcode[1]);
        z80_->DirectWrite(TEST_PC + 2, opcode[2]);
        z80_->DirectWrite(TEST_PC + 3, opcode[3]);
        
        // Reset internal state
        z80_->memptr = 0;
        z80_->q = 0;
        z80_->halted = 0;
        
        // Determine execution mode based on opcode pattern:
        // - EX AF,AF': 08 F1 xx 08 (4 single-byte instructions)
        // - LD A,I: ED 47 ED 57 (LD I,A then LD A,I - 2 instructions)
        // - LD A,R: ED 4F ED 5F (LD R,A then LD A,R - 2 instructions)
        // - SCF+CCF: 37 3F (SCF then CCF - 2 single-byte instructions)
        // - CCF+SCF: 3F 37 (CCF then SCF - 2 single-byte instructions)
        bool isExAfSequence = (opcode[0] == 0x08 && opcode[1] == 0xF1 && opcode[3] == 0x08);
        bool isLdAI = (opcode[0] == 0xED && opcode[1] == 0x47 && opcode[2] == 0xED && opcode[3] == 0x57);
        bool isLdAR = (opcode[0] == 0xED && opcode[1] == 0x4F && opcode[2] == 0xED && opcode[3] == 0x5F);
        bool isScfCcf = (opcode[0] == 0x37 && opcode[1] == 0x3F);  // SCF then CCF
        bool isCcfScf = (opcode[0] == 0x3F && opcode[1] == 0x37);  // CCF then SCF
        
        if (isExAfSequence || isLdAI || isLdAR)
        {
            // Execute all 4 opcode bytes as instructions
            constexpr uint16_t TEST_PC_END = TEST_PC + 4;
            int max_steps = 8;
            while (z80_->pc < TEST_PC_END && max_steps-- > 0 && !z80_->halted)
            {
                z80_->Z80Step(true);
            }
        }
        else if (isScfCcf || isCcfScf)
        {
            // Execute 2 single-byte instructions (SCF+CCF or CCF+SCF)
            z80_->Z80Step(true);  // First instruction
            z80_->Z80Step(true);  // Second instruction
        }
        else
        {
            // Execute single instruction
            z80_->Z80Step(true);
        }
        
        return z80_->f;
    }
    
    EmulatorManager* manager_ = nullptr;
    std::shared_ptr<Emulator> emulator_;
    Z80* z80_ = nullptr;
    Memory* memory_ = nullptr;
};

// Test a single vector - parameterized test would be better but start simple
TEST_F(Z80TestVerification, RunAllVectors)
{
    // Blacklist tests that require non-Zilog Z80 flavor (NEC NMOS, ST CMOS)
    static const std::vector<std::string> BLACKLIST =
    {
        "SCF (NEC)", "CCF (NEC)",
        "SCF (ST)", "CCF (ST)",
    };
    
    auto isBlacklisted = [&](const char* name)
    {
        return std::find(BLACKLIST.begin(), BLACKLIST.end(), name) != BLACKLIST.end();
    };
    
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    
    // Open file to dump C++ generated CRCs (ground truth)
    std::string crc_dump_path = (TestPathHelper::findProjectRoot() / "tools/verification/z80/cpp_ground_truth_crcs.json").string();
    std::ofstream crc_dump(crc_dump_path);
    crc_dump << "{\n";
    crc_dump << "  \"generated_by\": \"C++ Z80 Emulator (Ground Truth)\",\n";
    crc_dump << "  \"tests\": [\n";
    
    bool first_entry = true;
    
    for (size_t i = 0; i < NUM_VECTORS; i++)
    {
        const Z80TestVector& vec = VECTORS[i];
        
        if (isBlacklisted(vec.name))
        {
            skipped++;
            continue;
        }
        
        Z80TestIterator iter(vec.base, vec.counter, vec.shifter);
        
        uint32_t crc = 0xFFFFFFFF;
        uint32_t iterations = 0;
        std::array<uint8_t, VEC_SIZE> combined;
        std::vector<uint8_t> f_outputs;  // Collect F outputs for debugging
        
        while (iter.next(combined))
        {
            uint8_t f_out = executeIteration(vec.base, combined);
            crc = crc32_update(crc, f_out);
            iterations++;
            if (f_outputs.size() < 20)
            {  // Keep first 20 for debugging
                f_outputs.push_back(f_out);
            }
        }
        
        // Write to CRC dump file
        if (!first_entry) crc_dump << ",\n";
        first_entry = false;
        crc_dump << "    {\n";
        crc_dump << "      \"name\": \"" << vec.name << "\",\n";
        crc_dump << "      \"crc\": \"0x" << std::hex << std::setw(8) << std::setfill('0') << crc << "\",\n";
        crc_dump << "      \"iterations\": " << std::dec << iterations << ",\n";
        crc_dump << "      \"expected_crc\": \"0x" << std::hex << std::setw(8) << std::setfill('0') << vec.expected_crc << "\",\n";
        crc_dump << "      \"expected_iterations\": " << std::dec << vec.expected_iterations << ",\n";
        crc_dump << "      \"match\": " << (crc == vec.expected_crc && iterations == vec.expected_iterations ? "true" : "false") << "\n";
        crc_dump << "    }";
        
        bool crc_match = (crc == vec.expected_crc);
        bool iter_match = (iterations == vec.expected_iterations);
        
        if (crc_match && iter_match)
        {
            passed++;
        }
        else
        {
            failed++;
            std::stringstream ss;
            ss << "Test '" << vec.name << "' FAILED"
               << "\n  CRC: 0x" << std::hex << crc 
               << " (expected 0x" << vec.expected_crc << ")"
               << "\n  Iterations: " << std::dec << iterations
               << " (expected " << vec.expected_iterations << ")"
               << "\n  First 10 F_out values: ";
            for (size_t j = 0; j < std::min(f_outputs.size(), (size_t)10); j++)
            {
                ss << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)f_outputs[j] << " ";
            }
            
            // Second pass: compare against CSV reference to find first mismatch
            std::string csv_filename = getCSVFilename(i, vec.name);
            std::string csv_path = (TestPathHelper::findProjectRoot() / REFERENCE_REL_PATH / csv_filename).string();
            std::ifstream csv_file(csv_path);
            if (csv_file.is_open())
            {
                    std::string line;
                    std::getline(csv_file, line); // Skip header
                    
                    // Re-run the test and compare each iteration
                    Z80TestIterator iter2(vec.base, vec.counter, vec.shifter);
                    std::array<uint8_t, VEC_SIZE> combined2;
                    uint32_t iter_num = 0;
                    bool found_mismatch = false;
                    
                    while (iter2.next(combined2) && std::getline(csv_file, line))
                    {
                        uint8_t f_actual = executeIteration(vec.base, combined2);
                        
                        // Parse F_out from CSV line (field 1: iter,f_out,vec0...)
                        std::istringstream line_stream(line);
                        std::string field;
                        for (int f = 0; f <= 1 && std::getline(line_stream, field, ','); f++)
                        {
                            if (f == 1)  // F_out field
                            {
                                uint8_t f_expected = (uint8_t)std::stoul(field, nullptr, 16);
                                if (f_actual != f_expected)
                                {
                                    ss << "\n  First mismatch at iter " << std::dec << iter_num << ":"
                                       << " actual=0x" << std::hex << std::setw(2) << std::setfill('0') << (int)f_actual
                                       << " expected=0x" << std::setw(2) << std::setfill('0') << (int)f_expected
                                       << " (diff=0x" << std::setw(2) << std::setfill('0') << (int)(f_actual ^ f_expected) << ")";
                                    
                                    // Show opcode for context
                                    ss << "\n  Opcode: 0x" << std::setw(2) << std::setfill('0') << (int)combined2[0];
                                    if (combined2[0] == 0xED)
                                    {
                                        ss << " 0x" << std::setw(2) << std::setfill('0') << (int)combined2[1];
                                    }
                                    found_mismatch = true;
                                    break;
                                }
                            }
                        }
                        if (found_mismatch) break;
                        iter_num++;
                    }
                    if (!found_mismatch)
                    {
                        ss << "\n  No per-iteration mismatch found (CRC computation issue?)";
                    }
            }
            else
            {
                ss << "\n  (CSV not found at " << csv_path << ")";
            }
            
            ADD_FAILURE() << ss.str();
        }
    }
    
    crc_dump << "\n  ]\n";
    crc_dump << "}\n";
    crc_dump.close();
    
    std::cout << "\n=== Z80Test Verification Results ===" << std::endl;
    std::cout << "Passed: " << passed << "/" << (NUM_VECTORS - skipped) << std::endl;
    std::cout << "Failed: " << failed << "/" << (NUM_VECTORS - skipped) << std::endl;
    std::cout << "Skipped: " << skipped << " (NEC/ST flavor tests)" << std::endl;
    std::cout << "CRC dump written to: " << crc_dump_path << std::endl;
    
    EXPECT_EQ(failed, 0) << "Some z80test cases failed";
}

// Individual test for SCF to debug
TEST_F(Z80TestVerification, SCF_Individual)
{
    // Find SCF vector
    const Z80TestVector* scf = nullptr;
    for (size_t i = 0; i < NUM_VECTORS; i++)
    {
        if (std::string(VECTORS[i].name) == "SCF")
        {
            scf = &VECTORS[i];
            break;
        }
    }
    ASSERT_NE(scf, nullptr) << "SCF vector not found";
    
    Z80TestIterator iter(scf->base, scf->counter, scf->shifter);
    
    uint32_t crc = 0xFFFFFFFF;
    uint32_t iterations = 0;
    std::array<uint8_t, VEC_SIZE> combined;
    
    while (iter.next(combined))
    {
        uint8_t f_out = executeIteration(scf->base, combined);
        crc = crc32_update(crc, f_out);
        iterations++;
        
        // Debug first few iterations
        if (iterations <= 3)
        {
            std::cout << "Iter " << iterations << ": F_in=0x" << std::hex 
                     << (int)combined[4] << ", A=0x" << (int)combined[5]
                     << " -> F_out=0x" << (int)f_out << std::endl;
        }
    }
    
    std::cout << "SCF: " << std::dec << iterations << " iterations" << std::endl;
    std::cout << "CRC: 0x" << std::hex << crc 
             << " (expected 0x" << scf->expected_crc << ")" << std::endl;
    
    EXPECT_EQ(iterations, scf->expected_iterations);
    EXPECT_EQ(crc, scf->expected_crc);
}

// Generate reference CSVs for all tests - run manually when needed
// These CSVs serve as ground truth for Python reference comparison
TEST_F(Z80TestVerification, DISABLED_GenerateReferenceCSVs)
{
    auto refPath = TestPathHelper::findProjectRoot() / "tools/verification/z80/reference";
    std::filesystem::create_directories(refPath);
    
    std::cout << "Generating reference CSVs to: " << refPath << std::endl;
    
    for (size_t i = 0; i < NUM_VECTORS; i++)
    {
        const Z80TestVector& vec = VECTORS[i];
        
        // Create sanitized filename from test name
        std::string filename = vec.name;
        for (char& c : filename)
        {
            if (c == ' ' || c == ',' || c == '(' || c == ')' || c == '[' || c == ']' || c == '+' || c == '\'') c = '_';
            if (c == '/') c = '-';
        }
        filename = std::to_string(i) + "_" + filename + ".csv";
        
        std::ofstream csv(refPath / filename);
        csv << "iter,f_out";
        for (int j = 0; j < 20; j++) csv << ",vec" << j;
        csv << "\n";
        
        Z80TestIterator iter(vec.base, vec.counter, vec.shifter);
        std::array<uint8_t, VEC_SIZE> combined;
        uint32_t iteration = 0;
        
        while (iter.next(combined))
        {
            uint8_t f_out = executeIteration(vec.base, combined);
            csv << std::dec << iteration << "," << std::hex << "0x" << std::setw(2) << std::setfill('0') << (int)f_out;
            for (int j = 0; j < 20; j++)
            {
                csv << ",0x" << std::setw(2) << std::setfill('0') << (int)combined[j];
            }
            csv << "\n";
            iteration++;
        }
        
        std::cout << "  " << vec.name << ": " << std::dec << iteration << " iterations -> " << filename << std::endl;
    }
    
    std::cout << "Done generating " << NUM_VECTORS << " reference CSVs" << std::endl;
}
