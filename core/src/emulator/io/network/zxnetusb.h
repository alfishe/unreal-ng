#pragma once

/// @file zxnetusb.h
/// @brief ZXNETUSB network card (NedoPC, rev C): WIZnet W5300 + SL811 USB host
/// behind a CPLD on the ZX-Bus (network adapters TDD §4.1).
///
/// Port decoding follows the card's CPLD (svn zxusbnet cpld/rtl zbus.v,
/// ports.v, wizmap.v): the card owns every port with A7..A0 = #AB.
///   A15 = 1, A9..A8 = 11  #83AB  control: b4 W5300 /RESET (0 = held in reset),
///                                b2 route W5300 INT, b6 INT to the Z80,
///                                read: b0 W5300 INT active, b7 card INT
///   A15 = 1, A9..A8 = 10  #82AB  mode: b4 W5300 in I/O space, b3 invert chip A0,
///                                b2 W5300 over a ROM window (b1..0 which),
///                                b6 SL811 M/S; b2 and b4 together = both off.
///                                read: b7 VBUS, b6 M/S
///   A15 = 1, A9..A8 = 01  #81AB  W5300 address bits 9..6 (readable)
///   A15 = 1, A9..A8 = 00  #80AB  SL811 address register
///   A15 = 0               W5300 byte {#81AB[3..0], A13..A9, A8 ^ invert}
///                         when #82AB b4 is on; SL811 data otherwise
/// A14..A10 are not decoded (mirrors). Every register resets to 0 with the
/// machine, so the W5300 stays in reset until software sets #83AB bit 4.
///
/// The SL811 USB host is reported absent (reads #FF): the NedoOS W5300 kernel
/// probes it for a USB disk and copes with "none".
///
/// Memory-mapped mode (#82AB b2 on, b4 off; wizmap.v, PRM §6): the chip
/// replaces ROM in the 16 KB window #82AB b1..0 selects. Writes into the
/// window always reach the chip; reads come from it while ROM is paged there
/// (the ZX-Bus /CSROM condition: window 0 with ROM at #0000). Inside the
/// window, #0000-#1FFF is the chip's 1 KB space mirrored, #2000-#2FFF and
/// #3000-#3FFF are socket A11..A9's TX / RX FIFO registers repeated, so LDIR
/// and POP stream them. Installed as a host bus overlay only while on.
///
/// The card's /INT to the Z80 (#83AB b6) is not wired into the CPU: the Z80
/// model has one INT owner per machine and no additive device INT yet.
/// Software sees the interrupt state in #83AB b0 / b7 (NedoOS polls; no known
/// program uses the card interrupt).

#include <cstdint>

#include "emulator/io/network/w5300.h"
#include "emulator/memory/hostbusoverlay.h"
#include "emulator/ports/portdecoder.h"

class Core;

class ZxNetUsb final : public PortDevice
{
public:
    static constexpr uint8_t kPortLowByte = 0xAB;

    // #83AB bits
    static constexpr uint8_t kCtlW5300Run = 0x10;      ///< W5300 /RESET released
    static constexpr uint8_t kCtlW5300IntEna = 0x04;
    static constexpr uint8_t kCtlZxIntEna = 0x40;
    // #82AB bits
    static constexpr uint8_t kModeW5300Ports = 0x10;
    static constexpr uint8_t kModeInvertA0 = 0x08;
    static constexpr uint8_t kModeRomMap = 0x04;
    static constexpr uint8_t kModeSl811Ms = 0x40;

    /// @param core installs the memory-mapped window overlay (nullptr: ports only)
    explicit ZxNetUsb(VirtualNetwork* network, Core* core = nullptr);
    ~ZxNetUsb();

    ZxNetUsb(const ZxNetUsb&) = delete;
    ZxNetUsb& operator=(const ZxNetUsb&) = delete;

    /// Claim the #xxAB ports as a full-decode low-byte observer (every model)
    bool AttachToPorts(PortDecoder* decoder);
    void DetachFromPorts();

    /// Machine reset (ZX-Bus /RESET): card registers to 0, W5300 held in reset
    void Reset();

    // PortDevice
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;
    bool portDeviceClaimsRead(uint16_t) override { return true; }   // the card drives every #xxAB read

    /// Card /INT to the Z80 (level, no vector)
    bool InterruptActive() const;

    /// Drive the Z80's /INT from InterruptActive (Z80::SetDeviceIntLine).
    /// Called after everything that can change it: a bus access to the card,
    /// a network event, reset, a state load
    void UpdateIntLine();

    W5300& Chip() { return _chip; }
    const W5300& Chip() const { return _chip; }
    uint8_t Control() const { return _p83; }
    uint8_t Mode() const { return _p82; }
    uint8_t AddressHigh() const { return _p81; }

    /// TTD state: card ports, the chip, the virtual network's tables (netstate.h)
    /// `comGuest`: the COM port's peer, whose network sockets are saved and
    /// restored as its own (VirtualNetwork::SaveState)
    bool SaveState(netstate::Adapters& out, const SerialGuests& serial = {}) const;
    bool LoadState(const netstate::Adapters& in, const W5300::ByteSource& bytes, const SerialGuests& serial = {});

    /// W5300 byte address an I/O access at `port` reaches (A15 = 0)
    uint16_t ChipAddress(uint16_t port) const;
    bool ChipInPorts() const { return (_p82 & kModeW5300Ports) && !(_p82 & kModeRomMap); }
    bool ChipRunning() const { return (_p83 & kCtlW5300Run) != 0; }
    bool ChipInMemory() const { return (_p82 & kModeRomMap) && !(_p82 & kModeW5300Ports); }

    /// W5300 byte address a memory access at `address` inside the window reaches
    uint16_t WindowChipAddress(uint16_t address) const;

private:
    /// The memory-mapped window as a host bus overlay
    class RomWindow final : public HostBusOverlay
    {
    public:
        explicit RomWindow(ZxNetUsb& card) : _card(card) {}
        uint8_t onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged) override;
        void onWrite(uint16_t addr, uint8_t value, bool romPaged) override;

    private:
        ZxNetUsb& _card;
    };

    void UpdateWindow();   ///< install / move / remove the overlay after #82AB changed

    W5300 _chip;
    VirtualNetwork* _network = nullptr;
    Core* _core = nullptr;
    RomWindow _window{*this};
    bool _windowInstalled = false;
    PortDecoder* _decoder = nullptr;
    uint8_t _p83 = 0;
    uint8_t _p82 = 0;
    uint8_t _p81 = 0;
};
