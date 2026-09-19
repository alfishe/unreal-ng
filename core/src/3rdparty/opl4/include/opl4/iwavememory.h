// libopl4 — wave memory abstraction (core TDD §6).
//
// The library never owns the ROM image; the host supplies an IWaveMemory.
// Address space is 4 MiB (22-bit). Writes below romEnd() are the
// implementation's business (discarded by the default implementation).
#pragma once

#include <stdint.h>

namespace opl4
{

class IWaveMemory
{
public:
    virtual ~IWaveMemory() = default;

    virtual uint8_t Read(uint32_t addr) = 0; // 22-bit address space
    virtual void Write(uint32_t addr, uint8_t v) = 0;
    virtual uint32_t RomEnd() const = 0;
    virtual uint32_t RamEnd() const = 0;
};

} // namespace opl4
