#pragma once

#include "unrealdisk/detail/std.h"

#include <string>
#include <vector>

#include "unrealdisk/loaders/loader_trd.h"
#include "unrealdisk/options.h"

namespace unrealdisk
{

#if defined(_MSC_VER)
    #pragma pack(push, 1)
    #define PACKED  // Empty - packing handled by pragma. Needed for cross-compiler compatibility
#else
    #define PACKED __attribute__((packed))
#endif

/// SCL files use minimal 14-bytes catalog records (without start sector and track)
struct PACKED TRDOSDirectoryEntryBase
{
    char Name[8];
    uint8_t Type;
    uint16_t Start;
    uint16_t Length;
    uint8_t SizeInSectors;
};

/// Full 16 bytes TR-DOS catalog record with sector/track positioning on disk
struct PACKED TRDOSDirectoryEntry : public TRDOSDirectoryEntryBase
{
    uint8_t StartSector;
    uint8_t StartTrack;
};

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4200)  // the flexible array member (a GCC / Clang extension MSVC accepts too)
#endif
struct PACKED SCLHeader
{
    uint8_t Signature[8];  // Should be 'SINCLAIR'
    uint8_t FileCount;
    TRDOSDirectoryEntryBase Files[];
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#if defined(_MSC_VER)
#pragma pack(pop)
#else
#undef PACKED
#endif

class LoaderSCL
{
    /// region <Fields>
protected:
    LoaderOptions _options;
    std::string _filepath;

    DiskImage* _diskImage = nullptr;
    std::vector<std::string> _warnings;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderSCL(const LoaderOptions& options, const std::string& filepath)
    {
        _options = options;
        _filepath = filepath;
    };

    virtual ~LoaderSCL() = default;
    /// endregion </Constructors / destructors>

    /// region <Basic methods>
public:
    bool loadImage();
    /// An SCL archive in memory onto a new formatted TR-DOS disk (getImage; the caller owns it)
    bool loadData(const uint8_t* data, size_t len);
    bool writeImage();
    bool writeImage(const std::string& path);  // Save As - write to specified path
    DiskImage* getImage();
    void setImage(DiskImage* diskImage);

    /// Diagnostics collected by the last writeImage call (refusal reason, skipped files)
    const std::vector<std::string>& lastWarnings() const { return _warnings; }
    /// endregion </Basic methods>

    /// region <Helper methods>
protected:
    bool loadSCL();
    bool loadSCLData(const uint8_t* data, size_t len);
    bool addFile(TRDOSDirectoryEntryBase* fileDescriptor, uint8_t* fileData);
    bool checkSCLFileCRC(uint8_t* data, size_t length);

    static inline bool areUint8ArraysEqual(const uint8_t* arr1, const uint8_t* arr2, size_t size)
    {
        bool result = true;

        for (size_t i = 0; i < size; ++i)
        {
            if (arr1[i] != arr2[i])
            {
                result = false;
                break;
            }
        }

        return result;
    }
    /// endregion </Helper methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing /
// benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class LoaderSCLCUT : public LoaderSCL
{
public:
    LoaderSCLCUT(const LoaderOptions& options, const std::string& filepath) : LoaderSCL(options, filepath) {};
    using LoaderSCL::addFile;
    using LoaderSCL::checkSCLFileCRC;
    using LoaderSCL::loadSCL;

public:
    using LoaderSCL::_diskImage;
};
#endif  // _CODE_UNDER_TEST
}  // namespace unrealdisk
