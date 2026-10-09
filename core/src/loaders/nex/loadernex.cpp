#include "stdafx.h"

#include "loadernex.h"

#include <cstring>
#include <fstream>
#include <iterator>

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/video/screen.h"

namespace
{
constexpr size_t kHeaderSize = 512;
constexpr size_t kBankSize = 0x4000;
constexpr uint8_t kScreenLayer2 = 1, kScreenUla = 2, kScreenLoRes = 4, kScreenHiRes = 8, kScreenHiColour = 16, kScreenExt2 = 64,
                  kScreenNoPalette = 128;
constexpr size_t kBigLayer2 = 81920;

/// The file's bank order: 5, 2, 0, 1, 3, 4, 6, 7, 8 ... 111
std::vector<unsigned> BankOrder()
{
    std::vector<unsigned> order = {5, 2, 0, 1, 3, 4};
    for (unsigned b = 6; b < 112; b++)
        order.push_back(b);
    return order;
}
}  // namespace

bool LoaderNex::Fail(const std::string& why)
{
    _error = why;
    return false;
}

bool LoaderNex::Parse(const std::vector<uint8_t>& image, NexHeader& header)
{
    if (image.size() < kHeaderSize || std::memcmp(image.data(), "Next", 4) != 0)
        return Fail("not a NEX file (no \"Next\" signature)");
    header = NexHeader();
    header.version.assign(reinterpret_cast<const char*>(image.data()) + 4, 4);
    if (header.version.compare(0, 3, "V1.") != 0 || header.version[3] < '0' || header.version[3] > '3')
        return Fail("NEX version " + header.version + " is not one of V1.0 - V1.3");
    header.ramRequired = image[8];
    header.bankCount = image[9];
    header.screenFlags = image[10];
    header.border = image[11] & 7;
    header.sp = static_cast<uint16_t>(image[12] | (image[13] << 8));
    header.pc = static_cast<uint16_t>(image[14] | (image[15] << 8));
    for (unsigned b = 0; b < 112; b++)
        header.banksPresent[b] = image[18 + b] != 0;
    header.keepRegisters = image[134];
    header.entryBank = image[139];
    header.screenFlags2 = image.size() > 152 ? image[152] : 0;
    return true;
}

size_t LoaderNex::ScreenBytes(const NexHeader& h, bool& ok)
{
    ok = true;
    const uint8_t sf = h.screenFlags;
    const bool v13 = h.version == "V1.3";
    size_t bytes = 0;
    // the palette block: with Layer 2 / LoRes / (V1.3) the extended screens, unless the no-palette flag; before V1.3 the
    // ULA / Timex screens carry none even next to a paletted one (nexload.asm)
    bool palette = !(sf & kScreenNoPalette);
    if (palette && !v13 && (sf & (kScreenUla | kScreenHiRes | kScreenHiColour)))
        palette = false;
    if (palette && (sf & (kScreenLayer2 | kScreenLoRes | kScreenExt2)))
        bytes += 512;
    if (sf & kScreenLayer2)
        bytes += 49152;
    if (sf & kScreenUla)
        bytes += 6912;
    if (sf & kScreenLoRes)
        bytes += 12288;
    if (sf & kScreenHiRes)
        bytes += 12288;
    if (sf & kScreenHiColour)
        bytes += 12288;
    if (sf & kScreenExt2)
    {
        if (h.screenFlags2 == 1 || h.screenFlags2 == 2)
            bytes += kBigLayer2;
        else if (h.screenFlags2 != 3)
            ok = false;  // an extended screen of an unknown kind
    }
    return bytes;
}

bool LoaderNex::Load(const std::vector<uint8_t>& image)
{
    if (!Parse(image, _header))
        return false;
    bool sized = true;
    size_t offset = kHeaderSize + ScreenBytes(_header, sized);
    if (!sized)
        return Fail("NEX: the extended loading screen of the header has an unknown kind");

    auto* memory = dynamic_cast<NextMemory*>(_context->pMemory);
    auto* decoder = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
    if (!memory || !decoder)
        return Fail("NEX files run on the ZX Spectrum Next");

    // The machine as the loader leaves it: ROM in slots 0-1, banks 5 and 2 below, the entry bank at #C000; the
    // registers reset unless the file asks to keep them
    if (!_header.keepRegisters)
        decoder->PerformReset(false);
    memory->ResetMmu();
    memory->ApplyClassicPaging(0, 0);

    // The banks, in the file's order
    for (unsigned bank : BankOrder())
    {
        if (!_header.banksPresent[bank])
            continue;
        if (offset + kBankSize > image.size())
            return Fail("NEX: the file ends inside bank " + std::to_string(bank));
        if (bank >= MAX_RAM_PAGES)
            return Fail("NEX: bank " + std::to_string(bank) + " is beyond the machine's RAM");
        std::memcpy(memory->RAMPageAddress(static_cast<uint16_t>(bank)), image.data() + offset, kBankSize);
        offset += kBankSize;
    }

    // The entry bank in slot 3 (#C000)
    memory->SetMmu(6, static_cast<uint8_t>(_header.entryBank * 2));
    memory->SetMmu(7, static_cast<uint8_t>(_header.entryBank * 2 + 1));
    _context->emulatorState.pFE = static_cast<uint8_t>(0xE0 | _header.border);
    _context->emulatorState.border_attr = _header.border;
    if (_context->pScreen)
        _context->pScreen->SetBorderColor(_header.border);

    Z80* z80 = _context->pCore->GetZ80();
    z80->sp = _header.sp;
    z80->pc = _header.pc;
    z80->iff1 = z80->iff2 = 0;
    return true;
}

bool LoaderNex::LoadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Fail("NEX: cannot open " + path);
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return Load(image);
}
