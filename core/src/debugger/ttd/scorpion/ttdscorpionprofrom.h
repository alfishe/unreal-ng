#pragma once

/// @file ttdscorpionprofrom.h
/// @brief TTD serializer for Scorpion ZS 256/1024 ProfROM state.
///
/// Per TDD §6.4: model-specific chipset state uses the peripheral registry
/// rather than bloating TTDChipsetState. The common TTD framework knows
/// nothing about Scorpion — it only calls through TTDSerializable.
///
/// What lives here and why it cannot live anywhere else:
///   - plane_id (== EmulatorState::profrom_bank) is clocked by a GAL from
///     READ addresses (#0100-#010F), not by any OUT, so it is not
///     reproducible from port latches — it depends on the whole read history.
///   - scorpionDosTrigger (DD50.1 magic button) is host-armed and released by
///     an instruction fetch from the upper half — likewise not port-derived.
///   - p7EFD and p1FFD are ordinary latches, but they are Scorpion-specific
///     inputs to the #0000 paging chain and therefore are NOT carried by the
///     model-agnostic TTDChipsetState. Without them a restore loses the plane
///     window (p7EFD) and the service/RAM0 selection (p1FFD).

#include "debugger/ttd/ttdserializable.h"

#include <cstddef>

class EmulatorContext;

namespace ttd {

/// @brief Packed state blob for Scorpion ProfROM serialization.
///
/// Explicit `reserved` filler keeps the struct free of implicit padding: TTD
/// blobs are copied and hashed byte-wise, so unnamed padding would leak
/// uninitialized bytes into the stream and the divergence hash.
struct ScorpionProfROMState
{
    uint8_t plane_id;            ///< Effective 64K plane (quadrant) 0..31
    uint8_t rom_page;            ///< ROM page 0-3 inside the plane (kNoRomPage = RAM at #0000)
    uint8_t p7EFD;               ///< Plane window-select latch
    uint8_t p1FFD;               ///< Service / RAM0 / RAM-bank latch
    uint8_t scorpionDosTrigger;  ///< DD50.1 magic-button trigger
    uint8_t reserved[3];
};

static_assert(sizeof(ScorpionProfROMState) == 8, "ScorpionProfROMState must be 8 bytes");
static_assert(offsetof(ScorpionProfROMState, reserved) + 3 == sizeof(ScorpionProfROMState),
              "ScorpionProfROMState has implicit trailing padding - resize reserved[]");

/// @brief TTDSerializable implementation for Scorpion ProfROM state.
class TTDScorpionProfROM : public TTDSerializable
{
public:
    /// Sentinel for rom_page when RAM is latched at #0000 (#1FFD bit 0), so no
    /// ROM page is mapped there at all.
    static constexpr uint8_t kNoRomPage = 0xFF;

    explicit TTDScorpionProfROM(EmulatorContext* context);

    size_t TTDStateSize() const override { return sizeof(ScorpionProfROMState); }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "ScorpionProfROM"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::ScorpionProfROM; }
    uint64_t TTDHashState() const override;

    /// @brief ROM page 0-3 currently mapped at #0000, or #kNoRomPage.
    /// Derived from the live paging, not stored state — the capture path
    /// records it so a replay can prove the paging chain landed identically.
    uint8_t CurrentRomPage() const;

private:
    /// Fill a blob from live state. Shared by save and hash so the two can
    /// never disagree about what constitutes ProfROM state.
    ScorpionProfROMState Snapshot() const;

    EmulatorContext* _context;
};

} // namespace ttd
