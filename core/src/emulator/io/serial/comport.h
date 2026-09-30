#pragma once

/// @file comport.h
/// @brief The machine's COM port (network adapters TDD §7): a 16550 UART on
/// ports #F8EF..#FFEF (register = A10..A8) and the peer on its other end.
/// Owned by NetworkManager, fitted by [NETWORK] ComPort=; nothing exists (no
/// port claim, no per-frame work) while ComPort=NONE.

#include <cstdint>
#include <memory>
#include <string>

#include <functional>
#include <vector>

#include "emulator/io/network/netstate.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/io/serial/uart16550.h"
#include "emulator/ports/portdecoder.h"

class EmulatorContext;

class ComPort final : public PortDevice
{
public:
    static constexpr uint8_t kPortLowByte = 0xEF;

    ComPort(EmulatorContext* context, const Uart16550::Params& params, std::unique_ptr<ISerialPeer> peer);
    ~ComPort();

    ComPort(const ComPort&) = delete;
    ComPort& operator=(const ComPort&) = delete;

    /// Claim #xxEF (every model that has no device of its own there)
    bool AttachToPorts(PortDecoder* decoder);
    void DetachFromPorts();

    /// Machine reset: the UART to its reset values; the peer keeps its link
    void Reset();

    /// Frame boundary (machine thread): the UART catches up, the peer flushes
    void OnFrame();

    // PortDevice
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;
    bool portDeviceClaimsRead(uint16_t) override { return true; }

    Uart16550& Uart() { return _uart; }
    const Uart16550& Uart() const { return _uart; }
    ISerialPeer* Peer() const { return _peer.get(); }

    /// Absolute base-clock T-states now (the UART's clock)
    uint64_t Now() const;

    /// The stream peer as a virtual-network guest (its sockets in the network
    /// state belong to it), nullptr for loopback
    INetGuest* NetGuest() const;

    /// TTD state (netstate::Com): UART registers, the echo queue by value,
    /// bytes from the network by journal reference. False when something did
    /// not fit the limits (saved as far as it goes)
    bool SaveState(netstate::Com& out) const;

    /// `bytes` resolves a journal reference (the W5300's ByteSource). False
    /// when some referenced bytes were missing
    using ByteSource = std::function<bool(uint32_t source, uint32_t offset, uint32_t length, std::vector<uint8_t>& out)>;
    bool LoadState(const netstate::Com& in, const ByteSource& bytes);

private:
    void AddAccessWait();

    EmulatorContext* _context = nullptr;
    std::unique_ptr<ISerialPeer> _peer;
    Uart16550 _uart;
    PortDecoder* _decoder = nullptr;
};
