#include "espelfloader.h"

#include <cstring>

namespace vdac4
{

namespace
{

constexpr uint16_t kMachineXtensa = 94;
constexpr size_t kHeaderSize = 52;
constexpr size_t kSectionSize = 40;
constexpr size_t kSymbolSize = 16;
constexpr size_t kRelaSize = 12;
constexpr uint32_t kMaxSections = 1024;

constexpr uint32_t kShtProgbits = 1;
constexpr uint32_t kShtSymtab = 2;
constexpr uint32_t kShtRela = 4;
constexpr uint32_t kShtNobits = 8;
constexpr uint32_t kShtDynsym = 11;
constexpr uint32_t kShfWrite = 1;
constexpr uint32_t kShfAlloc = 2;
constexpr uint32_t kShfExecinstr = 4;

constexpr uint32_t kRelocRtld = 2;
constexpr uint32_t kRelocGlobDat = 3;
constexpr uint32_t kRelocJmpSlot = 4;
constexpr uint32_t kRelocRelative = 5;

// The symbol types esp_elf_relocate compares the RELOCATION type against (its quirk)
constexpr uint32_t kSttObject = 1;
constexpr uint32_t kSttSection = 3;
constexpr uint32_t kSttFile = 4;
constexpr uint32_t kSttCommon = 5;

constexpr uint8_t kStbWeak = 2;
constexpr uint16_t kShnUndef = 0;

uint16_t Read16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t Read32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void Write32(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

struct Section
{
    uint32_t name, type, flags, addr, offset, size, link;
};

Section SectionAt(const uint8_t* data, uint32_t table, uint32_t index)
{
    const uint8_t* p = data + table + index * kSectionSize;
    return Section{Read32(p), Read32(p + 4), Read32(p + 8), Read32(p + 12), Read32(p + 16), Read32(p + 20), Read32(p + 24)};
}

struct Symbol
{
    uint32_t name, value;
    uint8_t info;
    uint16_t shndx;
};

Symbol SymbolAt(const uint8_t* data, const Section& table, uint32_t index)
{
    const uint8_t* p = data + table.offset + index * kSymbolSize;
    return Symbol{Read32(p), Read32(p + 4), p[12], Read16(p + 14)};
}

bool InFile(uint64_t offset, uint64_t length, size_t size)
{
    return offset <= size && length <= size - offset;
}

bool StringOk(const uint8_t* data, const Section& table, uint32_t index)
{
    return index < table.size && std::memchr(data + table.offset + index, 0, table.size - index) != nullptr;
}

bool HasFlags(const Section& s, uint32_t flags)
{
    return (s.flags & flags) == flags;
}

/// The slot esp_elf_load_section puts a section in, or -1 (a repeated name: the last one wins)
int LoadedSlot(const Section& s, const char* name)
{
    if (s.type == kShtProgbits && HasFlags(s, kShfAlloc))
    {
        if (HasFlags(s, kShfExecinstr) && std::strcmp(name, ".text") == 0)
            return EspElfImage::Text;
        if (HasFlags(s, kShfWrite) && std::strcmp(name, ".data") == 0)
            return EspElfImage::Data;
        if (std::strcmp(name, ".rodata") == 0)
            return EspElfImage::Rodata;
        if (std::strcmp(name, ".data.rel.ro") == 0)
            return EspElfImage::RelRo;
        return -1;
    }
    if (s.type == kShtNobits && HasFlags(s, kShfAlloc | kShfWrite) && std::strcmp(name, ".bss") == 0)
        return EspElfImage::Bss;
    return -1;
}

}  // namespace

uint32_t EspElfImage::MapSymbol(uint32_t vaddr) const
{
    for (const EspElfSection& section : sections)
    {
        if (vaddr >= section.vaddr && vaddr < section.vaddr + section.size)
            return vaddr - section.vaddr + section.addr;
    }
    return 0;
}

uint32_t EspElfImage::RemapText(uint32_t address) const
{
    const EspElfSection& text = sections[Text];
    if (address >= text.addr && address < text.addr + text.size)
        return address + kEspIbusOffset;
    return address;
}

const char* EspElfImageProblem(const uint8_t* data, size_t size)
{
    if (data == nullptr || size < kHeaderSize)
        return "short header";
    if (std::memcmp(data, "\x7F" "ELF", 4) != 0)
        return "no ELF signature";
    if (data[4] != 1 || data[5] != 1)
        return "not ELF32 little-endian";
    if (Read16(data + 18) != kMachineXtensa)
        return "not Xtensa";
    const uint32_t entry = Read32(data + 24);
    const uint32_t table = Read32(data + 32);
    const uint16_t entrySize = Read16(data + 46);
    const uint16_t count = Read16(data + 48);
    const uint16_t namesIndex = Read16(data + 50);
    if (entrySize != kSectionSize || count == 0 || count > kMaxSections)
        return "bad section table";
    if (!InFile(table, static_cast<uint64_t>(count) * kSectionSize, size))
        return "section table outside file";
    if (namesIndex >= count)
        return "bad section name table index";
    for (uint32_t i = 0; i < count; ++i)
    {
        const Section s = SectionAt(data, table, i);
        if (s.type != kShtNobits && !InFile(s.offset, s.size, size))
            return "section outside file";
    }
    const Section names = SectionAt(data, table, namesIndex);
    if (names.type == kShtNobits)
        return "bad section name table";

    struct Range
    {
        uint32_t addr = 0;
        uint32_t size = 0;
    };
    Range loaded[EspElfImage::SlotCount];
    for (uint32_t i = 0; i < count; ++i)
    {
        const Section s = SectionAt(data, table, i);
        if (!StringOk(data, names, s.name))
            return "bad section name";
        const char* name = reinterpret_cast<const char*>(data + names.offset + s.name);
        const int slot = LoadedSlot(s, name);
        if (slot == EspElfImage::Text)
        {
            const uint64_t aligned = (static_cast<uint64_t>(s.size) + 3) & ~static_cast<uint64_t>(3);
            if (!InFile(s.offset, aligned, size))
                return ".text outside file";
            loaded[slot] = Range{s.addr, static_cast<uint32_t>(aligned)};
        }
        else if (slot >= 0)
        {
            loaded[slot] = Range{s.addr, s.size};
        }
    }
    const Range& text = loaded[EspElfImage::Text];
    if (text.size == 0)
        return "no .text";
    if (entry < text.addr || entry - text.addr >= text.size)
        return "entry outside .text";

    for (uint32_t i = 0; i < count; ++i)
    {
        const Section rela = SectionAt(data, table, i);
        if (rela.type != kShtRela)
            continue;
        if (rela.link >= count)
            return "bad relocation symbol table";
        const Section symbols = SectionAt(data, table, rela.link);
        if ((symbols.type != kShtSymtab && symbols.type != kShtDynsym) || symbols.link >= count)
            return "bad relocation symbol table";
        const Section strings = SectionAt(data, table, symbols.link);
        if (strings.type == kShtNobits)
            return "bad symbol string table";
        const uint32_t symbolCount = symbols.size / kSymbolSize;
        const uint32_t relocations = rela.size / kRelaSize;
        for (uint32_t r = 0; r < relocations; ++r)
        {
            const uint8_t* entryData = data + rela.offset + r * kRelaSize;
            const uint32_t where = Read32(entryData);
            const uint32_t info = Read32(entryData + 4);
            const uint32_t symbol = info >> 8;
            const uint32_t type = info & 0xFF;
            if (symbol >= symbolCount)
                return "relocation symbol out of range";
            const uint32_t symbolName = Read32(data + symbols.offset + symbol * kSymbolSize);
            if (!StringOk(data, strings, symbolName))
                return "bad symbol name";
            if (type == kRelocGlobDat || type == kRelocJmpSlot || type == kRelocRelative)
            {
                bool inside = false;
                for (const Range& range : loaded)
                {
                    if (range.size >= 4 && where >= range.addr && where - range.addr <= range.size - 4)
                    {
                        inside = true;
                        break;
                    }
                }
                if (!inside)
                    return "relocation outside loaded sections";
            }
        }
    }
    return nullptr;
}

bool EspElfLoad(const uint8_t* data, size_t size, IEspElfHost& host, EspElfImage& image, std::string& error)
{
    image = EspElfImage{};
    if (const char* problem = EspElfImageProblem(data, size))
    {
        error = problem;
        return false;
    }

    const uint32_t table = Read32(data + 32);
    const uint16_t count = Read16(data + 48);
    const Section names = SectionAt(data, table, Read16(data + 50));

    // esp_elf_load_section: pick the sections
    for (uint32_t i = 0; i < count; ++i)
    {
        const Section s = SectionAt(data, table, i);
        const char* name = reinterpret_cast<const char*>(data + names.offset + s.name);
        const int slot = LoadedSlot(s, name);
        if (slot < 0)
            continue;
        EspElfSection& section = image.sections[slot];
        section.vaddr = s.addr;
        section.size = slot == EspElfImage::Text ? (s.size + 3u) & ~3u : s.size;
        section.offset = s.offset;
    }

    EspElfSection* sections = image.sections;
    EspElfSection& text = sections[EspElfImage::Text];
    image.textBlock = host.AllocPsram(text.size);
    if (image.textBlock == 0)
    {
        error = "no memory for .text";
        return false;
    }
    const uint32_t dataSize = sections[EspElfImage::Data].size + sections[EspElfImage::Rodata].size +
                              sections[EspElfImage::Bss].size + sections[EspElfImage::RelRo].size;
    if (dataSize != 0)
    {
        image.dataBlock = host.AllocPsram(dataSize);
        if (image.dataBlock == 0)
        {
            EspElfUnload(host, image);
            error = "no memory for data";
            return false;
        }
    }

    text.addr = image.textBlock;
    std::memcpy(host.Bytes(text.addr, text.size), data + text.offset, text.size);

    uint32_t cursor = image.dataBlock;
    for (const int slot : {EspElfImage::Data, EspElfImage::Rodata, EspElfImage::RelRo})
    {
        EspElfSection& section = sections[slot];
        if (section.size == 0)
            continue;
        section.addr = cursor;
        std::memcpy(host.Bytes(cursor, section.size), data + section.offset, section.size);
        cursor += section.size;
    }
    if (sections[EspElfImage::Bss].size != 0)
    {
        EspElfSection& bss = sections[EspElfImage::Bss];
        bss.addr = cursor;
        std::memset(host.Bytes(cursor, bss.size), 0, bss.size);
    }

    image.entry = image.RemapText(Read32(data + 24) + text.addr - text.vaddr);

    // esp_elf_relocate
    for (uint32_t i = 0; i < count; ++i)
    {
        const Section rela = SectionAt(data, table, i);
        if (rela.type != kShtRela)
            continue;
        const Section symbols = SectionAt(data, table, rela.link);
        const Section strings = SectionAt(data, table, symbols.link);
        const uint32_t relocations = rela.size / kRelaSize;
        for (uint32_t r = 0; r < relocations; ++r)
        {
            const uint8_t* entryData = data + rela.offset + r * kRelaSize;
            const uint32_t offset = Read32(entryData);
            const uint32_t info = Read32(entryData + 4);
            const uint32_t type = info & 0xFF;
            const Symbol symbol = SymbolAt(data, symbols, info >> 8);
            const char* name = reinterpret_cast<const char*>(data + strings.offset + symbol.name);
            const bool weakUndefined = (symbol.info >> 4) == kStbWeak && symbol.shndx == kShnUndef;

            uint32_t address = 0;
            if (type == kSttCommon || type == kSttObject || type == kSttSection)
            {
                if (name[0] != '\0')
                {
                    address = host.FindSymbol(name);
                    if (address == 0 && !weakUndefined)
                    {
                        EspElfUnload(host, image);
                        error = std::string("can't find common ") + name;
                        return false;
                    }
                    image.imports += address != 0;
                }
            }
            else if (type == kSttFile)
            {
                address = symbol.value != 0 ? image.MapSymbol(symbol.value) : host.FindSymbol(name);
                if (address == 0 && !weakUndefined)
                {
                    EspElfUnload(host, image);
                    error = std::string("can't find symbol ") + name;
                    return false;
                }
                image.imports += symbol.value == 0 && address != 0;
            }

            // esp_elf_arch_relocate; ImageProblem has checked `where` for the written types
            const uint32_t where = image.MapSymbol(offset);
            switch (type)
            {
                case kRelocRelative:
                {
                    uint8_t* word = host.Bytes(where, 4);
                    const uint32_t value = Read32(word);
                    uint32_t past = 0;
                    uint32_t mapped = image.MapSymbol(value);
                    if (mapped == 0 && value != 0)
                    {
                        mapped = image.MapSymbol(value - 1);
                        past = mapped != 0 ? 1 : 0;
                    }
                    Write32(word, image.RemapText(mapped) + past);
                    break;
                }
                case kRelocGlobDat:
                case kRelocJmpSlot:
                    Write32(host.Bytes(where, 4), image.RemapText(address));
                    break;
                case kRelocRtld:
                default:
                    break;
            }
            image.relocations++;
        }
    }
    return true;
}

void EspElfUnload(IEspElfHost& host, EspElfImage& image)
{
    if (image.dataBlock != 0)
        host.FreePsram(image.dataBlock);
    if (image.textBlock != 0)
        host.FreePsram(image.textBlock);
    image = EspElfImage{};
}

}  // namespace vdac4
