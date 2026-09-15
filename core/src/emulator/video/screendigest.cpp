#include "screendigest.h"

#include "emulator/memory/memory.h"

uint64_t ScreenDigest::DigestRAMPage(Memory* memory, uint16_t page)
{
    uint64_t hash = kInitialValue;

    if (!memory)
        return hash;

    const uint8_t* base = memory->RAMPageAddress(page);
    if (!base)
        return hash;

    for (size_t i = 0; i < kRAMPageSize; i++)
    {
        hash ^= base[i];
        hash *= kPrime;
    }

    return hash;
}

uint64_t ScreenDigest::DigestZ80Range(Memory* memory, uint16_t start, uint16_t end)
{
    uint64_t hash = kInitialValue;

    if (!memory || start > end)
        return hash;

    for (uint32_t address = start; address <= end; address++)
    {
        hash ^= memory->DirectReadFromZ80Memory(static_cast<uint16_t>(address));
        hash *= kPrime;
    }

    return hash;
}
