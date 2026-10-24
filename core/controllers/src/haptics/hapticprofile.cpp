#include "haptics/hapticprofile.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace controllers
{

namespace
{
struct Library
{
    std::map<std::string, HapticEffect> effects;
    std::vector<std::string> ids;
};

/// haptics.md §6.2. `engine` and `rumble-strip` are continuous: a level source scales them per frame. Priorities: the
/// loss of a life over hits over the rest
const Library& TheLibrary()
{
    static const Library library = [] {
        Library l;
        auto pulse = [&l](const char* id, float low, float high, uint32_t ms, uint8_t priority) {
            HapticEffect effect({low, high, 0.0f, 0.0f}, ms);
            effect.priority = priority;
            l.effects[id] = effect;
            l.ids.emplace_back(id);
        };
        auto adsr = [&l](const char* id, float low, float high, uint32_t ms, uint32_t attack, uint32_t decay,
                         float sustain, uint32_t release, uint8_t priority) {
            HapticEffect effect({low, high, 0.0f, 0.0f}, ms);
            effect.shape = HapticShape::Adsr;
            effect.attackMs = attack;
            effect.decayMs = decay;
            effect.sustain = sustain;
            effect.releaseMs = release;
            effect.priority = priority;
            l.effects[id] = effect;
            l.ids.emplace_back(id);
        };
        auto pattern = [&l](const char* id, float low, float high, uint32_t ms,
                            std::vector<std::pair<uint32_t, float>> steps, uint8_t priority) {
            HapticEffect effect({low, high, 0.0f, 0.0f}, ms);
            effect.shape = HapticShape::Pattern;
            effect.pattern = std::move(steps);
            effect.priority = priority;
            l.effects[id] = effect;
            l.ids.emplace_back(id);
        };
        pulse("tick", 0.0f, 0.35f, 25, 0);
        adsr("knock", 0.5f, 0.2f, 60, 2, 40, 0.0f, 0, 1);
        adsr("thump", 1.0f, 0.4f, 220, 5, 60, 0.4f, 150, 1);
        adsr("explosion", 1.0f, 0.8f, 600, 5, 80, 0.3f, 300, 2);
        pulse("engine", 0.5f, 0.0f, 45, 0);
        pattern("heartbeat", 0.7f, 0.0f, 700, {{90, 1.0f}, {90, 0.0f}, {110, 0.7f}, {410, 0.0f}}, 1);
        adsr("hit", 0.8f, 0.6f, 150, 2, 60, 0.3f, 60, 2);
        pattern("life-lost", 1.0f, 1.0f, 800, {{150, 1.0f}, {100, 0.0f}, {150, 1.0f}, {100, 0.0f}, {300, 0.6f}}, 3);
        adsr("pickup", 0.0f, 0.6f, 80, 5, 30, 0.5f, 45, 1);
        adsr("shot", 0.3f, 0.7f, 70, 1, 30, 0.2f, 40, 1);
        pattern("rumble-strip", 0.3f, 0.3f, 45, {{20, 1.0f}, {20, 0.0f}}, 0);
        // The HD frequencies (haptics.md §6.1 `hd.frequency_hz`) per band: low and deep for the heavy effects, high
        // and bright for the light ones, inside the Switch HD bands (low 40-626 Hz, high 80-1253 Hz)
        const struct
        {
            const char* id;
            uint16_t lowHz;
            uint16_t highHz;
        } hd[] = {{"tick", 0, 320},       {"knock", 100, 200},  {"thump", 60, 160},   {"explosion", 50, 120},
                  {"engine", 80, 0},      {"heartbeat", 60, 0}, {"hit", 120, 240},    {"life-lost", 80, 160},
                  {"pickup", 0, 400},     {"shot", 150, 300},   {"rumble-strip", 90, 180}};
        for (const auto& entry : hd)
        {
            l.effects[entry.id].levels.lowHz = entry.lowHz;
            l.effects[entry.id].levels.highHz = entry.highHz;
        }
        return l;
    }();
    return library;
}

bool Number(const JsonValue& spec, const char* field, double& out, std::string& error, const std::string& where)
{
    const JsonValue* value = spec.Find(field);
    if (value == nullptr)
        return true;
    if (!value->IsNumber())
    {
        error = where + ": '" + field + "' is a number";
        return false;
    }
    out = value->AsNumber();
    return true;
}

bool Text(const JsonValue& spec, const char* field, std::string& out, std::string& error, const std::string& where)
{
    const JsonValue* value = spec.Find(field);
    if (value == nullptr)
        return true;
    if (!value->IsString())
    {
        error = where + ": '" + field + "' is a string";
        return false;
    }
    out = value->AsString();
    return true;
}

/// "0x5c3a", "23610", "#5C3A"
bool ParseNumberText(const std::string& text, unsigned long& out)
{
    if (text.empty())
        return false;
    const char* begin = text.c_str();
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        begin += 2;
        base = 16;
    }
    else if (text[0] == '#' || text[0] == '$')
    {
        begin += 1;
        base = 16;
    }
    char* end = nullptr;
    out = std::strtoul(begin, &end, base);
    return end != begin && *end == '\0';
}

bool ParseAddress(const JsonValue& spec, HapticSource& source, std::string& error, const std::string& where)
{
    const JsonValue* value = spec.Find("address");
    unsigned long address = 0;
    if (value != nullptr && value->IsNumber() && value->AsNumber() >= 0)
        address = static_cast<unsigned long>(value->AsNumber());
    else if (value != nullptr && value->IsString())
    {
        std::string text = value->AsString();
        if (text.rfind("page:", 0) == 0)
        {
            const size_t colon = text.find(':', 5);
            unsigned long page = 0;
            if (colon == std::string::npos || !ParseNumberText(text.substr(5, colon - 5), page) || page > 0xFFFE)
            {
                error = where + ": 'address' page:<n>:<addr> expected";
                return false;
            }
            source.page = static_cast<uint16_t>(page);
            text = text.substr(colon + 1);
        }
        if (!ParseNumberText(text, address))
        {
            error = where + ": 'address' '" + value->AsString() + "' is not a number";
            return false;
        }
    }
    else
    {
        error = where + ": 'address' is required";
        return false;
    }
    if (address > 0xFFFF)
    {
        error = where + ": 'address' is above #FFFF";
        return false;
    }
    source.address = static_cast<uint16_t>(source.page != 0xFFFF ? (address & 0x3FFF) : address);
    return true;
}

bool ParseByte(const JsonValue& spec, const char* field, uint8_t& out, std::string& error, const std::string& where)
{
    const JsonValue* value = spec.Find(field);
    if (value == nullptr)
        return true;
    unsigned long number = 0;
    if (value->IsNumber() && value->AsNumber() >= 0)
        number = static_cast<unsigned long>(value->AsNumber());
    else if (!value->IsString() || !ParseNumberText(value->AsString(), number))
    {
        error = where + ": '" + field + "' is a byte";
        return false;
    }
    if (number > 0xFF)
    {
        error = where + ": '" + field + "' is a byte";
        return false;
    }
    out = static_cast<uint8_t>(number);
    return true;
}

bool ParseWhen(const std::string& text, HapticWhen& out)
{
    static const std::pair<const char*, HapticWhen> kWhen[] = {
        {"changed", HapticWhen::Changed}, {"decreased", HapticWhen::Decreased}, {"increased", HapticWhen::Increased},
        {"==", HapticWhen::Equal},        {"!=", HapticWhen::NotEqual},         {"<", HapticWhen::Less},
        {">", HapticWhen::Greater}};
    for (const auto& [name, when] : kWhen)
    {
        if (text == name)
        {
            out = when;
            return true;
        }
    }
    return false;
}

/// "0xFE", 254 -> a 16-bit number
bool ParseWord(const JsonValue& spec, const char* field, uint16_t& out, std::string& error, const std::string& where)
{
    const JsonValue* value = spec.Find(field);
    if (value == nullptr)
        return true;
    unsigned long number = 0;
    if (value->IsNumber() && value->AsNumber() >= 0)
        number = static_cast<unsigned long>(value->AsNumber());
    else if (!value->IsString() || !ParseNumberText(value->AsString(), number))
    {
        error = where + ": '" + field + "' is a number";
        return false;
    }
    if (number > 0xFFFF)
    {
        error = where + ": '" + field + "' is above #FFFF";
        return false;
    }
    out = static_cast<uint16_t>(number);
    return true;
}

/// port-in / port-out: `port` (required), `port_mask`, and for port-out `value` / `mask` on the byte written
bool ParsePortProbe(const JsonValue& spec, HapticSource& source, std::string& error, const std::string& where)
{
    if (spec.Find("port") == nullptr)
    {
        error = where + ": 'port' is required";
        return false;
    }
    if (!ParseWord(spec, "port", source.port, error, where) ||
        !ParseWord(spec, "port_mask", source.portMask, error, where))
        return false;
    source.hasValue = spec.Find("value") != nullptr;
    if (source.hasValue && source.kind != HapticSourceKind::PortOut)
    {
        error = where + ": 'value' tests the byte an OUT writes: port-out only";
        return false;
    }
    return ParseByte(spec, "value", source.value, error, where) && ParseByte(spec, "mask", source.mask, error, where);
}

/// envelope / lowband / onset: the stream and the filter's numbers, each in its range
bool ParseStreamFilter(const JsonValue& spec, const std::string& stream, HapticSource& source, std::string& error,
                       const std::string& where)
{
    if (!stream.empty() && !ParseHapticStream(stream, source.stream))
    {
        error = where + ": 'source' '" + stream +
                "' unknown (known: mix, beeper, ay:0..ay:2, covox, soundrive, gs, neogs, next-dac)";
        return false;
    }
    struct Field
    {
        const char* name;
        float* out;
        double min, max;
    };
    const Field fields[] = {{"attack_ms", &source.attackMs, 0.0, 1000.0}, {"release_ms", &source.releaseMs, 0.0, 5000.0},
                            {"low_hz", &source.lowHz, 1.0, 1000.0},      {"high_hz", &source.highHz, 10.0, 3500.0},
                            {"rise_db", &source.riseDb, 1.0, 60.0},      {"within_ms", &source.withinMs, 1.0, 250.0},
                            {"floor", &source.floor, 0.0, 1.0},          {"scale", &source.scale, 0.0, 16.0}};
    for (const Field& field : fields)
    {
        double value = *field.out;
        if (!Number(spec, field.name, value, error, where))
            return false;
        if (value < field.min || value > field.max)
        {
            error = where + ": '" + field.name + "' is " + std::to_string(static_cast<int>(field.min)) + ".." +
                    std::to_string(static_cast<int>(field.max));
            return false;
        }
        *field.out = static_cast<float>(value);
    }
    if (source.kind == HapticSourceKind::Lowband && source.lowHz >= source.highHz)
    {
        error = where + ": 'low_hz' is below 'high_hz'";
        return false;
    }
    if (source.kind == HapticSourceKind::Onset && spec.Find("release_ms") == nullptr)
        source.releaseMs = 50.0f;
    return true;
}

bool ParseSource(const std::string& name, const JsonValue& spec, HapticSource& source, std::string& error)
{
    const std::string where = "haptics.sources." + name;
    if (!spec.IsObject())
    {
        error = where + ": an object expected";
        return false;
    }
    source.name = name;
    std::string filter, probe;
    if (!Text(spec, "filter", filter, error, where) || !Text(spec, "probe", probe, error, where))
        return false;
    if (filter.empty() == probe.empty())
    {
        error = where + ": one of 'filter' or 'probe'";
        return false;
    }
    if (!filter.empty())
    {
        if (filter == "ay-noise")
            source.kind = HapticSourceKind::AyNoise;
        else if (filter == "ay-envelope")
            source.kind = HapticSourceKind::AyEnvelope;
        else if (filter == "beeper-density")
            source.kind = HapticSourceKind::BeeperDensity;
        else if (filter == "envelope")
            source.kind = HapticSourceKind::Envelope;
        else if (filter == "lowband")
            source.kind = HapticSourceKind::Lowband;
        else if (filter == "onset")
            source.kind = HapticSourceKind::Onset;
        else
        {
            error = where + ": filter '" + filter +
                    "' unknown (known: ay-noise, ay-envelope, beeper-density, envelope, lowband, onset)";
            return false;
        }
        std::string chip;
        if (!Text(spec, "source", chip, error, where))
            return false;
        if (source.IsStream())
            return ParseStreamFilter(spec, chip, source, error, where);
        if (!chip.empty())
        {
            unsigned long index = 0;
            if (chip.rfind("ay:", 0) != 0 || !ParseNumberText(chip.substr(3), index) || index > 2)
            {
                error = where + ": 'source' is ay:0..ay:2";
                return false;
            }
            source.chip = static_cast<unsigned>(index);
            source.anyChip = false;
        }
        double rate = source.minRateKhz;
        if (!Number(spec, "min_rate_khz", rate, error, where))
            return false;
        source.minRateKhz = static_cast<float>(rate);
        return true;
    }
    if (probe == "write")
        source.kind = HapticSourceKind::Write;
    else if (probe == "value")
        source.kind = HapticSourceKind::Value;
    else if (probe == "frame")
        source.kind = HapticSourceKind::Frame;
    else if (probe == "exec")
        source.kind = HapticSourceKind::Exec;
    else if (probe == "read")
        source.kind = HapticSourceKind::Read;
    else if (probe == "port-in")
        source.kind = HapticSourceKind::PortIn;
    else if (probe == "port-out")
        source.kind = HapticSourceKind::PortOut;
    else
    {
        error = where + ": probe '" + probe + "' unknown (known: write, value, frame, exec, read, port-in, port-out)";
        return false;
    }
    if (source.kind == HapticSourceKind::PortIn || source.kind == HapticSourceKind::PortOut)
        return ParsePortProbe(spec, source, error, where);
    if (const JsonValue* each = spec.Find("each_write"))
    {
        if (!each->IsBool() || source.kind != HapticSourceKind::Write)
        {
            error = where + ": 'each_write' is true or false, on a write probe";
            return false;
        }
        source.eachWrite = each->AsBool();
    }
    if (const JsonValue* word = spec.Find("word"))
    {
        if (!word->IsBool() || source.kind != HapticSourceKind::Value)
        {
            error = where + ": 'word' is true or false, on a value probe";
            return false;
        }
        source.word = word->AsBool();
    }
    if (!ParseAddress(spec, source, error, where))
        return false;
    if (source.word && (source.page != 0xFFFF ? source.address == 0x3FFF : source.address == 0xFFFF))
    {
        error = where + ": a 'word' needs its second byte in the same page";
        return false;
    }
    std::string when;
    if (!Text(spec, "when", when, error, where))
        return false;
    if (!when.empty() && !ParseWhen(when, source.when))
    {
        error = where + ": 'when' is changed, decreased, increased, ==, !=, < or >";
        return false;
    }
    double scale = source.scale;
    if (!ParseByte(spec, "value", source.value, error, where) || !ParseByte(spec, "mask", source.mask, error, where) ||
        !Number(spec, "scale", scale, error, where))
        return false;
    source.hasValue = spec.Find("value") != nullptr;
    source.scale = static_cast<float>(scale);
    return true;
}

bool ParseOneTarget(const std::string& first, uint8_t& out)
{
    static const std::pair<const char*, uint8_t> kTargets[] = {{"handle.low", kHandleLow},
                                                               {"handle.high", kHandleHigh},
                                                               {"trigger.left", kTriggerLeft},
                                                               {"trigger.right", kTriggerRight},
                                                               {"all", kTargetAll}};
    for (const auto& [name, bit] : kTargets)
    {
        if (first == name)
        {
            out = bit;
            return true;
        }
    }
    return false;
}

/// `trigger.right|handle.high`: the first alternative and the fallbacks for devices without it
bool ParseTarget(const std::string& text, HapticRule& rule)
{
    size_t begin = 0;
    bool first = true;
    while (begin <= text.size())
    {
        const size_t bar = text.find('|', begin);
        const std::string part = text.substr(begin, bar == std::string::npos ? std::string::npos : bar - begin);
        uint8_t bit = 0;
        if (!ParseOneTarget(part, bit))
            return false;
        if (first)
            rule.target = bit;
        else
            rule.fallbacks.push_back(bit);
        first = false;
        if (bar == std::string::npos)
            break;
        begin = bar + 1;
    }
    return true;
}
}  // namespace

bool ParseHapticStream(const std::string& text, HapticStream& out)
{
    static const std::pair<const char*, HapticStream> kStreams[] = {
        {"mix", HapticStream::Mix},     {"beeper", HapticStream::Beeper}, {"ay:0", HapticStream::Ay0},
        {"ay:1", HapticStream::Ay1},    {"ay:2", HapticStream::Ay2},      {"covox", HapticStream::Covox},
        {"soundrive", HapticStream::Covox}, {"gs", HapticStream::Gs},     {"neogs", HapticStream::Gs},
        {"next-dac", HapticStream::NextDac}};
    for (const auto& [name, stream] : kStreams)
    {
        if (text == name)
        {
            out = stream;
            return true;
        }
    }
    return false;
}

const char* HapticStreamName(HapticStream stream)
{
    switch (stream)
    {
        case HapticStream::Mix:
            return "mix";
        case HapticStream::Beeper:
            return "beeper";
        case HapticStream::Ay0:
            return "ay:0";
        case HapticStream::Ay1:
            return "ay:1";
        case HapticStream::Ay2:
            return "ay:2";
        case HapticStream::Covox:
            return "covox";
        case HapticStream::Gs:
            return "gs";
        case HapticStream::NextDac:
            return "next-dac";
    }
    return "mix";
}

bool AddHapticProbeSource(JsonValue& haptics, const HapticProbe& probe, const std::string& play, std::string& name,
                          std::string& error)
{
    JsonValue section = haptics.IsObject() ? haptics : JsonValue::Object();
    JsonValue sources = section.Find("sources") != nullptr ? *section.Find("sources") : JsonValue::Object();
    JsonValue rules = section.Find("rules") != nullptr ? *section.Find("rules") : JsonValue::Array();
    if (!sources.IsObject() || !rules.IsArray())
    {
        error = "the haptics section's 'sources' is not an object or its 'rules' not a list";
        return false;
    }
    for (unsigned index = 1;; index++)
    {
        name = "probe-" + std::to_string(index);
        if (sources.Find(name) == nullptr)
            break;
    }
    auto hex = [](unsigned value) {
        char text[8];
        std::snprintf(text, sizeof(text), "0x%04X", value & 0xFFFF);
        return std::string(text);
    };
    JsonValue source = JsonValue::Object();
    static const char* const kKinds[] = {"exec", "read", "write", "port-in", "port-out"};
    source.Set("probe", JsonValue::String(kKinds[static_cast<size_t>(probe.kind)]));
    if (probe.kind == HapticProbeKind::PortIn || probe.kind == HapticProbeKind::PortOut)
    {
        source.Set("port", JsonValue::String(hex(probe.address)));
        if (probe.portMask != 0xFFFF)
            source.Set("port_mask", JsonValue::String(hex(probe.portMask)));
    }
    else
    {
        source.Set("address", JsonValue::String(probe.page == 0xFFFF ? hex(probe.address)
                                                                     : "page:" + std::to_string(probe.page) + ":" +
                                                                           hex(probe.address & 0x3FFF)));
        if (probe.kind == HapticProbeKind::Write)
        {
            source.Set("when", JsonValue::String("changed"));
            source.Set("each_write", JsonValue::Bool(true));
        }
    }
    sources.Set(name, std::move(source));
    std::vector<JsonValue> items = rules.Items();
    items.push_back(JsonValue::Object({{"on", JsonValue::String(name)}, {"play", JsonValue::String(play)}}));
    section.Set("sources", std::move(sources));
    section.Set("rules", JsonValue::Array(std::move(items)));
    HapticProfile checked;
    if (!ParseHapticProfile(section, checked, error))
        return false;
    haptics = std::move(section);
    return true;
}

HapticProbe HapticSource::Probe() const
{
    HapticProbe probe;
    switch (kind)
    {
        case HapticSourceKind::Read:
            probe.kind = HapticProbeKind::Read;
            break;
        case HapticSourceKind::Write:
            probe.kind = HapticProbeKind::Write;
            break;
        case HapticSourceKind::PortIn:
            probe.kind = HapticProbeKind::PortIn;
            break;
        case HapticSourceKind::PortOut:
            probe.kind = HapticProbeKind::PortOut;
            break;
        default:
            probe.kind = HapticProbeKind::Exec;
            break;
    }
    if (probe.kind == HapticProbeKind::PortIn || probe.kind == HapticProbeKind::PortOut)
    {
        probe.address = probe.end = port;
        probe.portMask = portMask;
    }
    else
    {
        probe.address = probe.end = address;
        probe.page = page;
    }
    return probe;
}

uint8_t HapticRule::TargetFor(uint8_t actuators) const
{
    if (target == 0 || (target & actuators) == target)
        return target;
    for (uint8_t fallback : fallbacks)
        if ((fallback & actuators) == fallback)
            return fallback;
    return fallbacks.empty() ? target : fallbacks.back();
}

const HapticSource* HapticProfile::Find(const std::string& name) const
{
    for (const HapticSource& source : sources)
        if (source.name == name)
            return &source;
    return nullptr;
}

bool ParseHapticProfile(const JsonValue& section, HapticProfile& out, std::string& error)
{
    if (!section.IsObject())
    {
        error = "haptics: an object expected";
        return false;
    }
    HapticProfile profile;
    for (const auto& [field, value] : section.GetMembers())
    {
        if (field != "sources" && field != "rules")
        {
            error = "haptics: unknown field '" + field + "' (known: sources, rules)";
            return false;
        }
    }
    if (const JsonValue* sources = section.Find("sources"))
    {
        if (!sources->IsObject())
        {
            error = "haptics.sources: an object expected";
            return false;
        }
        for (const auto& [name, spec] : sources->GetMembers())
        {
            HapticSource source;
            if (!ParseSource(name, spec, source, error))
                return false;
            profile.sources.push_back(std::move(source));
        }
    }
    if (const JsonValue* rules = section.Find("rules"))
    {
        if (!rules->IsArray())
        {
            error = "haptics.rules: a list expected";
            return false;
        }
        for (size_t index = 0; index < rules->Items().size(); index++)
        {
            const JsonValue& spec = rules->Items()[index];
            const std::string where = "haptics.rules[" + std::to_string(index) + "]";
            if (!spec.IsObject())
            {
                error = where + ": an object expected";
                return false;
            }
            HapticRule rule;
            std::string target;
            double scale = 1.0;
            double priority = 0;
            if (!Text(spec, "on", rule.on, error, where) || !Text(spec, "play", rule.play, error, where) ||
                !Text(spec, "target", target, error, where) || !Number(spec, "scale", scale, error, where) ||
                !Number(spec, "priority", priority, error, where))
                return false;
            if (priority < 0 || priority > 9)
            {
                error = where + ": 'priority' is 0..9";
                return false;
            }
            rule.priority = static_cast<uint8_t>(priority);
            if (const JsonValue* hd = spec.Find("hd"))
            {
                if (!hd->IsObject())
                {
                    error = where + ": 'hd' is an object ({frequency_hz, low_hz, high_hz})";
                    return false;
                }
                for (const auto& [field, value] : hd->GetMembers())
                {
                    if (field != "frequency_hz" && field != "low_hz" && field != "high_hz")
                    {
                        error = where + ".hd: unknown field '" + field + "' (frequency_hz, low_hz, high_hz)";
                        return false;
                    }
                    if (!value.IsNumber() || value.AsNumber() < 1 || value.AsNumber() > 2000)
                    {
                        error = where + ".hd." + field + ": 1..2000 Hz";
                        return false;
                    }
                }
                double frequency = 0;
                double lowHz = 0;
                double highHz = 0;
                Number(*hd, "frequency_hz", frequency, error, where);
                Number(*hd, "low_hz", lowHz, error, where);
                Number(*hd, "high_hz", highHz, error, where);
                rule.lowHz = static_cast<uint16_t>(lowHz != 0 ? lowHz : frequency);
                rule.highHz = static_cast<uint16_t>(highHz != 0 ? highHz : frequency);
            }
            if (profile.Find(rule.on) == nullptr)
            {
                error = where + ": 'on' names no source ('" + rule.on + "')";
                return false;
            }
            HapticEffect effect;
            if (!BuiltinHapticEffect(rule.play, effect))
            {
                std::string known;
                for (const std::string& id : BuiltinHapticEffectIds())
                    known += (known.empty() ? "" : ", ") + id;
                error = where + ": 'play' '" + rule.play + "' unknown (known: " + known + ")";
                return false;
            }
            if (!target.empty() && !ParseTarget(target, rule))
            {
                error = where + ": 'target' '" + target + "' unknown";
                return false;
            }
            if (scale < 0 || scale > 4)
            {
                error = where + ": 'scale' is 0..4";
                return false;
            }
            rule.scale = static_cast<float>(scale);
            profile.rules.push_back(std::move(rule));
        }
    }
    out = std::move(profile);
    return true;
}

bool BuiltinHapticEffect(const std::string& id, HapticEffect& out)
{
    const auto& effects = TheLibrary().effects;
    auto it = effects.find(id);
    if (it == effects.end())
        return false;
    out = it->second;
    return true;
}

const std::vector<std::string>& BuiltinHapticEffectIds()
{
    return TheLibrary().ids;
}

RumbleLevels RetargetLevels(const RumbleLevels& levels, uint8_t target)
{
    if (target == 0)
        return levels;
    const float peak = std::max(levels.low, levels.high);
    RumbleLevels out;
    out.low = (target & kHandleLow) ? (target == kTargetAll ? levels.low : peak) : 0.0f;
    out.high = (target & kHandleHigh) ? (target == kTargetAll ? levels.high : peak) : 0.0f;
    out.leftTrigger = (target & kTriggerLeft) ? peak : 0.0f;
    out.rightTrigger = (target & kTriggerRight) ? peak : 0.0f;
    // A band that takes over the other's level takes its frequency too when it has none of its own
    out.lowHz = out.low > 0 ? (levels.lowHz != 0 ? levels.lowHz : levels.highHz) : 0;
    out.highHz = out.high > 0 ? (levels.highHz != 0 ? levels.highHz : levels.lowHz) : 0;
    return out;
}

}  // namespace controllers
