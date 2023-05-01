#include "stdafx.h"

#include "tape.h"

uint8_t Tape::handlePortIn()
{
    uint8_t result = 0;

    /// region <Imitate analogue noise>
    static uint16_t counter = 0;
    static uint8_t prevValue = 0;
    static uint16_t prngState = std::rand();

    std::srand(std::time(nullptr));

    /*
    if (counter == 0)
    {
        int randomBit = std::rand() % 2;
        result = (randomBit << 6);

        prevValue = result;
    }
    else
    {
        result = prevValue;
    }
     */

    if (counter == 0)
    {
        result = prngState & 0b0100'0000;

        prngState = std::rand();
    }

    // Galois LFSR with 16-bit register
    // The polynomial used in this implementation is x^16 + x^5 + x^3 + x^2 + 1, which has a maximal period of 2^16-1, or 65,535 values
    uint16_t bit = (prngState >> 0) ^ (prngState >> 2) ^ (prngState >> 3) ^ (prngState >> 5);
    prngState = (prngState >> 1) | (bit << 15);

    /*
    // Simple XORshift algorithm for PRNG
    prngState ^= prngState << 7;
    prngState ^= prngState >> 5;
    prngState ^= prngState << 3;
    */

    counter++;
    /// endregion </Imitate analogue noise>

    return result;
}