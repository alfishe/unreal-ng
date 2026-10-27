#include "slotconfig.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>

namespace
{

std::string Trim(std::string_view text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
    {
        begin++;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
    {
        end--;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// "ay-socket" or "<bus>.<n>" with n >= 1
bool IsSlotId(const std::string& id)
{
    if (id == "ay-socket")
    {
        return true;
    }
    const size_t dot = id.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == id.size())
    {
        return false;
    }
    for (size_t i = dot + 1; i < id.size(); i++)
    {
        if (!std::isdigit(static_cast<unsigned char>(id[i])))
        {
            return false;
        }
    }
    return id[dot + 1] != '0';
}

SlotConfigEntry& EntryFor(SlotConfig& config, const std::string& slot)
{
    for (SlotConfigEntry& entry : config.entries)
    {
        if (entry.slot == slot)
        {
            return entry;
        }
    }
    SlotConfigEntry entry;
    entry.slot = slot;
    entry.source = "[SLOTS] " + slot;
    config.entries.push_back(std::move(entry));
    return config.entries.back();
}

} // namespace

void ParseSlotsSection(const std::vector<std::pair<std::string, std::string>>& keyValues, SlotConfig& out)
{
    out.Clear();
    out.section = true;

    for (const auto& [rawKey, rawValue] : keyValues)
    {
        const std::string key = Lower(Trim(rawKey));
        const std::string value = Trim(rawValue);
        const std::string line = key + " = " + value;

        if (key.rfind("builtin.", 0) == 0)
        {
            const std::string id = key.substr(strlen("builtin."));
            const std::string v = Lower(value);
            const bool on = v == "on" || v == "1" || v == "yes" || v == "true";
            const bool off = v == "off" || v == "0" || v == "no" || v == "false";
            if (id.empty() || (!on && !off))
            {
                out.errors.push_back(line + ": builtin.<id> = on | off");
                continue;
            }
            out.builtIns.push_back({ id, on, "[SLOTS] " + key });
            continue;
        }

        if (IsSlotId(key))
        {
            if (value.empty())
            {
                out.errors.push_back(line + ": no card named");
                continue;
            }
            SlotConfigEntry& entry = EntryFor(out, key);
            if (!entry.card.empty())
            {
                out.errors.push_back(line + ": the slot is named twice; " + entry.card + " kept");
                continue;
            }
            entry.card = Lower(value);
            continue;
        }

        // <slot>.<option>: the slot id itself may contain a dot (zxbus.1), so split at the last one
        const size_t dot = key.rfind('.');
        const std::string slot = dot == std::string::npos ? std::string() : key.substr(0, dot);
        if (dot == std::string::npos || !IsSlotId(slot))
        {
            out.errors.push_back(line + ": not a slot (ay-socket, <bus>.<n>), a slot option or builtin.<id>");
            continue;
        }
        // Option names keep their case (gsRam, ctrlMask): the original key's tail
        const std::string trimmedKey = Trim(rawKey);
        const std::string option = trimmedKey.substr(trimmedKey.rfind('.') + 1);
        SlotConfigEntry& entry = EntryFor(out, slot);
        const std::string optionLower = Lower(option);
        if (optionLower == "adapter")
        {
            entry.adapter = Lower(value);
        }
        else if (optionLower == "fit")
        {
            const std::string v = Lower(value);
            if (v != "unrealistic" && v != "real")
            {
                out.errors.push_back(line + ": fit = real | unrealistic");
                continue;
            }
            entry.fitOverride = v == "unrealistic";
        }
        else
        {
            if (!entry.options.empty())
            {
                entry.options += ' ';
            }
            entry.options += option + "=" + value;
        }
    }

    // Options or an adapter without a card: nothing to plug
    for (auto it = out.entries.begin(); it != out.entries.end();)
    {
        if (it->card.empty())
        {
            out.errors.push_back(it->slot + ": options without a card");
            it = out.entries.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

std::string FormatSlotsSection(const SlotConfig& config)
{
    std::string text = "[SLOTS]\n";
    for (const SlotConfigEntry& entry : config.entries)
    {
        text += entry.slot + " = " + entry.card + "\n";
        if (!entry.adapter.empty())
        {
            text += entry.slot + ".adapter = " + entry.adapter + "\n";
        }
        if (entry.fitOverride)
        {
            text += entry.slot + ".fit = unrealistic\n";
        }
        std::string_view options = entry.options;
        while (!options.empty())
        {
            const size_t space = options.find(' ');
            // An explicit length: substr(0, npos) makes gcc 13 warn about an unbounded memchr in the find below
            const std::string_view pair = options.substr(0, space == std::string_view::npos ? options.size() : space);
            const size_t eq = pair.find('=');
            if (eq != std::string_view::npos)
            {
                text += entry.slot + "." + std::string(pair.substr(0, eq)) + " = " + std::string(pair.substr(eq + 1)) +
                        "\n";
            }
            options = space == std::string_view::npos ? std::string_view() : options.substr(space + 1);
        }
    }
    for (const SlotBuiltInSwitch& builtIn : config.builtIns)
    {
        text += "builtin." + builtIn.id + " = " + (builtIn.on ? "on" : "off") + "\n";
    }
    return text;
}
