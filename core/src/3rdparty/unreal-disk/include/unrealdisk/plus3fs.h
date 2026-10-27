#pragma once

// The +3DOS file system (the Spectrum +3, CP/M 2.2's layout) on a disk image, both ways: list, read, write, delete, rename.
// The disk's geometry comes from the specification in its first sector (track 0, the first sector; byte 0 the format
// 0-3), else the +3 standard: one side, 40 tracks of nine 512-byte sectors numbered 1-9, one reserved track, 1K blocks,
// two directory blocks (64 entries). The directory: 32-byte entries (user, name 8, type 3 with the attribute bits,
// extent, records, the block numbers: 8-bit while the disk has fewer than 256 blocks). A file written by +3DOS starts
// with its 128-byte header "PLUS3DOS" #1A (the whole length, the BASIC type and parameters, a checksum).
//
// Example: the blank plus3-blank.dsk, Write("SOURCE.ASM", text, {}) puts the text after a header into block 2;
// List gives SOURCE.ASM, Read gives the text back.

#include <cstdint>
#include <string>
#include <vector>

namespace unrealdisk
{
class DiskImage;

struct Plus3File
{
    uint8_t user = 0;
    std::string name;            ///< up to 8 characters, trailing blanks removed
    std::string type;            ///< up to 3 characters, the attribute bits cleared, trailing blanks removed
    uint32_t size = 0;           ///< the bytes its records hold on the disk (header included)
    bool header = false;         ///< a +3DOS header (its checksum right)
    uint32_t length = 0;         ///< the data's length: the header's file length less 128, else `size`
    uint8_t basicType = 0;       ///< the header's BASIC type (3 = CODE)
    uint16_t param1 = 0;         ///< the header's first parameter (CODE: the load address)
    uint16_t param2 = 0;

    /// NAME.TYP (NAME when there is no type)
    std::string FullName() const { return type.empty() ? name : name + "." + type; }
};

class Plus3FileSystem
{
public:
    explicit Plus3FileSystem(DiskImage& image) : _image(image) {}

    /// The image has a +3DOS file system: 512-byte sectors where the geometry puts the directory, and a directory
    /// whose entries read
    bool IsPlus3() const;

    /// The files in directory order (user 0-15); false with the reason when there is no +3DOS directory
    bool List(std::vector<Plus3File>& out, std::string& error) const;

    /// The file NAME.TYP (without case)
    bool Find(const std::string& name, Plus3File& out, std::string& error) const;

    /// The file's data: after its header (`length` bytes) when it has one, else its records
    bool Read(const Plus3File& file, std::vector<uint8_t>& data, std::string& error) const;

    struct WriteOptions
    {
        bool header = true;          ///< a +3DOS header in front (as +3 BASIC's SAVE writes it)
        uint8_t basicType = 3;       ///< CODE
        uint16_t param1 = 0;         ///< CODE: the load address
        uint16_t param2 = 0x8000;
        uint8_t user = 0;
    };

    /// `bytes` as NAME.TYP: an older NAME.TYP is deleted first; the blocks are the first free ones. False with the
    /// reason (no directory, the directory full, not enough free blocks)
    bool Write(const std::string& name, const std::vector<uint8_t>& bytes, const WriteOptions& options, std::string& error);

    /// Every entry of NAME.TYP marked deleted (user #E5)
    bool Delete(const std::string& name, std::string& error);

    /// Every entry of NAME.TYP takes `newName` (the attribute bits stay). False when there is no such file, the new
    /// name is no CP/M name or a file of that name is there already
    bool Rename(const std::string& name, const std::string& newName, std::string& error);

    /// `name` is a CP/M file name: 1-8 characters, an optional type of up to 3, printable ASCII without
    /// < > . , ; : = ? * [ ], no blank in front
    static bool ValidName(const std::string& name);

private:
    DiskImage& _image;
};
}  // namespace unrealdisk
