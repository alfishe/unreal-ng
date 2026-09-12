/// @file ttd_v2_serialization_test.cpp
/// @brief Verify memcpy serialization for TTD structures without padding byte mismatches.

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

// Dummy TTD structures mimicking real ones
struct TTDCpuState {
    uint16_t pc, sp, af, bc, de, hl, ix, iy;
    uint16_t alt_af, alt_bc, alt_de, alt_hl;
    uint8_t  i, r_low, r_hi, iff1, iff2, im, halted, reserved0;
    uint16_t memptr;
    uint8_t  q;
    uint16_t eipos, haltpos;
    uint8_t  nmi_in_progress, int_pending, int_gate, reserved1;
    uint32_t halt_cycle;
};

TEST(TTDSerializationTest, MemcpyVsStructAssignment) {
    TTDCpuState state1;
    // We intentionally do not use = {} to simulate uninitialized padding
    std::memset(&state1, 0xAA, sizeof(state1));
    state1.pc = 0x8000;
    state1.sp = 0xFFFF;
    state1.q = 1;

    TTDCpuState state2;
    std::memset(&state2, 0xBB, sizeof(state2));
    // Struct assignment may not copy padding bytes, leaving them as 0xBB
    state2 = state1;

    // Memcpy copies everything, guaranteeing identical bytes
    TTDCpuState state3;
    std::memset(&state3, 0x00, sizeof(state3));
    std::memcpy(&state3, &state1, sizeof(state1));

    // XOR delta between state1 and state2 will be non-zero in padding bytes!
    std::vector<uint8_t> delta2(sizeof(TTDCpuState));
    auto* s1 = reinterpret_cast<uint8_t*>(&state1);
    auto* s2 = reinterpret_cast<uint8_t*>(&state2);
    size_t nonZero2 = 0;
    for (size_t i = 0; i < sizeof(TTDCpuState); ++i) {
        delta2[i] = s1[i] ^ s2[i];
        if (delta2[i] != 0) ++nonZero2;
    }

    // XOR delta between state1 and state3 will be zero!
    std::vector<uint8_t> delta3(sizeof(TTDCpuState));
    auto* s3 = reinterpret_cast<uint8_t*>(&state3);
    size_t nonZero3 = 0;
    for (size_t i = 0; i < sizeof(TTDCpuState); ++i) {
        delta3[i] = s1[i] ^ s3[i];
        if (delta3[i] != 0) ++nonZero3;
    }

    // This demonstrates that memcpy is safer for bit-exact XOR delta compression.
    // NOTE: On some compilers (like Clang), struct assignment is lowered to memcpy,
    // so nonZero2 might be 0. We don't assert EXPECT_GT(nonZero2, 0) because of this compiler-dependent behavior.
    EXPECT_EQ(nonZero3, 0) << "Memcpy should guarantee identical bits!";
}

TEST(TTDSerializationTest, ZeroInitializedMemcpy) {
    // Correct way to initialize for capture
    TTDCpuState state1;
    std::memset(&state1, 0, sizeof(state1));
    state1.pc = 0x1234;

    TTDCpuState state2;
    std::memset(&state2, 0, sizeof(state2));
    state2.pc = 0x1234;

    auto* s1 = reinterpret_cast<uint8_t*>(&state1);
    auto* s2 = reinterpret_cast<uint8_t*>(&state2);
    
    bool identical = true;
    for (size_t i = 0; i < sizeof(TTDCpuState); ++i) {
        if (s1[i] != s2[i]) identical = false;
    }
    EXPECT_TRUE(identical);
}
