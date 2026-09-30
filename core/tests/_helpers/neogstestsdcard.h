#pragma once

/// @file neogstestsdcard.h
/// @brief The NeoGS test SD cards, built at run time in the scratch folder
/// (fatimagebuilder.h). Each holds, in its root directory:
///   NEOGS.ROM    the main ROM v1.11 (tools/neogs/parts/neogs.rom), for the
///                loader's SD boot;
///   NGS_ROM.UPD  the NedoPC firmware update (tools/neogs/parts/ngs_rom.upd),
///                for the flasher;
///   EYEACHE.MP3  testdata/sound/neogs/mp3/eyeache1-44k-128k-cbr.mp3, for the
///                players;
///   EYE22K.MP3   testdata/sound/neogs/mp3/eyeache1-22k-mono-vbr-id3.mp3: a
///                second MP3, because Neo Player Light v0.44 hangs on a card
///                with exactly one (its FINDMP3 returns with the wrong memory
///                page when it found fewer than two files, neogs-tdd.md §14.2).

#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatimagebuilder.h"
#include "_helpers/testpathhelper.h"

enum class NeoGSTestSd
{
    Fat16Mbr,   // 8 MB, MBR type #06 (what real cards carry; Neo Player Light accepts only 1/6/B/C/E)
    Fat16NoMbr, // 8 MB, FAT16 boot sector at LBA 0: the loader's "bare boot sector" path
    Fat32Mbr,   // 36 MB (FAT32 needs 65,525+ clusters), MBR type #0C
};

inline std::vector<uint8_t> NeoGSTestSdReadFile(const std::string& relative)
{
    std::ifstream in(TestPathHelper::FindProjectRoot() / relative, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline FatImageSpec NeoGSTestSdSpec(NeoGSTestSd card)
{
    FatImageSpec spec;
    spec.fat = card == NeoGSTestSd::Fat32Mbr ? 32 : 16;
    spec.sizeBytes = (card == NeoGSTestSd::Fat32Mbr ? 36ull : 8ull) << 20;
    spec.mbr = card != NeoGSTestSd::Fat16NoMbr;
    spec.label = "NEOGS TEST";
    spec.files = {
        {"NEOGS.ROM", NeoGSTestSdReadFile("tools/neogs/parts/neogs.rom")},
        {"NGS_ROM.UPD", NeoGSTestSdReadFile("tools/neogs/parts/ngs_rom.upd")},
        {"EYEACHE.MP3", NeoGSTestSdReadFile("testdata/sound/neogs/mp3/eyeache1-44k-128k-cbr.mp3")},
        {"EYE22K.MP3", NeoGSTestSdReadFile("testdata/sound/neogs/mp3/eyeache1-22k-mono-vbr-id3.mp3")},
    };
    return spec;
}

inline const char* NeoGSTestSdName(NeoGSTestSd card)
{
    switch (card)
    {
        case NeoGSTestSd::Fat16Mbr: return "neogs-fat16-mbr.img";
        case NeoGSTestSd::Fat16NoMbr: return "neogs-fat16-nombr.img";
        default: return "neogs-fat32-mbr.img";
    }
}

/// Builds one of the cards; check ok() before use
inline std::unique_ptr<ScratchFatImage> MakeNeoGSTestSd(NeoGSTestSd card)
{
    return std::make_unique<ScratchFatImage>(NeoGSTestSdName(card), NeoGSTestSdSpec(card));
}
