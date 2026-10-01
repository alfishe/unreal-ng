#pragma once

#ifndef UNREAL_QT_FILEMANAGER_H
#define UNREAL_QT_FILEMANAGER_H

#include <QString>
#include <map>
#include <cstring>

enum SupportedFileCategoriesEnum
{
    FileUnknown = 0,
    FileROM,
    FileSnapshot,
    FileTape,
    FileDisk,
    FileStorage,  // CD, hard disk or memory card image: goes to the slot of that kind the machine has
    FileArchive,
    FileSymbol
};

enum SupportedPlatformEnum
{
    P_Unknown = 0,
    P_48k,
    P_128k,
    P_128kA,
    P_128kB,
    P_Pentagon128k,
    P_ScorpionZS256k,
    P_Profi,
    P_ATM,
    P_ZXEvo,
    P_ZXNext,

    // Peripherals
    P_GeneralSound,
    P_MoonSound
};

typedef std::map<std::string, SupportedFileCategoriesEnum> PatternCategoryMap;

class FileManager
{
public:
    static PatternCategoryMap _extensions;

    static SupportedFileCategoriesEnum determineFileCategoryByExtension(QString& filepath);
};


#endif //UNREAL_QT_FILEMANAGER_H
