#pragma once

#include <cstdint>
#include <string>
#include <vector>

class DiskImage;

/// Adds a virtual boot file to the in-memory TR-DOS disk image.
///
/// Modelled on Unreal Speccy FDD::addboot()/addfile(). The image is marked clean afterwards, so the
/// injected file never triggers "unsaved changes" - it is only saved together with the user's own changes
class TrdosBootInjector
{
public:
    static constexpr size_t HOBETA_HEADER_SIZE = 17;

    /// Inject a Hobeta file (17 byte header + data) as the disk's boot file. Fails (false) when the disk is
    /// not TR-DOS, already has "boot" (type B), the catalog is full or there is not enough free space
    static bool InjectHobeta(DiskImage& image, const std::vector<uint8_t>& hobeta);

    /// Inject a minimal generated BASIC boot: 1 RANDOMIZE USR 15619: REM : RUN "NAME"
    static bool InjectNamedBoot(DiskImage& image, const std::string& trimmedName);

    /// Build the generated boot as a Hobeta file (header + one sector). Empty when the name cannot be expressed
    static std::vector<uint8_t> BuildNamedBootHobeta(const std::string& trimmedName);

    /// Load a boot file resource (Hobeta) from the boot resource folder: executable dir, then resources dir
    static std::vector<uint8_t> LoadBundledCommander();
};
