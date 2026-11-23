#include "emulator/state/devicecatalog.h"

#include "debugger/history/historycontrol.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/ide/cdaudiocontrol.h"
#include "emulator/io/z80n/nextdivmmcjournal.h"
#include "emulator/io/z80n/nextreportquery.h"
#include "emulator/io/z80n/nextsdjournal.h"
#include "emulator/slots/card.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareobjects.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/sound/chips/gs/gsslotstats.h"
#include "emulator/sound/chips/gs/gszxdmatrace.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"

#include <algorithm>

namespace DeviceState
{
namespace
{
StateNode BuildScreen(EmulatorContext* context)
{
    return Screen(context, false);
}

StateNode BuildGs(EmulatorContext* context)
{
    return Gs(context);
}

StateNode NotAvailable(const std::string& why)
{
    StateNode n = StateNode::Object();
    n["available"] = false;
    n["description"] = why;
    return n;
}

/// A reply body of a GS read (firmware objects, statistics) as a catalog report: `available` with it, else its error
StateNode GsBody(int status, const StateNode& body)
{
    if (status != 200)
    {
        const StateNode* error = body.find("error");
        return NotAvailable(error && error->kind == StateNode::Kind::String ? error->s : std::string("not available"));
    }
    StateNode n = body;
    n["available"] = true;
    return n;
}

/// The newest entries of the GS command log a debugger pane shows (the whole log: the history verbs)
constexpr size_t kGsCommandRows = 500;

/// The newest entries of a command log history (`<device>`), as a catalog report
StateNode CommandLogReport(EmulatorContext* context, const std::string& device)
{
    HistoryRequest request;
    request.verb = "read";
    request.device = device;
    request.last = kGsCommandRows;
    const HistoryReply reply = HistoryControl::Execute(context, request);
    if (reply.httpStatus != 200)
        return NotAvailable("no command log on this card");
    StateNode n = reply.body;
    n["available"] = true;
    return n;
}

/// The GS slot card's command log (gs-debugger design §6.3): its history `<slot>.commands`, the newest entries
StateNode GsCommands(EmulatorContext* context)
{
    if (!context || !context->pSoundManager || !context->pSoundManager->getGeneralSound())
        return NotAvailable("General Sound card not fitted");
    return CommandLogReport(context, GSHistoryDevice(context) + ".commands");
}

/// The slot of the card that carries a General Sound (the ZX-MultiSound: the debugger's `ms` CPU); "" when none
std::string MsGsSlot(EmulatorContext* context)
{
    if (!context || !context->pSlotManager)
        return std::string();
    for (const std::unique_ptr<ICard>& card : context->pSlotManager->Cards())
        if (card->DebugGeneralSound())
            return card->SlotId();
    return std::string();
}

constexpr const char* kMsGsNotFitted = "No ZX-MultiSound with a General Sound fitted (slots plug zxbus.next multisound)";

/// The ZX-MultiSound GS's command log: its history `<slot>.gs.commands`
StateNode MsGsCommands(EmulatorContext* context)
{
    const std::string slot = MsGsSlot(context);
    if (slot.empty())
        return NotAvailable(kMsGsNotFitted);
    return CommandLogReport(context, slot + ".gs.commands");
}

void GsCommandsCommands(const StateNode& report, StateNode& entry)
{
    const StateNode* device = report.find("device");
    const std::string id = device && device->kind == StateNode::Kind::String ? device->s : std::string("gs.commands");
    const std::string last = std::to_string(kGsCommandRows);
    entry["route"] = "history/" + id + "?last=" + last;
    entry["cli"] = "history " + id + " " + last;
    entry["script"] = "history";
    entry["mcp"] = "device_history";
}

std::string GsCommandsSummary(const StateNode& report)
{
    const StateNode* mode = report.find("mode");
    const StateNode* kept = report.find("kept");
    if (!mode || mode->kind != StateNode::Kind::String)
        return std::string();
    if (mode->s == "off")
        return "off";
    return mode->s + ", " + std::to_string(kept ? kept->i : 0) + " entries";
}

/// The firmware's objects (gsfirmwareobjects.h): instances, variables, the dispatch table, the unknown commands; on the
/// card the debugger CPU `cpu` names
StateNode GsFirmwareOf(EmulatorContext* context, const std::string& cpu)
{
    GSFirmwareObjectsQuery query;
    query.cpu = cpu;
    const GSFirmwareObjectsReply reply = GSFirmwareObjectsRead(context, query);
    return GsBody(reply.status, reply.body);
}

StateNode GsFirmware(EmulatorContext* context)
{
    return GsFirmwareOf(context, std::string());
}

StateNode MsGsFirmware(EmulatorContext* context)
{
    if (MsGsSlot(context).empty())
        return NotAvailable(kMsGsNotFitted);
    return GsFirmwareOf(context, "ms");
}

/// The card's statistics block (gsslotstats.h); while the machine runs and no frame has ended since, `pending`
StateNode GsStatsOf(EmulatorContext* context, const std::string& cpu)
{
    GSStatsQuery query;
    query.cpu = cpu;
    const GSStatsReply reply = GSStatsRead(context, query);
    if (reply.pending)
    {
        StateNode n = StateNode::Object();
        n["available"] = true;
        n["pending"] = true;
        n["description"] = "the statistics come with the next frame end";
        return n;
    }
    return GsBody(reply.status, reply.body);
}

StateNode GsStats(EmulatorContext* context)
{
    return GsStatsOf(context, std::string());
}

StateNode MsGsStats(EmulatorContext* context)
{
    if (MsGsSlot(context).empty())
        return NotAvailable(kMsGsNotFitted);
    return GsStatsOf(context, "ms");
}

/// The NeoGS ZX-DMA trace (gszxdmatrace.h), its default rows; unavailable without a NeoGS
StateNode GsZxDma(EmulatorContext* context)
{
    return GsZxDmaTrace(context);
}

std::string GsZxDmaSummary(const StateNode& report)
{
    const StateNode* capturing = report.find("capturing");
    const StateNode* kept = report.find("total_bytes_kept");
    return std::string(capturing && capturing->b ? "capturing" : "not capturing") + ", " +
           std::to_string(kept ? kept->i : 0) + " bytes kept";
}

StateNode BuildCdAudio(EmulatorContext* context)
{
    return CdAudioControl::State(context);
}

StateNode BuildNextCopper(EmulatorContext* context)
{
    return NextCopper(context, NextCopperQuery());  // the default window: the first 64 words, the words around the PC
}
StateNode BuildNextSprites(EmulatorContext* context)
{
    return NextSprites(context, NextSpritesQuery());  // the visible sprites
}
StateNode BuildNextSd(EmulatorContext* context)
{
    NextSdJournalQuery query;
    query.limit = 1;  // the cards and the SPI engine; the journal is the report's own business
    return NextSdReport(context, query);
}

StateNode BuildNextDivMmc(EmulatorContext* context)
{
    NextDivMmcJournalQuery query;
    query.limit = 1;
    return NextDivMmcReport(context, query);
}

std::string Text(const StateNode& report, const char* key)
{
    const StateNode* v = report.find(key);
    return v != nullptr && v->kind == StateNode::Kind::String ? v->s : std::string();
}

bool Fitted(const StateNode& report, const char* key)
{
    const StateNode* part = report.find(key);
    const StateNode* fitted = part ? part->find("fitted") : nullptr;
    return fitted != nullptr && fitted->b;
}

bool HasAyChip(const StateNode& report)
{
    const StateNode* chips = report.find("available_chips");
    return chips != nullptr && chips->i > 0;
}

/// The network report is there on every machine: a device is a card, ZiFi, the ATM2IOESP or a serial port
std::vector<std::string> NetworkDevices(const StateNode& report)
{
    std::vector<std::string> devices;
    const StateNode* card = report.find("card");
    if (card != nullptr && !Text(*card, "kind").empty() && Text(*card, "kind") != "none")
        devices.push_back(Text(report, "cards").empty() ? Text(*card, "kind") : Text(report, "cards"));
    if (Fitted(report, "zifi"))
        devices.push_back("ZiFi");
    if (Fitted(report, "atm2ioesp"))
        devices.push_back("ATM2IOESP");
    if (Fitted(report, "com_port"))
        devices.push_back("COM port");
    if (Fitted(report, "machine_serial"))
        devices.push_back("serial port");
    return devices;
}

bool HasNetworkDevice(const StateNode& report)
{
    return !NetworkDevices(report).empty();
}

std::string NetworkSummary(const StateNode& report)
{
    std::string out;
    for (const std::string& d : NetworkDevices(report))
        out += (out.empty() ? "" : ", ") + d;
    return out;
}

std::string ExecutionSummary(const StateNode& report)
{
    return Text(report, "phase") + ", " + Text(report, "intent");
}

std::string ContentionSummary(const StateNode& report)
{
    return "rule " + Text(report, "rule") + ", " + Text(report, "memory_interface");
}

std::string ProfiSummary(const StateNode& report)
{
    return "board " + Text(report, "board");
}

/// The Next's three chips: AY or YM, turbosound, the DAC
std::string NextAudioSummary(const StateNode& report)
{
    const StateNode* config = report.find("config");
    if (config == nullptr)
        return std::string();
    const StateNode* turbo = config->find("turbosound");
    const StateNode* dac = config->find("dac");
    std::string out = "3 x " + std::string(Text(*config, "chip_model") == "ym2149" ? "YM2149" : "AY-3-8910");
    out += turbo != nullptr && turbo->b ? ", turbosound" : ", one chip";
    if (dac != nullptr && dac->b)
        out += ", DAC";
    return out;
}

/// The CPU clock and the palette DAC the build models (VDAC2 or the board's own)
std::string TsConfSummary(const StateNode& report)
{
    std::string out = "CPU " + Text(report, "cpu_clock");
    if (const StateNode* build = report.find("build"); build != nullptr && !Text(*build, "vdac").empty())
        out += Text(*build, "vdac") == "VDAC2" ? ", VDAC2" : ", DAC " + Text(*build, "vdac");
    return out;
}
}  // namespace

const std::vector<ReportEntry>& CatalogEntries()
{
    // The machine first, then its sound, storage, clock and network devices, then the platform reports
    static const std::vector<ReportEntry> entries = {
        {"execution", "Execution arbiter", "state/execution", "state execution", "execution_state", "execution", &Execution, nullptr, &ExecutionSummary},
        {"screen", "Screen", "state/screen", "state screen", "screen_state", "screen", &BuildScreen, nullptr, nullptr},
        {"contention", "Memory contention", "state/contention", "state contention", "contention_state", "contention", &Contention, nullptr, &ContentionSummary},
        {"slots", "ZX-bus slots", "slots", "slots", "slots_state", "slots", &Slots, nullptr, nullptr},
        {"ay", "AY sound chips", "state/audio/ay", "state audio ay", "audio_ay_state", "audio_ay", &Ay, &HasAyChip, nullptr},
        {"fm", "TurboSound FM", "state/audio/fm", "state audio fm", "audio_fm_state", "audio_fm", &Fm, nullptr, nullptr},
        {"gs", "General Sound", "state/audio/gs", "state audio gs", "gs_state", "audio_gs", &BuildGs, nullptr, nullptr},
        {"gs-commands", "GS command log", "history/gs.commands", "history gs.commands", "history", "device_history", &GsCommands, nullptr, &GsCommandsSummary, &GsCommandsCommands},
        {"gs-firmware", "GS firmware objects", "state/audio/gs/firmware", "gs firmware", "gs_firmware", "audio_gs_firmware", &GsFirmware, nullptr, nullptr},
        {"gs-stats", "GS statistics", "state/audio/gs/stats", "gs stats", "gs_stats", "audio_gs_stats", &GsStats, nullptr, nullptr},
        {"gs-zxdma", "NeoGS ZX-DMA trace", "state/audio/gs/zxdma", "gs zxdma", "gs_zxdma", "audio_gs_zxdma", &GsZxDma, nullptr, &GsZxDmaSummary},
        {"covox", "Covox / SounDrive", "state/audio/covox", "state audio covox", "audio_covox_state", "audio_covox", &Covox, nullptr, nullptr},
        {"moonsound", "MoonSound", "state/audio/moonsound", "state audio moonsound", "audio_moonsound_state", "audio_moonsound", &MoonSound, nullptr, nullptr},
        {"multisound", "ZX-MultiSound", "state/audio/multisound", "multisound", "multisound_state", "audio_multisound", &MultiSound, nullptr, nullptr},
        // The ZX-MultiSound's General Sound (the debugger's `ms` CPU): the card panes of the GS slot's for it; its
        // hardware and protocol are the `multisound` report's `gs`
        {"ms-gs-commands", "MultiSound GS command log", "history/zxbus.1.gs.commands", "history zxbus.1.gs.commands", "history", "device_history", &MsGsCommands, nullptr, &GsCommandsSummary, &GsCommandsCommands},
        {"ms-gs-firmware", "MultiSound GS firmware objects", "state/audio/gs/firmware?cpu=ms", "gs firmware --cpu ms", "gs_firmware", "audio_gs_firmware", &MsGsFirmware, nullptr, nullptr},
        {"ms-gs-stats", "MultiSound GS statistics", "state/audio/gs/stats?cpu=ms", "gs stats --cpu ms", "gs_stats", "audio_gs_stats", &MsGsStats, nullptr, nullptr},
        {"midi", "MIDI synthesizer", "state/audio/midi", "midi", "midi_state", "audio_midi", &Midi, nullptr, nullptr},
        {"wildsound", "Wild Sound", "state/audio/wildsound", "wildsound", "wildsound_state", "audio_wildsound", &WildSound, nullptr, nullptr},
        {"fdc", "Disk controller", "state/fdc", "state fdc", "fdc_state", "fdc", &Fdc, nullptr, nullptr},
        {"ide", "IDE board", "state/ide", "state ide", "ide_state", "ide", &Ide, nullptr, nullptr},
        {"cdaudio", "CD audio", "state/cdaudio", "state cdaudio", "cdaudio_state", "cdaudio", &BuildCdAudio, nullptr, nullptr},
        {"rtc", "Real-time clock", "state/rtc", "state rtc", "rtc_state", "rtc", &Rtc, nullptr, nullptr},
        {"network", "Network", "state/network", "network", "network_state", "network", &Network, &HasNetworkDevice, &NetworkSummary},
        {"isa", "ISA slots", "state/isa", "state isa", "isa_state", "isa", &Isa, nullptr, nullptr},
        {"profi", "ZX Profi board", "state/profi", "state profi", "profi_state", "profi", &ProfiPeripherals, nullptr, &ProfiSummary},
        {"tsconf", "TS-Conf", "state/tsconf", "state tsconf", "tsconf_state", "tsconf", &TsConf, nullptr, &TsConfSummary},
        {"sprinter", "Sprinter", "state/sprinter", "state sprinter", "sprinter_state", "sprinter", &Sprinter, nullptr, nullptr},
        {"next", "ZX Spectrum Next", "state/next", "state next", "next_state", "next", &Next, nullptr, nullptr},
        {"next-regs", "Next registers", "state/next/regs", "state next regs", "next_regs", "next_regs", &NextRegs, nullptr, nullptr},
        {"next-mmu", "Next MMU", "state/next/mmu", "state next mmu", "next_mmu", "next_mmu", &NextMmu, nullptr, nullptr},
        {"next-copper", "Next copper", "state/next/copper", "state next copper", "next_copper", "next_copper", &BuildNextCopper, nullptr, nullptr},
        {"next-sprites", "Next sprites", "state/next/sprites", "state next sprites", "next_sprites", "next_sprites", &BuildNextSprites, nullptr, nullptr},
        {"next-audio", "Next sound", "state/next/audio", "state next audio", "next_audio", "next_audio", &NextAudioReport, nullptr, &NextAudioSummary},
        {"next-dma", "Next DMA", "state/next/dma", "state next dma", "next_dma", "next_dma", &NextDma, nullptr, nullptr},
        {"next-uart", "Next UARTs", "state/next/uart", "state next uart", "next_uart", "next_uart", &NextUartReport, nullptr, nullptr},
        {"next-divmmc", "Next DivMMC", "state/next/divmmc", "state next divmmc", "next_divmmc", "next_divmmc", &BuildNextDivMmc, nullptr, nullptr},
        {"next-sd", "Next SD cards", "state/next/sd", "state next sd", "next_sd", "next_sd", &BuildNextSd, nullptr, nullptr},
    };
    return entries;
}

std::string ReportSummary(const StateNode& report)
{
    for (const char* key : {"description", "device", "card", "synthesizer", "controller", "chip", "board", "display_mode",
                            "scheme", "machine", "model", "type", "name"})
        if (const StateNode* v = report.find(key); v != nullptr && v->kind == StateNode::Kind::String && !v->s.empty())
            return v->s;
    return std::string();
}

StateNode Catalog(EmulatorContext* context, bool all)
{
    StateNode n = StateNode::Object();
    if (context == nullptr)
    {
        n["available"] = false;
        n["description"] = "No machine";
        return n;
    }
    n["available"] = true;
    StateNode reports = StateNode::Array();
    for (const ReportEntry& e : CatalogEntries())
    {
        const StateNode report = e.build(context);
        const StateNode* available = report.find("available");
        const bool present = available != nullptr && available->kind == StateNode::Kind::Bool && available->b &&
                             (e.present == nullptr || e.present(report));
        if (!present && !all)
            continue;
        StateNode r = StateNode::Object();
        r["id"] = e.id;
        r["title"] = e.title;
        r["available"] = present;
        if (present)
            r["summary"] = e.summary != nullptr ? e.summary(report) : ReportSummary(report);
        else
        {
            // The report's own words (`description`; the CD audio says `reason`) when it is unavailable
            const std::string why = !Text(report, "description").empty() ? Text(report, "description") : Text(report, "reason");
            r["description"] = !why.empty() && (available == nullptr || !available->b) ? why : std::string("not on this machine");
        }
        r["route"] = e.route;
        r["cli"] = e.cli;
        r["script"] = e.script;
        r["mcp"] = e.mcp;
        if (e.commands != nullptr && present)
            e.commands(report, r);
        reports.push(std::move(r));
    }
    n["reports"] = std::move(reports);
    return n;
}

std::string CatalogText(const StateNode& catalog, const std::string& newline)
{
    const StateNode* available = catalog.find("available");
    if (available == nullptr || !available->b)
    {
        const StateNode* description = catalog.find("description");
        return "unavailable: " + (description ? description->s : std::string()) + newline;
    }
    const StateNode* reports = catalog.find("reports");
    if (reports == nullptr || reports->size() == 0)
        return "no device reports" + newline;
    // Columns as wide as their longest cell
    auto text = [](const StateNode& r, const char* key) {
        const StateNode* v = r.find(key);
        return v ? v->s : std::string();
    };
    size_t idWidth = 0, titleWidth = 0;
    for (const StateNode& r : reports->items)
    {
        idWidth = std::max(idWidth, text(r, "id").size());
        titleWidth = std::max(titleWidth, text(r, "title").size());
    }
    std::string out;
    for (const StateNode& r : reports->items)
    {
        std::string line = text(r, "id");
        line.append(idWidth + 2 - line.size(), ' ');
        line += text(r, "title");
        const std::string summary = text(r, "summary");
        line.append(titleWidth + 2 - text(r, "title").size(), ' ');
        const StateNode* there = r.find("available");
        if (there != nullptr && !there->b)
            line += "absent: " + text(r, "description");
        else
        {
            if (!summary.empty())
                line += summary + "  ";
            line += "[" + text(r, "cli") + "]";
        }
        out += line + newline;
    }
    return out;
}
}  // namespace DeviceState
