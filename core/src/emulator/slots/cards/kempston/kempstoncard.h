#pragma once

// The Kempston joystick interface (Kempston Micro Electronics, 1983) in a slot: the edge-connector interface that gave
// the Spectrum its de-facto joystick port #1F. Machines without a board decode of their own (48K, 128K, +2, +2A, +3)
// read the stick only with this card fitted; the machines that have one (Pentagon, Scorpion, ZX-Evo, Profi, Sprinter,
// Next) refuse it, their built-in holds the function.
//
// The hardware:
//   - issue 4 (1989; zxsp KempstonJoy.cpp, MAME spectrum/kempjoy.cpp): A7-A5 compared to 000 by a 74LS138 with /IORQ,
//     /RD and /M1 = 1 (the acknowledge cycle does not see it), so every port #00-#1F of any high byte answers; the stick
//     through a 74LS366 inverting driver, active high, D5 = 0 through the sixth driver, D6 / D7 = 0 through diodes.
//     Option `decode` = `a7a6a5` (default);
//   - the early boards decode A5 alone (Sinc Times, 1988: "only one address line (A5) is decoded", the fix adds A6 and
//     A7; the WoS ports FAQ's `---- ---- --0- ----`). Option `decode` = `a5`;
//   - no IORQGE, no /DOS input. Behind a Beta 128 disk interface (its through connector) the interface never sees a
//     cycle while the TR-DOS ROM is paged in: the Beta 128 passes /IORQ on only outside DOS (MAME
//     spectrum/beta128.cpp: iorq_r / iorq_w reach the expansion only with the ROM paged out). The claims are therefore
//     gated `NonDos`, as the Pentagon's built-in: in TR-DOS #1F is the WD1793's command / status register.
//
// The stick is the machine's Kempston joystick (EmulatorContext::pJoystick): the card only puts its byte on the bus.
// With the card fitted the port decoder reports HasKempstonJoystick (SlotManager::BuildCards), so every input path of
// the machine's joystick reaches the guest: the `kempston` connector of the controller manager, the host keypad keys,
// joystick_input / `joystick` / POST /joystick on every automation plane, TTD's KempstonJoystick (23) blob. The
// interface drives five lines: D5-D7 read 0 whatever the connector's `extended` option says (the issue-4 board).
//
// Time travel: the interface has no state of its own (a decoder and a driver); its device (KempstonInterfaceTtd, type
// KempstonInterface = 69) is a one-byte version blob that tells a session the card was fitted. The stick byte is the
// machine's KempstonJoystick device, registered because the decoder now answers #1F.

#include <cstdint>
#include <memory>
#include <vector>

#include "emulator/slots/card.h"

class EmulatorContext;
class KempstonInterfaceTtd;

class KempstonCard : public ICard
{
public:
    static constexpr const char* kCardId = "kempston";
    /// The documented port
    static constexpr uint8_t kPort = 0x1F;
    /// The lines the interface drives from the stick (right, left, down, up, fire); D5-D7 read 0
    static constexpr uint8_t kStickBits = 0x1F;

    /// CardType::create
    static std::unique_ptr<ICard> Create(const CardContext& context);

    explicit KempstonCard(const CardContext& context);
    ~KempstonCard() override;

    /// region <PortDevice>
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;
    uint8_t portDeviceReadCycle(uint16_t port, bool& drives) override;
    /// endregion </PortDevice>

    /// region <ICard>
    /// The stick byte on every read the claims hand the card (the claims are the decode)
    uint8_t Peek(uint16_t port, bool& drives) const override;
    /// Time travel: the presence blob (KempstonInterfaceTtd)
    void CollectTtdDevices(std::vector<CardTtdDevice>& out) override;
    /// endregion </ICard>

private:
    EmulatorContext* _context;
    std::unique_ptr<KempstonInterfaceTtd> _ttd;
};
