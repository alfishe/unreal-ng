#pragma once

/// @file neogsflashimages.h
/// @brief Known NeoGS flash images, identified by the SHA-256 of the 512 KB
/// image (neogs-tdd.md §4.1). The host-ROM table (rom.cpp) does not cover card
/// images; the card looks its image up here when it loads it, and the GS
/// debugger's firmware profiles key on the same digests.

#include <cstring>

struct NeoGSFlashImage
{
    const char* sha256;
    const char* title;
    bool fpgaD;         // made for the fpgaD revision (2 MB boards)
    bool mainRomV111;   // main ROM v1.11: COMINT_ at #026E, NUMPG at #4080
};

inline const NeoGSFlashImage* findNeoGSFlashImage(const char* sha256)
{
    static const NeoGSFlashImage images[] = {
        {"f8087ecde8eb4ed08a90cfde409b24df8cca266e65b2f6053d234ff374d3b18e", "NeoGS flash v1.11", false, true},
        {"37b14b257d47c9c22e6ad06d6a294298093a2fdf323e96e1c9ac3914022d1cfc", "NeoGS flash v1.08 (bootGS.rom)", true, false},
        {"c12a87e9884ca6c16b1a6e33ac38e7120b0248385d1cf774dc33bd6f0124f075", "NeoGS flash v1.08 (ngsrom109)", true, false},
    };
    for (const NeoGSFlashImage& image : images)
    {
        if (strcmp(image.sha256, sha256) == 0)
            return &image;
    }
    return nullptr;
}
