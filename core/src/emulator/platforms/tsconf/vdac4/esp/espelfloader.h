#pragma once

/// @file espelfloader.h
/// @brief The VDAC4 firmware's app / module ELF loader, as the board runs it.
///
/// Design: docs/inprogress/2026-11-15-tsconf-vdac4/design.md §6; facts:
/// research-app-abi.md §4 in the same folder.
///
/// The firmware uses espressif elf_loader 1.3.3 with VDAC4 patches, configured
/// BUS_ADDRESS_MIRROR + LOAD_PSRAM (which selects CACHE_OFFSET), no shared
/// objects. This is a port of that path, quirks included, so an app gets the
/// same layout and the same relocated words it gets on the board:
///
///   - only the sections named .text, .data, .rodata, .data.rel.ro and .bss
///     are loaded; .text (size rounded up to 4) into one PSRAM block, then
///     data | rodata | data.rel.ro | bss (zeroed) back to back in a second one;
///   - code runs from the I-bus alias of its D-bus block (+0x06000000); a
///     relocated word that points into the app's own .text gets the alias;
///   - relocation types are compared against STT_* values when the symbol
///     name is looked up (esp_elf.c:591): RELATIVE (5), R_XTENSA_32 (1) and
///     GLOB_DAT (3) look the name up in the firmware tables only; JMP_SLOT (4)
///     maps a defined symbol's value, else looks the name up; a name the
///     firmware does not export fails the load unless it is weak undefined;
///   - RELATIVE maps the word through the sections (half-open), and a value
///     exactly one past a section end maps its last byte, then adds 1 (VDAC4);
///   - GLOB_DAT / JMP_SLOT write the resolved address; RTLD and all other
///     types write nothing.
///
/// Before loading, the firmware checks the image (elf_image.cpp); ImageProblem
/// is that check.

#include <cstddef>
#include <cstdint>
#include <string>

namespace vdac4
{

/// What the loader needs from the board model
class IEspElfHost
{
public:
    virtual ~IEspElfHost() = default;

    /// A PSRAM block (MALLOC_CAP_SPIRAM | 8BIT) on the D-bus; 0 when out of memory
    virtual uint32_t AllocPsram(uint32_t size) = 0;
    /// Release a block AllocPsram gave
    virtual void FreePsram(uint32_t address) = 0;
    /// Host view of `size` bytes at a D-bus address inside an allocated block
    virtual uint8_t* Bytes(uint32_t address, uint32_t size) = 0;
    /// The address the firmware exports under `name` (libc, esp-idf, then
    /// zm_symbols tables); 0 when it does not export it
    virtual uint32_t FindSymbol(const std::string& name) = 0;
};

/// One loaded section (esp_elf_sec_t)
struct EspElfSection
{
    uint32_t vaddr = 0;   ///< address in the ELF
    uint32_t size = 0;    ///< .text rounded up to 4
    uint32_t offset = 0;  ///< file offset
    uint32_t addr = 0;    ///< D-bus address it was loaded at
};

/// The loaded image
struct EspElfImage
{
    /// Section slots as the firmware numbers them (elf_types.h)
    enum Slot
    {
        Text = 0,
        Bss = 1,
        Data = 2,
        Rodata = 3,
        RelRo = 4,
        SlotCount = 5,
    };

    EspElfSection sections[SlotCount];
    uint32_t textBlock = 0;  ///< D-bus PSRAM block of .text (or the code_ram room it was placed in)
    /// .text lies in a room the caller lent (code_ram, ABI 1.19): not freed with the image
    bool textBorrowed = false;
    /// I-bus minus D-bus address of .text: kEspIbusOffset in PSRAM, the room's own offset when borrowed
    uint32_t textBusOffset = 0x42000000u - 0x3C000000u;
    uint32_t dataBlock = 0;  ///< D-bus PSRAM block of the rest (0 if none)
    uint32_t entry = 0;      ///< main(), on the I-bus
    uint32_t relocations = 0;
    uint32_t imports = 0;    ///< names resolved from the firmware tables

    /// esp_elf_map_sym: an ELF address to its D-bus address, 0 outside the loaded sections
    uint32_t MapSymbol(uint32_t vaddr) const;
    /// elf_remap_text: a D-bus address inside .text to its I-bus alias
    uint32_t RemapText(uint32_t address) const;
};

/// I-bus minus D-bus address of the same PSRAM byte (SOC_IROM_LOW - SOC_DROM_LOW)
inline constexpr uint32_t kEspIbusOffset = 0x42000000u - 0x3C000000u;

/// Where .text may go instead of a PSRAM block (esp_elf_t text_room, VDAC4 0.1.157): used when .text fits
struct EspElfTextRoom
{
    uint32_t address = 0;    ///< D-bus address
    uint32_t size = 0;
    uint32_t busOffset = 0;  ///< I-bus minus D-bus address there
};

/// The firmware's image check (elfImageProblem); nullptr when the image is fine
const char* EspElfImageProblem(const uint8_t* data, size_t size);

/// Load and relocate. On failure the blocks are released and `error` says why
bool EspElfLoad(const uint8_t* data, size_t size, IEspElfHost& host, EspElfImage& image, std::string& error,
                const EspElfTextRoom* room = nullptr);

/// Release the image's blocks (esp_elf_deinit); a borrowed .text room stays the lender's
void EspElfUnload(IEspElfHost& host, EspElfImage& image);

}  // namespace vdac4
