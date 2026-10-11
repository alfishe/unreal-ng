#pragma once

// The TR-DOS file system on a disk image, both ways: list the catalog, read a file's sectors, write a file (an older
// live file of the same name and type is deleted as TR-DOS deletes it, the new one goes after the last file), delete
// a file. It works on any DiskImage with TR-DOS tracks (16 x 256-byte sectors), whatever format it was loaded from;
// the caller saves the image (FloppyFormats::Save) or keeps it in a drive.
//
// Example: a blank TRD from LoaderTRD::format, Write("MAIN", 'H', 0, bytes) puts the file at track 1 sector 0, entry 0;
// List gives MAIN.H with 5 sectors; Read gives the bytes back.

#include <cstdint>
#include <string>
#include <vector>

#include "unrealdisk/trdoscatalog.h"

namespace unrealdisk
{
class DiskImage;

class TrdosFileSystem
{
public:
    explicit TrdosFileSystem(DiskImage& image) : _image(image) {}

    /// The image has a TR-DOS file system (TrdosCatalog::IsTrdos)
    bool IsTrdos() const;

    /// The live files in catalog order (deleted entries left out); false with the reason when there is no catalog
    bool List(std::vector<TrdosFile>& out, std::string& error) const;

    /// The last live file named `name` (trailing blanks optional) of type `type` (0 = any), as TR-DOS finds it
    bool Find(const std::string& name, char type, TrdosFile& out, std::string& error) const;

    /// The file's `length` bytes (`data`) and the rest of its last sector (`tail`, may be null)
    bool Read(const TrdosFile& file, std::vector<uint8_t>& data, std::vector<uint8_t>* tail, std::string& error) const;

    struct WriteOptions
    {
        uint16_t start = 0;            ///< the catalog's start field (a source's load address or the assembler's mark)
        bool lengthField = true;       ///< false: 0 in the length field (XAS reads whole sectors)
    };

    /// `bytes` as NAME.T: an older live NAME.T is deleted first; the data goes to the first free sector. False with
    /// the reason (no catalog, the catalog full, not enough free sectors, the free space past the last track)
    bool Write(const std::string& name, char type, const std::vector<uint8_t>& bytes, const WriteOptions& options, std::string& error);

    /// Marks every live NAME.T deleted (the first name byte #01; the sectors stay used, as TR-DOS leaves them)
    bool Delete(const std::string& name, char type, std::string& error);

private:
    DiskImage& _image;
};
}  // namespace unrealdisk
