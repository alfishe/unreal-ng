#include "emulator/io/sprinter/isa/cards/isazxbusadapter.h"

#include <cstdio>

#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"

namespace sprinterisa
{
namespace
{
std::string Hex(unsigned value, int digits)
{
    char text[16];
    std::snprintf(text, sizeof(text), "#%0*X", digits, value);
    return text;
}
}  // namespace

IsaZxBusAdapter::IsaZxBusAdapter(EmulatorContext* context, PortDecoder* decoder, bool carriesGs, std::string emptyWhy)
    : _context(context), _decoder(decoder), _carriesGs(carriesGs), _emptyWhy(std::move(emptyWhy))
{
}

uint16_t IsaZxBusAdapter::GsPort(uint16_t port)
{
    switch (port & 0x00FF)
    {
        case 0x33:
            return GeneralSoundCard::PORT_CONTROL;
        case 0xB3:
            return GeneralSoundCard::PORT_DATA;
        case 0xBB:
            return GeneralSoundCard::PORT_COMMAND;
        default:
            return 0;
    }
}

GeneralSoundCard* IsaZxBusAdapter::Gs() const
{
    if (!_carriesGs || !_context || !_context->pSoundManager)
        return nullptr;
    return _context->pSoundManager->getGeneralSound();
}

bool IsaZxBusAdapter::IoRead(const IsaCycle& cycle, uint8_t& value)
{
    // The adapter turns an I/O cycle into a ZX-bus IN only while AEN is low (an ISA I/O decoder qualifies with AEN)
    if (cycle.aen || !_decoder)
        return false;
    const uint16_t port = GsPort(static_cast<uint16_t>(cycle.address));
    // #33 is write-only on the GS (its read is not decoded): nothing drives the data bus, the pull-ups read #FF
    if (port == 0 || port == GeneralSoundCard::PORT_CONTROL || !Gs())
        return false;
    value = _decoder->PeripheralPortIn(port);
    return true;
}

bool IsaZxBusAdapter::IoWrite(const IsaCycle& cycle, uint8_t value)
{
    if (cycle.aen || !_decoder)
        return false;
    const uint16_t port = GsPort(static_cast<uint16_t>(cycle.address));
    if (port == 0 || !Gs())
        return false;
    _decoder->PeripheralPortOut(port, value);
    return true;
}

bool IsaZxBusAdapter::IoPeek(uint32_t address, uint8_t& value) const
{
    // What the host would read now, from the card's mailbox, without the read's side effects (an IN from #B3 clears
    // the data flag; a peek must not) and without running the card forward
    const GeneralSoundCard* gs = Gs();
    const uint16_t port = GsPort(static_cast<uint16_t>(address));
    if (!gs || port == 0 || port == GeneralSoundCard::PORT_CONTROL)
        return false;
    value = port == GeneralSoundCard::PORT_COMMAND ? static_cast<uint8_t>(gs->getStatusRaw() | 0x7E) : gs->getDataToHost();
    return true;
}

void IsaZxBusAdapter::SetReset(bool asserted)
{
    if (asserted == _resetHeld)
        return;
    _resetHeld = asserted;
    if (asserted)
        ++_resetPulses;
    // RESET DRV -> ZX-bus /RESET -> the GS card's reset (owner decision Q3: the #33 bit-7 path, CPU and banking reset,
    // the mailbox kept). Applied at both edges: the card is at its reset state when RESET DRV goes high and starts
    // from it again when RESET DRV goes low (between the two, the bus passes no cycle to the card). Through the port
    // map, so each personality applies its own reset at the moment of the access (catch-up first)
    if (Gs() && _decoder)
        _decoder->PeripheralPortOut(GeneralSoundCard::PORT_CONTROL, 0x80);
}

bool IsaZxBusAdapter::IoRange(uint32_t& first, uint32_t& last) const
{
    if (!Gs())
        return false;
    first = 0x033;
    last = 0x0BB;
    return true;
}

std::string IsaZxBusAdapter::DecodeNote() const
{
    return "the adapter passes every ISA I/O cycle as a Spectrum IN / OUT; the GS decodes A7-A0: #33 control, #B3 data, "
           "#BB command / status, mirrored every #100 of the window and at any #9FBD A19-A14";
}

const char* IsaZxBusAdapter::RegisterName(bool io, uint32_t address, bool write) const
{
    if (!io || !Gs())
        return "";
    switch (GsPort(static_cast<uint16_t>(address)))
    {
        case GeneralSoundCard::PORT_DATA:
            return write ? "GS data (#B3)" : "GS data to host (#B3)";
        case GeneralSoundCard::PORT_COMMAND:
            return write ? "GS command (#BB)" : "GS status (#BB)";
        case GeneralSoundCard::PORT_CONTROL:
            return write ? "GS control (#33)" : "";
        default:
            return "";
    }
}

std::string IsaZxBusAdapter::SummaryNote() const
{
    const GeneralSoundCard* gs = Gs();
    if (!gs)
        return " (ZX-bus empty: " + (_emptyWhy.empty() ? std::string("[SOUND] GSType=NONE") : _emptyWhy) + ")";
    const bool neo = gs->implementation() == GSCardImplementation::NGS;
    return std::string(" -> ") + (neo ? "NeoGS" : gs->implementation() == GSCardImplementation::LW ? "GS (lightweight)" : "GS") +
           " on the ZX-bus: #B3 / #BB / #33, RESET from ISA RESET DRV" + (_resetHeld ? " (held now)" : "");
}

void IsaZxBusAdapter::Describe(StateNode& out) const
{
    StateNode zx = StateNode::Object();
    zx["adapter"] = "ZX-bus adapter: an ISA I/O cycle at address A becomes a Spectrum IN / OUT at port A15-A0 (AEN = 0); "
                    "memory cycles, IRQ and WAIT are not passed";
    zx["reset"] = "ISA RESET DRV (#9FBD bit 7) drives the ZX-bus /RESET: the card resets (the GS #33 bit-7 reset: CPU and "
                  "banking, the host mailbox kept) and is held while the bit is set";
    zx["reset_held"] = _resetHeld;
    zx["reset_pulses"] = _resetPulses;
    zx["memory_cycles"] = "not passed (no /MREQ on the ZX-bus side): a NeoGS's ZX-DMA cannot reach the Sprinter's memory";
    StateNode cards = StateNode::Array();
    const GeneralSoundCard* gs = Gs();
    if (gs)
    {
        StateNode c = StateNode::Object();
        c["card"] = "gs";
        c["personality"] = gsImplementationShortName(gs->implementation());
        c["device"] = gs->deviceDescription();
        c["firmware"] = gs->firmwareDescription();
        c["ports"] = "#B3 data, #BB command (write) / status (read), #33 control (write); A7-A0 decoded";
        c["cpu_addresses"] = "#1FFD bit 4 set, window 3 page #D4 (slot 1) or #D6 (slot 2), #9FBD AEN = 0: CPU #C0B3 data, "
                             "#C0BB command / status, #C033 control (every #100 of #C000-#FFFF mirrors them)";
        c["status"] = Hex(static_cast<unsigned>(gs->getStatusRaw() | 0x7E), 2);
        c["data_flag"] = (gs->getStatusRaw() & 0x80) != 0;
        c["command_flag"] = (gs->getStatusRaw() & 0x01) != 0;
        c["data_to_host"] = Hex(gs->getDataToHost(), 2);
        c["ready_for_commands"] = gs->isReadyForCommands();
        c["sound"] = gs->hadAudioActivityLastFrame() ? "playing (the mix changed in the last frame)" : "silent";
        if (gs->implementation() == GSCardImplementation::NGS)
            c["zx_dma"] = "unavailable: the adapter passes no memory cycles (the module never sees a host access)";
        const bool gsReset = _context && _context->config.sound.gsreset != 0;
        c["machine_reset"] = gsReset ? "[SOUND] GSReset=1: a machine reset reinitializes the card"
                                     : "[SOUND] GSReset=0: a machine reset itself does not reach the card (the #9FBD latch has "
                                       "no reset input); the BIOS's own RESET DRV pulse at POST does";
        c["details"] = "the GS report: WebAPI GET /state/audio/gs, MCP inspect_state audio_gs, CLI state gs";
        cards.push(c);
    }
    else
        zx["empty"] = _emptyWhy.empty() ? std::string("[SOUND] GSType=NONE: no card on the ZX-bus, reads #FF") : _emptyWhy;
    zx["cards"] = cards;
    out["zx_bus"] = zx;
}

}  // namespace sprinterisa
