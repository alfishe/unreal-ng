#pragma once

/// @file eeprom93c46.h
/// @brief A 93C46 serial EEPROM in its 64 x 16-bit organization, as the RTL8019AS wires it (9346CR bits EECS,
/// EESK, EEDI, EEDO; RTL8019AS datasheet §6.3; the Microwire protocol of the 93C46 data sheets).
///
/// Every instruction starts with CS high and a 1 start bit, then two opcode bits and six address bits, MSB first,
/// each latched on a rising SK edge:
///   READ  1 10 aaaaaa : DO gives a dummy 0 with the last address bit, then D15..D0 on the following rising edges
///                       (sequential: the next word follows)
///   EWEN  1 00 11xxxx, EWDS 1 00 00xxxx : write enable / disable
///   WRITE 1 01 aaaaaa + 16 data bits, ERASE 1 11 aaaaaa, ERAL 1 00 10xxxx, WRAL 1 00 01xxxx + 16 data bits:
///                       carried out when CS falls, if enabled (the part is ready at once here)
/// Worked example (the RTL kit's READ_WORD of word 2): 9 rising edges shift in 1 10 000010; DO = 0; 16 more rising
/// edges read word 2 = bytes 4 and 5 of the content (low byte first) = the first two MAC bytes.

#include <array>
#include <cstdint>

class Eeprom93c46
{
public:
    static constexpr int kWords = 64;

    Eeprom93c46() { _s.words.fill(0xFFFF); }

    void Load(const std::array<uint16_t, kWords>& words) { _s.words = words; }
    const std::array<uint16_t, kWords>& Words() const { return _s.words; }
    /// Byte n of the content (low byte of word n / 2 first: how the RTL8019AS datasheet lists it)
    uint8_t Byte(int index) const
    {
        const uint16_t w = _s.words[(index >> 1) & (kWords - 1)];
        return static_cast<uint8_t>((index & 1) ? w >> 8 : w);
    }

    /// The pins as the host drives them (9346CR bits 3-1)
    void SetPins(bool cs, bool sk, bool di);
    /// DO (9346CR bit 0)
    bool DataOut() const { return _s.dataOut != 0; }

    struct State
    {
        std::array<uint16_t, kWords> words;
        uint8_t cs, sk, dataOut, writeEnabled;
        uint8_t bitCount;     ///< bits received in this instruction
        uint8_t phase;        ///< 0 command, 1 data in (WRITE / WRAL), 2 data out (READ), 3 done
        uint8_t opcode, address;
        uint16_t shift;       ///< command bits / data in
        uint16_t outWord;     ///< the word being read out
        uint8_t outBits;      ///< bits of outWord still to come
        uint8_t reserved[3];
    };
    const State& GetState() const { return _s; }
    void LoadState(const State& state) { _s = state; }

private:
    void RisingEdge(bool di);
    void Finish();

    State _s{};
};
