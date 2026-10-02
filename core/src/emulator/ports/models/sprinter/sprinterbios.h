#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct CONFIG;
class EmulatorContext;

/// @file sprinterbios.h
/// @brief The Sprinter's BIOS images and start options at runtime (Sprinter automation audit G11):
/// the images the project ships, which one is loaded (by CRC-32 of the flash) and which one the
/// configuration names, and the selection every interface uses - the create option
/// (`"sprinter": {"bios": ..}`), and on a running machine a selection applied at the next reset
/// (Emulator::RequestRomReload) or at once with a reset.
///
/// A name is a shipped file ("sp2k-3.06-hf2.rom"), its version alias ("3.04", "3.06", "3.07"),
/// "rom/sprinter/<file>", or the path of any other 256 KB image.
///
/// Worked example: select {bios: "3.07", fast_start: 0, reset: true} on a running SPRINTER sets
/// [ROM] SPRINTER = rom/sprinter/sp2k-3.07-beta1.rom and FastStart = 0 for this instance, reloads
/// the flash and resets: the PLD loader runs, then BIOS 3.07 (DSS 1.71 needs it).
namespace SprinterBios
{
struct Image
{
    const char* file;     ///< in rom/sprinter/
    const char* alias;    ///< short version name
    const char* version;  ///< what it is
    uint32_t crc32;       ///< of the 256 KB file
};

/// The shipped images (docs/inprogress/2026-09-28-sprinter/bios-versions.md)
const std::vector<Image>& Known();

/// A name -> the path [ROM] SPRINTER takes ("rom/sprinter/sp2k-3.06-hf2.rom" or the given path);
/// false with `error` when nothing matches or the file is missing
bool Resolve(const std::string& name, std::string& path, std::string& error);

/// What to change; -1 = leave as configured
struct Options
{
    std::string bios;          ///< empty = keep
    int fastStart = -1;        ///< [SPRINTER] FastStart: 1 skips the PLD loader
    int accelIntSuspend = -1;  ///< [SPRINTER] AccelIntSuspend: an INT acknowledge blocks accelerator operations
    bool reset = true;         ///< runtime: reset now (else at the next reset)
};

/// bios / fast_start / accel_int_suspend / reset as text ("1" / "0", on / off, true / false; empty = keep)
bool OptionsFromStrings(const std::string& bios, const std::string& fastStart, const std::string& accelIntSuspend,
                        const std::string& reset, Options& options, std::string& error);

/// The options written into a configuration (resolved BIOS path); false with `error`
bool ApplyToConfig(CONFIG& config, const Options& options, std::string& error);

/// For EmulatorManager::CreateEmulatorWithModel's configOverride (call after Resolve succeeded)
std::function<void(CONFIG&)> CreateOverride(const Options& options);

/// CRC-32 (IEEE) of a buffer
uint32_t Crc32(const uint8_t* data, size_t size);
}  // namespace SprinterBios
