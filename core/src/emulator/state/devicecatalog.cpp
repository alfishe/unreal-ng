#include "emulator/state/devicecatalog.h"

#include "emulator/io/ide/cdaudiocontrol.h"
#include "emulator/io/z80n/nextdivmmcjournal.h"
#include "emulator/io/z80n/nextsdjournal.h"
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

StateNode BuildCdAudio(EmulatorContext* context)
{
    return CdAudioControl::State(context);
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
        {"covox", "Covox / SounDrive", "state/audio/covox", "state audio covox", "audio_covox_state", "audio_covox", &Covox, nullptr, nullptr},
        {"moonsound", "MoonSound", "state/audio/moonsound", "state audio moonsound", "audio_moonsound_state", "audio_moonsound", &MoonSound, nullptr, nullptr},
        {"multisound", "ZX-MultiSound", "state/audio/multisound", "multisound", "multisound_state", "audio_multisound", &MultiSound, nullptr, nullptr},
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
