#include "devicestatus.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "hid/hidrawnode.h"

namespace controllers
{

namespace
{
namespace fs = std::filesystem;

std::string Trimmed(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

/// The whole file, "" when it cannot be read
std::string ReadText(const fs::path& path)
{
    std::ifstream file(path);
    if (!file)
        return {};
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

/// A non-negative decimal integer of at most 9 digits; -1 otherwise
int Decimal(const std::string& text)
{
    if (text.empty() || text.size() > 9)
        return -1;
    int value = 0;
    for (const char c : text)
    {
        if (c < '0' || c > '9')
            return -1;
        value = value * 10 + (c - '0');
    }
    return value;
}

int Percent(float level)
{
    const float clamped = level < 0 ? 0 : level > 1 ? 1 : level;
    return static_cast<int>(clamped * 100.0f + 0.5f);
}

/// The LEDs under @p hidDir, sorted by name (deterministic on any file system)
std::vector<fs::path> LedDirs(const std::string& hidDir)
{
    std::vector<fs::path> dirs;
    std::error_code error;
    for (fs::directory_iterator it(fs::path(hidDir) / "leds", error), end; !error && it != end; it.increment(error))
        dirs.push_back(it->path());
    std::sort(dirs.begin(), dirs.end());
    return dirs;
}

int MaxBrightness(const fs::path& led)
{
    const int max = Decimal(Trimmed(ReadText(led / "max_brightness")));
    return max > 0 ? max : 255;
}

int Scaled(uint8_t value, int max)
{
    return (value * max + 127) / 255;
}
}  // namespace

BatteryState BatteryFromGameController(float level, int state)
{
    BatteryState battery;
    switch (state)
    {
        case 0:  // discharging
            battery.known = true;
            battery.level = Percent(level);
            break;
        case 1:  // charging
            battery.known = true;
            battery.level = Percent(level);
            battery.charging = true;
            battery.wired = true;
            break;
        case 2:  // full
            battery.known = true;
            battery.level = level > 0 ? Percent(level) : 100;
            battery.wired = true;
            break;
        default:  // unknown: a level is still worth showing when GameController gives one
            if (level > 0)
            {
                battery.known = true;
                battery.level = Percent(level);
            }
            break;
    }
    return battery;
}

BatteryState BatteryFromXInput(uint8_t type, uint8_t level)
{
    BatteryState battery;
    if (type == 1)
    {
        battery.known = true;
        battery.wired = true;
    }
    else if (type == 2 || type == 3)
    {
        static const int steps[] = {0, 33, 67, 100};
        battery.known = true;
        battery.level = steps[level < 4 ? level : 3];
        battery.coarse = true;
    }
    return battery;
}

BatteryState BatteryFromPowerSupply(const std::string& status, const std::string& capacity,
                                    const std::string& capacityLevel)
{
    BatteryState battery;
    const std::string state = Trimmed(status);
    if (state == "Charging")
        battery.charging = battery.wired = battery.known = true;
    else if (state == "Full" || state == "Not charging")
        battery.wired = battery.known = true;
    else if (state == "Discharging")
        battery.known = true;
    const int percent = Decimal(Trimmed(capacity));
    if (percent >= 0 && percent <= 100)
    {
        battery.level = percent;
        battery.known = true;
        return battery;
    }
    const std::string step = Trimmed(capacityLevel);
    static const std::pair<const char*, int> steps[] = {
        {"Critical", 5}, {"Low", 20}, {"Normal", 50}, {"High", 80}, {"Full", 100}};
    for (const auto& [name, value] : steps)
        if (step == name)
        {
            battery.level = value;
            battery.coarse = true;
            battery.known = true;
            return battery;
        }
    if (state == "Full")
        battery.level = 100;
    return battery;
}

std::string HidDeviceDir(const std::string& sysRoot, const std::string& eventName)
{
    std::error_code error;
    // eventN/device is the input device (inputM), its device the HID device (0005:054C:0CE6.0004) for a HID driver
    const fs::path hid = fs::canonical(fs::path(sysRoot) / "class" / "input" / eventName / "device" / "device", error);
    if (error)
        return {};
    HidUevent uevent;
    if (!ParseHidUevent(ReadText(hid / "uevent"), uevent))
        return {};  // xpad's USB interface, a platform device: no HID device, no power supply or light of its own
    return hid.string();
}

std::string FindPowerSupply(const std::string& hidDir)
{
    std::vector<fs::path> batteries;
    std::error_code error;
    for (fs::directory_iterator it(fs::path(hidDir) / "power_supply", error), end; !error && it != end;
         it.increment(error))
    {
        const std::string type = Trimmed(ReadText(it->path() / "type"));
        if (type.empty() || type == "Battery")
            batteries.push_back(it->path());
    }
    if (batteries.empty())
        return {};
    std::sort(batteries.begin(), batteries.end());
    // scope Device: the device's own battery, not the system's it may report through as well
    for (const fs::path& battery : batteries)
        if (Trimmed(ReadText(battery / "scope")) == "Device")
            return battery.string();
    return batteries.front().string();
}

BatteryState ReadPowerSupply(const std::string& dir)
{
    if (dir.empty())
        return {};
    const fs::path path(dir);
    if (Trimmed(ReadText(path / "present")) == "0")
        return {};
    return BatteryFromPowerSupply(ReadText(path / "status"), ReadText(path / "capacity"),
                                  ReadText(path / "capacity_level"));
}

std::vector<std::string> SysfsLight::Files() const
{
    std::vector<std::string> files;
    if (kind == Kind::Multicolor)
    {
        files.push_back((fs::path(multicolor) / "multi_intensity").string());
        files.push_back((fs::path(multicolor) / "brightness").string());
    }
    else if (kind == Kind::Channels)
        for (const std::string& channel : channels)
            files.push_back((fs::path(channel) / "brightness").string());
    return files;
}

SysfsLight FindLight(const std::string& hidDir)
{
    SysfsLight light;
    const std::vector<fs::path> leds = LedDirs(hidDir);
    for (const fs::path& led : leds)
    {
        std::istringstream names(ReadText(led / "multi_index"));
        std::vector<std::string> index;
        for (std::string name; names >> name;)
            index.push_back(name);
        const auto has = [&index](const char* name) {
            return std::find(index.begin(), index.end(), name) != index.end();
        };
        if (has("red") && has("green") && has("blue"))
        {
            light.kind = SysfsLight::Kind::Multicolor;
            light.multicolor = led.string();
            light.index = index;
            light.maxBrightness = MaxBrightness(led);
            return light;
        }
    }
    // One LED per colour: "<prefix>:red", "<prefix>:green", "<prefix>:blue" of one prefix
    static const char* const colours[] = {":red", ":green", ":blue"};
    for (const fs::path& led : leds)
    {
        const std::string name = led.filename().string();
        const std::string suffix = colours[0];
        if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        const std::string prefix = name.substr(0, name.size() - suffix.size());
        SysfsLight found;
        found.kind = SysfsLight::Kind::Channels;
        bool all = true;
        for (size_t index = 0; index < 3 && all; index++)
        {
            const fs::path channel = led.parent_path() / (prefix + colours[index]);
            std::error_code error;
            all = fs::is_directory(channel, error);
            found.channels[index] = channel.string();
            found.channelMax[index] = MaxBrightness(channel);
        }
        if (all)
            return found;
    }
    return light;
}

std::vector<std::pair<std::string, std::string>> LightWrites(const SysfsLight& light, const LightColor& color)
{
    std::vector<std::pair<std::string, std::string>> writes;
    if (light.kind == SysfsLight::Kind::Multicolor)
    {
        std::string intensity;
        for (const std::string& name : light.index)
        {
            uint8_t value = 0;  // a channel of another colour (white) stays dark
            if (name == "red")
                value = color.red;
            else if (name == "green")
                value = color.green;
            else if (name == "blue")
                value = color.blue;
            intensity += (intensity.empty() ? "" : " ") + std::to_string(Scaled(value, light.maxBrightness));
        }
        const bool off = color == LightColor{};
        writes.emplace_back((fs::path(light.multicolor) / "multi_intensity").string(), intensity);
        writes.emplace_back((fs::path(light.multicolor) / "brightness").string(),
                            std::to_string(off ? 0 : light.maxBrightness));
    }
    else if (light.kind == SysfsLight::Kind::Channels)
    {
        const uint8_t values[] = {color.red, color.green, color.blue};
        for (size_t index = 0; index < 3; index++)
            writes.emplace_back((fs::path(light.channels[index]) / "brightness").string(),
                                std::to_string(Scaled(values[index], light.channelMax[index])));
    }
    return writes;
}

std::string SysfsLightUnavailable(const SysfsLight& light, const std::string& file)
{
    if (light.kind == SysfsLight::Kind::None)
        return "The device's driver exposes no colour light (LED class) to Linux";
    return "Setting the light needs write access to " + file + ", which this user does not have. " +
           kHidrawRulesHint;
}

}  // namespace controllers
