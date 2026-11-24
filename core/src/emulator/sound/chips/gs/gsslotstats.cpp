#include "gsslotstats.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "debugger/ttd/timetravelhooks.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gshostclock.h"
#include "emulator/slots/card.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/soundmanager.h"

/// region <Collecting>

void GSCollectStats(const GeneralSoundCard& card, GSStatsBlock& out)
{
    out.implementation = gsImplementationLabel(card.implementation());
    out.sections = StateNode::Object();
    out.counters.clear();
    out.capabilities.clear();
    const GSActivityCounters& c = card.getActivityCounters();
    const bool lw = card.implementation() == GSCardImplementation::LW;
    GSStatsWriter root(out.sections, "", out.counters);

    GSStatsWriter cardSection = root.Child("card");
    cardSection.State("implementation", out.implementation);
    cardSection.State("device", card.deviceDescription());
    cardSection.State("firmware", card.firmwareDescription());
    cardSection.State("ram_kb", static_cast<uint64_t>(card.getRamSizeKB()));
    cardSection.State("ready", card.isReadyForCommands());
    if (card.implementation() == GSCardImplementation::LLE)
        cardSection.State("clock_hz", GSClassicTiming::CLOCK_HZ);
    GSStatsWriter resets = cardSection.Child("resets");
    resets.Counter("cold", c.resetsCold);
    resets.Counter("host_33", c.resetsHost);
    resets.Counter("nmi", c.nmiRequests);

    // Both sides of the host protocol (§4.2)
    GSStatsWriter mailbox = root.Child("mailbox");
    mailbox.Counter("host_commands", c.hostCommandsReceived);
    mailbox.Counter("host_data_out", c.hostDataWritten);
    mailbox.Counter("host_data_in", c.hostDataRead);
    mailbox.Counter("host_status_polls", c.hostStatusPolls);
    mailbox.Counter("card_commands_read", c.cardCommandsRead);
    mailbox.Counter("card_command_acks", c.cardCommandAcks);
    mailbox.Counter("card_data_in", c.cardDataIn);
    mailbox.Counter("card_data_out", c.cardDataOut);
    if (!lw)
        mailbox.Counter("card_status_polls", c.cardStatusPolls);
    mailbox.Counter("command_overwrites", c.commandOverwrites);
    mailbox.Counter("data_overwrites", c.dataOverwrites);
    if (lw)
        mailbox.Counter("param_queue_overflows", c.hostDataDropped);
    mailbox.State("unconfirmed", (card.getStatusRaw() & 0x01) != 0);

    if (card.hasCoprocessor())
    {
        GSStatsWriter interrupts = root.Child("interrupts");
        interrupts.Counter("accepted", c.interruptsAccepted);
        interrupts.Counter("periods", c.interruptPeriods);
        interrupts.Counter("coalesced", c.interruptsCoalesced);
        interrupts.Counter("nmis_accepted", c.nmisAccepted);

        GSStatsWriter cpu = root.Child("cpu");
        cpu.State("halted", card.isCPUHalted());
        cpu.Counter("steps", c.cpuSteps);
        static constexpr std::pair<const char*, GSCpuRegister> kRegisters[] = {
            {"pc", GSCpuRegister::PC}, {"sp", GSCpuRegister::SP}, {"af", GSCpuRegister::AF},
            {"bc", GSCpuRegister::BC}, {"de", GSCpuRegister::DE}, {"hl", GSCpuRegister::HL},
            {"ix", GSCpuRegister::IX}, {"iy", GSCpuRegister::IY}, {"af_", GSCpuRegister::AFAlt},
            {"bc_", GSCpuRegister::BCAlt}, {"de_", GSCpuRegister::DEAlt}, {"hl_", GSCpuRegister::HLAlt},
            {"i", GSCpuRegister::I}, {"r", GSCpuRegister::R}, {"im", GSCpuRegister::IM},
            {"iff1", GSCpuRegister::IFF1}, {"iff2", GSCpuRegister::IFF2}};
        GSStatsWriter registers = cpu.Child("registers");
        for (const auto& [name, reg] : kRegisters)
            registers.State(name, card.getCPUReg(reg));
    }

    GSStatsWriter audio = root.Child("audio");
    StateNode channels = StateNode::Array();
    for (int i = 0; i < card.channelCount(); i++)
    {
        StateNode channel = StateNode::Object();
        channel["sample"] = card.getChannelSample(i);
        channel["volume"] = card.getChannelVolume(i);
        channels.push(std::move(channel));
    }
    audio.State("channels", std::move(channels));
    audio.Counter("dac_fetches", c.dacFetches);
    audio.Counter("volume_writes", c.volumeLatchWrites);
    audio.State("active", card.hadAudioActivityLastFrame());

    card.collectStats(root);

    for (const auto& [name, section] : out.sections.members)
        if (name != "card")
            out.capabilities.push_back(name);
    out.capabilities.push_back("rates");
    out.capabilities.push_back("errors");
}

/// endregion </Collecting>

/// region <Publishing>

const std::array<GSStatsPublisher::RateSource, 10> GSStatsPublisher::kRateSources = {{
    {"commands", {"mailbox.host_commands", nullptr}},
    {"data_bytes", {"mailbox.host_data_out", "mailbox.host_data_in"}},
    {"interrupts", {"interrupts.accepted", nullptr}},
    {"cpu_steps", {"cpu.steps", nullptr}},
    {"dac_fetches", {"audio.dac_fetches", nullptr}},
    {"zx_dma_bytes", {"dma.zx.bytes_read", "dma.zx.bytes_written"}},
    {"sd_dma_bytes", {"dma.sd.bytes", nullptr}},
    {"mp3_dma_bytes", {"dma.mp3.bytes", nullptr}},
    {"sd_bytes", {"spi.sd.bytes", nullptr}},
    {"mp3_bytes", {"mp3.bytes_received", nullptr}},
}};

namespace
{
const uint64_t* FindCounter(const GSStatsWriter::Counters& counters, const char* path)
{
    for (const auto& [name, value] : counters)
        if (name == path)
            return &value;
    return nullptr;
}
} // namespace

void GSStatsPublisher::Publish(const GeneralSoundCard& card, double frameSeconds, const ttd::ITimeTravelHooks* ttd)
{
    auto block = std::make_shared<GSStatsBlock>();
    GSCollectStats(card, *block);
    block->frame = Frame();
    block->replaying = ttd && ttd->IsReplayActive();

    // The rate window: a card change starts a new one
    if (block->implementation != _ringImplementation)
    {
        _ringImplementation = block->implementation;
        _ringCount = 0;
    }
    RateSample sample;
    sample.seconds = frameSeconds;
    for (size_t i = 0; i < kRateSources.size(); i++)
    {
        for (const char* path : kRateSources[i].paths)
        {
            if (!path)
                continue;
            if (const uint64_t* value = FindCounter(block->counters, path))
            {
                sample.values[i] += *value;
                sample.present = static_cast<uint16_t>(sample.present | (1u << i));
            }
        }
    }
    _ringHead = (_ringHead + 1) % _ring.size();
    _ring[_ringHead] = sample;
    _ringCount = std::min(_ringCount + 1, _ring.size());

    if (_ringCount >= 2)
    {
        const size_t frames = _ringCount - 1;
        const RateSample& oldest = _ring[(_ringHead + _ring.size() - frames) % _ring.size()];
        double seconds = 0;
        for (size_t k = 0; k < frames; k++)
            seconds += _ring[(_ringHead + _ring.size() - k) % _ring.size()].seconds;
        block->rates["window_frames"] = static_cast<uint64_t>(frames);
        for (size_t i = 0; i < kRateSources.size(); i++)
        {
            if (!(sample.present & oldest.present & (1u << i)) || seconds <= 0)
                continue;
            const uint64_t delta = sample.values[i] >= oldest.values[i] ? sample.values[i] - oldest.values[i] : 0;
            block->rates[kRateSources[i].name] = static_cast<double>(delta) / seconds;
        }
    }
    PublishBlock(std::move(block));
}

void GSStatsPublisher::CollectNow(const GeneralSoundCard& card, bool replaying)
{
    auto block = std::make_shared<GSStatsBlock>();
    GSCollectStats(card, *block);
    block->frame = Frame();
    block->replaying = replaying;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_latest && _latest->implementation == block->implementation)
            block->rates = _latest->rates;
    }
    PublishBlock(std::move(block));
}

void GSStatsPublisher::PublishBlock(std::shared_ptr<GSStatsBlock> block)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _latest = std::move(block);
    }
    _published.notify_all();
}

std::shared_ptr<const GSStatsBlock> GSStatsPublisher::Latest() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _latest;
}

bool GSStatsPublisher::WaitFor(uint64_t minFrame, uint32_t waitMs) const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _published.wait_for(lock, std::chrono::milliseconds(waitMs),
                               [&] { return _latest && _latest->frame >= minFrame; });
}

uint32_t GSStatsPublisher::AddBaseline(GSStatsWriter::Counters counters)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const uint32_t id = _nextBaseline++;
    _baselines.emplace_back(id, std::move(counters));
    if (_baselines.size() > kMaxBaselines)
        _baselines.erase(_baselines.begin());
    return id;
}

std::optional<GSStatsWriter::Counters> GSStatsPublisher::Baseline(uint32_t id) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto& [baselineId, counters] : _baselines)
        if (baselineId == id)
            return counters;
    return std::nullopt;
}

/// endregion </Publishing>

/// region <Reading>

GeneralSoundCard* GSCardForCpu(EmulatorContext* context, const std::string& cpu, GSStatsPublisher** stats, bool* known)
{
    if (known)
        *known = cpu.empty() || cpu == "gs" || cpu == "ms";
    if (stats)
        *stats = nullptr;
    if (!context)
        return nullptr;
    if (cpu.empty() || cpu == "gs")
    {
        SoundManager* sm = context->pSoundManager;
        GeneralSoundCard* card = sm ? sm->getGeneralSound() : nullptr;
        if (card && stats)
            *stats = &sm->gsStats();
        return card;
    }
    if (cpu != "ms" || !context->pSlotManager)
        return nullptr;
    // The General Sound a slot card carries (the debugger's `ms` CPU: DebugManager's MultiSoundGS target)
    for (const std::unique_ptr<ICard>& card : context->pSlotManager->Cards())
        if (GeneralSoundCard* gs = card->DebugGeneralSound())
        {
            if (stats)
                *stats = card->DebugGeneralSoundStats();
            return gs;
        }
    return nullptr;
}

namespace
{
GSStatsReply Error(int status, const std::string& message)
{
    GSStatsReply reply;
    reply.status = status;
    reply.body = StateNode::Object();
    reply.body["error"] = message;
    reply.text = message;
    return reply;
}

StateNode* FindPath(StateNode& root, const std::string& path)
{
    StateNode* node = &root;
    size_t start = 0;
    while (node)
    {
        const size_t dot = path.find('.', start);
        const std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        StateNode* next = nullptr;
        for (auto& member : node->members)
            if (member.first == key)
                next = &member.second;
        node = next;
        if (dot == std::string::npos)
            break;
        start = dot + 1;
    }
    return node;
}

/// The error counters (§4.13): a name and the counters summed into it
struct ErrorSource
{
    const char* name;
    std::array<const char*, 2> paths;
};
constexpr ErrorSource kErrorSources[] = {
    {"command_overwrites", {"mailbox.command_overwrites", nullptr}},
    {"data_overwrites", {"mailbox.data_overwrites", nullptr}},
    {"interrupts_coalesced", {"interrupts.coalesced", nullptr}},
    {"param_queue_overflows", {"mailbox.param_queue_overflows", nullptr}},
    {"zx_bytes_dropped", {"dma.zx.bytes_dropped", nullptr}},
    {"dma_aborts", {"dma.sd.aborts", "dma.mp3.aborts"}},
    {"sd_dma_errors", {"dma.sd.errors", nullptr}},
    {"sd_illegal", {"sd.illegal_commands", nullptr}},
    {"sd_crc", {"sd.crc_errors", nullptr}},
    {"sd_write_errors", {"sd.write_errors", nullptr}},
    {"mp3_dropped", {"mp3.bytes_dropped", nullptr}},
    {"mp3_resyncs", {"mp3.resync_bytes", nullptr}},
    {"flash_program_failures", {"flash.program_failures", nullptr}},
};

/// Wait for, or collect, the block this read needs; nullptr with reply.pending when it is not there yet
std::shared_ptr<const GSStatsBlock> Acquire(EmulatorContext* context, GSStatsQuery& query, GSStatsReply& reply,
                                            const GeneralSoundCard*& cardOut, GSStatsPublisher*& publisherOut)
{
    SoundManager* sm = context ? context->pSoundManager : nullptr;
    GSStatsPublisher* found = nullptr;
    bool known = true;
    const GeneralSoundCard* card = GSCardForCpu(context, query.cpu, &found, &known);
    cardOut = card;
    publisherOut = found;
    if (!known)
    {
        reply = Error(404, "No card CPU '" + query.cpu + "' (gs: the GS slot's card, ms: the ZX-MultiSound's)");
        return nullptr;
    }
    if (!card || !found || !sm)
    {
        reply = Error(404, query.cpu == "ms" ? "No ZX-MultiSound with a General Sound fitted"
                                             : "General Sound card not fitted");
        return nullptr;
    }
    GSStatsPublisher& publisher = *found;
    publisher.Want();

    const ttd::ITimeTravelHooks* ttd = context->pTimeTravelHooks;
    const bool replaying = ttd && ttd->IsReplayActive();
    Emulator* emulator = context->pEmulator;
    const bool stopped = !emulator || !emulator->IsRunning() || emulator->IsPaused();
    if (stopped || sm->onEmulationThread())
    {
        publisher.CollectNow(*card, replaying);
        return publisher.Latest();
    }

    std::shared_ptr<const GSStatsBlock> latest = publisher.Latest();
    // A block from before the last 2 s of reading, or one from before a fresh read, is not this read's
    if (query.minFrame == 0 &&
        (query.fresh || !latest || latest->frame + GSStatsPublisher::kWantedFrames < publisher.Frame()))
        query.minFrame = publisher.Frame() + 1;
    if (query.minFrame != 0 && (!latest || latest->frame < query.minFrame) && query.waitMs != 0)
    {
        publisher.WaitFor(query.minFrame, query.waitMs);
        latest = publisher.Latest();
    }
    if (query.minFrame != 0 && (!latest || latest->frame < query.minFrame))
    {
        reply.pending = true;
        reply.minFrame = query.minFrame;
        reply.body = StateNode::Object();
        reply.body["error"] = std::string("no frame has ended since the read; read again");
        reply.text = "GS stats: waiting for a frame end";
        return nullptr;
    }
    return latest;
}
} // namespace

GSStatsReply GSStatsRead(EmulatorContext* context, GSStatsQuery query)
{
    GSStatsReply reply;
    const GeneralSoundCard* card = nullptr;
    GSStatsPublisher* found = nullptr;
    const std::shared_ptr<const GSStatsBlock> block = Acquire(context, query, reply, card, found);
    if (!block)
        return reply;
    const GSStatsPublisher& publisher = *found;

    std::optional<GSStatsWriter::Counters> baseline;
    if (query.since != 0)
    {
        baseline = publisher.Baseline(query.since);
        if (!baseline)
            return Error(404, "No baseline " + std::to_string(query.since) + " (reset_counters makes one; the last 16 are kept)");
    }

    // The counters relative to the baseline; one that went back (a card replaced) counts from zero
    StateNode sections = block->sections;
    GSStatsWriter::Counters counters = block->counters;
    if (baseline)
    {
        for (auto& [path, value] : counters)
        {
            const uint64_t* base = FindCounter(*baseline, path.c_str());
            if (base && *base <= value)
                value -= *base;
            if (StateNode* node = FindPath(sections, path))
                *node = value;
        }
    }
    StateNode errors = StateNode::Object();
    uint64_t total = 0;
    for (const ErrorSource& source : kErrorSources)
    {
        bool present = false;
        uint64_t sum = 0;
        for (const char* path : source.paths)
        {
            if (const uint64_t* value = path ? FindCounter(counters, path) : nullptr)
            {
                present = true;
                sum += *value;
            }
        }
        if (!present)
            continue;
        errors[source.name] = sum;
        total += sum;
    }
    errors["total"] = total;

    StateNode& body = reply.body;
    body = StateNode::Object();
    body["implementation"] = block->implementation;
    body["frame"] = block->frame;
    body["age_frames"] = publisher.Frame() - block->frame;
    body["replaying"] = block->replaying;
    body["since"] = query.since;
    StateNode capabilities = StateNode::Array();
    for (const std::string& name : block->capabilities)
        capabilities.push(name);
    body["capabilities"] = capabilities;

    if (query.section.empty())
    {
        for (auto& [name, section] : sections.members)
            body[name] = std::move(section);
        body["rates"] = block->rates;
        body["errors"] = std::move(errors);
    }
    else if (query.section == "rates")
        body["rates"] = block->rates;
    else if (query.section == "errors")
        body["errors"] = std::move(errors);
    else if (StateNode* section = FindPath(sections, query.section); section && query.section.find('.') == std::string::npos)
        body[query.section] = std::move(*section);
    else
    {
        std::string names = "card";
        for (const std::string& name : block->capabilities)
            names += ", " + name;
        return Error(404, "The " + card->deviceDescription() + " (" + block->implementation + ") has no section '" +
                              query.section + "'. Sections: " + names);
    }
    reply.text = GSStatsText(body);
    for (const auto& [path, value] : counters)
        reply.counters.push_back(path);
    for (const ErrorSource& source : kErrorSources)
        if (body.find("errors") && body.find("errors")->find(source.name))
            reply.counters.push_back(std::string("errors.") + source.name);
    if (body.find("errors"))
        reply.counters.push_back("errors.total");
    return reply;
}

GSStatsReply GSStatsResetCounters(EmulatorContext* context, GSStatsQuery query)
{
    GSStatsReply reply;
    const GeneralSoundCard* card = nullptr;
    GSStatsPublisher* found = nullptr;
    const std::shared_ptr<const GSStatsBlock> block = Acquire(context, query, reply, card, found);
    if (!block)
        return reply;
    const uint32_t id = found->AddBaseline(block->counters);
    reply.body = StateNode::Object();
    reply.body["baseline"] = id;
    reply.body["frame"] = block->frame;
    reply.text = "GS stats: baseline " + std::to_string(id) + " at frame " + std::to_string(block->frame) +
                 " (read with since " + std::to_string(id) + ")";
    return reply;
}

/// endregion </Reading>

/// region <Text>

namespace
{
std::string Number(const StateNode& node)
{
    char buffer[48];
    switch (node.kind)
    {
        case StateNode::Kind::Bool: return node.b ? "yes" : "no";
        case StateNode::Kind::Int: return std::to_string(node.i);
        case StateNode::Kind::Double:
            std::snprintf(buffer, sizeof buffer, node.d >= 100 ? "%.0f" : "%.1f", node.d);
            return buffer;
        case StateNode::Kind::String: return node.s.empty() ? "-" : node.s;
        default: return "";
    }
}

void Flatten(const StateNode& node, const std::string& prefix, std::string& out)
{
    if (node.isObject())
    {
        for (const auto& [key, value] : node.members)
            Flatten(value, prefix.empty() ? key : prefix + "." + key, out);
        return;
    }
    if (node.isArray())
    {
        for (size_t i = 0; i < node.items.size(); i++)
            Flatten(node.items[i], prefix + "[" + std::to_string(i) + "]", out);
        return;
    }
    out += (out.empty() ? "" : ", ") + prefix + " " + Number(node);
}
} // namespace

std::string GSStatsText(const StateNode& body)
{
    if (const StateNode* error = body.find("error"))
        return error->s;
    std::string text = "GS slot stats: " + Number(*body.find("implementation"));
    if (const StateNode* frame = body.find("frame"))
        text += ", frame " + Number(*frame);
    if (const StateNode* age = body.find("age_frames"); age && age->i > 0)
        text += " (" + Number(*age) + " frames old)";
    if (const StateNode* since = body.find("since"); since && since->i > 0)
        text += ", since baseline " + Number(*since);
    if (const StateNode* replaying = body.find("replaying"); replaying && replaying->b)
        text += ", replaying";
    text += "\n";
    for (const auto& [name, section] : body.members)
    {
        if (!section.isObject())
            continue;
        std::string line;
        if (name == "rates")
        {
            for (const auto& [key, value] : section.members)
                if (key != "window_frames")
                    line += (line.empty() ? "" : ", ") + key + " " + Number(value) + "/s";
            const StateNode* window = section.find("window_frames");
            text += "rates" + (window ? " (last " + Number(*window) + " frames)" : std::string()) + ": " +
                    (line.empty() ? std::string("none yet (two frame ends with a reader)") : line) + "\n";
            continue;
        }
        Flatten(section, "", line);
        text += name + ": " + line + "\n";
    }
    return text;
}

/// endregion </Text>
