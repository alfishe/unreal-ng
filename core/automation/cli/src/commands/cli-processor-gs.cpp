// General Sound card state - 'state audio gs' command (GS design §11.4).
// Same chip getters the WebAPI /state/audio/gs endpoint serves.

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/sound/chips/gs/soundchip_gs.h>
#include <emulator/sound/soundmanager.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>

#include "cli-processor.h"

namespace
{
const char* GsTraceSideName(GSTraceSide side)
{
    switch (side)
    {
        case GSTraceSide::Host: return "HOST";
        case GSTraceSide::GsInternal: return "GS";
        case GSTraceSide::DacFetch: return "DAC";
        case GSTraceSide::Interrupt: return "INT";
    }
    return "?";
}
}  // namespace

void CLIProcessor::HandleStateAudioGS(const ClientSession& session, EmulatorContext* context, const std::string& optionArg)
{
    std::stringstream ss;
    ss << "General Sound Device State" << NEWLINE;
    ss << "==========================" << NEWLINE;
    ss << NEWLINE;

    SoundManager* soundManager = context->pSoundManager;
    GeneralSoundCard* gs = soundManager ? soundManager->getGeneralSound() : nullptr;

    if (!gs)
    {
        ss << "Status: Not fitted" << NEWLINE;
        ss << NEWLINE;
        ss << "The General Sound card is disabled on this machine." << NEWLINE;
        ss << "Enable it with [SOUND] GSType=Z80 (plus [ROM] GSROM pointing at" << NEWLINE;
        ss << "the 32 KB firmware) and restart the emulator." << NEWLINE;

        session.SendResponse(ss.str());
        return;
    }

    const uint8_t status = gs->getStatusRaw();
    const bool verbose = optionArg == "--verbose" || optionArg == "verbose" || optionArg == "-v";

    ss << "Device: " << (gs->hasCoprocessor() ? "General Sound (Z80 coprocessor @ 12 MHz, 4 x 8-bit DAC)"
                                               : "General Sound (lightweight mod player, 4 x 8-bit DAC)") << NEWLINE;
    ss << "ROM:    " << (gs->isROMLoaded() ? "Loaded (32 KB)" : "Missing (zero-filled)") << NEWLINE;
    ss << "RAM:    " << gs->getRamSizeKB() << " KB" << NEWLINE;
    ss << "Page:   " << (int)gs->getMPAG() << " (MPAG banking latch)" << NEWLINE;
    ss << NEWLINE;

    ss << "Mailbox (host ports #B3/#BB):" << NEWLINE;
    ss << "  Status:          0x" << std::hex << std::setw(2) << std::setfill('0') << (int)status << NEWLINE;
    ss << "  Command Pending: " << ((status & 0x01) ? "Yes" : "No") << " (bit0)" << NEWLINE;
    ss << "  Data Pending:    " << ((status & 0x80) ? "Yes" : "No") << " (bit7)" << NEWLINE;
    ss << "  Command queue:   " << gs->getCommandQueueCount() << "/16 pending (FIFO depth)" << NEWLINE;
    ss << "  Data queue:      " << gs->getDataQueueCount() << "/16 pending (FIFO depth)" << NEWLINE;
    ss << "  Command from ZX: 0x" << std::hex << std::setw(2) << (int)gs->getCommandFromHost() << NEWLINE;
    ss << "  Data from ZX:    0x" << std::hex << std::setw(2) << (int)gs->getDataFromHost() << NEWLINE;
    ss << "  Data to ZX:      0x" << std::hex << std::setw(2) << (int)gs->getDataToHost() << NEWLINE;
    ss << std::dec;
    ss << NEWLINE;

    ss << "DAC Channels:" << NEWLINE;
    for (int i = 0; i < 4; i++)
    {
        ss << "  Channel " << (i + 1) << ": Sample 0x" << std::hex << std::setw(2) << (int)gs->getChannelSample(i)
           << std::dec << "  Volume " << (int)gs->getChannelVolume(i) << "/63" << NEWLINE;
    }
    ss << NEWLINE;

    if (!gs->hasCoprocessor())
    {
        ss << "Coprocessor: none (lightweight personality - no registers)" << NEWLINE;
        ss << NEWLINE;
    }
    else if (verbose)
    {
        ss << "Coprocessor (Z80ex):" << NEWLINE;
        ss << "  PC: 0x" << std::hex << std::setw(4) << gs->getCPUReg(regPC) << NEWLINE;
        ss << "  SP: 0x" << std::hex << std::setw(4) << gs->getCPUReg(regSP) << NEWLINE;
        ss << "  AF: 0x" << std::hex << std::setw(4) << gs->getCPUReg(regAF) << NEWLINE;
        ss << std::dec;
        ss << "  Halted: " << (gs->isCPUHalted() ? "Yes" : "No") << NEWLINE;
        ss << NEWLINE;
    }
    else
    {
        ss << "Use 'state audio gs --verbose' for coprocessor registers" << NEWLINE;
    }

    session.SendResponse(ss.str());
}

/// 'gsporttrace' command family (alias 'gs-porttrace') - live triage for the
/// GS coprocessor: always-on activity counters + an opt-in structured event
/// trace (host ports, GS-side ports, DAC fetches, interrupts). Same data
/// model exposed by WebAPI/MCP/Lua/Python (see gsporttrace.h).
void CLIProcessor::HandleGSPortTrace(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    SoundManager* soundManager = context ? context->pSoundManager : nullptr;
    GeneralSoundCard* gs = soundManager ? soundManager->getGeneralSound() : nullptr;
    if (!gs)
    {
        session.SendResponse(std::string("Error: General Sound card is not fitted (set [SOUND] GSType=Z80).") + NEWLINE);
        return;
    }

    std::string sub = args.empty() ? "status" : args[0];
    std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

    std::stringstream ss;

    if (sub == "start")
    {
        gs->startPortTrace();
        ss << "GS port trace: capturing (buffer cleared)." << NEWLINE;
    }
    else if (sub == "stop")
    {
        gs->stopPortTrace();
        ss << "GS port trace: stopped." << NEWLINE;
    }
    else if (sub == "pause")
    {
        gs->pausePortTrace();
        ss << "GS port trace: paused." << NEWLINE;
    }
    else if (sub == "resume")
    {
        gs->resumePortTrace();
        ss << "GS port trace: resumed." << NEWLINE;
    }
    else if (sub == "clear")
    {
        gs->clearPortTrace();
        ss << "GS port trace: buffer cleared." << NEWLINE;
    }
    else if (sub == "counters" || sub == "status")
    {
        const GSActivityCounters& c = gs->getActivityCounters();
        ss << "General Sound Activity Counters" << NEWLINE;
        ss << "================================" << NEWLINE;
        ss << "  CPU steps:            " << c.cpuSteps << NEWLINE;
        ss << "  Periodic INTs taken:  " << c.interruptsAccepted << NEWLINE;
        ss << "  INT periods (320c):   " << c.interruptPeriods << NEWLINE;
        ss << "  INTs coalesced:       " << c.interruptsCoalesced << NEWLINE;
        ss << "  NMIs taken:           " << c.nmisAccepted << NEWLINE;
        ss << "  DAC fetches:          " << c.dacFetches << NEWLINE;
        ss << "  Volume latch writes:  " << c.volumeLatchWrites << NEWLINE;
        ss << "  Host commands (OUT #BB): " << c.hostCommandsReceived << NEWLINE;
        ss << "  Commands dropped (FIFO full): " << c.hostCommandsDropped << NEWLINE;
        ss << "  Host data written (OUT #B3): " << c.hostDataWritten << NEWLINE;
        ss << "  Data dropped (FIFO full):    " << c.hostDataDropped << NEWLINE;
        ss << "  Host data read (IN #B3):     " << c.hostDataRead << NEWLINE;
        if (c.lastDacFetchGsCycle >= 0)
            ss << "  Last DAC fetch: GS cycle " << c.lastDacFetchGsCycle << ", frame " << c.lastDacFetchFrame << NEWLINE;
        else
            ss << "  Last DAC fetch: never" << NEWLINE;
        ss << NEWLINE;
        ss << "Port trace: " << (gs->isPortTraceCapturing() ? "CAPTURING" : (gs->isPortTraceArmed() ? "PAUSED" : "stopped"))
           << ", " << gs->getPortTraceEventCount() << " events buffered"
           << " (produced " << gs->getPortTraceTotalProduced() << ", evicted " << gs->getPortTraceTotalEvicted() << ")"
           << NEWLINE;
        if (gs->hasCoprocessor())
            ss << "PC=0x" << std::hex << std::setw(4) << std::setfill('0') << gs->getCPUReg(regPC)
               << " halted=" << std::dec << (gs->isCPUHalted() ? "yes" : "no") << NEWLINE;
        else
            ss << "PC=- (lightweight personality, no coprocessor)" << NEWLINE;
        ss << NEWLINE << "Use 'gsporttrace start' to begin capturing, 'gsporttrace events [n]' to inspect." << NEWLINE;
    }
    else if (sub == "events")
    {
        size_t count = 50;
        if (args.size() > 1)
        {
            try { count = static_cast<size_t>(std::stoul(args[1])); } catch (...) {}
        }
        auto events = gs->getPortTraceLast(count);
        ss << "GS port trace: last " << events.size() << " event(s) (of " << gs->getPortTraceEventCount() << " buffered)" << NEWLINE;
        ss << std::hex << std::setfill('0');
        for (const auto& e : events)
        {
            ss << "[" << std::dec << e.timestamp << std::hex << "] frame=" << std::dec << e.frameNumber << std::hex
               << " " << GsTraceSideName(e.side) << " " << (e.isOut() ? "OUT" : "IN ")
               << " port=0x" << std::setw(4) << e.port << " val=0x" << std::setw(2) << (int)e.value
               << " pc=0x" << std::setw(4) << e.pc;
            if (e.side == GSTraceSide::DacFetch)
                ss << " ch=" << std::dec << (int)e.channel;
            if (e.side == GSTraceSide::Interrupt && e.isNmi())
                ss << " (NMI)";
            ss << std::dec << NEWLINE;
        }
    }
    else
    {
        ss << "Usage: gsporttrace <start|stop|pause|resume|clear|status|counters|events [n]>" << NEWLINE;
    }

    session.SendResponse(ss.str());
}

namespace
{
std::optional<uint8_t> parseByteArg(const std::string& text)
{
    try
    {
        size_t pos = 0;
        unsigned long value = std::stoul(text, &pos, 0); // base 0: accepts 0x.. / decimal
        if (pos != text.size() || value > 0xFF)
            return std::nullopt;
        return static_cast<uint8_t>(value);
    }
    catch (...)
    {
        return std::nullopt;
    }
}
} // namespace

/// 'gs' command: live control of the General Sound card, the same actions
/// the WebAPI /control/audio/gs endpoint and the MCP gs_* tool actions
/// serve (GS card personalities design §11.3) - previously CLI-only had
/// read access (state audio gs / gsporttrace); this closes that gap so all
/// automation surfaces (WebAPI/MCP/Lua/Python/CLI) offer the same actions.
///   gs reset                          - full power-on reset
///   gs reset_card                     - #33 bit7 pulse (mailbox survives)
///   gs nmi                            - #33 bit6 pulse
///   gs send_command <byte>            - OUT #BB (0-255, decimal or 0x..)
///   gs send_data <byte>               - OUT #B3
///   gs read_status                    - IN #BB
///   gs read_data                      - IN #B3
///   gs switch_personality <z80|lle|lw|lightweight> - runtime card swap
///   gs dump_module [path]             - write the last COM30..D2 upload
void CLIProcessor::HandleGS(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    SoundManager* soundManager = context ? context->pSoundManager : nullptr;
    GeneralSoundCard* gs = soundManager ? soundManager->getGeneralSound() : nullptr;
    if (!gs)
    {
        session.SendResponse(std::string("Error: General Sound card is not fitted (set [SOUND] GSType=Z80 or LW).") + NEWLINE);
        return;
    }

    static const char* kUsage =
        "Usage: gs <reset|reset_card|nmi|send_command <byte>|send_data <byte>|"
        "read_status|read_data|switch_personality <z80|lle|lw|lightweight>|dump_module [path]>";

    if (args.empty())
    {
        session.SendResponse(std::string(kUsage) + NEWLINE);
        return;
    }

    std::string sub = args[0];
    std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

    std::stringstream ss;

    if (sub == "reset")
    {
        gs->reset();
        ss << "GS: full reset done." << NEWLINE;
    }
    else if (sub == "reset_card")
    {
        gs->resetCard();
        ss << "GS: card reset done (mailbox survives)." << NEWLINE;
    }
    else if (sub == "nmi")
    {
        gs->triggerNMI();
        ss << "GS: NMI pulsed." << NEWLINE;
    }
    else if (sub == "send_command" || sub == "send_data")
    {
        if (args.size() < 2)
        {
            ss << "Error: '" << sub << "' requires a byte value (0-255)." << NEWLINE;
        }
        else if (auto byte = parseByteArg(args[1]))
        {
            if (sub == "send_command")
                gs->sendCommand(*byte);
            else
                gs->sendData(*byte);
            ss << "GS: " << sub << "(0x" << std::hex << std::setw(2) << std::setfill('0') << (int)*byte << std::dec
               << ") done." << NEWLINE;
        }
        else
        {
            ss << "Error: invalid byte value '" << args[1] << "' (expected 0-255)." << NEWLINE;
        }
    }
    else if (sub == "read_status")
    {
        const uint8_t value = gs->readStatus();
        ss << "GS status: 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)value << std::dec << NEWLINE;
    }
    else if (sub == "read_data")
    {
        const uint8_t value = gs->readData();
        ss << "GS data: 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)value << std::dec << NEWLINE;
    }
    else if (sub == "switch_personality")
    {
        if (args.size() < 2)
        {
            ss << "Error: 'switch_personality' requires z80, lle, lw or lightweight." << NEWLINE;
        }
        else
        {
            std::string target = args[1];
            std::transform(target.begin(), target.end(), target.begin(), ::tolower);

            GSTypeKind kind;
            if (target == "z80" || target == "lle")
                kind = GSTypeKind::Z80;
            else if (target == "lw" || target == "lightweight")
                kind = GSTypeKind::LW;
            else
            {
                ss << "Error: unknown personality '" << args[1] << "' (expected z80, lle, lw or lightweight)." << NEWLINE;
                session.SendResponse(ss.str());
                return;
            }

            if (soundManager->requestGeneralSoundCardSwitch(kind))
                ss << "GS: personality switch to '" << target << "' requested (applied at the next frame boundary)." << NEWLINE;
            else
                ss << "Error: personality switch request failed." << NEWLINE;
        }
    }
    else if (sub == "dump_module")
    {
        std::vector<uint8_t> bytes;
        bool playing = false;
        if (!gs->captureModuleUpload(bytes, playing))
        {
            ss << "Error: no completed module upload to dump (no COM30..D2 stream captured yet)." << NEWLINE;
        }
        else
        {
            const std::string path = args.size() > 1 ? args[1] : "gs-module-dump.mod";
            std::ofstream out(path, std::ios::binary);
            if (!out)
            {
                ss << "Error: cannot open '" << path << "' for writing." << NEWLINE;
            }
            else
            {
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                ss << "GS: module dumped (" << bytes.size() << " bytes, playing=" << (playing ? "yes" : "no")
                   << ") -> " << path << NEWLINE;
            }
        }
    }
    else
    {
        ss << kUsage << NEWLINE;
    }

    session.SendResponse(ss.str());
}
