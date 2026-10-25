#include "kempstoncard.h"

#include "debugger/ttd/ttdserializable.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/joystick/joystick.h"

/// The interface's blob: u1 version. The interface holds no state; the blob says the card was fitted (the stick byte
/// is the machine's KempstonJoystick device)
class KempstonInterfaceTtd final : public ttd::TTDSerializable
{
public:
    static constexpr uint8_t kVersion = 1;

    size_t TTDStateSize() const override
    {
        return 1;
    }
    void TTDSaveState(uint8_t* dst) const override
    {
        dst[0] = kVersion;
    }
    void TTDLoadState(const uint8_t* src) override
    {
        (void)src;
    }
    std::string TTDDeviceName() const override
    {
        return "KempstonInterface";
    }
    ttd::TTDDeviceType TTDType() const override
    {
        return ttd::TTDDeviceType::KempstonInterface;
    }
};

std::unique_ptr<ICard> KempstonCard::Create(const CardContext& context)
{
    return std::make_unique<KempstonCard>(context);
}

KempstonCard::KempstonCard(const CardContext& context)
    : ICard(context), _context(context.emulator), _ttd(std::make_unique<KempstonInterfaceTtd>())
{
}

KempstonCard::~KempstonCard() = default;

uint8_t KempstonCard::portDeviceInMethod(uint16_t port)
{
    bool drives = false;
    return Peek(port, drives);
}

void KempstonCard::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    // A read-only interface: /WR is not decoded (the claims are In only)
    (void)port;
    (void)value;
}

uint8_t KempstonCard::portDeviceReadCycle(uint16_t port, bool& drives)
{
    return Peek(port, drives);
}

uint8_t KempstonCard::Peek(uint16_t port, bool& drives) const
{
    (void)port;  // the claims are the decode: a read handed to the card is one the interface answers
    drives = true;
    // Five lines through the 74LS366; D5 is the sixth driver's input tied low, D6 / D7 diodes to 0 (issue 4)
    const uint8_t stick = _context != nullptr && _context->pJoystick != nullptr ? _context->pJoystick->Read() : 0x00;
    return static_cast<uint8_t>(stick & kStickBits);
}

void KempstonCard::CollectTtdDevices(std::vector<CardTtdDevice>& out)
{
    out.push_back({_ttd.get(), SlotId() + "." + kCardId, nullptr});
}
