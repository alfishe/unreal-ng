#include "stdafx.h"

#include "loader_z80.h"

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"

LoaderZ80::LoaderZ80(EmulatorContext* context)
{
    _context = context;
    _logger = context->pModuleLogger;
}

LoaderZ80::~LoaderZ80()
{

}

bool LoaderZ80::load(std::string& filepath)
{
    bool result = false;

    return result;
}


bool LoaderZ80::validate(std::string& filepath)
{
    bool result = false;

    // 1. Check that file exists
    if (FileHelper::FileExists(filepath))
    {
        // 2. Check file has appropriate size (header + N * 16k data bytes)
        size_t fileSize = FileHelper::GetFileSize(filepath);
        if (fileSize > -1)
        {
            size_t pagesSize = fileSize - sizeof(Z80Header_v1);
            if (pagesSize > 0 && pagesSize % 16384 == 0)
            {
                result = true;
            }
            else
            {
                MLOGWARNING("Z80 snapshot file '%s' has incorrect size %d", filepath.c_str(), fileSize);
            }
        }
    }
    else
    {
        MLOGWARNING("Z80 snapshot file '%s' not found", filepath.c_str());
    }

    return result;
}

bool LoaderZ80::stageLoad(std::string& filepath)
{
    bool result = false;

    return result;
}

void LoaderZ80::commitFromStage()
{
}

/// region <Debug methods>

std::string LoaderZ80::dumpSnapshotInfo()
{
    std::string result;
    std::stringstream ss;

    return result;
}

/// endregion </Debug methods>