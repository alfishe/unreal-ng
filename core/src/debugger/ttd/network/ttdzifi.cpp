#include "debugger/ttd/network/ttdzifi.h"

#include <cstring>

#include "emulator/emulatorcontext.h"
#include "emulator/io/network/zifi.h"

namespace ttd
{

size_t TTDZiFi::TTDStateSize() const
{
    return sizeof(ZiFi::State);
}

void TTDZiFi::TTDSaveState(uint8_t* dst) const
{
    ZiFi::State state{};
    if (_context && _context->pZiFi)
        _context->pZiFi->SaveState(state);
    std::memcpy(dst, &state, sizeof(state));
}

void TTDZiFi::TTDLoadState(const uint8_t* src)
{
    if (!_context || !_context->pZiFi)
        return;
    ZiFi::State state{};
    std::memcpy(&state, src, sizeof(state));
    _context->pZiFi->LoadState(state);
}

uint64_t TTDZiFi::TTDHashState() const
{
    if (!_context || !_context->pZiFi)
        return 0;
    ZiFi::State state{};
    _context->pZiFi->SaveState(state);
    uint64_t hash = 1469598103934665603ull;
    const auto* bytes = reinterpret_cast<const uint8_t*>(&state);
    for (size_t i = 0; i < sizeof(state); ++i)
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}

}  // namespace ttd
