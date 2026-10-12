#include "ttdnextstate.h"

#include <cstring>

#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextstatecodec.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace ttd {

TTDNextState::TTDNextState(PortDecoder_Next& decoder) : _decoder(decoder), _size(NextStateCodec::Save(decoder).size()) {}

void TTDNextState::TTDSaveState(uint8_t* dst) const
{
    if (!dst)
        return;
    const std::vector<uint8_t> image = NextStateCodec::Save(_decoder);
    std::memcpy(dst, image.data(), image.size() < _size ? image.size() : _size);
}

void TTDNextState::TTDLoadState(const uint8_t* src)
{
    if (src)
        NextStateCodec::Load(_decoder, src, _size);
}

uint64_t TTDNextState::TTDHashState() const
{
    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a offset basis
    for (uint8_t byte : NextStateCodec::Save(_decoder))
    {
        h ^= byte;
        h *= 0x100000001b3ULL;  // FNV-1a prime
    }
    return h;
}

void TTDNextSystemArea::TTDSaveState(uint8_t* dst) const
{
    if (!dst)
        return;
    Memory* memory = &_memory;
    for (unsigned i = 0; i < kPages; i++)
        std::memcpy(dst + i * 16384, memory->ROMPageHostAddress(static_cast<uint8_t>(kFirstPage + i)), 16384);
}

void TTDNextSystemArea::TTDLoadState(const uint8_t* src)
{
    if (!src)
        return;
    Memory* memory = &_memory;
    for (unsigned i = 0; i < kPages; i++)
        std::memcpy(memory->ROMPageHostAddress(static_cast<uint8_t>(kFirstPage + i)), src + i * 16384, 16384);
}

uint64_t TTDNextSystemArea::TTDHashState() const
{
    // FNV-1a over 64-bit words (a byte at a time over 256 KB would be a fifth of a millisecond per frame)
    uint64_t h = 0xcbf29ce484222325ULL;
    constexpr size_t kPageBytes = 16384;
    for (unsigned i = 0; i < kPages; i++)
    {
        const uint8_t* page = _memory.ROMPageHostAddress(static_cast<uint8_t>(kFirstPage + i));
        for (size_t offset = 0; offset < kPageBytes; offset += sizeof(uint64_t))
        {
            uint64_t word;
            std::memcpy(&word, page + offset, sizeof(word));
            h ^= word;
            h *= 0x100000001b3ULL;  // FNV-1a prime
        }
    }
    return h;
}

}  // namespace ttd
