#pragma once

/// @file ttdsprinter.h
/// @brief TTD serializers of the Peters Plus Sprinter Sp2000 (Sprinter phase S7,
/// tdd-integration §2).
///
/// What a Sprinter checkpoint holds besides the CPU registers (TTDCpuState: the
/// Z84C15 engine executes on the Z80State register file itself), the chipset
/// struct, the 256 RAM pages and the core devices (WD1793, IDE, AY, Kempston
/// mouse and joystick, tape):
///
///   | Id | Blob | Size (v1) |
///   |---|---|---|
///   | 25 SprinterPld | SprinterPldState, the decoder's own latches, the INT source, the frame height the raster runs with, the active configuration module (name + state), the accelerator | 177 + module state + accelerator |
///   | 18 Ds12887 | the CMOS (shared serializer, ttdds12887.h) | |
///   | 28 SprinterVideoRam | the 256 KB video RAM | 1 + 262 144 |
///   | 29 Z84C15 | the chip beside its register file (Z84C15::SaveState; v2: the CTC's anchors, triggered timers and clock rate, the watchdog's folded clocks) | 1 + 227 |
///   | 30 SprinterFastRam | the 64 KB fast RAM | 1 + 65 536 |
///   | 31 SprinterInput | the AT keyboard's byte stream and the serial mouse's packet generator | 85 |
///   | 32 SprinterCovoxBlaster | the Covox / Covox-Blaster: ring (256 x 16 bit), control, play / write indices, 16-bit phase, INT request, DAC words, the next play tick, statistics (phase S6) | 1 + 544 |
///   | 33 SprinterIsa | the #9FBD latch, the card kind fitted in each ISA slot, the cards' own bus state (none yet: network cards carry theirs in EthernetNics) | 4 |
///   | 35 Wd1793Context | the WD1793's command in flight beyond the BetaDisk blob (ttdwd1793context.h) | 1 + 112 |
///
/// Every blob starts with a version byte (kVersion); a blob of another version
/// is not loaded (the device keeps its live state, as for a missing blob).
/// Multi-byte fields are little-endian.
///
/// Host input never enters a blob: the keyboard arrives as journaled PC key
/// events (TTDInputKind::PcKey) and the mouse as the journaled Kempston
/// counters (MouseMove / MouseButtons ...). What the blobs hold is what the
/// machine made of that input so far - the bytes still on the keyboard's wire,
/// the serial mouse's packet in flight, the SIO's receive FIFOs - so a restore
/// in the middle of a byte or a packet resumes on the same bit.
///
/// Video RAM and fast RAM: whole-array blobs until TTD v2 memory regions exist
/// (migration-trajectory Phase 1, Step 5); then they move to regions and these
/// ids stop being written.

#include <cstddef>
#include <cstdint>
#include <string>

#include "debugger/ttd/ttdserializable.h"
#include "emulator/sound/sprinter/covoxblaster.h"
#include "debugger/ttd/engine/ttdregiontracker.h"

class PortDecoder_Sprinter;

namespace ttd
{

/// Id 25: the PLD and the decoder
class TTDSprinterPld : public TTDSerializable
{
public:
    /// 1: the layout with the accelerator section (SprinterAccelState, 280 bytes since S5)
    static constexpr uint8_t kVersion = 1;
    /// Bytes before the module state: version, SprinterPldState, the decoder fields, the module name
    static constexpr size_t kFixedSize = 175;
    /// Longest module name the blob keeps (the registry looks the module up by it)
    static constexpr size_t kModuleNameSize = 32;

    explicit TTDSprinterPld(PortDecoder_Sprinter& decoder) : _decoder(decoder) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "SprinterPld"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::SprinterPld; }
    uint64_t TTDHashState() const override;
    /// The INT source's acknowledged pulse (an i64 machine time at 128: frame x frame length + start) advances every
    /// frame: a time field (measured 2026-10-09)
    TTDDeviceDescriptor TTDDescribe() const override;
    static constexpr uint16_t kAckedPulseOffset = 128;

private:
    /// The largest StateSize() of the registered modules: the blob has room for any of them
    size_t ModuleStateRoom() const;

    PortDecoder_Sprinter& _decoder;
};

/// Id 29: the Z84C15's on-chip block (the CPU registers are TTDCpuState)
class TTDSprinterZ84 : public TTDSerializable
{
public:
    /// v2: the CTC counter mode (anchor, count, zero counts, triggered timers, the system clock period) and the
    /// watchdog's clocks before a speed change; v1 blobs (timer-only CTC) are not loaded
    static constexpr uint8_t kVersion = 2;

    explicit TTDSprinterZ84(PortDecoder_Sprinter& decoder) : _decoder(decoder) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "Z84C15"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::Z84C15; }
    uint64_t TTDHashState() const override;

private:
    PortDecoder_Sprinter& _decoder;
};

/// Id 31: the keyboard (Ps2KeyboardStream) and the serial mouse (MsSerialMouse)
class TTDSprinterInput : public TTDSerializable
{
public:
    static constexpr uint8_t kVersion = 3;
    static constexpr size_t kSize = 89;

    explicit TTDSprinterInput(PortDecoder_Sprinter& decoder) : _decoder(decoder) {}

    size_t TTDStateSize() const override { return kSize; }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "SprinterInput"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::SprinterInput; }
    uint64_t TTDHashState() const override;

private:
    PortDecoder_Sprinter& _decoder;
};

/// Id 28: the 256 KB video RAM (the palette cache and the INT list are rebuilt from it)
class TTDSprinterVideoRam : public TTDSerializable, public ITTDRegionSource
{
public:
    static constexpr uint8_t kVersion = 1;

    explicit TTDSprinterVideoRam(PortDecoder_Sprinter& decoder) : _decoder(decoder) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "SprinterVideoRam"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::SprinterVideoRam; }
    uint64_t TTDHashState() const override;

    /// Time-travel engine region (compared at each capture; restore rebuilds the palette and the INT list)
    void TTDRegions(std::vector<TTDDeviceRegion>& out) override;
    void TTDArmRegions(bool) override {}
    bool TTDStateWithoutRegions(uint8_t& peripheralId, std::vector<uint8_t>& state) const override;
    bool TTDLoadStateWithoutRegions(const uint8_t* state, size_t size) override;

private:
    PortDecoder_Sprinter& _decoder;
    TTDRegionTracker _tracker;
};

/// Id 30: the 64 KB fast RAM (Memory's four cache pages; not RAM pages, so not in the page store)
class TTDSprinterFastRam : public TTDSerializable, public ITTDRegionSource
{
public:
    static constexpr uint8_t kVersion = 1;
    static constexpr size_t kFastRamSize = 64 * 1024;

    explicit TTDSprinterFastRam(PortDecoder_Sprinter& decoder) : _decoder(decoder) {}

    size_t TTDStateSize() const override { return 1 + kFastRamSize; }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "SprinterFastRam"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::SprinterFastRam; }
    uint64_t TTDHashState() const override;

    /// Time-travel engine region (compared at each capture: written through the generic CPU path)
    void TTDRegions(std::vector<TTDDeviceRegion>& out) override;
    void TTDArmRegions(bool) override {}
    bool TTDStateWithoutRegions(uint8_t& peripheralId, std::vector<uint8_t>& state) const override;
    bool TTDLoadStateWithoutRegions(const uint8_t* state, size_t size) override;

private:
    PortDecoder_Sprinter& _decoder;
    TTDRegionTracker _tracker;
};

/// Id 32: the Covox / Covox-Blaster (CovoxBlasterState, little-endian fields). The PLD blob keeps its copy
/// of the control byte (layout v1 unchanged); both agree
class TTDSprinterCovoxBlaster : public TTDSerializable
{
public:
    static constexpr uint8_t kVersion = 1;
    static constexpr size_t kSize = 1 + sizeof(CovoxBlasterState);

    explicit TTDSprinterCovoxBlaster(PortDecoder_Sprinter& decoder) : _decoder(decoder) {}

    size_t TTDStateSize() const override { return kSize; }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "SprinterCovoxBlaster"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::SprinterCovoxBlaster; }
    uint64_t TTDHashState() const override;

private:
    PortDecoder_Sprinter& _decoder;
};

/// Id 33: the ISA slots (Sprinter ISA tdd §9): version, the whole #9FBD byte, the kind fitted in slot 1 and 2,
/// then each card's own bus state (SprinterIsaBus::SaveState). A blob recorded with another population is not
/// loaded (logged); a session with one is refused at load (TimeTravelManager's slot guards)
class TTDSprinterIsa : public TTDSerializable
{
public:
    explicit TTDSprinterIsa(PortDecoder_Sprinter& decoder) : _decoder(decoder) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "SprinterIsa"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::SprinterIsa; }
    uint64_t TTDHashState() const override;

private:
    PortDecoder_Sprinter& _decoder;
};

}  // namespace ttd
