#pragma once

/// @file card.h
/// @brief A card built for a slot (docs/inprogress/2026-10-03-zx-bus-slots/architecture.md §3.2): ICard is the running
/// card, CardType its registration - the code side of a reference-data CardDef, i.e. how to build one.
///
/// SlotManager builds a card for every fitted slot whose card id has a CardType here (BuildCards, once the machine's
/// sound manager and port decoder exist) and releases them before the machine goes. The cards that still have a
/// legacy owner (SoundManager's AY socket, GS, MoonSound and Covox; NetworkManager's ZX-bus cards) have no CardType:
/// their slots decide CONFIG fields instead (SlotManager::Apply). The ZX-MultiSound is the first card built here.
///
/// The card sits on the bus as a PortDevice: its port claims (the reference data, with the card's options) go into the
/// port decoder's claim table in its slot order, and the table resolves every cycle on them with the machine's bus
/// arbitration (IORQGE, BoardWins / CardWins, RdWr detection, ROM-fetch lock). The card keeps its own time base: the
/// access path hands it no time, it reads the machine's now when it needs it (portDeviceInMethod / OutMethod).

// Qt defines `slots` / `signals` as macros; this header names the slots namespace
#pragma push_macro("slots")
#pragma push_macro("signals")
#undef slots
#undef signals

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "emulator/ports/portdecoder.h"
#include "emulator/slots/slotplanner.h"
#include "emulator/sound/audiodeviceinfo.h"

class EmulatorContext;

/// What a card is built from
struct CardContext
{
    EmulatorContext* emulator = nullptr;
    const slots::CardDef* def = nullptr;    ///< the reference data entry (claims, options, functions)
    std::string slot;                       ///< "zxbus.1"
    slots::CardOptions options;             ///< as planned
};

/// One mixer row a card contributes to SoundManager
struct CardMixerRow
{
    AudioSourceType type = AudioSourceType::Custom;
    std::string name;
};

/// A running card (architecture.md §3.2). Every call comes from the emulation thread
class ICard : public PortDevice
{
public:
    explicit ICard(const CardContext& context) : _def(context.def), _slot(context.slot), _options(context.options)
    {
    }
    virtual ~ICard() = default;
    ICard(const ICard&) = delete;
    ICard& operator=(const ICard&) = delete;

    const slots::CardDef& Def() const
    {
        return *_def;
    }
    const std::string& SlotId() const
    {
        return _slot;
    }
    const slots::CardOptions& Options() const
    {
        return _options;
    }

    /// region <Bus>
    /// PortDevice: portDeviceInMethod / portDeviceOutMethod / portDeviceReadCycle are the bus cycles the claim table
    /// hands the card at the machine's now

    /// The value a read would return, without side effects (debugger); drives = false when the card would leave the
    /// data bus alone
    virtual uint8_t Peek(uint16_t port, bool& drives) const = 0;

    /// Bus /RESET (the machine's reset)
    virtual void BusReset()
    {
    }
    /// endregion </Bus>

    /// region <Frames and audio (SoundManager)>
    virtual void FrameStart()
    {
    }
    /// The frame ended: samples = the output frames SoundManager mixes this frame (0: none are mixed, the card still
    /// runs to the frame's end)
    virtual void FrameEnd(size_t samples)
    {
        (void)samples;
    }
    /// The mixer's core rate changed (frame boundary)
    virtual void SetOutputRate(uint32_t rate)
    {
        (void)rate;
    }
    /// The card's mixer rows, in display order
    virtual void MixerRows(std::vector<CardMixerRow>& out) const
    {
        (void)out;
    }
    /// A row's interleaved stereo int16 buffer of the last FrameEnd (the frames SoundManager asked for); nullptr for
    /// a type the card does not own
    virtual const int16_t* MixerBuffer(AudioSourceType type) const
    {
        (void)type;
        return nullptr;
    }
    /// Full-scale sources: the mixer sums on its wide float bus with the master limiter while the card is fitted
    virtual bool WantsWideMix() const
    {
        return false;
    }
    /// endregion </Frames and audio (SoundManager)>

private:
    const slots::CardDef* _def;
    std::string _slot;
    slots::CardOptions _options;
};

/// The registration of a card the slots build (architecture.md §3.2: the reference data holds everything else)
struct CardType
{
    const char* id = "";
    std::unique_ptr<ICard> (*create)(const CardContext& context) = nullptr;
};

/// Every registered card type
std::span<const CardType> CardTypes();

/// The registration of a card id; nullptr for a card that has none (built by a legacy owner, or not emulated)
const CardType* FindCardType(std::string_view id);

#pragma pop_macro("signals")
#pragma pop_macro("slots")
