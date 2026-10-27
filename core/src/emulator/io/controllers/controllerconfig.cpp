#include "controllerconfig.h"

#include <algorithm>
#include <cctype>

#include "common/inifile.h"
#include "controllercontrol.h"

namespace controllers
{

namespace
{
std::string Trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

std::string Lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// "sinclair-joystick, port=2" -> device and options; an item without '=' continues the option before it
bool ParseValue(const std::string& value, ControllerSetEntry& entry)
{
    std::string* last = nullptr;
    bool first = true;
    size_t start = 0;
    while (start <= value.size())
    {
        size_t end = value.find(',', start);
        if (end == std::string::npos)
            end = value.size();
        const std::string item = Trim(value.substr(start, end - start));
        start = end + 1;
        if (first)
        {
            entry.device = Lower(item);
            first = false;
            continue;
        }
        const size_t equals = item.find('=');
        if (equals == std::string::npos)
        {
            if (!last)
                return false;
            *last += "," + item;
            continue;
        }
        last = &entry.options[Lower(Trim(item.substr(0, equals)))];
        *last = Trim(item.substr(equals + 1));
    }
    return !entry.device.empty();
}

/// Unreal Speccy's [INPUT] Joystick= choices that are keyboard joysticks
bool LegacyJoystick(const std::string& value, ControllerSetEntry& entry)
{
    const std::string v = Lower(Trim(value));
    entry.options.clear();
    if (v == "sinclair1" || v == "sinclair2")
    {
        entry.device = "sinclair-joystick";
        entry.options["port"] = v == "sinclair1" ? "1" : "2";
    }
    else if (v == "cursor")
        entry.device = "cursor-joystick";
    else if (v == "qaop")
        entry.device = "keys-joystick";  // its default keys are QAOP + Space
    else
        return false;
    return true;
}
}  // namespace

std::vector<ControllerSetEntry> ControllerConfig::FromIni(const IniFile& ini, std::vector<std::string>* report)
{
    std::vector<ControllerSetEntry> entries;
    for (const auto& [rawKey, rawValue] : ini.GetSectionEntries("CONTROLLERS"))
    {
        ControllerSetEntry entry;
        entry.connector = Lower(Trim(rawKey));
        entry.where = "[CONTROLLERS] " + Trim(rawKey);
        if (entry.connector.empty())
            continue;
        if (!ParseValue(rawValue, entry))
        {
            if (report)
                report->push_back(entry.where + "='" + rawValue + "': expected <device>[, option=value ...]");
            continue;
        }
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&entry](const ControllerSetEntry& e) { return e.connector == entry.connector; }),
                      entries.end());
        entries.push_back(std::move(entry));
    }

    // Legacy: a keyboard joystick named by [INPUT] Joystick= goes into keyjoy.1 unless [CONTROLLERS] says otherwise
    if (const char* legacy = ini.GetValue("INPUT", "Joystick", nullptr))
    {
        ControllerSetEntry entry;
        entry.connector = "keyjoy.1";
        entry.where = std::string("[INPUT] Joystick=") + legacy;
        const bool named = std::any_of(entries.begin(), entries.end(),
                                       [](const ControllerSetEntry& e) { return e.connector == "keyjoy.1"; });
        if (!named && LegacyJoystick(legacy, entry))
            entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<std::string> ControllerConfig::Apply(EmulatorContext* context,
                                                 const std::vector<ControllerSetEntry>& entries)
{
    std::vector<std::string> problems;
    ControllerControl control(context);
    for (const ControllerSetEntry& entry : entries)
    {
        ControllerRequest request;
        request.selector = entry.connector;
        if (entry.device == "none")
            request.verb = "unplug";
        else
        {
            request.verb = "plug";
            request.device = entry.device;
            request.options = entry.options;
        }
        const ControllerReply reply = control.Execute(request);
        if (!reply.result.Ok())
            problems.push_back(entry.where + ": " + ControllerErrorCode(reply.result.error) + ": " +
                               reply.result.message);
    }
    return problems;
}

}  // namespace controllers
