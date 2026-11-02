#pragma once

/// @file ttdgmxpaging.h
/// @brief TTD serializer for the Scorpion GMX card's register file.
///
/// The host Scorpion's latches ride in ScorpionProfROM (#1FFD, #7EFD and the plane, the DD50 trigger, turbo); this
/// blob adds what only the card has: #00, #78FD, the scroll start (#7AFD / #7CFD), #DFFD, the magic shift register
/// a RECONF armed, the Magic-button freeze of the read-backs, and a RECONF still waiting for the end of its
/// instruction. None of it is in the model-agnostic TTDChipsetState (GMX design section 6).

#include "debugger/ttd/ttdserializable.h"

#include <cstddef>

class EmulatorContext;

namespace ttd {

/// @brief Packed blob, version 1. Explicit reserved bytes: blobs are copied and hashed byte-wise
struct GmxPagingState
{
    uint8_t version;      ///< 1
    uint8_t p00;          ///< #xx00: scheme, RECONF, FIXROM, BLKEXT, BLKEX2, ST/PENT
    uint8_t p78FD;        ///< the #8000 page (stored, before the ^2)
    uint8_t p7AFD;        ///< scroll start, low (bits 7-4)
    uint8_t p7CFD;        ///< scroll start, high (bits 5-0)
    uint8_t pDFFD;        ///< RAM page bits 4-6 for #C000
    uint8_t magicShift;   ///< the magic byte bits not read yet
    uint8_t magicLock;    ///< 1 = the read-backs are frozen (Magic button, released by IN #FF)
    uint8_t lock7AFD;     ///< frozen #7AFD read-back
    uint8_t lock7EFD;     ///< frozen #7EFD read-back
    uint8_t reconfPending;  ///< 1 = a RECONF waits for the end of its instruction
    uint8_t bootState;      ///< PortDecoder_ScorpionGMX::BOOT_* (0 in older blobs: a booted machine)
    uint8_t reserved[4];
};

static_assert(sizeof(GmxPagingState) == 16, "GmxPagingState must be 16 bytes");
static_assert(offsetof(GmxPagingState, reserved) + 4 == sizeof(GmxPagingState),
              "GmxPagingState has implicit trailing padding - resize reserved[]");

class TTDGmxPaging : public TTDSerializable
{
public:
    static constexpr uint8_t kVersion = 1;

    explicit TTDGmxPaging(EmulatorContext* context);

    size_t TTDStateSize() const override { return sizeof(GmxPagingState); }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "GmxPaging"; }
    TTDDeviceType TTDType() const override { return TTDDeviceType::GmxPaging; }
    uint64_t TTDHashState() const override;

private:
    GmxPagingState Snapshot() const;

    EmulatorContext* _context;
};

}  // namespace ttd
