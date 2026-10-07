#include "snapshotdigest.h"

#include <cstdio>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"

namespace
{
/// FNV-1a 64, fed field by field
class Hash
{
public:
    void Byte(uint8_t b)
    {
        _h ^= b;
        _h *= 1099511628211ull;
    }
    void U16(uint16_t v)
    {
        Byte(static_cast<uint8_t>(v));
        Byte(static_cast<uint8_t>(v >> 8));
    }
    void U32(uint32_t v)
    {
        U16(static_cast<uint16_t>(v));
        U16(static_cast<uint16_t>(v >> 16));
    }
    void Bytes(const uint8_t* data, size_t size)
    {
        for (size_t i = 0; i < size; ++i)
            Byte(data[i]);
    }
    uint64_t Value() const { return _h; }

private:
    uint64_t _h = 14695981038346656037ull;
};
}  // namespace

namespace SnapshotDigest
{
Digest Capture(Emulator* emulator)
{
    Digest d;
    EmulatorContext* context = emulator->GetContext();
    Memory* memory = context->pMemory;
    EmulatorState& st = context->emulatorState;
    Z80* z80 = context->pCore->GetZ80();

    {   // RAM: the configured size, page by page (page index first, so a moved page changes the hash)
        Hash h;
        const uint32_t pages = context->config.ramsize >> 4;
        h.U32(pages);
        for (uint32_t page = 0; page < pages; ++page)
        {
            h.U32(page);
            h.Bytes(memory->RAMPageAddress(static_cast<uint16_t>(page)), PAGE_SIZE);
        }
        d.ram = h.Value();
    }

    {   // The latches the loaders write or leave alone, and what Memory made of them
        Hash h;
        // The 0 is the removed EmulatorState::pXXXX (never written, always 0): the byte stays so the
        // golden table (testdata/loaders/golden/commit-digests.txt) keeps its values
        for (uint8_t v : {st.p7FFD, st.pFE, st.pEFF7, uint8_t{0}, st.pBFFD, st.pFFFD, st.pDFFD, st.pFDFD, st.p1FFD,
                          st.pFF77, st.atm.aFE, st.atm.aFB, st.atm.borderBright})
            h.Byte(v);
        for (unsigned v : st.atm.pFFF7)
            h.U32(v);
        for (uint8_t bank = 0; bank < 4; ++bank)
        {
            h.Byte(static_cast<uint8_t>(memory->GetMemoryBankMode(bank)));
            h.U16(memory->GetPageForBank(bank));
        }
        d.ports = h.Value();
    }

    {   // CPU
        Hash h;
        for (uint16_t v : {z80->af, z80->bc, z80->de, z80->hl, z80->ix, z80->iy, z80->sp, z80->pc, z80->alt.af,
                           z80->alt.bc, z80->alt.de, z80->alt.hl, z80->memptr})
            h.U16(v);
        for (uint8_t v : {z80->i, z80->r_low, z80->r_hi, z80->iff1, z80->iff2, z80->im, z80->halted})
            h.Byte(v);
        h.U32(z80->t);
        d.cpu = h.Value();
    }

    {   // AY 0
        Hash h;
        SoundChip_AY8910* ay = context->pSoundManager ? context->pSoundManager->getAYChip(0) : nullptr;
        if (ay)
        {
            for (uint8_t reg = 0; reg < 16; ++reg)
                h.Byte(ay->readRegister(reg));
            d.ay = h.Value();
        }
    }

    {   // Border and the execution flags (CF_TRDOS and friends)
        Hash h;
        h.Byte(st.border_attr);
        h.Byte(st.flags);
        d.misc = h.Value();
    }
    return d;
}

std::string ToText(const Digest& d)
{
    char text[128];
    std::snprintf(text, sizeof(text), "ram=%08x ports=%08x cpu=%08x ay=%08x misc=%08x", static_cast<uint32_t>(d.ram),
                  static_cast<uint32_t>(d.ports), static_cast<uint32_t>(d.cpu), static_cast<uint32_t>(d.ay),
                  static_cast<uint32_t>(d.misc));
    return text;
}
}  // namespace SnapshotDigest
