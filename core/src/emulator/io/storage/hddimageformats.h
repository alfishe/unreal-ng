#pragma once

/// @file hddimageformats.h
/// @brief Hard-disk and CD image formats (IDE design §7.2, implementation-plan.md D6).
///
/// | Format | Recognized by | Layout |
/// |---|---|---|
/// | ISO 9660 (CD) | `CD001` at byte #8001 | 2048-byte blocks from byte 0, read-only |
/// | HDF (RS-IDE) | `RS-IDE` + #1A at byte 0 | data from the offset at bytes 9-10; geometry from the IDENTIFY copy at #16; flag bit 0: 8-bit halved |
/// | fixed VHD | `conectix` footer in the last 512 bytes, disk type 2 | data from byte 0, the footer's size and geometry |
/// | HDI (Anex86) | the `.hdi` extension | little-endian: header size at 8, data size at 12, sector size at 16, S / H / C at 20 / 24 / 28 |
/// | CHD (MAME) | `MComprHD` at byte 0 | hunks, compressed or not, maybe a parent's; geometry from `GDDD`; read-only (ChdImage) |
/// | raw | anything else | sector n at byte n × 512 |

#include <memory>
#include <string>

#include "emulator/io/storage/rawimage.h"

class HddImageFormats
{
public:
    /// "iso", "hdf", "vhd", "hdi", "chd" or "raw" for the file's content (and, for
    /// HDI, its extension). Empty when the file cannot be read or is a
    /// broken image of a format with a signature (the reason in `error`)
    static std::string Probe(const std::string& path, std::string* error = nullptr);

    /// Open the image with its layout. `format` from Probe, any but "chd"
    static std::unique_ptr<RawImage> Open(const std::string& path, const std::string& format, RawImage::Access access,
                                          std::string* error = nullptr);

    /// Open any hard-disk format as a block device: the RawImage family, or a
    /// CHD (always read-only: its writes belong in a change layer)
    static std::unique_ptr<IBlockDevice> OpenBlock(const std::string& path, const std::string& format, RawImage::Access access,
                                                   std::string* error = nullptr);

    /// Extensions only hard-disk images use (hdd hd hdf hdi vhd chd): `media insert
    /// auto` sends them to an IDE unit rather than an SD card. `.img` is both
    static bool IsHardDiskExtension(const std::string& extension);
};
