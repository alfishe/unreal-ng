#include "stdafx.h"

#include "sprinterbios.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

#include "common/filehelper.h"
#include "emulator/platform.h"

namespace SprinterBios
{

const std::vector<Image>& Known()
{
    static const std::vector<Image> kKnown = {
        {"sp2k-3.04.rom", "3.04", "Sprinter BIOS 3.04 (Peters Plus, 17.06.2003; DSS 1.62, not 1.71)", 0x1729CB5Cu},
        {"sp2k-3.06-hf2.rom", "3.06", "Firmware v3.06 Hotfix 2 (community build, 19.01.2026; the MAME pack's firmware)", 0x9AA7BB29u},
        {"sp2k-3.07-beta1.rom", "3.07", "Firmware v3.07 BETA 1 (community build, 24.09.2026; the default)", 0xA06A1A02u},
    };
    return kKnown;
}

namespace
{
std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// As ROM::LoadROM finds a ROM: as given, then beside the executable, then in the resources folder
bool Exists(const std::string& path)
{
    if (FileHelper::FileExists(FileHelper::NormalizePath(path)))
        return true;
    for (const std::string& base : {FileHelper::GetExecutablePath(), FileHelper::GetResourcesPath()})
        if (!base.empty() && FileHelper::FileExists(FileHelper::PathCombine(base, path)))
            return true;
    return false;
}

bool Flag(const std::string& text, const char* name, int& out, std::string& error)
{
    const std::string v = Lower(text);
    if (v.empty())
        out = -1;
    else if (v == "1" || v == "on" || v == "true" || v == "yes")
        out = 1;
    else if (v == "0" || v == "off" || v == "false" || v == "no")
        out = 0;
    else
    {
        error = std::string(name) + " must be 0 / 1 (on / off, true / false)";
        return false;
    }
    return true;
}
}  // namespace

bool Resolve(const std::string& name, std::string& path, std::string& error)
{
    const std::string wanted = Lower(name);
    for (const Image& image : Known())
    {
        const std::string file = image.file;
        if (wanted == file || wanted == Lower(image.alias) || wanted == "rom/sprinter/" + file)
        {
            path = "rom/sprinter/" + file;
            if (!Exists(path))
            {
                error = "the image " + path + " is not installed (data/rom/sprinter)";
                return false;
            }
            return true;
        }
    }
    if (!name.empty() && Exists(name))
    {
        path = name;
        return true;
    }
    std::string known;
    for (const Image& image : Known())
        known += (known.empty() ? "" : ", ") + std::string(image.alias) + " (" + image.file + ")";
    error = "unknown BIOS '" + name + "': " + known + ", or the path of a 256 KB image";
    return false;
}

bool OptionsFromStrings(const std::string& bios, const std::string& fastStart, const std::string& accelIntSuspend,
                        const std::string& reset, Options& options, std::string& error)
{
    options = Options();
    options.bios = bios;
    int doReset = -1;
    if (!Flag(fastStart, "fast_start", options.fastStart, error) ||
        !Flag(accelIntSuspend, "accel_int_suspend", options.accelIntSuspend, error) || !Flag(reset, "reset", doReset, error))
        return false;
    options.reset = doReset != 0;
    return true;
}

bool ApplyToConfig(CONFIG& config, const Options& options, std::string& error)
{
    if (!options.bios.empty())
    {
        std::string path;
        if (!Resolve(options.bios, path, error))
            return false;
        std::strncpy(config.sprinter_rom_path, path.c_str(), sizeof config.sprinter_rom_path - 1);
        config.sprinter_rom_path[sizeof config.sprinter_rom_path - 1] = '\0';
    }
    if (options.fastStart >= 0)
        config.sprinter.fast_start = static_cast<uint8_t>(options.fastStart);
    if (options.accelIntSuspend >= 0)
        config.sprinter.accel_int_suspend = static_cast<uint8_t>(options.accelIntSuspend);
    return true;
}

std::function<void(CONFIG&)> CreateOverride(const Options& options)
{
    return [options](CONFIG& config) {
        std::string error;
        ApplyToConfig(config, options, error);  // the caller resolved the name already
    };
}

uint32_t Crc32(const uint8_t* data, size_t size)
{
    static const std::array<uint32_t, 256> kTable = [] {
        std::array<uint32_t, 256> table{};
        for (uint32_t i = 0; i < 256; i++)
        {
            uint32_t c = i;
            for (int bit = 0; bit < 8; bit++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        return table;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++)
        crc = kTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

}  // namespace SprinterBios
