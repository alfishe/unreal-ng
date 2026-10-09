#include "stdafx.h"

#include "loadernex.h"

#include <cstring>
#include <fstream>
#include <iterator>

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextboard.h"
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

void LoaderNex::FillUlaPalette(NextBoard& board)
{
    static const uint8_t kDefaultPalette[16] = {0x00, 0x02, 0xA0, 0xA2, 0x14, 0x16, 0xB4, 0xB6, 0x00, 0x03, 0xE0, 0xE7, 0x1C, 0x1F, 0xFC, 0xFF};
    board.Write(0x43, 0);
    for (unsigned first : {0u, 128u})  // the 16 defaults, eight times, in both halves
    {
        board.Write(0x40, static_cast<uint8_t>(first));
        for (unsigned i = 0; i < 128; i++)
            board.Write(0x41, kDefaultPalette[i & 15]);
    }
}

/// nexload.asm, "Reset All registers": the video and sound registers a program starts from, the 16 default ULA colours
/// repeated, identity palettes for Layer 2 and the sprites, transparency #E3, priorities SLU with the sprites on
void LoaderNex::ResetRegisters(NextBoard& board)
{
    auto nr = [&](uint8_t reg, uint8_t value) { board.Write(reg, value); };
    nr(0x62, 0);  // stop the copper
    nr(0x61, 0);
    nr(0x07, 0x03);  // 28 MHz
    nr(0x12, 9);
    nr(0x13, 12);
    nr(0x14, 0xE3);
    nr(0x15, 0x01);
    nr(0x16, 0);
    nr(0x17, 0);
    nr(0x1C, 0x0F);
    for (uint8_t window = 0x18; window <= 0x1B; window++)
    {
        const bool tilemap = window == 0x1B;
        nr(window, 0);
        nr(window, tilemap ? 159 : 255);
        nr(window, 0);
        nr(window, tilemap ? 255 : 191);
    }
    nr(0x2D, 0);
    nr(0x32, 0);
    nr(0x33, 0);
    nr(0x42, 15);
    FillUlaPalette(board);
    for (uint8_t control : {uint8_t(0x10), uint8_t(0x20)})  // Layer 2, sprites: the identity
    {
        nr(0x43, control);
        nr(0x40, 0);
        for (unsigned i = 0; i < 256; i++)
            nr(0x41, static_cast<uint8_t>(i));
    }
    nr(0x43, 0);
    nr(0x4A, 0);
    nr(0x4B, 0xE3);
    board.Video().WritePort123b(0);
}

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

    // The loader's register reset (nexload.asm "Reset All registers"), unless the file asks to keep them
    if (!_header.keepRegisters)
        ResetRegisters(decoder->Board());

    // The machine as the loader leaves it: ROM in slots 0-1, banks 5 and 2 below, the entry bank at #C000; the
    // registers reset unless the file asks to keep them
    memory->ResetMmu();
    _context->emulatorState.p7FFD = 0x10;
    _context->emulatorState.p1FFD = 0x04;
    memory->ApplyClassicPaging(0x10, 0x04);  // ROM 3, the 48K BASIC ROM: RST 16 and the font at #3D00 work

    // The loading screens (nexload.asm): a palette block, then the screens in the order Layer 2, ULA, LoRes, HiRes, HiColour
    // and the V1.3 big Layer 2; each leaves its display mode set. They are drawn into the banks the loader uses (Layer 2
    // 9-11, the ULA screen in bank 5) before the file's own banks replace them
    {
        const uint8_t sf = _header.screenFlags;
        const bool v13 = _header.version == "V1.3";
        NextBoard& board = decoder->Board();
        auto show = [&](uint8_t port123b, uint8_t nr15, uint8_t portFf) {
            board.Video().WritePort123b(port123b);
            board.Write(0x15, nr15);
            board.Video().WritePortFf(portFf);
        };
        size_t at = kHeaderSize;
        bool palette = !(sf & kScreenNoPalette);
        if (palette && !v13 && (sf & (kScreenUla | kScreenHiRes | kScreenHiColour)))
            palette = false;
        if (palette && (sf & (kScreenLayer2 | kScreenLoRes | kScreenExt2)) && at + 512 <= image.size())
        {
            board.Write(0x43, (sf & kScreenLoRes) ? 0x01 : (v13 && _header.screenFlags2 == 3 ? 0x30 : 0x10));
            board.Write(0x40, 0);
            for (unsigned i = 0; i < 256; i++)
            {
                board.Write(0x44, image[at + i * 2]);
                board.Write(0x44, image[at + i * 2 + 1] & 1);
            }
            board.Write(0x43, 0);
            at += 512;
        }
        auto block = [&](size_t size, auto&& place) {
            if (at + size > image.size())
                return false;
            place(image.data() + at);
            at += size;
            return true;
        };
        if (sf & kScreenLayer2)
            if (block(49152, [&](const uint8_t* d) {
                    for (unsigned b = 0; b < 3; b++)
                        std::memcpy(memory->RAMPageAddress(static_cast<uint16_t>(9 + b)), d + b * kBankSize, kBankSize);
                }))
                show(0x02, 0x01, 0x00);
        if (sf & kScreenUla)
            if (block(6912, [&](const uint8_t* d) { std::memcpy(memory->RAMPageAddress(5), d, 6912); }))
                show(0x00, 0x01, 0x00);
        auto halves = [&](const uint8_t* d) {  // two reads of 6144, at #4000 and #6000: the 2048 between are not touched
            std::memcpy(memory->RAMPageAddress(5), d, 6144);
            std::memcpy(memory->RAMPageAddress(5) + 0x2000, d + 6144, 6144);
        };
        if (sf & kScreenLoRes)
            if (block(12288, halves))
                show(0x00, 0x01 | 0x80, 0x03);
        if (sf & kScreenHiRes)
            if (block(12288, halves))
                show(0x00, 0x01, static_cast<uint8_t>((image[138] & 0x38) | 0x06));
        if (sf & kScreenHiColour)
            if (block(12288, halves))
                show(0x00, 0x01, 0x02);
        if ((sf & kScreenExt2) && (_header.screenFlags2 == 1 || _header.screenFlags2 == 2))
        {
            const bool is320 = _header.screenFlags2 == 1;
            if (block(kBigLayer2, [&](const uint8_t* d) {
                    for (unsigned b = 0; b < 5; b++)
                        std::memcpy(memory->RAMPageAddress(static_cast<uint16_t>(9 + b)), d + b * kBankSize, kBankSize);
                }))
            {
                board.Write(0x1C, 0x01);
                for (uint8_t v : {uint8_t(0), uint8_t(159), uint8_t(0), uint8_t(255)})
                    board.Write(0x18, v);
                board.Write(0x70, static_cast<uint8_t>((image[138] & 0x0F) | (is320 ? 0x10 : 0x20)));
                board.Write(0x12, 9);
                board.Write(0x69, 0x80);
            }
        }
    }

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
    z80->halted = 0;  // a CPU waiting in HALT (NextZXOS idles there) leaves it
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
