#include "debugboards.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <sstream>

#include "common/filehelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareprofile.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gsslotstats.h"
#include "emulator/sound/chips/neogs/neogsmedia.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"

/// region <DebugBoardBuilder>

DebugBoardBuilder::DebugBoardBuilder(std::string id, std::string title, std::string device, std::string cpu, bool available)
    : _id(std::move(id)), _title(std::move(title)), _device(std::move(device)), _cpu(std::move(cpu)), _available(available)
{
}

DebugBoardBuilder& DebugBoardBuilder::Column()
{
    StateNode column = StateNode::Object();
    column["groups"] = StateNode::Array();
    _columns.push(std::move(column));
    return *this;
}

DebugBoardBuilder& DebugBoardBuilder::Group(const std::string& title, const std::string& raw)
{
    if (_columns.items.empty())
        Column();
    StateNode group = StateNode::Object();
    group["title"] = title;
    if (!raw.empty())
        group["raw"] = raw;
    group["controls"] = StateNode::Array();
    _columns.items.back()["groups"].push(std::move(group));
    return *this;
}

StateNode& DebugBoardBuilder::Add(const std::string& id, const std::string& label, const char* type, StateNode value,
                                  const std::string& note)
{
    if (_columns.items.empty() || _columns.items.back()["groups"].items.empty())
        Group("");
    StateNode control = StateNode::Object();
    control["id"] = id;
    control["label"] = label;
    control["type"] = type;
    control["value"] = value;
    if (!note.empty())
        control["note"] = note;
    _values.emplace_back(id, std::move(value));
    return _columns.items.back()["groups"].items.back()["controls"].push(std::move(control));
}

DebugBoardBuilder& DebugBoardBuilder::Hex8(const std::string& id, const std::string& label, uint8_t value, const std::string& note)
{
    Add(id, label, "hex8", int(value), note);
    return *this;
}

DebugBoardBuilder& DebugBoardBuilder::Hex16(const std::string& id, const std::string& label, uint16_t value, const std::string& note)
{
    Add(id, label, "hex16", int(value), note);
    return *this;
}

DebugBoardBuilder& DebugBoardBuilder::Hex21(const std::string& id, const std::string& label, uint32_t value, const std::string& note)
{
    Add(id, label, "hex21", int64_t(value & 0x1FFFFF), note);
    return *this;
}

DebugBoardBuilder& DebugBoardBuilder::Dec(const std::string& id, const std::string& label, int64_t value, const std::string& note)
{
    Add(id, label, "dec", value, note);
    return *this;
}

DebugBoardBuilder& DebugBoardBuilder::Counter(const std::string& id, const std::string& label, uint64_t value, const std::string& note)
{
    Add(id, label, "counter", value, note);
    return *this;
}

DebugBoardBuilder& DebugBoardBuilder::Rate(const std::string& id, const std::string& label, double value, const std::string& note)
{
    Add(id, label, "rate", value, note);
    return *this;
}

DebugBoardBuilder& DebugBoardBuilder::Bool(const std::string& id, const std::string& label, bool value, const std::string& note)
{
    Add(id, label, "bool", value, note);
    return *this;
}

DebugBoardBuilder& DebugBoardBuilder::Led(const std::string& id, const std::string& label, bool value, const std::string& note)
{
    Add(id, label, "led", value, note);
    return *this;
}

DebugBoardBuilder& DebugBoardBuilder::Text(const std::string& id, const std::string& label, const std::string& value, const std::string& note)
{
    Add(id, label, "text", value, note);
    return *this;
}

DebugBoardBuilder& DebugBoardBuilder::Bits(const std::string& id, const std::string& label, uint8_t value,
                                           const std::vector<std::string>& names, const std::string& note)
{
    StateNode& control = Add(id, label, "bits", int(value), note);
    StateNode list = StateNode::Array();
    for (const std::string& name : names)
        list.push(name);
    control["names"] = std::move(list);
    return *this;
}

DebugBoardBuilder& DebugBoardBuilder::Enum(const std::string& id, const std::string& label, const std::string& value,
                                           const std::vector<std::string>& values, const std::string& note)
{
    StateNode& control = Add(id, label, "enum", value, note);
    StateNode list = StateNode::Array();
    for (const std::string& v : values)
        list.push(v);
    control["values"] = std::move(list);
    return *this;
}

DebugBoardBuilder& DebugBoardBuilder::Bar(const std::string& id, const std::string& label, int64_t value, int64_t min, int64_t max,
                                          const std::string& note)
{
    StateNode& control = Add(id, label, "bar", value, note);
    control["min"] = min;
    control["max"] = max;
    return *this;
}

DebugBoardBuilder& DebugBoardBuilder::Action(const std::string& id, const std::string& title, const std::string& param)
{
    StateNode action = StateNode::Object();
    action["id"] = id;
    action["title"] = title;
    if (!param.empty())
        action["param"] = param;
    _actions.push(std::move(action));
    return *this;
}

StateNode DebugBoardBuilder::Summary() const
{
    StateNode node = StateNode::Object();
    node["id"] = _id;
    node["title"] = _title;
    node["device"] = _device;
    node["cpu"] = _cpu;
    node["available"] = _available;
    node["actions"] = _actions;
    return node;
}

StateNode DebugBoardBuilder::Build(bool valuesOnly) const
{
    if (valuesOnly)
    {
        StateNode node = StateNode::Object();
        node["id"] = _id;
        node["available"] = _available;
        StateNode values = StateNode::Object();
        for (const auto& [id, value] : _values)
            values[id] = value;
        node["values"] = std::move(values);
        return node;
    }
    StateNode node = Summary();
    node["columns"] = _columns;
    return node;
}

/// endregion </DebugBoardBuilder>

namespace
{

std::string Hex(uint32_t value, int digits)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%0*X", digits, value);
    return buffer;
}

/// The classic card's mailbox (widget catalog §4.4), also on NeoGS
void MailboxGroup(DebugBoardBuilder& b, const GeneralSoundCard& gs)
{
    const uint8_t status = gs.getStatusRaw();
    b.Group("Mailbox", "status=" + Hex(status, 2))
        .Hex8("command_from_host", "command", gs.getCommandFromHost(), "the host's last OUT (#BB)")
        .Hex8("data_from_host", "data in", gs.getDataFromHost(), "the host's last OUT (#B3)")
        .Hex8("data_to_host", "data out", gs.getDataToHost(), "the card's last OUT (#03), the host's IN (#B3)")
        .Bits("status", "status", status, {"C", "", "", "", "", "", "", "D"}, "bit 7 data flag, bit 0 command flag");
}

void ChannelsGroup(DebugBoardBuilder& b, const GeneralSoundCard& gs)
{
    b.Group("DAC");
    for (int i = 0; i < gs.channelCount(); i++)
    {
        const std::string channel = "channels[" + std::to_string(i) + "]";
        b.Hex8(channel + ".sample", "ch" + std::to_string(i + 1) + " sample", gs.getChannelSample(i))
            .Bar(channel + ".volume", "ch" + std::to_string(i + 1) + " volume", gs.getChannelVolume(i), 0, 63);
    }
}

/// `W.board.gs`: the classic card (LLE or the lightweight player)
DebugBoardBuilder GsBoard(const GeneralSoundCard& gs)
{
    const bool lle = gs.hasCoprocessor();
    DebugBoardBuilder b("gs", "General Sound card hardware", gs.deviceDescription(), lle ? "gs" : "main");
    const uint8_t mpag = gs.getMPAG();
    b.Column().Group("Paging", "MPAG=" + Hex(mpag, 2));
    if (lle)
        b.Hex8("mpag", "page", mpag, "port #00: RAM page " + std::to_string(mpag * 2u) + "-" + std::to_string(mpag * 2u + 1) + " at #8000");
    else
        b.Hex8("mpag", "page", mpag, "the lightweight player has no paging");
    ChannelsGroup(b, gs);
    b.Column();
    MailboxGroup(b, gs);
    std::string rom = gs.firmwareDescription();
    if (const GSFirmwareIdentity identity = IdentifyGSFirmware(gs); identity.hasRom)
        rom = (identity.profile ? std::string(identity.profile->title) : std::string("unknown firmware")) + ", SHA-256 " +
              identity.sha256.substr(0, 16) + "...";
    b.Group("Firmware")
        .Bool("ready", "ready", gs.isReadyForCommands(), "the firmware is in its command loop")
        .Text("rom", "rom", rom);
    return b;
}

std::vector<std::string> Gscfg0Names()
{
    return {"NOROM", "RAMRO", "8CHANS", "EXPAG", "CLK0", "CLK1", "PAN4CH", "INV7B"};
}

/// `W.board.neogs`: the card's configuration, paging, clock, DAC, interrupts, SPI and mailbox (widget catalog §4.5)
DebugBoardBuilder NeoGsBoard(const GeneralSoundCard& gs, const NeoGSStateInfo& s, const SoundManager* sm)
{
    DebugBoardBuilder b("neogs", "NeoGS card hardware", gs.deviceDescription(), "gs");
    b.Column().Group("Config", "GSCFG0=" + Hex(s.gscfg0, 2))
        .Bits("gscfg0", "GSCFG0", s.gscfg0, Gscfg0Names(), "NOROM: RAM mode; clock: bits 5:4");
    b.Group("Paging", s.extendedPaging ? "MPAG=" + Hex(s.mpag, 2) + " MPAGEX=" + Hex(s.mpagex, 2) : "MPAG=" + Hex(s.mpag, 2));
    for (int w = 0; w < 4; w++)
    {
        const std::string window = std::to_string(w);
        b.Hex8("pages[" + window + "]", "window " + window + " page", s.pages[w], Hex(w * 0x4000u, 4) + "-" + Hex(w * 0x4000u + 0x3FFF, 4))
            .Bool("window_flash[" + window + "]", "window " + window + " flash", s.windowFlash[w]);
    }
    b.Hex8("mpag", "MPAG", s.mpag, "the last MPAG (#00) write");
    b.Hex8("mpagex", "MPAGEX", s.mpagex,
           s.extendedPaging ? "the last MPAGEX (#10) write; extended paging on: MPAG sets window 2, MPAGEX window 3"
                            : "extended paging off (GSCFG0.EXPAG): MPAG sets windows 2 and 3, MPAGEX writes are ignored");
    b.Bool("extended_paging", "extended paging", s.extendedPaging, "GSCFG0 bit 3 (EXPAG)");
    b.Group("Clock").Dec("clock_hz", "clock", s.clockHz, "Hz: 10 / 12 / 20 / 24 MHz from GSCFG0 bits 5:4");
    ChannelsGroup(b, gs);
    const char* audioMode = (s.gscfg0 & 0x04) ? "8ch" : (s.gscfg0 & 0x40) ? "pan4ch" : "4ch";
    b.Enum("audio_mode", "audio mode", audioMode, {"4ch", "8ch", "pan4ch"});
    b.Column().Group("Output")
        .Dec("dac_level.left", "left", s.dacLeft, "the DAC's level before the line output's coupling")
        .Dec("dac_level.right", "right", s.dacRight)
        .Enum("stereo_mode", "listening", sm ? neogsStereoModeName(sm->neoGSStereoMode()) : "separated", {"separated", "gs", "mono"});
    // TIM_FREQ picks a falling edge of the 4.8 MHz counter: bits 6, 7, 8, 9, 10, 12, 14, 16
    static const int kTimerBits[8] = {6, 7, 8, 9, 10, 12, 14, 16};
    const double timerHz = 4800000.0 / double(2 << kTimerBits[s.timFreq & 7]);
    b.Group("Interrupts")
        .Bits("int_enable", "INTENA", s.intEnable, {"timer", "sd_dma", "mp3_dma"})
        .Bits("int_request", "INTREQ", s.intRequest, {"timer", "sd_dma", "mp3_dma"})
        .Dec("tim_freq", "TIM_FREQ", s.timFreq & 7, std::to_string(timerHz) + " Hz")
        .Rate("timer_hz", "timer", timerHz, "ticks per second");
    b.Group("SPI", "SCTRL=" + Hex(s.sctrl, 2))
        .Bits("sctrl", "SCTRL", s.sctrl, {"SD_nCS", "MC_nCS", "XRESET", "MCSPD0", "MDHLF", "MCSPD1"});
    b.Group("Status", "SSTAT=" + Hex(s.sstat, 2))
        .Bits("sstat", "SSTAT", s.sstat, {"DREQ", "/SD_DET", "/SD_WP", "MCRDY"}, "/SD_DET and /SD_WP are low-active");
    b.Group("LED").Led("led_on", "LED", s.ledOn);
    MailboxGroup(b, gs);
    b.Group("Firmware")
        .Bool("ready", "ready", s.readyForCommands, "the firmware is in its command loop")
        .Text("flash_title", "flash", s.flashTitle)
        .Bool("flash_modified", "modified", s.flashModified);
    return b;
}

/// `W.board.neogs_dma`: the three DMA modules and the ZX-DMA detail (widget catalog §4.6)
DebugBoardBuilder NeoGsDmaBoard(const GeneralSoundCard& gs, const NeoGSStateInfo& s)
{
    DebugBoardBuilder b("neogs_dma", "NeoGS DMA and ZX-DMA", gs.deviceDescription(), "gs");
    static const char* const kModules[3] = {"zx", "sd", "mp3"};
    static const char* const kTitles[3] = {"ZX module", "SD module", "MP3 module"};
    b.Column();
    for (int m = 0; m < 3; m++)
    {
        const std::string module = std::string("modules[") + kModules[m] + "]";
        b.Group(kTitles[m])
            .Led(module + ".selected", "selected", s.dmaSelect == m + 1, "DMA_MOD: 1 ZX, 2 SD, 3 MP3")
            .Led(module + ".running", "running", s.dmaRunning[m], "CST bit 7")
            .Hex21(module + ".address", "address", s.dmaAddress[m])
            .Dec(module + ".page", "page", s.dmaAddress[m] >> 14, "its 16 KB page");
    }
    b.Column().Group("ZX-DMA")
        .Enum("zx.mode", "mode", s.zxMode, {"off", "watch", "divert"})
        .Enum("zx.watch_setting", "watch", s.zxWatchSetting, {"selected", "always"})
        .Dec("zx.watch_frames", "watch frames", s.zxWatchFrames)
        .Dec("zx.watch_frames_left", "frames left", s.zxWatchFramesLeft, "-1: the window is closed")
        .Hex8("zx.read_latch", "read latch", s.zxReadLatch, "the byte the last fetch latched")
        .Hex8("zx.next_byte", "next byte", s.zxNextByte, "the byte the next host read gets (a fetch in flight: its cell)")
        .Enum("zx.pending", "pending", s.zxPending, {"none", "read", "write"})
        .Hex21("zx.pending_address", "pending address", s.zxPendingAddress)
        .Counter("zx.bytes_read", "bytes read", s.zxBytesRead)
        .Counter("zx.bytes_written", "bytes written", s.zxBytesWritten)
        .Counter("zx.bytes_dropped", "bytes dropped", s.zxBytesDropped)
        .Counter("zx.wait_tstates", "wait T-states", s.zxWaitTStates)
        .Counter("zx.late_starts", "late starts", s.zxLateStarts)
        .Counter("zx.late_start_ticks", "late start ticks", s.zxLateStartUnits, "120 MHz card ticks")
        .Led("zx.overlay_installed", "overlay", s.zxOverlayInstalled, "the host bus overlay is in")
        .Bool("zx.host_memory_bus", "host memory bus", s.zxHostMemoryBus,
              s.zxHostMemoryBus ? "" : "no host memory cycles on this ZX-bus (the Sprinter's ISA adapter passes I/O only)");
    return b;
}

/// `W.board.neogs_sd`: the SD card in the media manager's slot `sd.ngs` (widget catalog §4.7, N8)
DebugBoardBuilder NeoGsSdBoard(EmulatorContext* context, const GeneralSoundCard& gs, const NeoGSStateInfo& s)
{
    DebugBoardBuilder b("neogs_sd", "NeoGS SD card (slot sd.ngs)", gs.deviceDescription(), "gs");
    std::optional<SlotInfo> slot;
    if (context && context->pMediaManager)
        slot = context->pMediaManager->Info(SoundChip_NeoGS::SD_SLOT_ID);
    const std::string source = slot ? slot->source : s.sdPath;
    const bool folder = s.sdPresent && !source.empty() && FileHelper::IsFolder(source);
    b.Column().Group("Medium")
        .Led("present", "present", s.sdPresent)
        .Text("source", "source", source, "an image or a host folder")
        .Enum("source_type", "kind", !s.sdPresent ? "none" : folder ? "folder" : "image", {"none", "image", "folder"})
        .Enum("type", "type", !s.sdPresent ? "none" : s.sdSdhc ? "SDHC" : "SDSC", {"none", "SDSC", "SDHC"})
        .Counter("size_bytes", "size", s.sdSizeBytes, "bytes")
        .Enum("write_mode", "writes", s.sdWriteMode, {"session", "persist", "off"}, "[NGS] SDWrite")
        .Bool("write_protect", "write protect", slot ? slot->writeProtect || s.sdWriteProtect : s.sdWriteProtect)
        .Bool("pending", "pending", slot && slot->pending, "an insert or eject waits for the emulation thread")
        .Bool("dirty", "unsaved", slot && slot->dirty, slot && slot->dirty ? slot->changes : "");
    b.Group("Protocol")
        .Bool("initialized", "initialized", s.sdInitialized, "ACMD41 done")
        .Enum("state", "state", s.sdState, {"power_on", "idle", "ready"})
        .Enum("mode", "mode", s.sdMode, {"command", "read_multi", "wait_token", "receive_data"})
        .Dec("last_command", "last command", s.sdLastCommand, "CMD" + std::to_string(s.sdLastCommand))
        .Hex16("last_argument.high", "argument high", static_cast<uint16_t>(s.sdLastArgument >> 16))
        .Hex16("last_argument.low", "argument low", static_cast<uint16_t>(s.sdLastArgument))
        .Counter("blocks_read", "blocks read", s.sdBlocksRead)
        .Counter("blocks_written", "blocks written", s.sdBlocksWritten);
    b.Action("insert", "Insert an image or a folder", "path").Action("eject", "Eject the card");
    return b;
}

/// `W.board.neogs_mp3`: the VS10xx decoder (widget catalog §4.7)
DebugBoardBuilder NeoGsMp3Board(const GeneralSoundCard& gs, const NeoGSStateInfo& s)
{
    DebugBoardBuilder b("neogs_mp3", "NeoGS MP3 decoder", gs.deviceDescription(), "gs", s.mp3Fitted);
    b.Column().Group("Decoder")
        .Text("chip", "chip", s.mp3Fitted ? s.mp3Chip : "none")
        .Led("running", "running", s.mp3Running)
        .Led("dreq", "DREQ", s.mp3Dreq, "room for 32 more bytes")
        .Dec("rate", "rate", s.mp3Rate, "Hz")
        .Dec("channels", "channels", s.mp3Channels)
        .Dec("decode_time_s", "decode time", s.mp3DecodeSeconds, "s")
        .Bar("input_fill", "input", int64_t(s.mp3InputFill), 0, 2048, "bytes of 2048")
        .Counter("pcm_queued", "PCM queued", s.mp3PcmQueued, "frames")
        .Rate("gain", "gain", s.mp3Gain, "[NGS] MP3Gain")
        .Counter("frames_decoded", "frames decoded", s.mp3Frames)
        .Counter("samples_played", "samples played", s.mp3SamplesPlayed)
        .Counter("bytes_received", "bytes received", s.mp3BytesReceived);
    static const char* const kSci[16] = {"MODE", "STATUS", "BASS", "CLOCKF", "DECODE_TIME", "AUDATA", "WRAM", "WRAMADDR",
                                         "HDAT0", "HDAT1", "AIADDR", "VOL", "AICTRL0", "AICTRL1", "AICTRL2", "AICTRL3"};
    b.Column().Group("SCI registers");
    for (int r = 0; r < 16; r++)
        b.Hex16("sci[" + std::to_string(r) + "]", kSci[r], s.mp3Sci[r]);
    return b;
}

/// `W.board.neogs_flash`: the 29F040 flash chip (widget catalog §4.7)
DebugBoardBuilder NeoGsFlashBoard(const GeneralSoundCard& gs, const NeoGSStateInfo& s)
{
    DebugBoardBuilder b("neogs_flash", "NeoGS flash", gs.deviceDescription(), "gs");
    b.Column().Group("Flash")
        .Text("title", "title", s.flashTitle)
        .Bool("modified", "modified", s.flashModified, "changed since it was loaded")
        .Enum("mode", "mode", s.flashMode, {"read", "busy", "command"}, "busy: a program or erase runs; command: autoselect")
        .Enum("vendor", "vendor", s.flashVendor, {"ST", "AMD"})
        .Enum("write_mode", "writes", s.flashWriteMode, {"session", "persist", "off"}, "[NGS] FlashWrite")
        .Text("persist_path", "saved to", s.flashPersistPath);
    b.Action("save", "Save the flash");
    return b;
}

DebugBoardReply Refused(int status, const std::string& message)
{
    DebugBoardReply reply;
    reply.status = status;
    reply.text = message;
    reply.body = StateNode::Object();
    reply.body["error"] = message;
    return reply;
}

std::string Known(const std::vector<DebugBoardBuilder>& boards)
{
    std::string ids;
    for (const DebugBoardBuilder& b : boards)
        ids += (ids.empty() ? "" : ", ") + b.Id();
    return ids.empty() ? "none (no device publishes a board on this machine)" : ids;
}

int MediaStatus(NeoGSMediaResult r)
{
    switch (r)
    {
        case NeoGSMediaResult::Done:
        case NeoGSMediaResult::Queued: return 200;
        case NeoGSMediaResult::NoNeoGS: return 404;
        case NeoGSMediaResult::TtdRecording:
        case NeoGSMediaResult::ReplayOwnsInput: return 409;
        case NeoGSMediaResult::NoPath: return 400;
        default: return 422;
    }
}

} // namespace

namespace DebugBoards
{

namespace
{
/// The leaves of a statistics section as controls: counters, rates, state (ids are the paths in the block)
void StatsControls(DebugBoardBuilder& b, const StateNode& node, const std::string& id, const std::string& label,
                   const std::vector<std::string>& counters, bool rates)
{
    if (node.isObject())
    {
        for (const auto& [key, value] : node.members)
            StatsControls(b, value, id + "." + key, label.empty() ? key : label + "." + key, counters, rates);
        return;
    }
    if (node.isArray())
    {
        for (size_t i = 0; i < node.items.size(); i++)
            StatsControls(b, node.items[i], id + "[" + std::to_string(i) + "]", label + "[" + std::to_string(i) + "]",
                          counters, rates);
        return;
    }
    switch (node.kind)
    {
        case StateNode::Kind::Bool: b.Bool(id, label, node.b); break;
        case StateNode::Kind::Int:
            if (std::find(counters.begin(), counters.end(), id) != counters.end())
                b.Counter(id, label, static_cast<uint64_t>(node.i));
            else
                b.Dec(id, label, node.i);
            break;
        case StateNode::Kind::Double:
            if (rates)
                b.Rate(id, label, node.d, "per second");
            else
            {
                char text[32];
                std::snprintf(text, sizeof text, "%.2f", node.d);
                b.Text(id, label, text);
            }
            break;
        case StateNode::Kind::String: b.Text(id, label, node.s); break;
        default: break;
    }
}

/// The Stats widget (gs-debugger N7): the GS slot's published statistics block (gsslotstats.h), the same numbers as
/// `GET /state/audio/gs/stats` in the same frame. A running machine shows it from the first frame end after the read
DebugBoardBuilder StatsBoard(EmulatorContext* context, const GeneralSoundCard& gs, bool read)
{
    if (!read)
        return DebugBoardBuilder("gs_stats", "GS slot statistics", gs.deviceDescription(), gs.hasCoprocessor() ? "gs" : "main", true);
    const GSStatsReply stats = GSStatsRead(context, GSStatsQuery{});
    const bool ready = stats.status == 200 && !stats.pending;
    DebugBoardBuilder b("gs_stats", "GS slot statistics", gs.deviceDescription(), gs.hasCoprocessor() ? "gs" : "main", ready);
    b.Column();
    if (!ready)
    {
        b.Group("Statistics").Text("status", "status", stats.text, "read again after a frame end");
        return b;
    }
    const StateNode* frame = stats.body.find("frame");
    const StateNode* age = stats.body.find("age_frames");
    b.Group("Block", frame ? "frame " + std::to_string(frame->i) : "")
        .Text("implementation", "card", stats.body.find("implementation")->s)
        .Dec("frame", "frame", frame ? frame->i : 0, "the publisher's frame count")
        .Dec("age_frames", "age", age ? age->i : 0, "frames since it was collected")
        .Bool("replaying", "replaying", stats.body.find("replaying")->b, "collected while TTD replays");
    int groups = 1;
    for (const auto& [name, section] : stats.body.members)
    {
        if (!section.isObject())
            continue;
        if (groups++ % 4 == 0)
            b.Column();
        b.Group(name);
        StatsControls(b, section, name, "", stats.counters, name == "rates");
    }
    return b;
}
} // namespace

std::vector<DebugBoardBuilder> Build(EmulatorContext* context, const std::string& statsBoard)
{
    const bool readStats = statsBoard == "gs_stats";
    std::vector<DebugBoardBuilder> boards;
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    const GeneralSoundCard* gs = sm ? sm->getGeneralSound() : nullptr;
    if (!gs)
        return boards;
    NeoGSStateInfo s;
    if (!gs->neogsState(s))
    {
        boards.push_back(GsBoard(*gs));
        boards.push_back(StatsBoard(context, *gs, readStats));
        return boards;
    }
    boards.push_back(NeoGsBoard(*gs, s, sm));
    boards.push_back(NeoGsDmaBoard(*gs, s));
    boards.push_back(NeoGsSdBoard(context, *gs, s));
    boards.push_back(NeoGsMp3Board(*gs, s));
    boards.push_back(NeoGsFlashBoard(*gs, s));
    boards.push_back(StatsBoard(context, *gs, readStats));
    return boards;
}

DebugBoardReply List(EmulatorContext* context)
{
    DebugBoardReply reply;
    reply.body = StateNode::Object();
    StateNode list = StateNode::Array();
    std::ostringstream text;
    for (const DebugBoardBuilder& b : Build(context, ""))
    {
        StateNode summary = b.Summary();
        text << b.Id() << " - " << summary["title"].s << " (cpu " << summary["cpu"].s << ")"
             << (summary["available"].b ? "" : ", not available") << "\n";
        list.push(std::move(summary));
    }
    if (list.items.empty())
        text << "No boards: no device publishes one on this machine (the GS-slot cards do: slots plug zxbus.next neogs)\n";
    reply.body["boards"] = std::move(list);
    reply.text = text.str();
    return reply;
}

DebugBoardReply Read(EmulatorContext* context, const std::string& id, bool valuesOnly)
{
    const std::vector<DebugBoardBuilder> boards = Build(context, id);
    for (const DebugBoardBuilder& b : boards)
    {
        if (b.Id() != id)
            continue;
        DebugBoardReply reply;
        reply.body = b.Build(valuesOnly);
        reply.text = Text(b.Build(false));
        return reply;
    }
    return Refused(404, "No board '" + id + "'. Boards: " + Known(boards));
}

DebugBoardReply Action(EmulatorContext* context, const std::string& id, const std::string& action, const std::string& arg)
{
    const std::vector<DebugBoardBuilder> boards = Build(context, "");
    bool found = false;
    for (const DebugBoardBuilder& b : boards)
        found = found || b.Id() == id;
    if (!found)
        return Refused(404, "No board '" + id + "'. Boards: " + Known(boards));

    NeoGSMediaResult result;
    if (id == "neogs_sd" && action == "insert")
    {
        if (arg.empty())
            return Refused(400, "neogs_sd insert needs a path: an image file or a host folder");
        result = NeoGSRequestSdInsert(context, arg);
    }
    else if (id == "neogs_sd" && action == "eject")
        result = NeoGSRequestSdEject(context);
    else if (id == "neogs_flash" && action == "save")
        result = NeoGSRequestFlashSave(context);
    else
        return Refused(404, "Board '" + id + "' has no action '" + action + "' (see the board's actions)");

    const std::string what = id + " " + action + (arg.empty() ? std::string() : " " + arg);
    if (!NeoGSMediaAccepted(result))
        return Refused(MediaStatus(result), what + ": " + NeoGSMediaResultText(result));
    DebugBoardReply reply;
    reply.body = StateNode::Object();
    reply.body["board"] = id;
    reply.body["action"] = action;
    if (!arg.empty())
        reply.body["arg"] = arg;
    reply.body["status"] = result == NeoGSMediaResult::Queued ? "queued" : "done";
    reply.text = what + ": " + (result == NeoGSMediaResult::Queued ? "queued (at the next frame boundary)" : "done");
    return reply;
}

std::string Text(const StateNode& board)
{
    std::ostringstream out;
    const StateNode* id = board.find("id");
    const StateNode* title = board.find("title");
    out << (id ? id->s : "?") << " - " << (title ? title->s : "") << "\n";
    const StateNode* available = board.find("available");
    if (available && !available->b)
        out << "  (not available)\n";
    const StateNode* columns = board.find("columns");
    if (!columns)
        return out.str();
    for (const StateNode& column : columns->items)
    {
        const StateNode* groups = column.find("groups");
        for (const StateNode& group : groups ? groups->items : std::vector<StateNode>{})
        {
            const StateNode* raw = group.find("raw");
            out << "  " << group.find("title")->s << (raw ? "  " + raw->s : "") << "\n";
            for (const StateNode& c : group.find("controls")->items)
            {
                const std::string& type = c.find("type")->s;
                const StateNode& v = *c.find("value");
                std::string value;
                if (type == "hex8")
                    value = Hex(uint32_t(v.i), 2);
                else if (type == "hex16")
                    value = Hex(uint32_t(v.i), 4);
                else if (type == "hex21")
                    value = Hex(uint32_t(v.i), 6);
                else if (type == "bits")
                {
                    value = Hex(uint32_t(v.i), 2);
                    std::string set;
                    const StateNode& names = *c.find("names");
                    for (size_t bit = 0; bit < names.items.size() && bit < 8; bit++)
                        if ((v.i >> bit) & 1 && !names.items[bit].s.empty())
                            set += (set.empty() ? "" : " ") + names.items[bit].s;
                    value += " [" + set + "]";
                }
                else if (type == "bool" || type == "led")
                    value = v.b ? (type == "led" ? "on" : "yes") : (type == "led" ? "off" : "no");
                else if (type == "bar")
                    value = std::to_string(v.i) + "/" + std::to_string(c.find("max")->i);
                else if (v.kind == StateNode::Kind::Double)
                {
                    char buffer[32];
                    std::snprintf(buffer, sizeof(buffer), "%.2f", v.d);
                    value = buffer;
                }
                else if (v.kind == StateNode::Kind::Int)
                    value = std::to_string(v.i);
                else
                    value = v.s;
                const StateNode* note = c.find("note");
                out << "    " << c.find("label")->s << ": " << value << (note ? "  (" + note->s + ")" : "") << "\n";
            }
        }
    }
    const StateNode* actions = board.find("actions");
    if (actions && !actions->items.empty())
    {
        out << "  actions:";
        for (const StateNode& a : actions->items)
        {
            const StateNode* param = a.find("param");
            out << " " << a.find("id")->s << (param ? " <" + param->s + ">" : "");
        }
        out << "\n";
    }
    return out.str();
}

} // namespace DebugBoards
