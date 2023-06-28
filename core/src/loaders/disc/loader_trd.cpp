#include "loader_trd.h"

#include "common/filehelper.h"

 /// region <Methods>
 DiskImage* LoaderTRD::load()
 {
    DiskImage* result = nullptr;

    if (FileHelper::FileExists(_filepath))
    {
        size_t fileSize = FileHelper::GetFileSize(_filepath);
        if (fileSize > 0)
        {
            uint8_t* buffer = new uint8_t[fileSize];
            if (FileHelper::ReadFileToBuffer(_filepath, buffer, fileSize))
            {
                result = new DiskImage();
                result->setRawDiskImageData(buffer, fileSize);
            }
        }
    }

    return result;
 }
 /// endregion </Methods>