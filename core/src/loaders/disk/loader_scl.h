#pragma once

#include <stdafx.h>

#include "loaders/disk/loader_trd.h"

#pragma pack(push, 1)

/// SCL files use minimal 14-bytes catalog records (without start sector and track)
struct TRDOSDirectoryEntryBase
{
    char Name[8];
    uint8_t Type;
    uint16_t Start;
    uint16_t Length;
    uint8_t SizeInSectors;
};

/// Full 16 bytes TR-DOS catalog record with sector/track positioning on disk
struct TRDOSDirectoryEntry : public TRDOSDirectoryEntryBase
{
    uint8_t StartSector;
    uint8_t StartTrack;
};

struct SCLHeader
{
    uint8_t Signature[8];  // Should be 'SINCLAIR'
    uint8_t FileCount;
#pragma warning(push)
#pragma warning(disable: 4200)
    TRDOSDirectoryEntryBase Files[];
#pragma warning(pop)
};
#pragma pack(pop)


class LoaderSCL
{
    /// region <Fields>
protected:
    EmulatorContext* _context;
    std::string _filepath;

    DiskImage* _diskImage = nullptr;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderSCL(EmulatorContext* context, const std::string& filepath)
    {
        _context = context;
        _filepath = filepath;
    };

    virtual ~LoaderSCL() = default;
    /// endregion </Constructors / destructors>

    /// region <Basic methods>
public:
    bool loadImage();
    bool writeImage();
    DiskImage* getImage();
    /// endregion </Basic methods>

    /// region <Helper methods>
protected:
    bool loadSCL();
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
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class LoaderSCLCUT : public LoaderSCL
{
public:
    LoaderSCLCUT(EmulatorContext* context, const std::string& filepath) : LoaderSCL(context, filepath) {};

public:
    using LoaderSCL::_diskImage;
};
#endif // _CODE_UNDER_TEST
