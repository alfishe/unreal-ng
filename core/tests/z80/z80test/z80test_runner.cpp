/**
 * Z80Test Verification Runner
 * 
 * Runs all 115 CRC-validated z80test cases against the unreal-ng Z80 emulator.
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
#include "_helpers/test_path_helper.h"

// Relative path from project root to reference CSVs
static const std::string REFERENCE_REL_PATH = "tools/verification/z80/reference/";

// Map test names to CSV filenames - includes all tests with CSV reference
static const std::map<std::string, std::string> TEST_CSV_MAP = {
    // Original passing tests
    {"SBC HL,RR", "073_sbc_hl_rr.csv"},
    {"ADC HL,RR", "071_adc_hl_rr.csv"},
    {"ADD HL,RR", "069_add_hl_rr.csv"},
    // Tests requiring Python fix (use C++ as ground truth)
    {"ALO A,(HL)", "029_alo_a__p_hl.csv"},
    {"ALO A,(XY)", "032_alo_a__p_xy.csv"},
    {"RLC [R,(HL)]", "047_rlc__r__p_hl_.csv"},
    {"RRC [R,(HL)]", "048_rrc__r__p_hl_.csv"},
    {"RL [R,(HL)]", "049_rl__r__p_hl_.csv"},
    {"RR [R,(HL)]", "050_rr__r__p_hl_.csv"},
    {"SLA [R,(HL)]", "051_sla__r__p_hl_.csv"},
    {"SRA [R,(HL)]", "052_sra__r__p_hl_.csv"},
    {"SLIA [R,(HL)]", "053_slia__r__p_hl_.csv"},
    {"SRL [R,(HL)]", "054_srl__r__p_hl_.csv"},
    {"SRO (XY)", "055_sro__p_xy.csv"},
    {"SRO (XY),R", "056_sro__p_xy_r.csv"},
    {"INC [R,(HL)]", "059_inc__r__p_hl_.csv"},
    {"DEC [R,(HL)]", "060_dec__r__p_hl_.csv"},
    {"INC (XY)", "063_inc__p_xy.csv"},
    {"DEC (XY)", "064_dec__p_xy.csv"},
    {"BIT N,(HL)", "075_bit_n__p_hl.csv"},
    {"BIT N,[R,(HL)]", "076_bit_n__r__p_hl_.csv"},
    {"BIT N,(XY)", "077_bit_n__p_xy.csv"},
    {"BIT N,(XY),-", "078_bit_n__p_xy_-.csv"},
    {"LDI", "089_ldi.csv"},
    {"LDD", "090_ldd.csv"},
    {"LDIR", "091_ldir.csv"},
    {"LDDR", "092_lddr.csv"},
    {"LDIR->NOP'", "093_ldir-_nop_prime.csv"},
    {"LDDR->NOP'", "094_lddr-_nop_prime.csv"},
    {"CPI", "095_cpi.csv"},
    {"CPD", "096_cpd.csv"},
    {"CPIR", "097_cpir.csv"},
    {"CPDR", "098_cpdr.csv"},
    {"IN R,(C)", "100_in_r__p_c.csv"},
    {"IN (C)", "101_in__p_c.csv"},
    {"INI", "102_ini.csv"},
    {"IND", "103_ind.csv"},
    {"INIR", "104_inir.csv"},
    {"INDR", "105_indr.csv"},
    {"INIR->NOP'", "106_inir-_nop_prime.csv"},
    {"INDR->NOP'", "107_indr-_nop_prime.csv"},
    {"OUTI", "111_outi.csv"},
    {"OUTD", "112_outd.csv"},
    {"OTIR", "113_otir.csv"},
    {"OTDR", "114_otdr.csv"},
    {"PUSH+POP RR", "129_push_plus_pop_rr.csv"},
    {"POP+PUSH AF", "130_pop_plus_push_af.csv"},
    {"LD [R,(HL)],[R,(HL)]", "137_ld__r__p_hl___r__p_hl_.csv"},
    {"LD [X,(XY)],[X,(XY)]", "138_ld__x__p_xy___x__p_xy_.csv"},
    {"LD R,(XY)", "139_ld_r__p_xy.csv"},
    {"LD (XY),R", "140_ld__p_xy_r.csv"},
};

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
     */
    uint8_t executeIteration(const std::array<uint8_t, VEC_SIZE>& combined)
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
        
        // Write memory operand at HL (for (HL) tests)
        uint16_t hl = z80_->hl;
        z80_->DirectWrite(hl, combined[16]);
        z80_->DirectWrite(hl + 1, combined[17]);
        
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
        
        // Handle indexed memory ops - write at IX+d or IY+d if applicable
        // Displacement is in opcode[2] for DD CB dd xx or FD CB dd xx
        if (opcode[0] == 0xDD || opcode[0] == 0xFD) {
            uint16_t base = (opcode[0] == 0xDD) ? z80_->ix : z80_->iy;
            int8_t d = static_cast<int8_t>(opcode[2]);
            uint16_t addr = base + d;
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
        bool isExAfSequence = (opcode[0] == 0x08 && opcode[1] == 0xF1 && opcode[3] == 0x08);
        bool isLdAI = (opcode[0] == 0xED && opcode[1] == 0x47 && opcode[2] == 0xED && opcode[3] == 0x57);
        bool isLdAR = (opcode[0] == 0xED && opcode[1] == 0x4F && opcode[2] == 0xED && opcode[3] == 0x5F);
        
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
    // Also EX AF,AF' which requires multi-instruction sequence (08 F1 C5 08)
    static const std::vector<std::string> BLACKLIST =
    {
        "SCF (NEC)", "CCF (NEC)",
        "SCF (ST)", "CCF (ST)",
        "SCF+CCF", "CCF+SCF",
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
            uint8_t f_out = executeIteration(combined);
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
            for (size_t j = 0; j < std::min(f_outputs.size(), (size_t)10); j++) {
                ss << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)f_outputs[j] << " ";
            }
            
            // Second pass: compare against CSV reference to find first mismatch
            auto csv_it = TEST_CSV_MAP.find(vec.name);
            if (csv_it != TEST_CSV_MAP.end()) {
                std::string csv_path = (TestPathHelper::findProjectRoot() / REFERENCE_REL_PATH / csv_it->second).string();
                std::ifstream csv_file(csv_path);
                if (csv_file.is_open()) {
                    std::string line;
                    std::getline(csv_file, line); // Skip header
                    
                    // Re-run the test and compare each iteration
                    Z80TestIterator iter2(vec.base, vec.counter, vec.shifter);
                    std::array<uint8_t, VEC_SIZE> combined2;
                    uint32_t iter_num = 0;
                    bool found_mismatch = false;
                    
                    while (iter2.next(combined2) && std::getline(csv_file, line)) {
                        uint8_t f_actual = executeIteration(combined2);
                        
                        // Parse F_out from CSV line (field 9, after iter,F_in,A_in,BC_in,DE_in,HL_in,IX_in,IY_in,SP_in,F_out)
                        std::istringstream line_stream(line);
                        std::string field;
                        for (int f = 0; f <= 9 && std::getline(line_stream, field, ','); f++) {
                            if (f == 9) {  // F_out field
                                uint8_t f_expected = (uint8_t)std::stoul(field, nullptr, 16);
                                if (f_actual != f_expected) {
                                    ss << "\n  First mismatch at iter " << std::dec << iter_num << ":"
                                       << " actual=0x" << std::hex << std::setw(2) << std::setfill('0') << (int)f_actual
                                       << " expected=0x" << std::setw(2) << std::setfill('0') << (int)f_expected
                                       << " (diff=0x" << std::setw(2) << std::setfill('0') << (int)(f_actual ^ f_expected) << ")";
                                    
                                    // Show opcode for context
                                    ss << "\n  Opcode: 0x" << std::setw(2) << std::setfill('0') << (int)combined2[0];
                                    if (combined2[0] == 0xED) {
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
                    if (!found_mismatch) {
                        ss << "\n  No per-iteration mismatch found (CRC computation issue?)";
                    }
                } else {
                    ss << "\n  (CSV not found at " << csv_path << ")";
                }
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
        uint8_t f_out = executeIteration(combined);
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
