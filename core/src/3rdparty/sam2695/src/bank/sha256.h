// libsam2695 - incremental SHA-256 (FIPS 180-4): the bank identity, hashed while a file streams in.
#pragma once

#include "sam2695/soundbank.h"

#include <cstddef>
#include <cstdint>

namespace sam2695
{

class Sha256Stream
{
public:
    Sha256Stream() { Reset(); }
    void Reset();
    void Update(const uint8_t* data, size_t size);
    BankDigest Final(); // also resets

private:
    uint32_t _h[8];
    uint8_t _block[64];
    size_t _used = 0;
    uint64_t _total = 0;
};

} // namespace sam2695
