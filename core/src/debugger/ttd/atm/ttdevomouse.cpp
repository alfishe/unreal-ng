#include "stdafx.h"

#include "ttdevomouse.h"

#include <cstring>
#include <type_traits>

#include "emulator/memory/atm/evoavrmouse.h"

namespace ttd
{

namespace
{
/// Blob layout: version, then the mouse state; padding-free (hashed byte-wise)
struct EvoMouseBlob
{
    uint8_t version;
    EvoAvrMouse::State state;
    uint8_t reserved[3];
};
static_assert(sizeof(EvoMouseBlob) == 8, "EvoMouse TTD blob size drift");
static_assert(std::is_trivially_copyable_v<EvoMouseBlob>, "EvoMouse TTD blob must be POD");

constexpr uint8_t kEvoMouseVersion = 1;
}  // namespace

size_t TTDEvoMouse::TTDStateSize() const
{
    return sizeof(EvoMouseBlob);
}

void TTDEvoMouse::TTDSaveState(uint8_t* dst) const
{
    EvoMouseBlob blob{};
    blob.version = kEvoMouseVersion;
    blob.state = _mouse.GetState();
    std::memcpy(dst, &blob, sizeof(blob));
}

void TTDEvoMouse::TTDLoadState(const uint8_t* src)
{
    EvoMouseBlob blob{};
    std::memcpy(&blob, src, sizeof(blob));
    _mouse.SetState(blob.state);
}

uint64_t TTDEvoMouse::TTDHashState() const
{
    const EvoAvrMouse::State state = _mouse.GetState();
    uint64_t h = 0xcbf29ce484222325ULL;
    for (uint8_t byte : {state.x, state.y, state.buttons, state.connected})
    {
        h ^= byte;
        h *= 0x100000001b3ULL;
    }
    return h;
}

}  // namespace ttd
