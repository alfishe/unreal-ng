#pragma once
#include "z80test_vectors.h"
#include <cstdint>
#include <array>
#include <vector>

/**
 * Z80TestIterator - Exact port of counter/shifter iteration from z80test idea.asm
 * 
 * Counter: Starts at mask, decrements through all masked bit combinations
 * Shifter: Walks single bit through each set bit position in mask
 * 
 * Total iterations = (2^counter_bits) * (shifter_bits + 1)
 * The +1 accounts for phase 0 (no shift applied).
 * 
 * Port of Python version in z80_verification.py
 */
class Z80TestIterator
{
public:
    Z80TestIterator(const std::array<uint8_t, VEC_SIZE>& base,
                    const std::array<uint8_t, VEC_SIZE>& counter_mask,
                    const std::array<uint8_t, VEC_SIZE>& shifter_mask)
        : base_(base), counter_mask_(counter_mask)
        {
        
        // Pre-compute shifter bit positions (like Python version)
        for (size_t byte_idx = 0; byte_idx < VEC_SIZE; byte_idx++)
        {
            uint8_t mask = shifter_mask[byte_idx];
            for (int bit = 0; bit < 8; bit++) {
                if (mask & (1 << bit)) {
                    shifter_positions_.push_back({byte_idx, bit});
                }
            }
        }
        
        reset();
    }
    
    void reset()
    {
        // Start in phase 0 (shifter = all zeros)
        counter_ = counter_mask_;
        shifter_.fill(0);
        shifter_phase_ = 0;
        counter_done_ = false;
        done_ = false;
    }
    
    bool next(std::array<uint8_t, VEC_SIZE>& combined)
    {
        if (done_) return false;
        
        // Build combined = base XOR counter XOR shifter
        for (size_t i = 0; i < VEC_SIZE; i++)
        {
            combined[i] = base_[i] ^ counter_[i] ^ shifter_[i];
        }
        
        // Advance to next state (Python style: yield then advance)
        advanceCounter();
        
        return true;
    }
    
    bool isDone() const { return done_; }
    
private:
    void advanceCounter()
    {
        // Multibyte decrement with borrow through masked bits
        for (size_t i = 0; i < VEC_SIZE; i++) {
            if (counter_[i] == 0)
            {
                counter_[i] = counter_mask_[i];
                continue;  // Borrow to next byte
            }
            counter_[i] = (counter_[i] - 1) & counter_mask_[i];
            return;  // Successfully decremented
        }
        
        // All bytes wrapped - counter exhausted for this shifter phase
        advanceShifter();
    }
    
    void advanceShifter()
    {
        shifter_phase_++;
        
        if (shifter_phase_ > shifter_positions_.size())
        {
            // All phases exhausted
            done_ = true;
            return;
        }
        
        // Reset counter for new phase
        counter_ = counter_mask_;
        
        // Set up shifter for this phase
        shifter_.fill(0);
        if (shifter_phase_ > 0 && shifter_phase_ <= shifter_positions_.size())
        {
            auto& pos = shifter_positions_[shifter_phase_ - 1];
            shifter_[pos.first] = 1 << pos.second;
        }
    }
    
    std::array<uint8_t, VEC_SIZE> base_;
    std::array<uint8_t, VEC_SIZE> counter_mask_;
    std::array<uint8_t, VEC_SIZE> counter_;
    std::array<uint8_t, VEC_SIZE> shifter_;
    std::vector<std::pair<size_t, int>> shifter_positions_;
    size_t shifter_phase_;
    bool counter_done_;
    bool done_;
};

// CRC-32 table lookup (IEEE 802.3 polynomial)
inline uint32_t crc32_table[256];
inline bool crc32_table_init = false;

inline void init_crc32_table()
{
    if (crc32_table_init) return;
    for (int i = 0; i < 256; i++)
    {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
        {
            if (crc & 1)
            {
                crc = (crc >> 1) ^ 0xEDB88320;
            }
            else
            {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_table_init = true;
}

inline uint32_t crc32_update(uint32_t crc, uint8_t byte)
{
    return crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
}
