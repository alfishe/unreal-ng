#pragma once

/// @file gsdebughooks.h
/// @brief What a card CPU debugger sees of a General Sound slot card that runs a real Z80 (classic GS, NeoGS) -
/// gs-debugger design §4.
///
/// A card with hooks set (GeneralSoundCard::setDebugHooks) runs its debug loop (GSCardRunner::runToDebug) and its
/// debug bus callbacks; without them it runs the fast loop and the plain callbacks, so a machine nobody debugs pays
/// nothing. The swap happens between two runs, on the emulation thread.
///
/// The bus calls never stop the card: the core cannot leave an instruction half done. They report the access, and
/// the hooks keep a hit pending until the next instruction boundary (beforeStep / endRun), where they may park the
/// machine: the stop is "write #4100 <- #12 by the instruction at #0C30", the card on a boundary.

#include <cstdint>
#include <cstring>

/// A General Sound event (BRK_EVENT, design §4.1 B2 / B3). The card side (the INT and NMI acceptance, a reset, a
/// page register write, the card's mailbox accesses) is reported by the card's hooks to the card CPU's breakpoints;
/// the host side (the main CPU's mailbox accesses) by the card's host ports to the main CPU's breakpoints
enum class GSCardEvent : uint8_t
{
    Int = 0,
    Nmi,
    Reset,
    PageSwitch,        ///< `port` the page register written (GS: #00 MPAG), `value` the byte
    CardCommandRead,   ///< the card read the host's command (port #01): `value` the command
    CardDataRead,      ///< the card read the host's data byte (port #02)
    CardReplyWrite,    ///< the card wrote its reply to the host (port #03)
    HostCommandWrite,  ///< the host wrote a command (OUT #BB)
    HostDataWrite,     ///< the host wrote a data byte (OUT #B3)
    HostReplyRead,     ///< the host read the card's reply (IN #B3)
    // Card hardware (appended: the values above are stored in breakpoints). `port` carries the event's detail, the
    // tap hit's `arg`
    DacFetch,          ///< a read in #6000-#7FFF latched a DAC: `port` the channel (0-3), `value` the byte
    ClockChange,       ///< the card CPU's clock changed (NeoGS GSCFG0): `port` the new clock in MHz, `value` GSCFG0
    DmaStart,          ///< a NeoGS DMA module started: `value` the module (0 ZX-DMA, 1 SD, 2 MP3)
    DmaDone,           ///< a NeoGS DMA module ended: `value` the module, `port` 1 when it finished (interrupt), 0 aborted
    ConfigWrite,       ///< the card wrote NeoGS GSCFG0 (every write, a clock change or not): `value` the byte
    Count
};

/// The host side's events fire on the main CPU, the others on the card's
constexpr bool GSCardEventIsHostSide(GSCardEvent kind)
{
    return kind >= GSCardEvent::HostCommandWrite && kind <= GSCardEvent::HostReplyRead;
}

/// The surfaces' names ("int", "nmi", "reset", "page", "card_command", ...)
inline const char* GSCardEventName(GSCardEvent kind)
{
    switch (kind)
    {
        case GSCardEvent::Int: return "int";
        case GSCardEvent::Nmi: return "nmi";
        case GSCardEvent::Reset: return "reset";
        case GSCardEvent::PageSwitch: return "page";
        case GSCardEvent::CardCommandRead: return "card_command";
        case GSCardEvent::CardDataRead: return "card_data";
        case GSCardEvent::CardReplyWrite: return "card_reply";
        case GSCardEvent::HostCommandWrite: return "host_command";
        case GSCardEvent::HostDataWrite: return "host_data";
        case GSCardEvent::HostReplyRead: return "host_reply";
        case GSCardEvent::DacFetch: return "dac_fetch";
        case GSCardEvent::ClockChange: return "clock_change";
        case GSCardEvent::DmaStart: return "dma_start";
        case GSCardEvent::DmaDone: return "dma_done";
        case GSCardEvent::ConfigWrite: return "gscfg0";
        case GSCardEvent::Count: break;
    }
    return "unknown";
}

/// The kind of a surface name; false when there is none
inline bool ParseGSCardEvent(const char* name, GSCardEvent& out)
{
    for (uint8_t k = 0; k < static_cast<uint8_t>(GSCardEvent::Count); k++)
    {
        if (std::strcmp(GSCardEventName(static_cast<GSCardEvent>(k)), name) == 0)
        {
            out = static_cast<GSCardEvent>(k);
            return true;
        }
    }
    return false;
}

class IGSDebugHooks
{
public:
    /// The card stands at the boundary before the instruction at `pc`: an execute breakpoint there, or a hit the bus
    /// calls of the last instruction left pending, parks the machine inside this call
    virtual void beforeStep(uint16_t pc) = 0;
    /// The run reached its target, the card on the boundary before `pc`: a pending hit is reported now, not at the
    /// next run's first instruction (no execute check: the instruction at `pc` has not started)
    virtual void endRun(uint16_t pc) = 0;

    /// Memory data accesses (no opcode fetch) and port accesses of the instruction at `pc`, after the card did them.
    /// A read returns the byte the CPU gets (`value` unless a hook replaces it)
    virtual uint8_t onMemoryRead(uint16_t pc, uint16_t addr, uint8_t value) = 0;
    virtual void onMemoryWrite(uint16_t pc, uint16_t addr, uint8_t value) = 0;
    virtual uint8_t onPortIn(uint16_t pc, uint16_t port, uint8_t value) = 0;
    virtual void onPortOut(uint16_t pc, uint16_t port, uint8_t value) = 0;

    /// A card event; reported at the next boundary like a bus hit
    virtual void onEvent(GSCardEvent kind, uint16_t port, uint8_t value) = 0;

    /// A bus master other than the card CPU (a NeoGS DMA module: 0 ZX-DMA, 1 SD) wrote a RAM page of the card:
    /// `ramPage` the 16 KB page, `offset` in it. Only a watchpoint tied to that page sees it (the CPU's address is
    /// not involved); reported at the next boundary like a bus hit
    virtual void onBusMasterWrite(uint8_t master, uint8_t ramPage, uint16_t offset, uint8_t value)
    {
        (void)master;
        (void)ramPage;
        (void)offset;
        (void)value;
    }

protected:
    ~IGSDebugHooks() = default;
};
