// General Sound card state - 'state audio gs' command (GS design §11.4).
// Same chip getters the WebAPI /state/audio/gs endpoint serves.

#include <emulator/emulator.h>
#include <debugger/ttd/timetravelmanager.h>
#include <emulator/emulatorcontext.h>
#include <emulator/sound/chips/gs/soundchip_gs.h>
#include <emulator/sound/chips/neogs/neogsmedia.h>
#include <emulator/sound/soundmanager.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>

#include "cli-processor.h"
#include <emulator/state/devicestate.h>

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
        case GSTraceSide::ZxDma: return "ZXDMA";
    }
    return "?";
}

// GS host-port stimuli step the card's Z80, so they go through the live-input
// path: applied on the machine's thread at an instruction boundary and
// journaled for TTD replay. False while TTD replay owns input
bool SubmitGSInput(EmulatorContext* context, ttd::TTDInputKind kind, uint8_t value = 0)
{
    if (!context->pTimeTravelManager)
        return false;
    ttd::TTDInputEvent ev;
    ev.kind = kind;
    ev.value = value;
    return context->pTimeTravelManager->SubmitLiveInput(ev);
}

const char* const kGSInputRefused = "Error: GS input refused - TTD replay owns input.";
}  // namespace

void CLIProcessor::HandleStateAudioGS(const ClientSession& session, EmulatorContext* context, const std::string& optionArg)
{
    (void)optionArg;  // "verbose" used to add the CPU; the shared report always carries it
    std::stringstream ss;
    ss << "General Sound Device State" << NEWLINE;
    ss << "==========================" << NEWLINE;
    ss << NEWLINE;
    // One report for every interface (DeviceState::Gs)
    ss << DeviceState::ToText(DeviceState::Gs(context));
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
        session.SendResponse(std::string("Error: General Sound card is not fitted (plug one: slots plug zxbus.next gs | gs-lw | neogs).") + NEWLINE);
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
            ss << "PC=0x" << std::hex << std::setw(4) << std::setfill('0') << gs->getCPUReg(GSCpuRegister::PC)
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
///   gs read_status                    - IN #BB value, peeked (no side effects)
///   gs read_data                      - IN #B3 value, peeked (bit 7 not cleared)
/// Writes, resets and NMI are live input: applied on the machine's thread at
/// the next instruction boundary (while paused: when execution continues)
///   gs switch_personality <z80|lle|lw|lightweight|ngs|neogs> - the card in the GS slot replaced (slots gs: a restart)
///   gs dump_module [path]             - write the last COM30..D2 upload
///   gs sd_insert <image> / sd_eject   - NeoGS SD card slot
///   gs flash_save                     - NeoGS: save the reprogrammed flash
///   gs stereo_mode <separated|gs|mono> - NeoGS: DAC channels as on the board,
///                                       with the classic GS's 50% cross-feed, or mono
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
        session.SendResponse(std::string("Error: General Sound card is not fitted (plug one: slots plug zxbus.next gs | gs-lw | neogs).") + NEWLINE);
        return;
    }

    static const char* kUsage =
        "Usage: gs <reset|reset_card|nmi|send_command <byte>|send_data <byte>|"
        "read_status|read_data|switch_personality <z80|lle|lw|lightweight|ngs|neogs>|dump_module [path]|"
        "sd_insert <image>|sd_eject|flash_save|stereo_mode <separated|gs|mono>>";

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
        if (SubmitGSInput(context, ttd::TTDInputKind::GSReset))
            ss << "GS: full reset submitted." << NEWLINE;
        else
            ss << kGSInputRefused << NEWLINE;
    }
    else if (sub == "reset_card")
    {
        if (SubmitGSInput(context, ttd::TTDInputKind::GSResetCard))
            ss << "GS: card reset submitted (mailbox survives)." << NEWLINE;
        else
            ss << kGSInputRefused << NEWLINE;
    }
    else if (sub == "nmi")
    {
        if (SubmitGSInput(context, ttd::TTDInputKind::GSNmi))
            ss << "GS: NMI submitted." << NEWLINE;
        else
            ss << kGSInputRefused << NEWLINE;
    }
    else if (sub == "send_command" || sub == "send_data")
    {
        if (args.size() < 2)
        {
            ss << "Error: '" << sub << "' requires a byte value (0-255)." << NEWLINE;
        }
        else if (auto byte = parseByteArg(args[1]))
        {
            const auto kind = sub == "send_command" ? ttd::TTDInputKind::GSCommand : ttd::TTDInputKind::GSData;
            if (SubmitGSInput(context, kind, *byte))
                ss << "GS: " << sub << "(0x" << std::hex << std::setw(2) << std::setfill('0') << (int)*byte
                   << std::dec << ") submitted." << NEWLINE;
            else
                ss << kGSInputRefused << NEWLINE;
        }
        else
        {
            ss << "Error: invalid byte value '" << args[1] << "' (expected 0-255)." << NEWLINE;
        }
    }
    else if (sub == "read_status")
    {
        // Peek: no host read cycle, the card is not stepped
        const uint8_t value = gs->getStatusRaw() | 0x7E;
        ss << "GS status: 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)value << std::dec << NEWLINE;
    }
    else if (sub == "read_data")
    {
        // Peek: the status data bit is left set, the card is not stepped
        const uint8_t value = gs->getDataToHost();
        ss << "GS data: 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)value << std::dec << NEWLINE;
    }
    else if (sub == "switch_personality")
    {
        if (args.size() < 2)
        {
            ss << "Error: 'switch_personality' requires " << GS_PERSONALITY_NAMES << "." << NEWLINE;
        }
        else
        {
            // The card in the GS slot, replaced as a slot change applied by a restart (owner decision Q10): the
            // same as `slots gs <card>`. Extra flags: --replace, --dry-run, --media save|discard
            std::vector<std::string> slotArgs = {"gs", args[1]};
            slotArgs.insert(slotArgs.end(), args.begin() + 2, args.end());
            gs = nullptr;
            soundManager = nullptr;
            context = nullptr;
            emulator.reset();
            HandleSlots(session, slotArgs);
            return;
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
    else if (sub == "sd_insert" || sub == "sd_eject" || sub == "flash_save")
    {
        // Checked here, carried out on the machine's thread (neogsmedia.h);
        // insert / eject are refused while a TTD recording runs
        const NeoGSMediaResult result = sub == "sd_insert" ? NeoGSRequestSdInsert(context, args.size() > 1 ? args[1] : std::string())
                                        : sub == "sd_eject" ? NeoGSRequestSdEject(context)
                                                            : NeoGSRequestFlashSave(context);
        if (NeoGSMediaAccepted(result))
            ss << "NeoGS: " << sub << " " << NeoGSMediaResultText(result) << "." << NEWLINE;
        else
            ss << "Error: " << sub << ": " << NeoGSMediaResultText(result) << "." << NEWLINE;
    }
    else if (sub == "stereo_mode")
    {
        NeoGSConfig::StereoMode mode = NeoGSConfig::StereoMode::Separated;
        if (args.size() < 2)
            ss << "NeoGS stereo mode: " << neogsStereoModeName(soundManager->neoGSStereoMode()) << NEWLINE;
        else if (!neogsParseStereoMode(args[1], mode))
            ss << "Error: stereo_mode: expected separated, gs or mono (got '" << args[1] << "')." << NEWLINE;
        else
        {
            soundManager->setNeoGSStereoMode(mode);
            ss << "NeoGS stereo mode: " << neogsStereoModeName(mode) << " (applied at the next frame)." << NEWLINE;
        }
    }
    else
    {
        ss << kUsage << NEWLINE;
    }

    session.SendResponse(ss.str());
}
